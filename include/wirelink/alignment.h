/* SPDX-License-Identifier: Apache-2.0 */

#ifndef INCLUDE_WIRELINK_ALIGNMENT_H_
#define INCLUDE_WIRELINK_ALIGNMENT_H_

#include <stddef.h>

/*
 * MSVC's C frontend does not expose C11 max_align_t. Keep the opaque public
 * storage conservatively aligned without making applications special-case
 * allocation on Windows. Sixteen bytes matches the MSVC x64 heap guarantee
 * and is at least as strict as every type stored in Wirelink's contexts.
 */
#if defined(_MSC_VER) && !defined(__clang__)
typedef union wl_max_align {
  __declspec(align(16)) unsigned char byte;
} wl_max_align_t;
#else
typedef max_align_t wl_max_align_t;
#endif

#endif /* INCLUDE_WIRELINK_ALIGNMENT_H_ */
