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

#else // !(defined(ZOSLIB_OVERRIDE_CLIB) || defined(ZOSLIB_OVERRIDE_CLIB_ACL))

// Without the override, the z/OS declarations pass through unchanged.
#include_next <sys/acl.h>

#endif

#ifdef __cplusplus
extern "C" {
#endif

// Implementation entry points, see src/zos-acl.c.  Declared
// unconditionally, like __open_ascii in <fcntl.h>, so the definitions
// get default visibility for the shared library export side-deck.
__Z_EXPORT extern lacl_t __acl_get_file(const char *path_p, short type);
__Z_EXPORT extern int __acl_set_file(const char *path_p, short type, lacl_t acl);
__Z_EXPORT extern int __acl_free(void *obj_p);

#if defined(ZOSLIB_OVERRIDE_CLIB) || defined(ZOSLIB_OVERRIDE_CLIB_ACL)

// The POSIX 1003.1e style API, bound to the implementation entry points
// above.

// Opaque handle to the ACL of a file: the z/OS working storage.
typedef struct loc_ACL *acl_t;

// Type of ACL to operate on.  Only access ACLs exist on z/OS, so
// ACL_TYPE_DEFAULT is rejected with EINVAL by the functions below.
#define ACL_TYPE_ACCESS  ((acl_type_t)ACL_ACCESS)
#define ACL_TYPE_DEFAULT ((acl_type_t)ACL_DIRDEF)

/**
 * Retrieves the ACL of the file "path_p".
 * \param [in] path_p file name, in ASCII.
 * \param [in] type must be ACL_TYPE_ACCESS.
 * \return a handle in allocated memory; a file without extended ACL
 *   entries yields a handle without entries.  NULL only on error
 *   (errno is set).
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
 * \param [in] type must be ACL_TYPE_ACCESS.
 * \param [in] acl handle from acl_get_file(), may be NULL; not modified.
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

#endif // ZOSLIB_OVERRIDE_CLIB

#ifdef __cplusplus
}
#endif

#endif // ZOS_SYS_ACL_H_
