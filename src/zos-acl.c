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

lacl_t __acl_get_file(const char *path_p, short type) {
  if (path_p == NULL || type != ACL_ACCESS) {
    errno = EINVAL; // z/OS has no default ACLs
    return NULL;
  }

  char epath[1024];
  if (zos_acl_path_a2e(path_p, epath, sizeof(epath)) != 0)
    return NULL;

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
      return acl; // possibly with zero extended entries
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
            return acl;
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
            return zos_acl_synthesize(fst.st_mode);
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
        return zos_acl_synthesize(st.st_mode);
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

int __acl_set_file(const char *path_p, short type, lacl_t acl) {
  if (path_p == NULL || type != ACL_ACCESS) {
    errno = EINVAL;
    return -1;
  }
  if (acl == NULL)
    return 0;

  char epath[1024];
  if (zos_acl_path_a2e(path_p, epath, sizeof(epath)) != 0)
    return -1;

  struct ACL *hdr = &acl->lacl_data.aclb_entry;
  // An empty ACL removes the extended ACL of the destination file and
  // restores the base permissions (owner/group/other) of the source.
  // The update runs through a file descriptor, so a restrictive mode
  // cannot make the removal or the rollback fail to resolve the path.
  // The permissions are first narrowed to the intersection of the old
  // and the new mode, so no intermediate state is broader than either;
  // on failure the old mode and the old extended ACL are restored.  The
  // setuid, setgid and sticky bits are not part of an access ACL and
  // are kept.
  if (hdr->acl_entnum <= 0) {
    if (acl->lacl_base != 3)
      return __acl_delete_file_le(epath, ACL_ACCESS);

    mode_t new_mode = 0;
    for (int i = 0; i < 3; i++)
      new_mode = (new_mode << 3) | ACL_PERM(&acl->lacl_base_entry[i]);

    // The special bits are kept; stat() works even for a destination
    // that cannot be opened.
    struct stat st;
    int have_st = stat(path_p, &st) == 0;
    if (have_st)
      new_mode |= st.st_mode & (S_ISUID | S_ISGID | S_ISVTX);
    mode_t old_mode = have_st ? (st.st_mode & 07777) : new_mode;

    // Open the destination first, so a restrictive mode cannot make the
    // removal or the rollback fail to resolve the path.  O_NONBLOCK
    // keeps the open from blocking on a FIFO, O_NOCTTY keeps a terminal
    // destination from becoming the controlling terminal, O_CLOEXEC
    // keeps the descriptor from leaking into spawned processes.  An already restricted
    // destination, which the owner cannot open without read permission,
    // falls back to path based operations.
    int fd = open(path_p, O_RDONLY | O_NONBLOCK | O_NOCTTY | O_CLOEXEC);
    if (fd < 0) {
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
      if (have_st && (st.st_mode & (S_ISUID | S_ISGID | S_ISVTX)) != 0) {
        errno = EACCES;
        return -1;
      }
      return chmod(path_p, new_mode & 0777);
    }

    // The descriptor is pinned; trust it over the earlier stat(), which
    // may describe a different inode after a rename.  Only the rwx bits
    // carry over from it; the special bits come from the pinned
    // descriptor alone.  Without the fstat() neither the new nor the
    // old mode is known reliably: refuse before modifying anything.
    struct stat fst;
    if (fstat(fd, &fst) != 0) {
      close(fd);
      return -1;
    }
    new_mode = (new_mode & 0777) | (fst.st_mode & (S_ISUID | S_ISGID | S_ISVTX));
    old_mode = fst.st_mode & 07777;

    // Applying a base mode that grants write to group or other while
    // the destination carries setuid/setgid would arm privilege on
    // writable content; chmod on Linux clears the bits in that case.
    // The old mode counts as well, like in the non-empty branch: a
    // descriptor opened for writing before the update keeps write
    // access through the narrowing.  Refuse all of these.
    if ((new_mode & (S_ISUID | S_ISGID)) != 0 &&
        ((new_mode & (S_IWGRP | S_IWOTH)) != 0 ||
         (old_mode & (S_IWGRP | S_IWOTH)) != 0)) {
      close(fd);
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
    if (snap_error) {
      close(fd);
      return -1;
    }
    if (saved != NULL &&
        (((new_mode & S_ISVTX) != 0 && S_ISDIR(fst.st_mode) &&
          zos_acl_grants(saved, fst.st_uid, 03)) ||
         ((new_mode & (S_ISUID | S_ISGID)) != 0 &&
          zos_acl_grants(saved, fst.st_uid, 02)))) {
      __acl_free_le(saved);
      close(fd);
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
    close(fd);
    return rc;
  }

  // ACL_ADD replaces the destination ACL; ACL_MODIFY would merge.  The
  // handle is not modified: the masks were normalized on retrieval.
  // The set request applies the base entries as rwx bits only and
  // clears the special bits, which are not part of an access ACL.  The
  // whole update runs through a descriptor pinned to the destination
  // inode, so the safety checks, the set request and the special-bit
  // restoration all target the same file even across a concurrent
  // rename; destinations that cannot be pinned are refused.
  // A failed open leaves its own errno (ENOENT, EACCES, ...) for the
  // caller.
  int fd = open(path_p, O_RDONLY | O_NONBLOCK | O_NOCTTY | O_CLOEXEC);
  if (fd < 0)
    return -1;
  struct stat fst;
  if (fstat(fd, &fst) != 0) {
    close(fd);
    return -1;
  }
  mode_t special = fst.st_mode & (S_ISUID | S_ISGID | S_ISVTX);

  // A sticky directory whose incoming ACL grants write and search to
  // non-owners would let them unlink or rename other users' files while
  // the sticky bit is cleared between the set request and its
  // restoration; refuse rather than risk that.
  // A setuid/setgid destination whose incoming ACL grants write to
  // non-owners could be modified by them while the privilege bits are
  // cleared, and the restoration would make their content privileged;
  // the destination's own mode counts as well, because a non-owner may
  // hold a descriptor opened for writing before the update — write
  // access is checked at open time only.  Refuse all of these.
  if (((special & S_ISVTX) != 0 && S_ISDIR(fst.st_mode) &&
       zos_acl_grants(acl, fst.st_uid, 03)) ||
      ((special & (S_ISUID | S_ISGID)) != 0 &&
       (zos_acl_grants(acl, fst.st_uid, 02) ||
        (fst.st_mode & (S_IWGRP | S_IWOTH)) != 0))) {
    close(fd);
    errno = EPERM;
    return -1;
  }

  // Snapshot for the rollback when the special bits need restoring
  // after the set request.
  lacl_t saved = NULL;
  int snap_error = 0;
  if (special != 0) {
    saved = zos_acl_get_fd(fd, &snap_error);
    if (snap_error) {
      close(fd);
      return -1;
    }
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
      close(fd);
      errno = EPERM;
      return -1;
    }
  }

  int rc = __acl_set_fd_le(fd, ACL_ACCESS, acl, ACL_ADD, NULL);
  if (rc == 0 && special != 0) {
    mode_t base = 0;
    for (int i = 0; i < 3; i++)
      base = (base << 3) | ACL_PERM(&acl->lacl_base_entry[i]);
    rc = fchmod(fd, base | special);
    if (rc != 0) {
      // Roll back to the old extended entries (or their absence) and
      // the old mode, like the empty-ACL path does.  The saved entries
      // carry the old rwx bits, so restoring them keeps the mode
      // narrow; without saved entries the mode is narrowed first,
      // because the incoming entries may be the only thing denying
      // access that the incoming base permissions grant — leave them in
      // place when the narrowing fails.
      int save_errno = errno;
      if (saved != NULL) {
        if (__acl_set_fd_le(fd, ACL_ACCESS, saved, ACL_ADD, NULL) == 0)
          fchmod(fd, fst.st_mode & 07777);
      } else {
        if (fchmod(fd, fst.st_mode & 07777) == 0)
          __acl_delete_fd_le(fd, ACL_ACCESS);
      }
      errno = save_errno;
    }
  }
  if (saved != NULL)
    __acl_free_le(saved);
  close(fd);
  return rc;
}

int __acl_free(void *obj_p) {
  if (obj_p == NULL) {
    errno = EINVAL;
    return -1;
  }
  return __acl_free_le((lacl_t)obj_p);
}
