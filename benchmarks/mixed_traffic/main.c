/* SPDX-License-Identifier: Apache-2.0 */
#include "workload.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
  if (argc != 2 || (strcmp(argv[1], "--check") && strcmp(argv[1], "--report"))) {
    fprintf(stderr, "usage: %s --check|--report\n", argv[0]);
    return 2;
  }
  return mixed_traffic_run(!strcmp(argv[1], "--check"));
}
