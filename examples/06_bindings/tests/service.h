/* SPDX-License-Identifier: Apache-2.0 */
#ifndef CALCULATOR_TEST_SERVICE_H
#define CALCULATOR_TEST_SERVICE_H
#include "calculator_values.h"
#ifdef __cplusplus
extern "C" {
#endif
int32_t calculator_test_add(void* context, const add_request_value_t* request,
                            add_response_value_t* response);
#ifdef __cplusplus
}
#endif
#endif
