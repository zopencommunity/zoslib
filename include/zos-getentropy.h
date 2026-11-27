#ifndef __ZOS_GET_ENTROPY_H_
#define __ZOS_GET_ENTROPY_H_

/* __XPLAT is defined by <features.h>; include it before testing it so the
 * result does not depend on whether a system header was included first. */
#include <features.h>

/* The system's <unistd.h> declares getentropy() only when __XPLAT is
 * defined.  Whenever __XPLAT is absent zoslib must supply it, regardless
 * of target level or _POSIX_C_SOURCE.  This also matches the guards in
 * src/zos-getentropy.cc. */
#if !defined(__XPLAT)
#include "zos-macros.h"
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif
/**
 * Fill a buffer with random bytes
 * \param [out] buffer to store random bytes to.
 * \param [in] number of random bytes to generate.
 * \return On success, returns 0, or -1 on error.
 */
#if defined(ZOSLIB_ENABLE_V2R5_FEATURES)
__Z_EXPORT extern int (*getentropy)(void *, size_t);
__Z_EXPORT int __getentropy(void* buffer, size_t length);
#else
__Z_EXPORT int getentropy(void* buffer, size_t length);
#endif

#ifdef __cplusplus
}
#endif

#endif // __ZOS_GET_ENTROPY_H_
#endif // !__XPLAT
