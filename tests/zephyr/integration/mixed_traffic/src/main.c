/* SPDX-License-Identifier: Apache-2.0 */
#include <zephyr/ztest.h>
#include "workload.h"

ZTEST(wirelink_mixed_traffic, test_freshness_and_rpc_progress) {
  zassert_ok(mixed_traffic_run(true));
}
ZTEST_SUITE(wirelink_mixed_traffic, NULL, NULL, NULL, NULL, NULL);
