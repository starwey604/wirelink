/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_TEST_THREADED_FIXTURE_H
#define WIRELINK_TEST_THREADED_FIXTURE_H
#ifdef __cplusplus
extern "C" {
#endif
/* C owns all atomic storage. C++ supplies only portable thread creation/join. */
void wl_test_thread_setup(void);
void wl_test_thread_produce(void);
void wl_test_thread_consume(void);
void wl_test_thread_finish(void);
#ifdef __cplusplus
}
#endif
#endif
