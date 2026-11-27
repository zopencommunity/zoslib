///////////////////////////////////////////////////////////////////////////////
// Licensed Materials - Property of IBM
// ZOSLIB
// (C) Copyright IBM Corp. 2021. All Rights Reserved.
// US Government Users Restricted Rights - Use, duplication
// or disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
///////////////////////////////////////////////////////////////////////////////

#ifndef ZOS_TIME_H_
#define ZOS_TIME_H_

#include "zos-macros.h"

#include_next <time.h>

/* TODO: Consider removing clock_gettime and nanosleep since they are 
   now available on 2.5 and up */
/* The system's <time.h> declares clockid_t, CLOCK_REALTIME,
 * clock_gettime() and nanosleep() for V2R5 targets when __RT is defined.
 * Provide the definitions zoslib needs whenever the system does not. */
#if !(defined(__RT) && (__EDC_TARGET >= 0x42050000))
#ifndef __clockid_t
  #define __clockid_t    1
  typedef unsigned  int clockid_t;
#endif

// These are defined in the system's time.h  #if __EDC_TARGET >= __EDC_LE4205
#ifndef CLOCK_REALTIME
  #define CLOCK_REALTIME        0
#endif
#ifndef CLOCK_MONOTONIC
  #define CLOCK_MONOTONIC       1
#endif
// These are not defined anywhere as of LE 3.1:
#define CLOCK_HIGHRES           2
#define CLOCK_THREAD_CPUTIME_ID 3
#endif /* !(__RT && __EDC_TARGET >= 0x42050000) */

#if defined(ZOSLIB_ENABLE_V2R5_FEATURES)
#if !(defined(__RT) && (__EDC_TARGET >= 0x42050000))
#if defined(__cplusplus)
extern "C" {
#endif
/* With ZOSLIB_ENABLE_V2R5_FEATURES zoslib resolves these from the LE
 * vector table at startup and exposes function pointers instead. */
__Z_EXPORT extern int (*clock_gettime)(clockid_t cld_id, struct timespec * tp);

/**
 * Suspends the execution of the calling thread until either at least the
 * time specified in *req has elapsed, an event occurs, or a signal arrives.
 * \param [in] req struct used to specify intervals of time with nanosecond
 *  precision
 * \param [out] rem the remaining time if the call is interrupted
 */
__Z_EXPORT extern int (*nanosleep)(const struct timespec*, struct timespec*);
#if defined(__cplusplus)
}
#endif
#endif /* !(__RT && __EDC_TARGET >= 0x42050000) */
#elif !(defined(__RT) && (__EDC_TARGET >= 0x42050000))
#if defined(__cplusplus)
extern "C" {
#endif
__Z_EXPORT int clock_gettime(clockid_t cld_id, struct timespec * tp);

/**
 * Suspends the execution of the calling thread until either at least the
 * time specified in *req has elapsed, an event occurs, or a signal arrives.
 * \param [in] req struct used to specify intervals of time with nanosecond
 *  precision
 * \param [out] rem the remaining time if the call is interrupted
 */
__Z_EXPORT int nanosleep(const struct timespec*, struct timespec*);
#if defined(__cplusplus)
}
#endif
#endif /* !(__RT && __EDC_TARGET >= 0x42050000) */

#if defined(__cplusplus)
extern "C" {
#endif
__Z_EXPORT time_t timegm(struct tm *tm);

#if defined(__cplusplus)
}
#endif

#endif
