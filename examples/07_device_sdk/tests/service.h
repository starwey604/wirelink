/* SPDX-License-Identifier: Apache-2.0 */
#ifndef DEVICE_TEST_SERVICE_H
#define DEVICE_TEST_SERVICE_H
#include "device.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { uint32_t calls; } device_test_state_t;
int32_t device_test_get_info(void*, const info_request_t*, info_response_t*);
int32_t device_test_configure(void*, const configure_request_t*, configure_response_t*);
#ifdef __cplusplus
}
#endif
#endif
