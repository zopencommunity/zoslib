///////////////////////////////////////////////////////////////////////////////
// Licensed Materials - Property of IBM
// ZOSLIB
// (C) Copyright IBM Corp. 2026. All Rights Reserved.
// US Government Users Restricted Rights - Use, duplication
// or disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
///////////////////////////////////////////////////////////////////////////////

// POSIX-named link-time aliases for the ACL entry points.
//
// The implementations live in zos-acl.c under __-prefixed names; the
// POSIX names are bound to them at compile time through the __asm()
// labels in <sys/acl.h>, which requires including that header with the
// override macro defined.  Link-time symbol searches that compile
// nothing — autoconf's AC_CHECK_FUNCS() and AC_SEARCH_LIBS() declare
// the function themselves and only link — cannot see them, so
// configure concludes that the platform has no ACL support.
//
// These aliases give the POSIX names a link-time presence, like the
// plain-named exports zoslib already provides for functions the
// Language Environment lacks (strnlen, getentropy, ...).  They cannot
// conflict with LE: references through the system <sys/acl.h> are
// pragma-mapped to the ||L-prefixed CEL entries and never use the
// plain names, and consumers of the override header bind to the
// __-prefixed symbols.  Nothing links against these but symbol
// searches.
//
// This file must not include <sys/acl.h>: in non-override builds it
// declares the same names with the incompatible z/OS signatures.

#define _AE_BIMODAL 1
#include "zos-base.h"

#include <stddef.h>
#include <sys/types.h>

struct zos_acl_handle;
struct zos_acl_entry_desc;
struct ACL_perm;

extern struct zos_acl_handle *__acl_get_file(const char *path_p, short type);
extern int __acl_set_file(const char *path_p, short type,
                          struct zos_acl_handle *acl);
extern struct zos_acl_handle *__acl_get_fd(int fd);
extern int __acl_set_fd(int fd, struct zos_acl_handle *acl);
extern int __acl_free(void *obj_p);
extern struct zos_acl_handle *__acl_from_text(const char *buf_p);
extern char *__acl_to_text(struct zos_acl_handle *acl, ssize_t *len_p);
extern int __acl_delete_def_file(const char *path_p);
extern int __acl_get_entry(struct zos_acl_handle *acl, int entry_id,
                           struct zos_acl_entry_desc **entry_p);
extern int __acl_get_tag_type(struct zos_acl_entry_desc *entry_d,
                              int *tag_type_p);
extern int __acl_get_permset(struct zos_acl_entry_desc *entry_d,
                             struct ACL_perm **permset_p);
extern int __acl_get_perm(struct ACL_perm *permset_d, int perm);
extern void *__acl_get_qualifier(struct zos_acl_entry_desc *entry_d);

// POSIX: acl_t acl_get_file(const char *path_p, acl_type_t type_d)
__Z_EXPORT struct zos_acl_handle *acl_get_file(const char *path_p,
                                               short type) {
  return __acl_get_file(path_p, type);
}

// POSIX: int acl_set_file(const char *path_p, acl_type_t type_d, acl_t acl)
__Z_EXPORT int acl_set_file(const char *path_p, short type,
                            struct zos_acl_handle *acl) {
  return __acl_set_file(path_p, type, acl);
}

// POSIX: acl_t acl_get_fd(int fd)
__Z_EXPORT struct zos_acl_handle *acl_get_fd(int fd) {
  return __acl_get_fd(fd);
}

// POSIX: int acl_set_fd(int fd, acl_t acl)
__Z_EXPORT int acl_set_fd(int fd, struct zos_acl_handle *acl) {
  return __acl_set_fd(fd, acl);
}

__Z_EXPORT int acl_free(void *obj_p) {
  return __acl_free(obj_p);
}

__Z_EXPORT struct zos_acl_handle *acl_from_text(const char *buf_p) {
  return __acl_from_text(buf_p);
}

__Z_EXPORT char *acl_to_text(struct zos_acl_handle *acl, ssize_t *len_p) {
  return __acl_to_text(acl, len_p);
}

__Z_EXPORT int acl_delete_def_file(const char *path_p) {
  return __acl_delete_def_file(path_p);
}

__Z_EXPORT int acl_get_entry(struct zos_acl_handle *acl, int entry_id,
                             struct zos_acl_entry_desc **entry_p) {
  return __acl_get_entry(acl, entry_id, entry_p);
}

__Z_EXPORT int acl_get_tag_type(struct zos_acl_entry_desc *entry_d,
                                int *tag_type_p) {
  return __acl_get_tag_type(entry_d, tag_type_p);
}

__Z_EXPORT int acl_get_permset(struct zos_acl_entry_desc *entry_d,
                               struct ACL_perm **permset_p) {
  return __acl_get_permset(entry_d, permset_p);
}

__Z_EXPORT int acl_get_perm(struct ACL_perm *permset_d, int perm) {
  return __acl_get_perm(permset_d, perm);
}

__Z_EXPORT void *acl_get_qualifier(struct zos_acl_entry_desc *entry_d) {
  return __acl_get_qualifier(entry_d);
}
