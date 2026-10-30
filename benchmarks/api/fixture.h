/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_API_FIXTURE_H
#define WIRELINK_API_FIXTURE_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void api_fixture_init(void);
void api_fixture_close(void);
void api_fixture_idle(void);
void api_fixture_rpc(int rejected);
void api_fixture_sample(size_t bytes);
size_t api_fixture_endpoint_bytes(void);
size_t api_fixture_total_bytes(void);
#ifdef __cplusplus
}
#endif
#endif
