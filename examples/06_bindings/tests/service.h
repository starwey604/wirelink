/* SPDX-License-Identifier: Apache-2.0 */
#ifndef CALCULATOR_TEST_SERVICE_H
#define CALCULATOR_TEST_SERVICE_H
#include "peer.h"
#ifdef __cplusplus
extern "C" {
#endif
int32_t calculator_test_add(void* context, const peer_add_request_t* request,
                            peer_add_response_t* response);
#ifdef __cplusplus
}
#endif
#endif
