/* SPDX-License-Identifier: Apache-2.0 */
#ifndef EXAMPLE_SELF_TEST_H
#define EXAMPLE_SELF_TEST_H
#include "device_service.h"
void self_test_bind(device_kernel_handlers_t *);
void self_test_poll(device_service_t *, device_kernel_t *);
#endif
