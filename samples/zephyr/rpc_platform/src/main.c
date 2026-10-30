/* SPDX-License-Identifier: Apache-2.0 */
#include "rpc_validation_kernel.h"
#include "wirelink/storage/fixed_pool.h"
#include "wirelink/zephyr/wait.h"
#include "wirelink/platform.h"
#include <zephyr/sys/printk.h>
#include <string.h>
#include <stdlib.h>
#if defined(CONFIG_CPU_CORTEX_M_HAS_DWT)
#include <cmsis_core.h>
#endif
#define CHECK(x) do { if (!(x)) { printk("RPC_PLATFORM FAIL line=%u: %s\n", __LINE__, #x); abort(); } } while (0)
typedef struct owner owner_t;
typedef struct job {
  owner_t *owner;
  bool download;
  uint32_t began, timeout;
  request_value_t request;
  union { response_value_t response; large_value_t large; } output;
  wl_kernel_result_t result;
  wl_err_t admission;
  struct k_sem admitted, done;
} job_t;
struct owner {
  rpc_validation_kernel_t *endpoint;
  wl_packet_port_t port;
  owner_t *peer;
  uint8_t rx[2][2053];
  struct k_thread thread;
  struct k_msgq commands;
  char command_storage[sizeof(job_t *)];
  wl_zephyr_waiter_t activity;
  wl_waiter_t wait;
  struct k_sem paused;
  atomic_t stopping, pause, waiting_for_space, stalls, passes;
  job_t *completed[WL_KERNEL_CALLS]; /* Handoff after callback/step returns. */
  unsigned completed_count, waits, executions, downloads;
};
static owner_t actors[2];
static job_t jobs[5];
K_THREAD_STACK_DEFINE(client_stack, 4096);
K_THREAD_STACK_DEFINE(server_stack, 4096);
K_THREAD_STACK_DEFINE(stopper_stack, 1024);
static struct k_thread stopper;
static struct k_sem stop_gate;
static union { rpc_validation_kernel_t align; uint8_t bytes[2 * sizeof(rpc_validation_kernel_t)]; } storage;
static wl_fixed_pool_t pool;
static unsigned allocations, frees;
static uint64_t identity = 2000;
static uint32_t roundtrips[100];
static large_value_t large, decoded;
static volatile large_value_t copied;
static uint8_t encoded[2052];
static void wake(void *user) {
  owner_t *actor = user;
  wl_zephyr_waiter_notify(&actor->activity);
  wl_zephyr_waiter_notify(&actor->peer->activity); /* Includes RX space publication. */
}
/* Two sole producers, one per direction. Peer claim/commit is the actual RAM
 * transfer; return SENT only after the full copy, with no retained source. */
static wl_packet_submit_t submit(void *user, const uint8_t *data, size_t length, wl_packet_token_t token) {
  (void)token; owner_t *actor = user; wl_packet_claim_t claim;
  wl_err_t error = wl_packet_rx_claim(&actor->peer->port, length, &claim);
  if (error == WL_ERR_WOULD_BLOCK) {
    atomic_set(&actor->waiting_for_space, 1); atomic_inc(&actor->stalls); return WL_PACKET_BUSY;
  }
  if (error != WL_OK) return WL_PACKET_FAILED;
  memcpy(claim.span.data, data, length);
  error = wl_packet_rx_commit(&actor->peer->port, &claim, length);
  if (error != WL_OK) { (void)wl_packet_rx_abort(&actor->peer->port, &claim); return WL_PACKET_FAILED; }
  return WL_PACKET_SENT;
}
static int32_t execute(void *user, const request_t *request, response_value_t *response) {
  owner_t *actor = user; ++actor->executions;
  CHECK(request->name.length == 3 && memcmp(request->name.data, "a\0b", 3) == 0);
  if (request->input == -1 || request->input == INT32_MAX) return 17;
  response->has_output = response->has_name = true; response->output = request->input + 1;
  response->name.length = 3; memcpy(response->name.data, request->name.data, 3); return 0;
}
static int32_t download(void *user, const empty_t *request, large_value_t *response) {
  (void)request; ++((owner_t *)user)->downloads;
  response->has_data = true; response->data.length = sizeof(response->data.data);
  memset(response->data.data, 0xa5, response->data.length); return 0;
}
static void complete(job_t *job, wl_kernel_result_t result) {
  owner_t *actor = job->owner; job->result = result;
  CHECK(actor->completed_count < WL_KERNEL_CALLS);
  actor->completed[actor->completed_count++] = job;
}
static void done(void *user, wl_kernel_result_t result, const response_t *response) {
  job_t *job = user;
  if (result.outcome == WL_KERNEL_SUCCESS &&
      (!response || response_value_from_view(response, &job->output.response) != WL_CODEC_OK))
    result = (wl_kernel_result_t){WL_KERNEL_LOCAL_ERROR, WL_ERR_CORRUPT_PAYLOAD};
  complete(job, result);
}
static void downloaded(void *user, wl_kernel_result_t result, const large_t *response) {
  job_t *job = user;
  if (result.outcome == WL_KERNEL_SUCCESS &&
      (!response || large_value_from_view(response, &job->output.large) != WL_CODEC_OK))
    result = (wl_kernel_result_t){WL_KERNEL_LOCAL_ERROR, WL_ERR_CORRUPT_PAYLOAD};
  complete(job, result);
}
static void handoff(owner_t *actor) {
  /* Caller may release the job only after callback and the core pass return. */
  unsigned count = actor->completed_count; actor->completed_count = 0;
  for (unsigned i = 0; i < count; ++i) k_sem_give(&actor->completed[i]->done);
}
static void initialize_actors(void) {
  memset(actors, 0, sizeof(actors));
  for (unsigned i = 0; i < 2; ++i) {
    owner_t *actor = &actors[i]; actor->peer = &actors[1U - i];
    CHECK(wl_zephyr_waiter_init(&actor->activity) == WL_OK);
    actor->wait = wl_zephyr_waiter_descriptor(&actor->activity);
    CHECK(k_sem_init(&actor->paused, 0, 1) == 0);
    k_msgq_init(&actor->commands, actor->command_storage, sizeof(job_t *), 1);
  }
}
static wl_err_t create(owner_t *actor, wl_allocator_t allocator, uint64_t instance) {
  ++allocations;
  rpc_validation_kernel_t *endpoint = allocator.allocate(allocator.context, sizeof(*endpoint), _Alignof(rpc_validation_kernel_t));
  if (!endpoint) return WL_ERR_NO_MEM;
  wl_packet_config_t config = {.instance = instance, .rx_storage = &actor->rx[0][0],
    .rx_storage_size = sizeof(actor->rx), .rx_unit_size = sizeof(actor->rx[0]),
    .submit = submit, .notify = wake, .user = actor};
  wl_err_t error = wl_packet_init(&actor->port, &config);
  const rpc_validation_kernel_handlers_t handlers = {.on_rpc_execute = execute, .on_rpc_download = download};
  if (error == WL_OK) error = rpc_validation_kernel_init(endpoint, &actor->port, &handlers, NULL, actor);
  if (error != WL_OK) {
    allocator.deallocate(allocator.context, endpoint, sizeof(*endpoint), _Alignof(rpc_validation_kernel_t));
    ++frees; return error;
  }
  actor->endpoint = endpoint; return WL_OK;
}
static void destroy(owner_t *actor, wl_allocator_t allocator) {
  CHECK(wl_kernel_quiescent(&actor->endpoint->core));
  allocator.deallocate(allocator.context, actor->endpoint, sizeof(*actor->endpoint), _Alignof(rpc_validation_kernel_t));
  actor->endpoint = NULL; ++frees;
}
static void owner(void *user, void *unused1, void *unused2) {
  (void)unused1; (void)unused2; owner_t *actor = user;
  while (!atomic_get(&actor->stopping)) {
    if (atomic_get(&actor->pause)) {
      k_sem_give(&actor->paused);
      while (atomic_get(&actor->pause) && !atomic_get(&actor->stopping)) {
        wl_err_t waited = actor->wait.wait(actor->wait.user_data, UINT32_MAX);
        CHECK(waited == WL_OK || waited == WL_ERR_CANCELLED);
      }
      if (atomic_get(&actor->stopping)) break;
    }
    atomic_inc(&actor->passes);
    job_t *job;
    if (k_msgq_get(&actor->commands, &job, K_NO_WAIT) == 0) {
      uint32_t now = k_uptime_get_32(), elapsed = now - job->began;
      wl_kernel_call_handle_t handle;
      wl_err_t error = WL_ERR_TIMEOUT;
      if (elapsed < job->timeout) {
        if (job->download) {
          const empty_value_t request = {0};
          error = rpc_validation_kernel_download_start(actor->endpoint, &request, now,
            job->timeout - elapsed, downloaded, job, &handle);
        } else error = rpc_validation_kernel_execute_start(actor->endpoint, &job->request,
          now, job->timeout - elapsed, done, job, &handle);
      }
      job->admission = error; k_sem_give(&job->admitted);
    }
    if (atomic_get(&actor->waiting_for_space) && wl_packet_rx_available(&actor->peer->port)) {
      atomic_clear(&actor->waiting_for_space); wl_packet_activity(&actor->port);
    }
    uint32_t sampled = k_uptime_get_32();
    CHECK(wl_kernel_step(&actor->endpoint->core, sampled, 16) == WL_OK);
    handoff(actor);
    int has_deadline = 0; uint32_t deadline = 0;
    if (wl_kernel_ready(&actor->endpoint->core, sampled, &has_deadline, &deadline) ||
        k_msgq_num_used_get(&actor->commands)) continue;
    uint32_t remaining = has_deadline ? deadline - sampled : UINT32_MAX;
    remaining = wl_wait_remaining(remaining, sampled, k_uptime_get_32());
    ++actor->waits;
    wl_err_t waited = actor->wait.wait(actor->wait.user_data, remaining);
    CHECK(waited == WL_OK || waited == WL_ERR_NO_DATA || (waited == WL_ERR_CANCELLED && atomic_get(&actor->stopping)));
  }
  job_t *job;
  while (k_msgq_get(&actor->commands, &job, K_NO_WAIT) == 0) {
    job->admission = WL_ERR_NOT_INITIALIZED; k_sem_give(&job->admitted);
  }
  CHECK(wl_kernel_close(&actor->endpoint->core) == WL_OK);
  wl_packet_stop(&actor->port);
  do {
    CHECK(wl_kernel_step(&actor->endpoint->core, k_uptime_get_32(), 16) == WL_OK);
    handoff(actor);
    if (!wl_kernel_quiescent(&actor->endpoint->core)) k_sleep(K_MSEC(1));
  } while (!wl_kernel_quiescent(&actor->endpoint->core));
}
static void enqueue(owner_t *actor, job_t *job, bool download_request, int32_t input, uint32_t timeout) {
  CHECK(k_sem_count_get(&job->admitted) == 0 && k_sem_count_get(&job->done) == 0);
  job->owner = actor; job->download = download_request; job->timeout = timeout;
  job->request = (request_value_t){.has_input = true, .input = input,
    .has_name = true, .name = {.length = 3, .data = {'a', 0, 'b'}}};
  job->began = k_uptime_get_32();
  CHECK(k_msgq_put(&actor->commands, &job, K_NO_WAIT) == 0);
  wl_zephyr_waiter_notify(&actor->activity);
}
static wl_err_t admitted(job_t *job) {
  CHECK(k_sem_take(&job->admitted, K_SECONDS(3)) == 0);
  memset(&job->request, 0, sizeof(job->request)); /* Core already copied on success. */
  return job->admission;
}
static void finished(job_t *job) { CHECK(k_sem_take(&job->done, K_SECONDS(3)) == 0); }
static void pause_owner(owner_t *actor) {
  atomic_set(&actor->pause, 1); wl_zephyr_waiter_notify(&actor->activity);
  CHECK(k_sem_take(&actor->paused, K_SECONDS(3)) == 0);
}
static void resume_owner(owner_t *actor) { atomic_clear(&actor->pause); wl_zephyr_waiter_notify(&actor->activity); }
static void stop_owner(owner_t *actor) { atomic_set(&actor->stopping, 1); wl_zephyr_waiter_stop(&actor->activity); }
static void stop_later(void *unused1, void *unused2, void *unused3) {
  (void)unused1; (void)unused2; (void)unused3;
  CHECK(k_sem_take(&stop_gate, K_SECONDS(3)) == 0); k_sleep(K_MSEC(10)); stop_owner(&actors[0]);
}
static int compare(const void *a, const void *b) {
  uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b; return (x > y) - (x < y);
}
int main(void) {
#if defined(CONFIG_CPU_CORTEX_M_HAS_DWT)
  DCB->DEMCR |= DCB_DEMCR_TRCENA_Msk; DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk; __DSB(); __ISB();
#endif
  initialize_actors();
  CHECK(wl_fixed_pool_init(&pool, storage.bytes, sizeof(storage.bytes), sizeof(rpc_validation_kernel_t),
    _Alignof(rpc_validation_kernel_t), 2) == WL_OK);
  wl_allocator_t allocator = wl_fixed_pool_allocator(&pool);
  CHECK(create(&actors[0], allocator, 0) == WL_ERR_INVALID_ARG && wl_fixed_pool_in_use(&pool) == 0);
  for (unsigned i = 0; i < 2; ++i) CHECK(create(&actors[i], allocator, ++identity) == WL_OK);
  CHECK(create(&actors[0], allocator, ++identity) == WL_ERR_NO_MEM && wl_fixed_pool_in_use(&pool) == 2);
  for (unsigned i = 0; i < 5; ++i) {
    CHECK(k_sem_init(&jobs[i].admitted, 0, 1) == 0); CHECK(k_sem_init(&jobs[i].done, 0, 1) == 0);
  }
  CHECK(k_sem_init(&stop_gate, 0, 1) == 0);
  k_thread_create(&actors[0].thread, client_stack, K_THREAD_STACK_SIZEOF(client_stack), owner, &actors[0], NULL, NULL, 5, 0, K_NO_WAIT);
  k_thread_create(&actors[1].thread, server_stack, K_THREAD_STACK_SIZEOF(server_stack), owner, &actors[1], NULL, NULL, 5, 0, K_NO_WAIT);
  k_sleep(K_MSEC(20));
  int passes = atomic_get(&actors[0].passes) + atomic_get(&actors[1].passes); k_sleep(K_MSEC(20));
  CHECK(atomic_get(&actors[0].passes) + atomic_get(&actors[1].passes) - passes < 20);
  pause_owner(&actors[1]);
  for (unsigned i = 0; i < 4; ++i) { enqueue(&actors[0], &jobs[i], false, 41, 5000); CHECK(admitted(&jobs[i]) == WL_OK); }
  enqueue(&actors[0], &jobs[4], false, 41, 5000); CHECK(admitted(&jobs[4]) == WL_ERR_WOULD_BLOCK);
  CHECK(k_sem_count_get(&jobs[4].done) == 0);
  for (unsigned i = 0; i < 100 && !atomic_get(&actors[0].stalls); ++i) k_sleep(K_MSEC(1));
  CHECK(atomic_get(&actors[0].stalls) > 0); resume_owner(&actors[1]);
  for (unsigned i = 0; i < 4; ++i) { finished(&jobs[i]); CHECK(jobs[i].result.outcome == WL_KERNEL_SUCCESS && jobs[i].output.response.output == 42); }
  unsigned allocated = allocations, freed = frees;
  for (unsigned i = 0; i < 100; ++i) {
    uint32_t began = k_cycle_get_32(); enqueue(&actors[0], &jobs[0], false, 41, 1000);
    CHECK(admitted(&jobs[0]) == WL_OK); finished(&jobs[0]); roundtrips[i] = k_cycle_get_32() - began;
    CHECK(jobs[0].result.outcome == WL_KERNEL_SUCCESS && jobs[0].output.response.output == 42);
  }
  enqueue(&actors[0], &jobs[0], false, -1, 1000); CHECK(admitted(&jobs[0]) == WL_OK); finished(&jobs[0]);
  CHECK(jobs[0].result.outcome == WL_KERNEL_REJECTED && jobs[0].result.status == 17);
  enqueue(&actors[0], &jobs[0], true, 41, 1000); CHECK(admitted(&jobs[0]) == WL_OK); finished(&jobs[0]);
  CHECK(jobs[0].result.outcome == WL_KERNEL_SUCCESS && jobs[0].output.large.data.length == 2023);
  large = jobs[0].output.large;
  pause_owner(&actors[0]); enqueue(&actors[0], &jobs[0], false, 41, 5); k_sleep(K_MSEC(15)); resume_owner(&actors[0]);
  CHECK(admitted(&jobs[0]) == WL_ERR_TIMEOUT && k_sem_count_get(&jobs[0].done) == 0);
  pause_owner(&actors[1]);
  k_thread_create(&stopper, stopper_stack, K_THREAD_STACK_SIZEOF(stopper_stack), stop_later, NULL, NULL, NULL, 4, 0, K_NO_WAIT);
  enqueue(&actors[0], &jobs[0], false, 41, 10000); CHECK(admitted(&jobs[0]) == WL_OK);
  k_sem_give(&stop_gate); finished(&jobs[0]); CHECK(jobs[0].result.outcome == WL_KERNEL_CLOSED);
  CHECK(k_thread_join(&stopper, K_SECONDS(3)) == 0); stop_owner(&actors[1]);
  for (unsigned i = 0; i < 2; ++i) CHECK(k_thread_join(&actors[i].thread, K_SECONDS(3)) == 0);
  CHECK(allocations == allocated && frees == freed && actors[1].executions == 105 && actors[1].downloads == 1);
  size_t unused_main, unused_client, unused_server;
  CHECK(k_thread_stack_space_get(k_current_get(), &unused_main) == 0);
  CHECK(k_thread_stack_space_get(&actors[0].thread, &unused_client) == 0);
  CHECK(k_thread_stack_space_get(&actors[1].thread, &unused_server) == 0);
  printk("RPC_PLATFORM tasks slots=4 hot_alloc=0 stalls=%d waits=%u/%u stack_unused=%u/%u/%u\n",
    (int)atomic_get(&actors[0].stalls), actors[0].waits, actors[1].waits,
    (unsigned)unused_main, (unsigned)unused_client, (unsigned)unused_server);
  for (unsigned i = 0; i < 2; ++i) destroy(&actors[i], allocator);
  CHECK(wl_fixed_pool_in_use(&pool) == 0 && large.data.data[2022] == 0xa5);
  qsort(roundtrips, 100, sizeof(roundtrips[0]), compare);
  printk("RPC_PLATFORM scheduled_RAM cycles p50=%u p95=%u p99=%u irq=on simulator_diagnostic\n", roundtrips[49], roundtrips[94], roundtrips[98]);
  initialize_actors(); CHECK(create(&actors[0], allocator, ++identity) == WL_OK);
  wl_err_t error = WL_OK; uint32_t start; unsigned key = irq_lock(); start = k_cycle_get_32();
  for (unsigned i = 0; i < 128; ++i) error |= wl_kernel_step(&actors[0].endpoint->core, 0, 16);
  uint32_t idle_cycles = k_cycle_get_32() - start; irq_unlock(key); CHECK(error == WL_OK);
  size_t length = 0; wl_codec_status_t codec = WL_CODEC_OK;
  key = irq_lock(); start = k_cycle_get_32();
  for (unsigned i = 0; i < 32; ++i) codec |= large_value_encode(&large, encoded, sizeof(encoded), &length);
  uint32_t encode_cycles = k_cycle_get_32() - start; irq_unlock(key); CHECK(codec == WL_CODEC_OK);
  key = irq_lock(); start = k_cycle_get_32();
  for (unsigned i = 0; i < 32; ++i) codec |= large_value_decode(encoded, length, &decoded);
  uint32_t decode_cycles = k_cycle_get_32() - start; irq_unlock(key); CHECK(codec == WL_CODEC_OK);
  key = irq_lock(); start = k_cycle_get_32();
  for (unsigned i = 0; i < 128; ++i) copied = large;
  uint32_t copy_cycles = k_cycle_get_32() - start; irq_unlock(key);
  CHECK(decoded.data.length == 2023 && copied.data.data[2022] == 0xa5);
  CHECK(wl_kernel_close(&actors[0].endpoint->core) == WL_OK); wl_packet_stop(&actors[0].port);
  CHECK(wl_kernel_step(&actors[0].endpoint->core, 0, 16) == WL_OK); destroy(&actors[0], allocator);
  CHECK(wl_fixed_pool_in_use(&pool) == 0);
  printk("RPC_PLATFORM isolated cycles idle=%u encode2023=%u decode2023=%u volatile_copy=%u irq=off core_reads_per_idle=0\n",
    idle_cycles / 128, encode_cycles / 32, decode_cycles / 32, copy_cycles / 128);
  printk("RPC_PLATFORM pool attempts=%u frees=%u in_use=0 ALL PASS\n", allocations, frees);
  printk("RPC_PLATFORM ALL PASS\n"); return 0;
}
