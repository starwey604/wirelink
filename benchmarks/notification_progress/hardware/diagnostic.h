/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WL_RESEARCH_DIAGNOSTIC_H
#define WL_RESEARCH_DIAGNOSTIC_H
#include <stddef.h>
#include <stdint.h>
#define DIAG_EVENTS(X) \
 X(rx_commit) X(rx_release) X(rx_abort) X(tx_publish) \
 X(tx_consumed) X(tx_drained) X(tx_error) X(activity) X(port_notify) \
 X(adapter_notify) X(callback_exit) X(callback_rx) X(callback_tx) \
 X(service) X(service_active) X(rx_enqueue) X(rx_pause) X(tx_submit) X(tx_busy) \
 X(owner_pass) X(step) X(step_empty) X(step_work) X(step_rx) X(step_tx) \
 X(step_terminal) X(step_completion) X(terminal_scan) X(terminal_slot) \
 X(generation_slot) X(ready) X(ready_call_slot) X(ready_tx_slot) \
 X(step_no_calls) X(wait_query) X(wait_ready) X(sem_give) X(sem_saturated) \
 X(sem_take) X(sem_immediate) X(sem_block) X(sem_timeout) X(handler) X(sample)
#define DIAG_ENUM(name) D_##name,
enum diag_event { DIAG_EVENTS(DIAG_ENUM) D_COUNT };
void diag_add(enum diag_event event, uint32_t amount);
#define D(name) diag_add(D_##name, 1U)
void diag_start(void);
void diag_stop(void);
size_t diag_json(char *data, size_t capacity, unsigned page);
#endif
