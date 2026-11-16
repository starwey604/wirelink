/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_EXAMPLE_CHECK_H
#define WIRELINK_EXAMPLE_CHECK_H
#include <stdio.h>
#include <stdlib.h>
#define EXAMPLE_REQUIRE(condition) do { if (!(condition)) { \
  fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); abort(); \
} } while (0)
#endif
