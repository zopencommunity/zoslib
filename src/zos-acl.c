///////////////////////////////////////////////////////////////////////////////
// Licensed Materials - Property of IBM
// ZOSLIB
// (C) Copyright IBM Corp. 2026. All Rights Reserved.
// US Government Users Restricted Rights - Use, duplication
// or disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
///////////////////////////////////////////////////////////////////////////////

// POSIX 1003.1e style ACL support for z/OS UNIX files, see
// include/sys/acl.h for the API.  Implemented on top of the z/OS
// Language Environment ACL functions, aliased here through their CEL
// entry points ("@@" is the ASCII spelling of the "\174\174" prefix of
// the pragma maps in the system <sys/acl.h>).  LE quirks handled below:
//   - entry points expect EBCDIC paths,
//   - acl_init(count) does not grow, retry with a larger count on E2BIG,
//   - ACL_ADD takes the permissions of an entry from acle_value alone
//     (ACL_MODIFY, which we do not use, would want them in acle_mask),
//   - a file without extended ACL entries is reported as ENOENT or as
//     success with zero entries, depending on the file system,
//   - acl_valid() may reorder the entries of the working storage, so
//     handles are validated before they are published, never after.

#define _AE_BIMODAL 1
#define _OPEN_SYS 1
#include "zos-base.h"

#include <sys/acl.h>  // this zoslib wrapper; pulls in the system header

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "zos-char-util.h"

// z/OS only defines O_CLOEXEC when __XPLAT is defined; the value is
// stable, define it for the metadata open in __acl_set_file().
#ifndef O_CLOEXEC
#define O_CLOEXEC 0x00001000
#endif

// The permission bits of an ACL entry (struct ACL_perm), as an rwx octal
// digit.
#define ACL_PERM(e) (*(unsigned char *)&(e)->acle_value & 07)



// Entry points of the z/OS LE ACL library.
extern lacl_t __acl_init_le(int count) asm("@@LINIT");
extern int __acl_get_file_le(const char *path_p, short type_d, lacl_t acl_d,
                             int *num) asm("@@LGFILE");
extern int __acl_set_file_le(const char *path_p, short type_d, lacl_t acl_d,
                             short op_type, void *entry_p) asm("@@LSFILE");
extern int __acl_free_le(lacl_t acl_d) asm("@@LFREE");
extern int __acl_delete_file_le(const char *path_p, short type_d) asm("@@LDFILE");
extern int __acl_get_fd_le(int fd, short type_d, lacl_t acl_d, int *num) asm("@@LGFD");
extern int __acl_set_fd_le(int fd, short type_d, lacl_t acl_d, short op_type,
                           void *entry_p) asm("@@LSFD");
extern int __acl_delete_fd_le(int fd, short type_d) asm("@@LDFD");
extern int __acl_valid_le(lacl_t acl_d, void *entry_p) asm("@@LVALID");
extern int __le_acl_from_text(const char *buf_p, short op_type, acl_all_t set_ptr,
                              char **ret) asm("@@LFTEXT");
extern char *__le_acl_to_text(const lacl_t acl_d, ssize_t *len_p, short type_d,
                              char delim) asm("@@LTTEXT");

// Every object released through acl_free() - a handle or a qualifier -
// carries this header in front of the pointer the caller holds, so
// that acl_free() can tell them apart.  The size_t keeps the payload
// aligned.
struct zos_acl_obj_header {
    size_t oh_tag;
};
#define ZOS_ACL_HANDLE_TAG    ((size_t)0x5A434C01)
#define ZOS_ACL_QUALIFIER_TAG ((size_t)0x5A434C02)
#define ZOS_ACL_TEXT_TAG      ((size_t)0x5A434C03)

// Wraps LE working storage into a published handle with the entry
// descriptors; takes ownership of "le" (freed on failure).  The base
// entries are counted from lacl_base: an access ACL always has three,
// a default-ACL handle has none.
static struct zos_acl_handle *zos_acl_publish_as(lacl_t le, int is_default) {
  if (le == NULL)
    return NULL;
  int n = le->lacl_base + le->lacl_data.aclb_entry.acl_entnum;
  char *mem =
      (char *)malloc(sizeof(struct zos_acl_obj_header) +
                     sizeof(struct zos_acl_handle) +
                     (size_t)(n - 1) * sizeof(struct zos_acl_entry_desc));
  if (mem == NULL) {
    __acl_free_le(le);
    return NULL;
  }
  ((struct zos_acl_obj_header *)mem)->oh_tag = ZOS_ACL_HANDLE_TAG;
  struct zos_acl_handle *h =
      (struct zos_acl_handle *)(mem + sizeof(struct zos_acl_obj_header));
  h->zh_le = le;
  h->zh_cursor = 0;
  h->zh_default = is_default;
  for (int i = 0; i < n; i++) {
    h->zh_desc[i].ed_acl = h;
    // The entry accessors use a fixed coordinate system: indices 0-2
    // are the base entries, 3 and beyond the extended entries.  Keep
    // the descriptors there even when the handle has no base entries,
    // so that a default handle's entries are not mistaken for base
    // entries.
    h->zh_desc[i].ed_index = i < le->lacl_base ? i : i - le->lacl_base + 3;
  }
  return h;
}

static struct zos_acl_handle *zos_acl_publish(lacl_t le) {
  return zos_acl_publish_as(le, 0);
}

// Deep-copies LE working storage for use as a private buffer in set
// requests: LE's acl_set_fd() may reorder the entries of the buffer
// it is given, and a published handle must never be written to.  The
// base entries are copied as well.  Returns NULL on allocation
// failure.
static lacl_t zos_acl_le_copy(lacl_t le) {
  int n = le->lacl_base + le->lacl_data.aclb_entry.acl_entnum;
  lacl_t copy = __acl_init_le(n < 1 ? 1 : n);
  if (copy == NULL)
    return NULL;
  copy->lacl_base = le->lacl_base;
  memcpy(copy->lacl_base_entry, le->lacl_base_entry,
         sizeof(le->lacl_base_entry));
  copy->lacl_data.aclb_entry.acl_entnum =
      le->lacl_data.aclb_entry.acl_entnum;
  copy->lacl_data.aclb_entry.acl_elen = le->lacl_data.aclb_entry.acl_elen;
  copy->lacl_data.aclb_entry.acl_len = le->lacl_data.aclb_entry.acl_len;
  int nentries = le->lacl_data.aclb_entry.acl_entnum;
  if (nentries > 0)
    memcpy((char *)&copy->lacl_data + sizeof(struct ACL_buf),
           (char *)&le->lacl_data + sizeof(struct ACL_buf),
           (size_t)nentries * sizeof(struct ACL_entry));
  return copy;
}

// Do two default-ACL working storages carry the same entries?  (A
// default ACL has no base entries, only extended ones.)
static int default_acls_equal(lacl_t a, lacl_t b) {
  struct ACL *ha = &a->lacl_data.aclb_entry;
  struct ACL *hb = &b->lacl_data.aclb_entry;
  if (ha->acl_entnum != hb->acl_entnum)
    return 0;
  struct ACL_entry *ea =
      (struct ACL_entry *)((char *)&a->lacl_data + sizeof(struct ACL_buf));
  struct ACL_entry *eb =
      (struct ACL_entry *)((char *)&b->lacl_data + sizeof(struct ACL_buf));
  for (int i = 0; i < ha->acl_entnum; i++)
    if (ea[i].acle_type != eb[i].acle_type
        || ea[i].acle_id != eb[i].acle_id
        || *(unsigned char *)&ea[i].acle_value != *(unsigned char *)&eb[i].acle_value)
      return 0;
  return 1;
}

// The real entry an entry descriptor refers to.
static struct ACL_entry *zos_acl_entry_real(struct zos_acl_entry_desc *entry) {
  lacl_t le = entry->ed_acl->zh_le;
  if (entry->ed_index < 3)
    return (struct ACL_entry *)&le->lacl_base_entry[entry->ed_index];
  return (struct ACL_entry *)((char *)&le->lacl_data + sizeof(struct ACL_buf))
         + (entry->ed_index - 3);
}

// Upper bound of ACL entries to retrieve; RACF and the file system
// regulate the real limit.
#define ZOS_ACL_MAX_ENTRIES 4096

// Converts a path to EBCDIC in "buf".  Returns 0 or -1 with errno set.
static int zos_acl_path_a2e(const char *path_p, char *buf, size_t buflen) {
  if (path_p == NULL || buf == NULL) {
    errno = EINVAL;
    return -1;
  }
  if (snprintf(buf, buflen, "%s", path_p) >= (int)buflen) {
    errno = ENAMETOOLONG;
    return -1;
  }
  __a2e_s(buf);
  return 0;
}

// Exported entry points, see include/sys/acl.h.  They use the z/OS
// system types (lacl_t, short) so that this file compiles independent
// of ZOSLIB_OVERRIDE_CLIB; the POSIX names in the header alias them.

// Builds a handle without extended entries carrying "mode" as the base
// permissions.  Returns NULL when the storage cannot be allocated.
static lacl_t zos_acl_synthesize(mode_t mode) {
  lacl_t empty = __acl_init_le(16);
  if (empty == NULL)
    return NULL;
  empty->lacl_base = 3;
  static const char types[3] = {ACL_USER, ACL_GROUP, ACL_OTHER};
  for (int i = 0; i < 3; i++) {
    empty->lacl_base_entry[i].acle_type = types[i];
    empty->lacl_base_entry[i].acle_id = 0;
    *(unsigned char *)&empty->lacl_base_entry[i].acle_value =
        (mode >> (6 - 3 * i)) & 07;
    *(unsigned char *)&empty->lacl_base_entry[i].acle_mask = 07;
  }
  return empty;
}

// Initial working storage size for acl_get_file(); grows on E2BIG.
// Internal, adjustable through __acl_set_initial_count() for the tests
// to force the growth path.  A variable cannot be exported from the
// shared library (the loader rejects data imports), so it is reached
// through functions.
static int zos_acl_initial_count = 16;

__Z_EXPORT void __acl_set_initial_count(int count) { zos_acl_initial_count = count; }

struct zos_acl_handle *__acl_get_file(const char *path_p, short type) {
  if (path_p == NULL) {
    errno = EINVAL;
    return NULL;
  }

  char epath[1024];
  if (zos_acl_path_a2e(path_p, epath, sizeof(epath)) != 0)
    return NULL;

  if (type == ACL_DIRDEF || type == ACL_FILDEF) {
    // The default ACL of a directory.  A directory without one yields
    // a handle without entries - Linux behaves the same way, and
    // gnulib's copy-acl relies on it: the empty handle is trivial and
    // routes to acl_delete_def_file() on the destination.  z/OS has
    // two default ACL types: the handle carries the file default when
    // one is stored, the directory default otherwise; ACL_FILDEF
    // retrieves the file default alone.  A non-directory has no
    // default ACL at all.
    struct stat st;
    if (stat(path_p, &st) != 0)
      return NULL;
    if (!S_ISDIR(st.st_mode)) {
      errno = EINVAL;
      return NULL;
    }

    static const short types[] = {ACL_FILDEF, ACL_DIRDEF};
    int ntypes = type == ACL_FILDEF ? 1 : 2;
    lacl_t stored[2] = {NULL, NULL};
    for (int t = 0; t < ntypes; t++) {
      for (int count = zos_acl_initial_count;; count *= 8) {
        if (count > ZOS_ACL_MAX_ENTRIES)
          count = ZOS_ACL_MAX_ENTRIES;

        lacl_t acl = __acl_init_le(count);
        if (acl == NULL)
          goto cleanup_fail;

        int num = 0;
        if (__acl_get_file_le(epath, types[t], acl, &num) == 0) {
          if (acl->lacl_data.aclb_entry.acl_entnum > 0)
            stored[t] = acl;
          else
            __acl_free_le(acl);
          break; // got this type (or it is empty), try the next
        }

        int save_errno = errno;
        __acl_free_le(acl);
        errno = save_errno;
        if (errno == E2BIG && count != ZOS_ACL_MAX_ENTRIES)
          continue; // grow the storage
        if (errno != ENOENT)
          goto cleanup_fail; // a real retrieval error
        break; // nothing of this type, try the next
      }
    }

    // A POSIX default ACL is one template; z/OS has two (for files and
    // directories created inside).  One handle cannot represent two
    // different templates - refuse the lossy round-trip.
    if (stored[0] != NULL && stored[1] != NULL
        && !default_acls_equal(stored[0], stored[1]))
    {
      __acl_free_le(stored[0]);
      __acl_free_le(stored[1]);
      errno = EINVAL;
      return NULL;
    }

    if (stored[0] != NULL)
    {
      if (stored[1] != NULL)
        __acl_free_le(stored[1]);
      return zos_acl_publish_as(stored[0], 1);
    }
    if (stored[1] != NULL)
      return zos_acl_publish_as(stored[1], 1);

    lacl_t empty = __acl_init_le(3);
    if (empty == NULL)
      return NULL;
    empty->lacl_base = 0; // no base entries: a default ACL has none
    return zos_acl_publish_as(empty, 1);

cleanup_fail:
    if (stored[0] != NULL)
      __acl_free_le(stored[0]);
    if (stored[1] != NULL)
      __acl_free_le(stored[1]);
    return NULL;
  }

  if (type != ACL_ACCESS) {
    errno = EINVAL;
    return NULL;
  }

  // Grow the working storage on E2BIG, up to the maximum.
  int count = zos_acl_initial_count;
  for (;;) {
    if (count > ZOS_ACL_MAX_ENTRIES)
      count = ZOS_ACL_MAX_ENTRIES;

    lacl_t acl = __acl_init_le(count);
    if (acl == NULL)
      return NULL;

    int num = 0;
    if (__acl_get_file_le(epath, ACL_ACCESS, acl, &num) == 0) {
      // Validate before the handle is published: acl_valid() may
      // reorder the entries, which is fine while it is still private.
      if (__acl_valid_le(acl, NULL) != 0) {
        __acl_free_le(acl);
        return NULL;
      }
      return zos_acl_publish(acl); // possibly with zero extended entries
    }

    if (errno == ENOENT) {
      // ENOENT through the path means "no extended ACL entries" for a
      // file that exists -- but a concurrent rename could have replaced
      // the file with one that has entries.  Pin the inode and re-check
      // through the descriptor, so the entries and the mode come from
      // the same file.  A file the owner cannot open falls back to the
      // path stat(), accepting the rename race for it.
      int pfd = open(path_p, O_RDONLY | O_NONBLOCK | O_NOCTTY | O_CLOEXEC);
      if (pfd >= 0) {
        for (;;) {
          int num2 = 0;
          if (__acl_get_fd_le(pfd, ACL_ACCESS, acl, &num2) == 0) {
            // The pinned inode's ACL, with zero or more extended entries,
            // consistent with its own mode.
            int rc2 = __acl_valid_le(acl, NULL);
            close(pfd);
            if (rc2 != 0) {
              __acl_free_le(acl);
              return NULL;
            }
            return zos_acl_publish(acl);
          }
          // Grow the storage like the main loop: the replacement may
          // have more entries than the initial size could hold.
          if (errno != E2BIG || count == ZOS_ACL_MAX_ENTRIES)
            break;
          __acl_free_le(acl);
          count = count * 8;
          if (count > ZOS_ACL_MAX_ENTRIES)
            count = ZOS_ACL_MAX_ENTRIES;
          acl = __acl_init_le(count);
          if (acl == NULL) {
            close(pfd);
            return NULL;
          }
        }
        int pinned_errno = errno;
        struct stat fst;
        int have_fst = fstat(pfd, &fst) == 0;
        close(pfd);
        __acl_free_le(acl);
        if (pinned_errno == ENOENT) {
          // No extended entries on the pinned inode.
          if (have_fst)
            return zos_acl_publish(zos_acl_synthesize(fst.st_mode));
          return NULL;
        }
        // A real retrieval error (E2BIG, ...), not "no entries":
        // propagate it, an empty handle could clear unretrieved
        // entries.
        errno = pinned_errno;
        return NULL;
      }
      // Not openable (the owner has no read permission): fall back to
      // the path stat().
      __acl_free_le(acl);
      struct stat st;
      if (stat(path_p, &st) == 0)
        return zos_acl_publish(zos_acl_synthesize(st.st_mode));
      return NULL; // no such file
    }

    int save_errno = errno;
    __acl_free_le(acl);
    errno = save_errno;
    if (errno != E2BIG)
      return NULL;
    if (count == ZOS_ACL_MAX_ENTRIES) {
      errno = E2BIG; // the ACL has more entries than the maximum
      return NULL;
    }
    count *= 8;
  }
}

// Retrieves the extended ACL of "fd" into fresh working storage with
// normalized masks, for the rollback in __acl_set_file().  Returns NULL
// when it has no extended entries or on error; "*error" distinguishes
// the two (set when the retrieval failed, with errno left set).
static lacl_t zos_acl_get_fd(int fd, int *error) {
  *error = 0;
  for (int count = zos_acl_initial_count;; count *= 8) {
    if (count > ZOS_ACL_MAX_ENTRIES)
      count = ZOS_ACL_MAX_ENTRIES;

    lacl_t acl = __acl_init_le(count);
    if (acl == NULL) {
      *error = 1; // allocation failure, errno is set
      return NULL;
    }

    int num = 0;
    if (__acl_get_fd_le(fd, ACL_ACCESS, acl, &num) == 0) {
      if (acl->lacl_data.aclb_entry.acl_entnum <= 0) {
        __acl_free_le(acl);
        return NULL;
      }
      return acl;
    }

    int save_errno = errno;
    __acl_free_le(acl);
    errno = save_errno;
    if (errno != E2BIG || count == ZOS_ACL_MAX_ENTRIES) {
      // ENOENT means the file has no extended ACL entries (some file
      // systems report an existing file without entries that way); the
      // callers only invoke this for a file that exists.
      if (errno != ENOENT)
        *error = 1;
      return NULL;
    }
  }
}

// Like zos_acl_get_fd(), but for an EBCDIC path, for the path based
// fallback in __acl_set_file().
static lacl_t zos_acl_get_epath(const char *epath, int *error) {
  *error = 0;
  for (int count = zos_acl_initial_count;; count *= 8) {
    if (count > ZOS_ACL_MAX_ENTRIES)
      count = ZOS_ACL_MAX_ENTRIES;

    lacl_t acl = __acl_init_le(count);
    if (acl == NULL) {
      *error = 1; // allocation failure, errno is set
      return NULL;
    }

    int num = 0;
    if (__acl_get_file_le(epath, ACL_ACCESS, acl, &num) == 0) {
      if (acl->lacl_data.aclb_entry.acl_entnum <= 0) {
        __acl_free_le(acl);
        return NULL;
      }
      return acl;
    }

    int save_errno = errno;
    __acl_free_le(acl);
    errno = save_errno;
    if (errno != E2BIG || count == ZOS_ACL_MAX_ENTRIES) {
      // ENOENT means the file has no extended ACL entries (some file
      // systems report an existing file without entries that way); the
      // callers only invoke this for a file that exists.
      if (errno != ENOENT)
        *error = 1;
      return NULL;
    }
  }
}

// Does the ACL grant "perm" (an rwx octal mask) to anyone but the file
// owner?  Base group/other entries and all extended entries count; an
// extended user entry for the owner does not.  Write+search (03) is the
// permission to unlink or rename entries in a directory, write (02) is
// the permission to modify the contents of a file.
static int zos_acl_grants(lacl_t acl, uid_t owner_uid, int perm) {
  if ((ACL_PERM(&acl->lacl_base_entry[1]) & perm) == perm)
    return 1; // group
  if ((ACL_PERM(&acl->lacl_base_entry[2]) & perm) == perm)
    return 1; // other
  struct ACL *h = &acl->lacl_data.aclb_entry;
  struct ACL_entry *e =
      (struct ACL_entry *)((char *)&acl->lacl_data + sizeof(struct ACL_buf));
  for (int i = 0; i < h->acl_entnum; ++i) {
    if ((ACL_PERM(&e[i]) & perm) == perm &&
        !(e[i].acle_type == ACL_USER && e[i].acle_id == (int)owner_uid))
      return 1;
  }
  return 0;
}

// The fd based core of the empty-ACL update.  The descriptor is
// pinned to the destination inode and owned by the caller.
// An empty ACL removes the extended ACL of the destination file and
// restores the base permissions (owner/group/other) of the source.
// The permissions are first narrowed to the intersection of the old
// and the new mode, so no intermediate state is broader than either;
// on failure the old mode and the old extended ACL are restored.  The
// setuid, setgid and sticky bits are not part of an access ACL and
// are kept.
static int zos_acl_set_empty_fd(int fd, lacl_t acl) {
  mode_t new_mode = 0;
  for (int i = 0; i < 3; i++)
    new_mode = (new_mode << 3) | ACL_PERM(&acl->lacl_base_entry[i]);

  // Only the rwx bits carry over from the handle; the special bits
  // come from the pinned descriptor alone.  Without the fstat()
  // neither the new nor the old mode is known reliably: refuse before
  // modifying anything.
  struct stat fst;
  if (fstat(fd, &fst) != 0)
    return -1;
  new_mode = (new_mode & 0777) | (fst.st_mode & (S_ISUID | S_ISGID | S_ISVTX));
  mode_t old_mode = fst.st_mode & 07777;

  // Applying a base mode that grants write to group or other while
  // the destination carries setuid/setgid would arm privilege on
  // writable content; chmod on Linux clears the bits in that case.
  // The old mode counts as well, like in the non-empty core: a
  // descriptor opened for writing before the update keeps write
  // access through the narrowing.  Refuse all of these.
  if ((new_mode & (S_ISUID | S_ISGID)) != 0 &&
      ((new_mode & (S_IWGRP | S_IWOTH)) != 0 ||
       (old_mode & (S_IWGRP | S_IWOTH)) != 0)) {
    errno = EPERM;
    return -1;
  }

  // The old extended ACL, restored if the update fails.  When it
  // cannot be saved, the update is refused: a rollback would not be
  // able to restore it.  Restoring it needs the LE set request, which
  // clears the sticky bit: a sticky directory whose saved ACL grants
  // write and search to non-owners cannot be rolled back safely;
  // refuse before modifying anything.
  int snap_error = 0;
  lacl_t saved = zos_acl_get_fd(fd, &snap_error);
  if (snap_error)
    return -1;
  if (saved != NULL &&
      (((new_mode & S_ISVTX) != 0 && S_ISDIR(fst.st_mode) &&
        zos_acl_grants(saved, fst.st_uid, 03)) ||
       ((new_mode & (S_ISUID | S_ISGID)) != 0 &&
        zos_acl_grants(saved, fst.st_uid, 02)))) {
    __acl_free_le(saved);
    errno = EPERM;
    return -1;
  }

  int rc = fchmod(fd, old_mode & new_mode);
  if (rc == 0) {
    rc = __acl_delete_fd_le(fd, ACL_ACCESS);
    if (rc == 0)
      rc = fchmod(fd, new_mode);
    if (rc != 0) {
      // Restore the extended ACL before the mode, so no user gains
      // access through the mode while the ACL is still missing; if
      // the ACL cannot be restored, keep the narrow mode.
      int save_errno = errno;
      int restored = saved == NULL ||
                     __acl_set_fd_le(fd, ACL_ACCESS, saved, ACL_ADD, NULL) == 0;
      if (restored)
        fchmod(fd, old_mode);
      errno = save_errno;
    }
  }
  if (saved != NULL)
    __acl_free_le(saved);
  return rc;
}

// The fd based core of the non-empty ACL update.  The descriptor is
// pinned to the destination inode and owned by the caller.
// ACL_ADD replaces the destination ACL; ACL_MODIFY would merge.  The
// handle is not modified: the masks were normalized on retrieval.
// The set request applies the base entries as rwx bits only and
// clears the special bits, which are not part of an access ACL.
static int zos_acl_set_nonempty_fd(int fd, lacl_t acl) {
  struct stat fst;
  if (fstat(fd, &fst) != 0)
    return -1;
  mode_t special = fst.st_mode & (S_ISUID | S_ISGID | S_ISVTX);

  // A sticky directory whose incoming ACL grants write and search to
  // non-owners would let them unlink or rename other users' files while
  // the sticky bit is cleared between the set request and its
  // restoration; refuse rather than risk that.
  // A setuid/setgid destination whose incoming ACL grants write to
  // non-owners could be modified by them while the privilege bits are
  // cleared, and the restoration would make their content privileged;
  // the destination's own mode counts as well, because a non-owner may
  // hold a descriptor opened for writing before the update - write
  // access is checked at open time only.  Refuse all of these.
  if (((special & S_ISVTX) != 0 && S_ISDIR(fst.st_mode) &&
       zos_acl_grants(acl, fst.st_uid, 03)) ||
      ((special & (S_ISUID | S_ISGID)) != 0 &&
       (zos_acl_grants(acl, fst.st_uid, 02) ||
        (fst.st_mode & (S_IWGRP | S_IWOTH)) != 0))) {
    errno = EPERM;
    return -1;
  }

  // Snapshot for the rollback when the special bits need restoring
  // after the set request.
  lacl_t saved = NULL;
  int snap_error = 0;
  if (special != 0) {
    saved = zos_acl_get_fd(fd, &snap_error);
    if (snap_error)
      return -1;
    // Restoring the saved entries needs the LE set request, which
    // clears the special bits: a sticky directory whose saved ACL
    // grants write and search to non-owners, or a setuid/setgid
    // destination whose saved ACL grants write to non-owners, cannot be
    // rolled back safely; refuse before modifying anything.
    if (saved != NULL &&
        (((special & S_ISVTX) != 0 && S_ISDIR(fst.st_mode) &&
          zos_acl_grants(saved, fst.st_uid, 03)) ||
         ((special & (S_ISUID | S_ISGID)) != 0 &&
          zos_acl_grants(saved, fst.st_uid, 02)))) {
      __acl_free_le(saved);
      errno = EPERM;
      return -1;
    }
  }

  lacl_t set_copy = zos_acl_le_copy(acl);
  if (set_copy == NULL)
  {
    if (saved != NULL)
      __acl_free_le(saved);
    // The descriptor belongs to the caller: acl_set_fd() must not
    // close it, and acl_set_file() closes it after this returns -
    // closing here would double-close.
    return -1;
  }
  int rc = __acl_set_fd_le(fd, ACL_ACCESS, set_copy, ACL_ADD, NULL);
  __acl_free_le(set_copy);
  if (rc == 0 && special != 0) {
    mode_t base = 0;
    for (int i = 0; i < 3; i++)
      base = (base << 3) | ACL_PERM(&acl->lacl_base_entry[i]);
    rc = fchmod(fd, base | special);
    if (rc != 0) {
      // Roll back to the old extended entries (or their absence) and
      // the old mode, like the empty-ACL core does.  The saved entries
      // carry the old rwx bits, so restoring them keeps the mode
      // narrow; without saved entries the mode is narrowed first,
      // because the incoming entries may be the only thing denying
      // access that the incoming base permissions grant - leave them in
      // place when the narrowing fails.
      int save_errno = errno;
      if (saved != NULL) {
        lacl_t saved_copy = zos_acl_le_copy(saved);
        if (saved_copy != NULL)
        {
          if (__acl_set_fd_le(fd, ACL_ACCESS, saved_copy, ACL_ADD, NULL) == 0)
            fchmod(fd, fst.st_mode & 07777);
          __acl_free_le(saved_copy);
        }
      } else {
        if (fchmod(fd, fst.st_mode & 07777) == 0)
          __acl_delete_fd_le(fd, ACL_ACCESS);
      }
      errno = save_errno;
    }
  }
  if (saved != NULL)
    __acl_free_le(saved);
  return rc;
}

int __acl_set_file(const char *path_p, short type, struct zos_acl_handle *acl) {
  if (path_p == NULL || acl == NULL) {
    if (acl == NULL && path_p != NULL &&
        (type == ACL_ACCESS || type == ACL_DIRDEF || type == ACL_FILDEF))
      return 0; // no ACL to apply
    errno = EINVAL;
    return -1;
  }
  if (type != ACL_ACCESS && type != ACL_DIRDEF && type != ACL_FILDEF) {
    errno = EINVAL;
    return -1;
  }
  lacl_t le = acl->zh_le;

  char epath[1024];
  if (zos_acl_path_a2e(path_p, epath, sizeof(epath)) != 0)
    return -1;

  if (type == ACL_DIRDEF || type == ACL_FILDEF) {
    // Setting the default ACL of a directory: no mode, no special
    // bits, none of the access-ACL machinery - just the LE requests,
    // pinned to the directory inode so a concurrent rename cannot
    // update different directories' defaults.  An empty handle removes
    // the defaults; a handle with entries is stored as both the file
    // and the directory default (ACL_FILDEF stores the file default
    // alone), the POSIX single-default concept.  A default ACL has no
    // base entries, LE rejects them.
    if (le->lacl_base != 0) {
      errno = EINVAL; // not a default-ACL handle
      return -1;
    }

    // A failed open leaves its own errno (ENOENT, EACCES, ...).
    int fd = open(path_p, O_RDONLY | O_NONBLOCK | O_NOCTTY | O_CLOEXEC);
    if (fd < 0)
      return -1;
    struct stat st;
    if (fstat(fd, &st) != 0)
    {
      close(fd);
      return -1;
    }
    if (!S_ISDIR(st.st_mode)) {
      close(fd);
      errno = EINVAL;
      return -1;
    }

    int rc;
    if (le->lacl_data.aclb_entry.acl_entnum <= 0) {
      rc = __acl_delete_fd_le(fd, ACL_FILDEF);
      if (rc == 0 && type == ACL_DIRDEF)
        rc = __acl_delete_fd_le(fd, ACL_DIRDEF);
    } else {
      lacl_t set_copy = zos_acl_le_copy(le);
      if (set_copy == NULL)
      {
        close(fd);
        return -1;
      }
      rc = __acl_set_fd_le(fd, ACL_FILDEF, set_copy, ACL_ADD, NULL);
      if (rc == 0 && type == ACL_DIRDEF)
        rc = __acl_set_fd_le(fd, ACL_DIRDEF, set_copy, ACL_ADD, NULL);
      __acl_free_le(set_copy);
    }
    close(fd);
    return rc;
  }

  if (le->lacl_base != 3) {
    errno = EINVAL; // not an access-ACL handle (e.g. a default handle)
    return -1;
  }

  struct ACL *hdr = &le->lacl_data.aclb_entry;
  int empty = hdr->acl_entnum <= 0;

  // O_NONBLOCK keeps the open from blocking on a FIFO, O_NOCTTY keeps
  // a terminal destination from becoming the controlling terminal,
  // O_CLOEXEC keeps the descriptor from leaking into spawned
  // processes.  A failed open leaves its own errno (ENOENT, EACCES,
  // ...) for the caller.
  int fd = open(path_p, O_RDONLY | O_NONBLOCK | O_NOCTTY | O_CLOEXEC);
  if (fd < 0) {
    if (!empty)
      return -1; // the update needs the pinned descriptor
    // The destination cannot be pinned (no read permission for the
    // owner).  When it has an extended ACL, removing it first would
    // briefly grant the access the ACL denies, and a failed update
    // could not restore it; refuse instead of risking that.  Without
    // an extended ACL there is nothing to remove and only the mode
    // is applied.
    int snap_error = 0;
    lacl_t saved = zos_acl_get_epath(epath, &snap_error);
    if (saved != NULL)
      __acl_free_le(saved);
    if (snap_error)
      return -1; // cannot tell: refuse, errno left set
    if (saved != NULL) {
      errno = EACCES; // the open failed: the destination is not readable
      return -1;
    }
    // The destination's special bits cannot be applied through the
    // path: after a concurrent rename they would come from the old
    // inode, setting setuid/setgid on a replacement executable.
    // Refuse rather than risk that.
    struct stat st;
    if (stat(path_p, &st) == 0 &&
        (st.st_mode & (S_ISUID | S_ISGID | S_ISVTX)) != 0) {
      errno = EACCES;
      return -1;
    }
    mode_t base = 0;
    for (int i = 0; i < 3; i++)
      base = (base << 3) | ACL_PERM(&le->lacl_base_entry[i]);
    return chmod(path_p, base);
  }

  int rc = empty ? zos_acl_set_empty_fd(fd, le)
                 : zos_acl_set_nonempty_fd(fd, le);
  close(fd);
  return rc;
}

int __acl_set_fd(int fd, struct zos_acl_handle *acl) {
  if (fd < 0 || acl == NULL) {
    if (acl == NULL && fd >= 0)
      return 0; // no ACL to apply
    errno = EINVAL;
    return -1;
  }
  lacl_t le = acl->zh_le;

  // The descriptor belongs to the caller and is not closed.
  if (le->lacl_base != 3) {
    errno = EINVAL; // not an access-ACL handle (e.g. a default handle)
    return -1;
  }
  if (le->lacl_data.aclb_entry.acl_entnum <= 0)
    return zos_acl_set_empty_fd(fd, le);
  return zos_acl_set_nonempty_fd(fd, le);
}

struct zos_acl_handle *__acl_get_fd(int fd) {
  if (fd < 0) {
    errno = EINVAL;
    return NULL;
  }

  // Grow the working storage on E2BIG, up to the maximum.
  for (int count = zos_acl_initial_count;; count *= 8) {
    if (count > ZOS_ACL_MAX_ENTRIES)
      count = ZOS_ACL_MAX_ENTRIES;

    lacl_t acl = __acl_init_le(count);
    if (acl == NULL)
      return NULL;

    int num = 0;
    if (__acl_get_fd_le(fd, ACL_ACCESS, acl, &num) == 0) {
      // Validate before the handle is published.
      if (__acl_valid_le(acl, NULL) != 0) {
        __acl_free_le(acl);
        return NULL;
      }
      return zos_acl_publish(acl); // possibly with zero extended entries
    }

    if (errno == ENOENT) {
      // No extended entries on the pinned inode; the mode comes from
      // the same descriptor.
      struct stat fst;
      int have_fst = fstat(fd, &fst) == 0;
      __acl_free_le(acl);
      if (have_fst)
        return zos_acl_publish(zos_acl_synthesize(fst.st_mode));
      return NULL;
    }

    int save_errno = errno;
    __acl_free_le(acl);
    errno = save_errno;
    if (errno != E2BIG)
      return NULL;
    if (count == ZOS_ACL_MAX_ENTRIES) {
      errno = E2BIG; // the ACL has more entries than the maximum
      return NULL;
    }
  }
}

struct zos_acl_handle *__acl_from_text(const char *buf_p) {
  if (buf_p == NULL) {
    errno = EINVAL;
    return NULL;
  }

  // The LE parser expects EBCDIC text: comma or newline separated
  // entries of "type::perm" (base) or "type:id:perm" (extended), with
  // rwx / '-' / octal permissions and uid, gid or username ids.
  size_t len = strlen(buf_p);
  char *ebuf = (char *)malloc(len + 1);
  if (ebuf == NULL)
    return NULL;
  memcpy(ebuf, buf_p, len + 1);
  __a2e_s(ebuf);

  struct ACL_set set = {NULL, NULL, NULL};
  char *ret = NULL;
  int rc = __le_acl_from_text(ebuf, ACL_ADD, &set, &ret);
  free(ebuf);
  if (rc != 0)
    return NULL; // errno set by the parser

  int access_set = set.acls_data[0] != NULL;
  int file_default_set = set.acls_data[1] != NULL;
  int dir_default_set = set.acls_data[2] != NULL;

  if (access_set && (file_default_set || dir_default_set)) {
    // Mixing access and default entries is ambiguous.
    for (int i = 0; i < 3; i++)
      if (set.acls_data[i] != NULL)
        __acl_free_le(set.acls_data[i]);
    errno = EINVAL;
    return NULL;
  }

  if (file_default_set || dir_default_set) {
    // A default-ACL handle, from default: or fdefault: tagged entries.
    // A default ACL has no base entries.
    if (file_default_set && dir_default_set) {
      for (int i = 0; i < 3; i++)
        if (set.acls_data[i] != NULL)
          __acl_free_le(set.acls_data[i]);
      errno = EINVAL; // both default kinds in one text
      return NULL;
    }
    lacl_t acl = file_default_set ? set.acls_data[1] : set.acls_data[2];
    if (acl->lacl_base != 0 || __acl_valid_le(acl, NULL) != 0) {
      __acl_free_le(acl);
      errno = EINVAL;
      return NULL;
    }
    return zos_acl_publish_as(acl, 1);
  }

  if (set.acls_data[0] == NULL) {
    errno = EINVAL;
    return NULL;
  }

  lacl_t acl = set.acls_data[0];
  // Validate before the handle is published: text without the three
  // base entries parses (acl_valid() does not require them), but the
  // update paths read the base entries for the mode - reject it.
  if (acl->lacl_base != 3 || __acl_valid_le(acl, NULL) != 0) {
    __acl_free_le(acl);
    errno = EINVAL;
    return NULL;
  }
  return zos_acl_publish(acl);
}

char *__acl_to_text(struct zos_acl_handle *acl, ssize_t *len_p) {
  if (acl == NULL) {
    errno = EINVAL;
    return NULL;
  }
  lacl_t le = acl->zh_le;

  // The LE acl_to_text() emits the extended entries only (and returns
  // NULL for a handle without any); the base entries are rendered here
  // so that the text form carries the whole ACL and round-trips
  // through acl_from_text().  A default-ACL handle has no base
  // entries - LE rejects them - and renders its entries only.
  char base[64];
  int off = 0;
  base[0] = '\0'; // a default handle has no base entries to render
  if (!acl->zh_default) {
    static const char *names[3] = {"user", "group", "other"};
    for (int i = 0; i < 3; i++) {
      int perm = ACL_PERM(&le->lacl_base_entry[i]);
      off += snprintf(base + off, sizeof(base) - off, "%s%s::%c%c%c",
                      i > 0 ? "," : "", names[i], (perm & 04) ? 'r' : '-',
                      (perm & 02) ? 'w' : '-', (perm & 01) ? 'x' : '-');
    }
  }

  // A default handle is rendered as its kind: the formatter prefixes
  // the entries with the default tag, so the text round-trips through
  // acl_from_text() as a default ACL.
  ssize_t ext_len = 0;
  char *ext = __le_acl_to_text(le, &ext_len,
                               acl->zh_default ? (short)ACL_DIRDEF : (short)ACL_ACCESS,
                               (char)0x6B);
  char *etext = NULL;
  if (ext != NULL) {
    etext = (char *)malloc(ext_len + 1);
    if (etext != NULL) {
      memcpy(etext, ext, ext_len);
      etext[ext_len] = '\0';
      __e2a_s(etext); // the LE text is EBCDIC
    }
    free(ext);
    if (etext == NULL)
      return NULL;
  } else if (le->lacl_data.aclb_entry.acl_entnum > 0) {
    // The formatter failed although there are extended entries; only a
    // handle without entries produces empty output.
    return NULL;
  }

  int has_base = base[0] != '\0';
  size_t total = strlen(base)
                 + (etext != NULL ? (has_base ? 1 : 0) + strlen(etext) : 0);
  char *mem = (char *)malloc(sizeof(struct zos_acl_obj_header) + total + 1);
  if (mem == NULL) {
    free(etext);
    return NULL;
  }
  ((struct zos_acl_obj_header *)mem)->oh_tag = ZOS_ACL_TEXT_TAG;
  char *text = mem + sizeof(struct zos_acl_obj_header);
  snprintf(text, total + 1, "%s%s%s", base,
           etext != NULL && has_base ? "," : "", etext != NULL ? etext : "");
  free(etext);
  if (len_p != NULL)
    *len_p = (ssize_t)total;
  return text;
}

int __acl_delete_def_file(const char *path_p) {
  if (path_p == NULL) {
    errno = EINVAL;
    return -1;
  }

  char epath[1024];
  if (zos_acl_path_a2e(path_p, epath, sizeof(epath)) != 0)
    return -1;

  // z/OS has two default ACL types (file and directory default); the
  // POSIX "default" concept covers both, remove them all.  Deleting a
  // default ACL that does not exist is not an error.
  int rc = __acl_delete_file_le(epath, ACL_FILDEF);
  if (rc == 0)
    rc = __acl_delete_file_le(epath, ACL_DIRDEF);
  return rc;
}

int __acl_free(void *obj_p) {
  if (obj_p == NULL) {
    errno = EINVAL;
    return -1;
  }
  size_t tag = ((struct zos_acl_obj_header *)obj_p)[-1].oh_tag;
  if (tag == ZOS_ACL_HANDLE_TAG) {
    struct zos_acl_handle *h = (struct zos_acl_handle *)obj_p;
    int rc = __acl_free_le(h->zh_le);
    free((char *)obj_p - sizeof(struct zos_acl_obj_header));
    return rc;
  }
  if (tag == ZOS_ACL_QUALIFIER_TAG || tag == ZOS_ACL_TEXT_TAG) {
    free((char *)obj_p - sizeof(struct zos_acl_obj_header));
    return 0;
  }
  errno = EINVAL; // not an object of this API
  return -1;
}

int __acl_get_entry(struct zos_acl_handle *acl, int entry_id,
                    struct zos_acl_entry_desc **entry_p) {
  if (acl == NULL || entry_p == NULL ||
      (entry_id != ACL_FIRST_ENTRY && entry_id != ACL_NEXT_ENTRY)) {
    errno = EINVAL;
    return -1;
  }
  int n = acl->zh_le->lacl_base + acl->zh_le->lacl_data.aclb_entry.acl_entnum;
  if (entry_id == ACL_FIRST_ENTRY)
    acl->zh_cursor = 0;
  if (acl->zh_cursor >= n) {
    *entry_p = NULL;
    return 0; // no more entries
  }
  *entry_p = &acl->zh_desc[acl->zh_cursor++];
  return 1;
}

int __acl_get_tag_type(struct zos_acl_entry_desc *entry_d, int *tag_type_p) {
  if (entry_d == NULL || tag_type_p == NULL) {
    errno = EINVAL;
    return -1;
  }
  if (entry_d->ed_index < 3) {
    // The base entries, by position: owner, group, other.
    static const int tags[3] = {ACL_USER_OBJ, ACL_GROUP_OBJ, ACL_OTHER};
    *tag_type_p = tags[entry_d->ed_index];
  } else {
    *tag_type_p =
        zos_acl_entry_real(entry_d)->acle_type == ACL_USER ? ACL_USER : ACL_GROUP;
  }
  return 0;
}

int __acl_get_permset(struct zos_acl_entry_desc *entry_d,
                      struct ACL_perm **permset_p) {
  if (entry_d == NULL || permset_p == NULL) {
    errno = EINVAL;
    return -1;
  }
  *permset_p = &zos_acl_entry_real(entry_d)->acle_value;
  return 0;
}

// Private permission bit masks, matching the rwx octal digit; the
// system ACL_READ/... constants are boolean values and are redefined
// only for the override consumers.
#define ZOS_ACL_PERM_R 04
#define ZOS_ACL_PERM_W 02
#define ZOS_ACL_PERM_X 01

int __acl_get_perm(struct ACL_perm *permset_d, int perm) {
  if (permset_d == NULL ||
      (perm != ZOS_ACL_PERM_R && perm != ZOS_ACL_PERM_W &&
       perm != ZOS_ACL_PERM_X)) {
    errno = EINVAL; // not one of the three permissions
    return -1;
  }
  return (*(unsigned char *)permset_d & perm) == perm ? 1 : 0;
}

void *__acl_get_qualifier(struct zos_acl_entry_desc *entry_d) {
  if (entry_d == NULL || entry_d->ed_index < 3) {
    errno = EINVAL; // the base entries have no qualifier
    return NULL;
  }
  char *mem = (char *)malloc(sizeof(struct zos_acl_obj_header) +
                             sizeof(uid_t));
  if (mem == NULL)
    return NULL;
  ((struct zos_acl_obj_header *)mem)->oh_tag = ZOS_ACL_QUALIFIER_TAG;
  uid_t *id = (uid_t *)(mem + sizeof(struct zos_acl_obj_header));
  *id = (uid_t)zos_acl_entry_real(entry_d)->acle_id;
  return id;
}
