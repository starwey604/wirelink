/* SPDX-License-Identifier: Apache-2.0 */
#include "device_service.h"
#include "self_test.h"
#include <string.h>
void device_example_handlers(device_kernel_handlers_t *handlers, device_service_t *state) {
  memset(handlers, 0, sizeof(*handlers));
  device_service_init(state);
  device_service_bind(handlers);
  calculator_service_bind(handlers);
  self_test_bind(handlers);
}
