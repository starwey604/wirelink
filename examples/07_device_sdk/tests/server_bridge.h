/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <wirelink/endpoint.h>
#ifdef __cplusplus
extern "C" {
#endif
size_t device_test_server_size(void);
size_t device_test_server_alignment(void);
wl_err_t device_test_server_init(void*, wl_endpoint_driver_t*);
#ifdef __cplusplus
}
#endif
