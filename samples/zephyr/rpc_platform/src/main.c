/* SPDX-License-Identifier: Apache-2.0 */
#include "rpc_validation_endpoint.h"
#include "wirelink/storage/fixed_pool.h"
#include "wirelink/zephyr/wait.h"
#include "wirelink/port.h"
#include <zephyr/sys/printk.h>
#include <stdlib.h>
#if defined(CONFIG_CPU_CORTEX_M_HAS_DWT)
#include <cmsis_core.h>
#endif

#define CHECK(x) do { if (!(x)) { printk("RPC_H2 FAIL line=%u: %s\n", __LINE__, #x); abort(); } } while (0)
typedef struct port {
  rpc_validation_endpoint_t *endpoint;
  struct port *peer;
  wl_zephyr_waiter_t activity;
  wl_waiter_t base_wait;
  unsigned reads, waits, handlers;
  atomic_t closed;
  atomic_t waiting_for_space;
  uint8_t receive[2 * RPC_VALIDATION_ENDPOINT_UNIT_CAPACITY];
  bool drop;
} port_t;
static port_t client, server;
static union {
  rpc_validation_endpoint_t alignment;
  uint8_t bytes[2 * sizeof(rpc_validation_endpoint_t)];
} memory;
static wl_fixed_pool_t pool;
static unsigned allocations, deallocations;
static bool fail_allocation;
static struct k_thread server_thread, stopper_thread;
K_THREAD_STACK_DEFINE(server_stack, 4096);
K_THREAD_STACK_DEFINE(stopper_stack, 1024);
static uint32_t roundtrips[100];
static large_value_t large, decoded;
static volatile large_value_t copied;
static uint8_t encoded[2048];
static unsigned callbacks;

static void cycle_init(void) {
#if defined(CONFIG_CPU_CORTEX_M_HAS_DWT)
  DCB->DEMCR |= DCB_DEMCR_TRCENA_Msk;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  __DSB();
  __ISB();
#endif
}
static uint32_t cycle_now(void) {
#if defined(CONFIG_CPU_CORTEX_M_HAS_DWT)
  return DWT->CYCCNT; /* IRQ-independent; SysTick cannot count multiple masked wraps. */
#else
  return k_cycle_get_32(); /* Simulator functional diagnostics only. */
#endif
}

static void *allocate(void *context, size_t size, size_t alignment) {
  CHECK(context == &pool);
  ++allocations;
  if (fail_allocation) return NULL;
  wl_allocator_t backend = wl_fixed_pool_allocator(&pool);
  return backend.allocate(backend.context, size, alignment);
}
static void deallocate(void *context, void *pointer, size_t size, size_t alignment) {
  CHECK(context == &pool);
  ++deallocations;
  memset(pointer, 0xdd, size);
  wl_allocator_t backend = wl_fixed_pool_allocator(&pool);
  backend.deallocate(backend.context, pointer, size, alignment);
}
static wl_time_ms_t now(void *context) {
  ++((port_t *)context)->reads;
  return k_uptime_get_32();
}
static wl_err_t wait(void *context, uint32_t maximum) {
  port_t *port = context;
  ++port->waits;
  /* Called after the owner drained RX; give a blocked producer a credit wake.
   * Its failed claim also notifies us, covering the check-to-wait race. */
  if (atomic_cas(&port->peer->waiting_for_space, 1, 0))
    wl_zephyr_waiter_notify(&port->peer->activity);
  return port->base_wait.wait(port->base_wait.user_data, maximum);
}
static void notify(void *context) { wl_zephyr_waiter_notify(&((port_t *)context)->activity); }

/* Test-only task transport. One producer feeds the opposite owner's packet queue.
 * A whole encoded unit is committed or BUSY; never retry a partial frame.
 * This is RAM traffic, not USB/UART or a DMA hardware test. */
static wl_sink_result_t send(void *context, wl_io_token_t token, const uint8_t *data, size_t size) {
  port_t *port = context;
  (void)token;
  if (port->drop) return WL_SINK_SENT;
  if (atomic_get(&port->peer->closed)) return WL_SINK_FAILED;
  wl_ctx_t *link = wl_endpoint_link(rpc_validation_endpoint_handle(port->peer->endpoint));
  wl_rx_unit_claim_t claim;
  if (wl_rx_unit_claim(link, size, &claim) != WL_OK) {
    atomic_set(&port->waiting_for_space, 1);
    notify(port->peer);
    return WL_SINK_BUSY;
  }
  memcpy(claim.span.data, data, size);
  CHECK(wl_rx_unit_commit(link, &claim, size) == WL_OK);
  notify(port->peer);
  return WL_SINK_SENT;
}
static void quiesce(void *context) {
  port_t *port = context;
  atomic_set(&port->closed, 1);
  CHECK(wl_set_sink(wl_endpoint_link(rpc_validation_endpoint_handle(port->endpoint)), NULL, NULL) == WL_OK);
}
static int32_t execute(void *context, const request_value_t *request, response_value_t *response) {
  port_t *port = context;
  ++port->handlers;
  if (request->input == -1) return 17;
  response->has_output = response->has_name = true;
  response->output = request->input + 1;
  response->name.length = request->name.length;
  memcpy(response->name.data, request->name.data, request->name.length);
  return 0;
}
static int32_t download(void *context, const empty_value_t *request, large_value_t *response) {
  (void)context; (void)request;
  response->has_data = true;
  response->data.length = 2031;
  memset(response->data.data, 0xa5, response->data.length);
  return 0;
}
static void attach(port_t *port) {
  CHECK(wl_zephyr_waiter_init(&port->activity) == WL_OK);
  port->base_wait = wl_zephyr_waiter_descriptor(&port->activity);
  wl_waiter_t waiter = {wait, port, notify};
  wl_pump_hooks_t adapter = {0};
  adapter.adapter_user_data = port;
  adapter.quiesce = quiesce;
  wl_endpoint_t *handle = rpc_validation_endpoint_handle(port->endpoint);
  const wl_rx_unit_queue_config_t queue = {
    port->receive, sizeof(port->receive), RPC_VALIDATION_ENDPOINT_UNIT_CAPACITY, 2};
  CHECK(wl_rx_unit_queue_init(wl_endpoint_link(handle), &queue) == WL_OK);
  CHECK(wl_endpoint_attach(handle, &adapter) == WL_OK);
  CHECK(wl_endpoint_set_waiter(handle, &waiter) == WL_OK);
  CHECK(wl_set_sink(wl_endpoint_link(handle), send, port) == WL_OK);
}
static void server_run(void *a, void *b, void *c) {
  (void)a; (void)b; (void)c;
  for (;;) {
    CHECK(rpc_validation_endpoint_step(server.endpoint) == WL_OK);
    wl_poll_hint_t hint;
    CHECK(wl_endpoint_get_hint(rpc_validation_endpoint_handle(server.endpoint), &hint) == WL_OK);
    if (hint.work_pending || hint.next_deadline_ms == 0U) continue;
    const int result = wait(&server, hint.next_deadline_ms);
    if (result == WL_ERR_CANCELLED) break;
    CHECK(result == WL_OK || result == WL_ERR_NO_DATA);
  }
  CHECK(rpc_validation_endpoint_close(server.endpoint) == WL_OK);
}
static void stop_client(void *a, void *b, void *c) {
  (void)a; (void)b; (void)c;
  k_msleep(20);
  wl_zephyr_waiter_stop(&client.activity);
}
static int compare(const void *a, const void *b) {
  const uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
  return (x > y) - (x < y);
}
static void done(void *context, const wl_rpc_completion_t *result, const response_value_t *response) {
  (void)context;
  CHECK(result->status == WL_RPC_SUCCESS && response->output == 42);
  ++callbacks;
}

int main(void) {
  cycle_init();
  printk("RPC_H2 ABI=%u capacity=%u endpoint_bytes=%u cycle_hz=%u start\n",
      RPC_VALIDATION_RUNTIME_CODEGEN_ABI_VERSION, (unsigned)RPC_VALIDATION_ENDPOINT_RPC_CAPACITY,
      (unsigned)sizeof(rpc_validation_endpoint_t), sys_clock_hw_cycles_per_sec());
  CHECK(wl_fixed_pool_init(&pool, memory.bytes, sizeof(memory.bytes), sizeof(rpc_validation_endpoint_t),
      RPC_VALIDATION_ENDPOINT_ALIGNMENT, 2) == WL_OK);
  wl_allocator_t allocator = {allocate, deallocate, &pool};
  rpc_validation_endpoint_config_t config;
  CHECK(rpc_validation_endpoint_config_defaults(&config, 101) == WL_OK);
  config.clock = (wl_clock_t){now, &client};
  fail_allocation = true;
  CHECK(rpc_validation_endpoint_create(&client.endpoint, &config, &allocator) == WL_ERR_NO_MEM);
  CHECK(client.endpoint == NULL && wl_fixed_pool_in_use(&pool) == 0);
  fail_allocation = false;
  rpc_validation_endpoint_config_t bad = config;
  bad.clock.now_ms = NULL;
  CHECK(rpc_validation_endpoint_create(&client.endpoint, &bad, &allocator) == WL_ERR_INVALID_ARG);
  CHECK(client.endpoint == NULL && wl_fixed_pool_in_use(&pool) == 0 && deallocations == 1);
  CHECK(rpc_validation_endpoint_create(&client.endpoint, &config, &allocator) == WL_OK);
  config.link.session_id = 102;
  config.clock.user_data = &server;
  config.on_execute = execute;
  config.execute_user_data = &server;
  config.on_download = download;
  CHECK(rpc_validation_endpoint_create(&server.endpoint, &config, &allocator) == WL_OK);
  rpc_validation_endpoint_t *extra = NULL;
  CHECK(rpc_validation_endpoint_create(&extra, &config, &allocator) == WL_ERR_NO_MEM && extra == NULL);
  const unsigned before_allocations = allocations, before_deallocations = deallocations;
  client.peer = &server; server.peer = &client;
  attach(&client); attach(&server);
  k_thread_create(&server_thread, server_stack, K_THREAD_STACK_SIZEOF(server_stack), server_run,
      NULL, NULL, NULL, 5, 0, K_NO_WAIT);
  request_value_t request;
  request_value_clear(&request);
  request.has_input = request.has_name = true;
  request.input = 41; request.name.length = 3;
  memcpy(request.name.data, "abc", 3);
  response_value_t response;
  for (unsigned i = 0; i < 100; ++i) {
    /* IRQ-enabled system clock includes sleep/scheduling; DWT is for CPU work. */
    const uint32_t start = k_cycle_get_32();
    wl_rpc_completion_t result = rpc_validation_endpoint_execute_sync(client.endpoint, &request, &response, 2000);
    roundtrips[i] = k_cycle_get_32() - start;
    if (result.status != WL_RPC_SUCCESS) printk("RPC_H2 call=%u result=%d local=%d runtime=%d transport=%d waits=%u/%u handlers=%u reads=%u/%u\n",
        i, result.status, result.local_error, result.runtime_error, result.transport_error,
        client.waits, server.waits, server.handlers, client.reads, server.reads);
    CHECK(result.status == WL_RPC_SUCCESS && response.output == 42 && response.name.length == 3);
    CHECK(memcmp(response.name.data, "abc", 3) == 0);
  }
  request.input = -1;
  wl_rpc_completion_t result = rpc_validation_endpoint_execute_sync(client.endpoint, &request, &response, 2000);
  CHECK(result.status == WL_RPC_REJECTED && result.rejection == 17 && response.output == 42);
  empty_value_t empty;
  empty_value_clear(&empty);
  result = rpc_validation_endpoint_download_sync(client.endpoint, &empty, &large, 2000);
  CHECK(result.status == WL_RPC_SUCCESS && large.data.length == 2031 && large.data.data[2030] == 0xa5);
  uint32_t completion_cycles = 0;
  request.input = 41;
  for (unsigned i = 0; i < 20; ++i) {
    CHECK(rpc_validation_endpoint_execute_async(client.endpoint, &request, 2000, done, NULL, NULL) == WL_OK);
    while (callbacks == i) {
      const uint32_t start = cycle_now();
      CHECK(rpc_validation_endpoint_step(client.endpoint) == WL_OK);
      if (callbacks != i) { completion_cycles += cycle_now() - start; break; }
      wl_poll_hint_t hint;
      CHECK(wl_endpoint_get_hint(rpc_validation_endpoint_handle(client.endpoint), &hint) == WL_OK);
      if (!hint.work_pending && hint.next_deadline_ms != 0U) {
        const int waited = wait(&client, hint.next_deadline_ms);
        CHECK(waited == WL_OK || waited == WL_ERR_NO_DATA);
      }
    }
  }
  CHECK(allocations == before_allocations && deallocations == before_deallocations);
  client.drop = true;
  k_thread_create(&stopper_thread, stopper_stack, K_THREAD_STACK_SIZEOF(stopper_stack), stop_client,
      NULL, NULL, NULL, 4, 0, K_NO_WAIT);
  result = rpc_validation_endpoint_execute_sync(client.endpoint, &request, &response, 10000);
  CHECK(result.status == WL_RPC_CANCELLED && result.local_error == WL_ERR_CANCELLED);
  CHECK(k_thread_join(&stopper_thread, K_SECONDS(1)) == 0);
  wl_zephyr_waiter_stop(&server.activity);
  CHECK(k_thread_join(&server_thread, K_SECONDS(1)) == 0);
  CHECK(server.handlers == 121 && client.waits != 0 && server.waits != 0);
  size_t unused_main, unused_server;
  CHECK(k_thread_stack_space_get(k_current_get(), &unused_main) == 0);
  CHECK(k_thread_stack_space_get(&server_thread, &unused_server) == 0);
  CHECK(rpc_validation_endpoint_destroy(&client.endpoint) == WL_OK);
  CHECK(rpc_validation_endpoint_destroy(&server.endpoint) == WL_OK);
  CHECK(wl_fixed_pool_in_use(&pool) == 0 && response.output == 42 && large.data.data[2030] == 0xa5);
  qsort(roundtrips, 100, sizeof(roundtrips[0]), compare);
  printk("RPC_H2 tasks calls=122 callbacks=20 hot_alloc=0 waits=%u/%u stack_unused=%u/%u\n",
      client.waits, server.waits, (unsigned)unused_main, (unsigned)unused_server);
  printk("RPC_H2 scheduled_RAM cycles p50=%u p95=%u p99=%u completion_pass_avg=%u irq=on\n",
      roundtrips[49], roundtrips[94], roundtrips[98], completion_cycles / 20);

  /* Isolate CPU paths after both other tasks joined; IRQ-off bounded batches.
   * QEMU/native numbers are functional diagnostics, never H7 performance. */
  CHECK(rpc_validation_endpoint_config_defaults(&config, 103) == WL_OK);
  config.clock = (wl_clock_t){now, &client};
  CHECK(rpc_validation_endpoint_create(&client.endpoint, &config, &allocator) == WL_OK);
  const unsigned reads = client.reads;
  const unsigned key = irq_lock();
  uint32_t start = cycle_now();
  for (unsigned i = 0; i < 128; ++i) CHECK(rpc_validation_endpoint_step(client.endpoint) == WL_OK);
  const uint32_t idle_cycles = cycle_now() - start;
  irq_unlock(key);
#if defined(CONFIG_CPU_CORTEX_M_HAS_DWT)
  CHECK(idle_cycles != 0U); /* A disabled/locked cycle counter is not a measurement. */
#endif
  CHECK(client.reads == reads + 128);
  size_t length = 0;
  unsigned locked = irq_lock();
  start = cycle_now();
  for (unsigned i = 0; i < 32; ++i)
    CHECK(large_value_encode(&large, encoded, sizeof(encoded), &length) == WL_CODEC_OK);
  const uint32_t encode_cycles = cycle_now() - start;
  irq_unlock(locked);
  locked = irq_lock();
  start = k_cycle_get_32();
  for (unsigned i = 0; i < 32; ++i)
    CHECK(large_value_decode(encoded, length, &decoded) == WL_CODEC_OK);
  const uint32_t decode_cycles = cycle_now() - start;
  irq_unlock(locked);
  locked = irq_lock();
  start = k_cycle_get_32();
  for (unsigned i = 0; i < 128; ++i) copied = large;
  const uint32_t copy_cycles = cycle_now() - start;
  irq_unlock(locked);
  CHECK(decoded.data.length == 2031 && copied.data.data[2030] == 0xa5);
  CHECK(k_thread_stack_space_get(k_current_get(), &unused_main) == 0);
  printk("RPC_H2 isolated cycles idle=%u encode2031=%u decode2031=%u volatile_copy=%u irq=off reads_per_idle=1 stack_unused=%u\n",
      idle_cycles / 128, encode_cycles / 32, decode_cycles / 32, copy_cycles / 128, (unsigned)unused_main);
  CHECK(rpc_validation_endpoint_destroy(&client.endpoint) == WL_OK);
  CHECK(wl_fixed_pool_in_use(&pool) == 0 && allocations == deallocations + 2); /* Two NULL failures. */
  printk("RPC_H2 pool attempts=%u frees=%u in_use=0 ALL PASS\n", allocations, deallocations);
  printk("RPC_H2 ALL PASS\n");
  return 0;
}
