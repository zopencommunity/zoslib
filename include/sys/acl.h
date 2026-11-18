///////////////////////////////////////////////////////////////////////////////
// Licensed Materials - Property of IBM
// ZOSLIB
// (C) Copyright IBM Corp. 2026. All Rights Reserved.
// US Government Users Restricted Rights - Use, duplication
// or disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
///////////////////////////////////////////////////////////////////////////////

// POSIX 1003.1e style ACL API for z/OS UNIX files.  The z/OS Language
// Environment provides acl_get_file(), acl_set_file() and acl_free()
// with z/OS specific, incompatible signatures.  When
// ZOSLIB_OVERRIDE_CLIB is defined, this header hides them and declares
// the POSIX signatures instead, implemented in src/zos-acl.c, so that
// portable software works unmodified:
//
//   acl_t acl = acl_get_file("original", ACL_TYPE_ACCESS);
//   ... create a new file, write to it, rename it into place ...
//   acl_set_file("newfile", ACL_TYPE_ACCESS, acl);
//   acl_free(acl);
//
// z/OS imposes two deviations from POSIX: only ACL_TYPE_ACCESS is
// supported (no default ACLs), and a handle cannot be inspected or
// modified entry by entry; it can only be moved between files.

#ifndef ZOS_SYS_ACL_H_
#define ZOS_SYS_ACL_H_

#include "zos-macros.h"

// The system <sys/acl.h> only defines anything when _OPEN_SYS is set.
#ifndef _OPEN_SYS
#define _OPEN_SYS 1
#endif



#if defined(ZOSLIB_OVERRIDE_CLIB) || defined(ZOSLIB_OVERRIDE_CLIB_ACL)

// Hide the declarations of the z/OS acl_* functions, they have
// incompatible signatures.  The structures and typedefs they use pass
// through unchanged.
#undef acl_init
#undef acl_free
#undef acl_first_entry
#undef acl_get_entry
#undef acl_valid
#undef acl_create_entry
#undef acl_delete_entry
#undef acl_update_entry
#undef acl_delete_fd
#undef acl_delete_file
#undef acl_get_fd
#undef acl_get_file
#undef acl_set_fd
#undef acl_set_file
#undef acl_from_text
#undef acl_to_text
#undef acl_sort
#define acl_init         __zos_acl_init_replaced
#define acl_free         __zos_acl_free_replaced
#define acl_first_entry  __zos_acl_first_entry_replaced
#define acl_get_entry    __zos_acl_get_entry_replaced
#define acl_valid        __zos_acl_valid_replaced
#define acl_create_entry __zos_acl_create_entry_replaced
#define acl_delete_entry __zos_acl_delete_entry_replaced
#define acl_update_entry __zos_acl_update_entry_replaced
#define acl_delete_fd    __zos_acl_delete_fd_replaced
#define acl_delete_file  __zos_acl_delete_file_replaced
#define acl_get_fd       __zos_acl_get_fd_replaced
#define acl_get_file     __zos_acl_get_file_replaced
#define acl_set_fd       __zos_acl_set_fd_replaced
#define acl_set_file     __zos_acl_set_file_replaced
#define acl_from_text    __zos_acl_from_text_replaced
#define acl_to_text      __zos_acl_to_text_replaced
#define acl_sort         __zos_acl_sort_replaced

#include_next <sys/acl.h>

#undef acl_init
#undef acl_free
#undef acl_first_entry
#undef acl_get_entry
#undef acl_valid
#undef acl_create_entry
#undef acl_delete_entry
#undef acl_update_entry
#undef acl_delete_fd
#undef acl_delete_file
#undef acl_get_fd
#undef acl_get_file
#undef acl_set_fd
#undef acl_set_file
#undef acl_from_text
#undef acl_to_text
#undef acl_sort

// The system header typedefs acl_entry_t as "struct ACL_entry *"; the
// POSIX entry API needs descriptors that know their position in the
// ACL.  A typedef cannot be redefined — shadow it with a macro.
#define acl_entry_t zos_acl_entry_t

// Permission bits for the permset API, matching the rwx octal digit of
// an entry.  The system header defines these as boolean values for its
// bitfields; redefined only in override mode, where the native API is
// hidden anyway.
#undef ACL_READ
#undef ACL_WRITE
#undef ACL_EXECUTE
#define ACL_READ    04
#define ACL_WRITE   02
#define ACL_EXECUTE 01

#else // !(defined(ZOSLIB_OVERRIDE_CLIB) || defined(ZOSLIB_OVERRIDE_CLIB_ACL))

// Without the override, the z/OS declarations pass through unchanged.
#include_next <sys/acl.h>

#endif

// The public handle: the z/OS working storage, owned by the handle,
// plus the iteration state and the entry descriptors.  The entry
// descriptors index the base entries (owner, group, other) as 0..2 and
// the extended entries as 3..., which is what distinguishes the POSIX
// object tags from the extended ones.
// The entry descriptor pointer; acl_entry_t is shadowed by a macro in
// the override section, because the system header typedefs that name
// differently and a typedef cannot be redefined.  The macro expands to
// this typedef so that "acl_entry_t a, b;" declares two pointers.
typedef struct zos_acl_entry_desc *zos_acl_entry_t;

// One ACL entry: the handle it belongs to and the entry index.
struct zos_acl_entry_desc {
    struct zos_acl_handle *ed_acl;
    int ed_index;
};

struct zos_acl_handle {
    lacl_t zh_le;			   // z/OS working storage
    int zh_cursor;			   // iteration state
    int zh_default;			   // set for a default-ACL handle
    struct zos_acl_entry_desc zh_desc[1]; // base + entries descriptors
};

// Opaque handle to the ACL of a file.
typedef struct zos_acl_handle *acl_t;
// Opaque descriptor of one ACL entry, valid as long as the handle:
// the acl_entry_t name is shadowed by a macro in the override section,
// because the system header typedefs it differently.
// Opaque permission set of an entry, pointing into the handle.
typedef struct ACL_perm *acl_permset_t;

// Entry ids for acl_get_entry().
#define ACL_FIRST_ENTRY 0
#define ACL_NEXT_ENTRY  1

// POSIX tag types.  ACL_USER, ACL_GROUP and ACL_OTHER come from the
// system header (the z/OS values); the object tags have no z/OS
// counterpart and get their own values.  ACL_MASK is never returned:
// z/OS has no mask entries.
#define ACL_USER_OBJ  0x1000
#define ACL_GROUP_OBJ 0x2000
#define ACL_MASK      0x4000

// Permission bits for the permset API, matching the rwx octal digit of
// an entry.  The system header defines these as 1; redefined here.


#ifdef __cplusplus
extern "C" {
#endif

// Implementation entry points, see src/zos-acl.c.  Declared
// unconditionally, like __open_ascii in <fcntl.h>, so the definitions
// get default visibility for the shared library export side-deck.
__Z_EXPORT extern struct zos_acl_handle *__acl_get_file(const char *path_p,
                                                        short type);
__Z_EXPORT extern int __acl_set_file(const char *path_p, short type,
                                     struct zos_acl_handle *acl);
__Z_EXPORT extern int __acl_free(void *obj_p);
__Z_EXPORT extern struct zos_acl_handle *__acl_get_fd(int fd);
__Z_EXPORT extern int __acl_set_fd(int fd, struct zos_acl_handle *acl);
__Z_EXPORT extern struct zos_acl_handle *__acl_from_text(const char *buf_p);
__Z_EXPORT extern char *__acl_to_text(struct zos_acl_handle *acl, ssize_t *len_p);
__Z_EXPORT extern int __acl_delete_def_file(const char *path_p);
__Z_EXPORT extern int __acl_get_entry(struct zos_acl_handle *acl, int entry_id,
                                      struct zos_acl_entry_desc **entry_p);
__Z_EXPORT extern int __acl_get_tag_type(struct zos_acl_entry_desc *entry_d,
                                        int *tag_type_p);
__Z_EXPORT extern int __acl_get_permset(struct zos_acl_entry_desc *entry_d,
                                        struct ACL_perm **permset_p);
__Z_EXPORT extern int __acl_get_perm(struct ACL_perm *permset_d, int perm);
__Z_EXPORT extern void *__acl_get_qualifier(struct zos_acl_entry_desc *entry_d);

#if defined(ZOSLIB_OVERRIDE_CLIB) || defined(ZOSLIB_OVERRIDE_CLIB_ACL)

// The POSIX 1003.1e style API, bound to the implementation entry points
// above.



// Type of ACL to operate on.  Only access ACLs exist on z/OS, so
// ACL_TYPE_DEFAULT is rejected with EINVAL by the functions below.
#define ACL_TYPE_ACCESS  ((acl_type_t)ACL_ACCESS)
#define ACL_TYPE_DEFAULT ((acl_type_t)ACL_DIRDEF)



/**
 * Retrieves the ACL of the file "path_p".
 * \param [in] path_p file name, in ASCII.
 * \param [in] type ACL_TYPE_ACCESS, or ACL_TYPE_DEFAULT for the
 *   default ACL of a directory.
 * \return a handle in allocated memory; a file without extended ACL
 *   entries yields a handle without entries.  NULL only on error
 *   (errno is set).  For ACL_TYPE_DEFAULT the path must be an
 *   existing directory: it yields a handle without entries when it
 *   carries no default ACL.  z/OS has two default ACL types (file and
 *   directory default); the handle carries the file default when one
 *   is stored, the directory default otherwise, and ACL_FILDEF
 *   retrieves the file default alone.  Anything else gets EINVAL.
 */
__Z_EXPORT extern acl_t acl_get_file(const char *path_p, acl_type_t type)
    __asm("__acl_get_file");

/**
 * Applies an ACL previously retrieved with acl_get_file() to the file
 * "path_p", replacing the extended ACL the destination file may have:
 * an ACL without entries removes the destination's extended ACL.  The
 * file must already exist.  This also restores the base permissions
 * (owner/group/other) recorded in the ACL.
 * \param [in] path_p file name, in ASCII.
 * \param [in] type ACL_TYPE_ACCESS, or ACL_TYPE_DEFAULT to set the
 *   default ACL of a directory: an empty handle removes the
 *   directory's default ACLs, a handle with entries stores them as
 *   both the file and the directory default (the POSIX single default
 *   concept), and ACL_FILDEF stores the file default alone.  A
 *   default handle carries no base entries.
 * \param [in] acl handle from acl_get_file(), may be NULL; not
 *   modified.
 * \return 0 on success, -1 on error (errno is set).
 */
__Z_EXPORT extern int acl_set_file(const char *path_p, acl_type_t type, acl_t acl)
    __asm("__acl_set_file");

/**
 * Releases an ACL handle.
 * \param [in] obj_p handle from acl_get_file(), may be NULL.
 * \return 0 on success, -1 on error (errno is set).
 */
__Z_EXPORT extern int acl_free(void *obj_p) __asm("__acl_free");

/**
 * Retrieves the ACL of the file referred to by the descriptor "fd".
 * \param [in] fd file descriptor pinned to the file.
 * \return a handle in allocated memory; a file without extended ACL
 *   entries yields a handle without entries.  NULL only on error
 *   (errno is set).
 */
__Z_EXPORT extern acl_t acl_get_fd(int fd) __asm("__acl_get_fd");

/**
 * Applies an ACL previously retrieved with acl_get_file() or
 * acl_get_fd() to the file referred to by the descriptor "fd", with
 * the same semantics as acl_set_file().  The descriptor belongs to
 * the caller and is not closed.
 * \param [in] fd file descriptor pinned to the file.
 * \param [in] acl handle from acl_get_file()/acl_get_fd()/
 *   acl_from_text(), may be NULL; not modified.
 * \return 0 on success, -1 on error (errno is set).
 */
__Z_EXPORT extern int acl_set_fd(int fd, acl_t acl)
    __asm("__acl_set_fd");

/**
 * Converts the text form of an access ACL into a handle.  Entries are
 * separated by commas or newlines: "type::perm" for the base entries
 * and "type:id:perm" for extended entries, e.g.
 * "user::rw-,group::r--,other::r--,user:123:rwx".  Permissions are
 * rwx, '-' or an octal digit; ids are uid/gid numbers or user names.
 * The text form carries a single access ACL, or — as an extension —
 * entries tagged default: or fdefault:, yielding a default-ACL
 * handle for acl_set_file(path, ACL_TYPE_DEFAULT, ...).  Mixing
 * access and default entries is rejected.  The resulting handle is validated: text without the
 * three base entries is rejected with EINVAL (POSIX allows returning
 * an invalid ACL, applying one would set an undefined mode).
 * \param [in] buf_p the ACL text, in ASCII, null terminated.
 * \return a handle in allocated memory, or NULL on error (errno is
 *   set).
 */
__Z_EXPORT extern acl_t acl_from_text(const char *buf_p) __asm("__acl_from_text");

/**
 * Converts an ACL into its text form: comma separated entries of
 * "type::perm" (base) and "type:id:perm" (extended), e.g.
 * "user::rw-,group::r--,other::r--,user:123:rwx".  Ids are rendered as
 * user or group names where they can be resolved, numbers otherwise.
 * \param [in] acl handle from acl_get_file()/acl_get_fd()/
 *   acl_from_text(); not modified.
 * \param [out] len_p if not NULL, receives the text length excluding
 *   the null terminator.
 * \return the text in allocated memory, to be released with
 *   acl_free(), or NULL on error (errno is set).
 */
__Z_EXPORT extern char *acl_to_text(acl_t acl, ssize_t *len_p)
    __asm("__acl_to_text");

/**
 * Removes the default ACL of the directory "path_p".  z/OS has two
 * default ACL types (file and directory default); both are removed.
 * Removing a default ACL that does not exist is not an error.
 * \param [in] path_p directory name, in ASCII.
 * \return 0 on success, -1 on error (errno is set).
 */
__Z_EXPORT extern int acl_delete_def_file(const char *path_p)
    __asm("__acl_delete_def_file");

/**
 * Obtains the next entry of the ACL for the iteration.  The base
 * entries (owner, group, other) come first, then the extended entries.
 * The iteration state lives in the handle, like POSIX; concurrent
 * iteration of a shared handle is therefore not safe (the ACL content
 * is not modified).
 * \param [in] acl handle to iterate.
 * \param [in] entry_id ACL_FIRST_ENTRY to restart or ACL_NEXT_ENTRY
 *   to continue.
 * \param [out] entry_p receives the entry descriptor.
 * \return 1 when an entry was returned, 0 when the ACL is exhausted,
 *   -1 on error (errno is set).
 */
__Z_EXPORT extern int acl_get_entry(acl_t acl, int entry_id,
                                    acl_entry_t *entry_p) __asm("__acl_get_entry");

// The tag type of an ACL entry.
typedef int acl_tag_t;

/**
 * Obtains the tag type of an entry: ACL_USER_OBJ, ACL_GROUP_OBJ or
 * ACL_OTHER for the base entries, ACL_USER or ACL_GROUP for the
 * extended ones.
 * \param [in] entry_d the entry descriptor.
 * \param [out] tag_type_p receives the tag type.
 * \return 0 on success, -1 on error (errno is set).
 */
__Z_EXPORT extern int acl_get_tag_type(acl_entry_t entry_d,
                                       acl_tag_t *tag_type_p)
    __asm("__acl_get_tag_type");

/**
 * Obtains the permission set of an entry, pointing into the handle;
 * valid as long as the handle.  Query it with acl_get_perm().
 */
__Z_EXPORT extern int acl_get_permset(acl_entry_t entry_d,
                                      acl_permset_t *permset_p)
    __asm("__acl_get_permset");

/**
 * Checks whether the permission set contains the permission: ACL_READ,
 * ACL_WRITE or ACL_EXECUTE.
 * \return 1 when contained, 0 when not, -1 on error (errno is set).
 */
__Z_EXPORT extern int acl_get_perm(acl_permset_t permset_d, int perm)
    __asm("__acl_get_perm");

/**
 * Returns the qualifier (uid or gid) of an extended entry, in
 * allocated memory to be released with acl_free().  The base entries
 * have no qualifier: NULL with errno EINVAL.
 */
__Z_EXPORT extern void *acl_get_qualifier(acl_entry_t entry_d)
    __asm("__acl_get_qualifier");

#endif // ZOSLIB_OVERRIDE_CLIB

#ifdef __cplusplus
}
#endif

#endif // ZOS_SYS_ACL_H_
