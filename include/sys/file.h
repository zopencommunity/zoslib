///////////////////////////////////////////////////////////////////////////////
// Licensed Materials - Property of IBM
// ZOSLIB
// (C) Copyright IBM Corp. 2023. All Rights Reserved.
// US Government Users Restricted Rights - Use, duplication
// or disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
///////////////////////////////////////////////////////////////////////////////

#ifndef ZOS_SYSFILE_H_
#define ZOS_SYSFILE_H_

#include "zos-macros.h"

/* zoslib provides flock when the system does not.
 * The system's sys/file.h only provides flock when:
 *   __EDC_TARGET >= __EDC_LE4205 AND __XPLAT is defined
 * Since features.h itself gates __XPLAT with __EDC_TARGET >= __EDC_LE4205,
 * __XPLAT is impossible when __TARGET_LIB__ < 0x42050000, so the first
 * branch needs no !defined(__XPLAT) guard - zoslib's flock is always
 * needed there. Only the >= 0x42050000 branch needs the __XPLAT guard.
 */
#if (__TARGET_LIB__ < 0x42050000) || \
    (__TARGET_LIB__ >= 0x42050000 && \
    (((_POSIX_C_SOURCE + 0) < 200809L) && !defined(_XPLATFORM_SOURCE) && \
     !defined(__XPLAT)))
#include <sys/file.h>

#define   LOCK_SH  0x01    // shared file lock
#define   LOCK_EX  0x02    // exclusive file lock
#define   LOCK_NB  0x04    // do not block when locking
#define   LOCK_UN  0x08    // unlock file

#if defined(__cplusplus)
extern "C" {
#endif
__Z_EXPORT int flock(int fd, int operation) __asm("__flock");
#if defined(__cplusplus)
}
#endif
#else
#include_next <sys/file.h>
#endif

#endif
