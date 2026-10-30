/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_CALCULATOR_EXAMPLE_SERVICE_H
#define WIRELINK_CALCULATOR_EXAMPLE_SERVICE_H
#include "calculator_kernel.h"
int32_t calculator_add(void *, const add_request_t *, add_response_value_t *);
void calculator_async_example(calculator_kernel_t *, calculator_kernel_t *);
#endif
