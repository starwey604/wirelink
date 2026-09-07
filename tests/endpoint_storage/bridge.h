/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_STORAGE_BRIDGE_H
#define WIRELINK_STORAGE_BRIDGE_H
#include <stdint.h>
#if defined(_WIN32)
#if defined(STORAGE_BRIDGE_BUILD)
#define STORAGE_API __declspec(dllexport)
#else
#define STORAGE_API __declspec(dllimport)
#endif
#else
#define STORAGE_API __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
STORAGE_API void *storage_bridge_create(int fail_second);
STORAGE_API int storage_bridge_call(void *object, int32_t left, int32_t *sum);
STORAGE_API int storage_bridge_close(void *object);
STORAGE_API void storage_bridge_destroy(void *object);
STORAGE_API unsigned storage_bridge_allocations(void *object);
STORAGE_API unsigned storage_bridge_deallocations(void *object);
#ifdef __cplusplus
}
#endif
#endif
