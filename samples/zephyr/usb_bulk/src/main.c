/* SPDX-License-Identifier: Apache-2.0 */
#include <sample_usbd.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/usb/usbd.h>
#include "application.h"
#include "wirelink/platform.h"
#include "wirelink/zephyr/kernel_usb_bulk.h"
#include "wirelink/zephyr/kernel_timeout.h"

static wl_kernel_t endpoint;
static wl_packet_port_t port;
static wl_kernel_usb_bulk_t adapter;
static usb_sample_application_t application;
static uint8_t snapshots[WL_KERNEL_TX_COUNT][USB_SAMPLE_FRAME_MAX];
static uint8_t rx[2][USB_SAMPLE_FRAME_MAX + 1];
K_SEM_DEFINE(owner_wake, 0, 1);
static void wake(void *user) { (void)user; k_sem_give(&owner_wake); }
#if defined(CONFIG_SAMPLE_WIRELINK_USB_CPU_STATS)
#include <esp_cpu.h>
typedef struct { atomic_t calls, cycles, maximum; } irq_region_t;
static irq_region_t dwc2_isr, dwc2_thread, usbd_thread;
static uint64_t pass_calls, pass_cycles;
static uint32_t pass_max;
uint32_t wirelink_usb_cpu_cycle_count(void) { return (uint32_t)esp_cpu_get_cycle_count(); }
static void record(irq_region_t *region, uint32_t cycles) {
  atomic_inc(&region->calls); atomic_add(&region->cycles, (atomic_val_t)cycles);
  atomic_val_t previous = atomic_get(&region->maximum);
  while (cycles > (uint32_t)previous && !atomic_cas(&region->maximum, previous, (atomic_val_t)cycles))
    previous = atomic_get(&region->maximum);
}
/* Optional externally instrumented driver hooks; zero calls means unobserved. */
void wirelink_usb_cpu_record_dwc2_isr(uint32_t cycles) { record(&dwc2_isr, cycles); }
void wirelink_usb_cpu_record_dwc2_thread(uint32_t cycles) { record(&dwc2_thread, cycles); }
void wirelink_usb_cpu_record_usbd_thread(uint32_t cycles) { record(&usbd_thread, cycles); }
static uint32_t adapter_cycles(void *user) { (void)user; return wirelink_usb_cpu_cycle_count(); }
static void print_region(const char *name, const irq_region_t *region) {
  printk("wirelink_usb_kernel_cpu_v2,region,%s,calls,%u,cycles_mod32,%u,max,%u\n", name,
    (unsigned)atomic_get(&region->calls), (unsigned)atomic_get(&region->cycles),
    (unsigned)atomic_get(&region->maximum));
}
static void print_stats(void) {
  wl_kernel_usb_bulk_stats_t stats;
  wl_kernel_usb_bulk_get_stats(&adapter, &stats);
  printk("wirelink_usb_kernel_cpu_v2,region,application_pass,calls,%llu,cycles,%llu,max,%u\n",
    (unsigned long long)pass_calls, (unsigned long long)pass_cycles, pass_max);
  printk("wirelink_usb_kernel_cpu_v2,rx,%u,tx,%u,rx_callback_cycles_mod32,%u,tx_callback_cycles_mod32,%u,"
    "sink_cycles_mod32,%u,active_service_cycles_mod32,%u,errors,%u\n", stats.rx_completions,
    stats.tx_completions, stats.rx_callback_cycles, stats.tx_callback_cycles,
    stats.tx_sink_cycles, stats.active_service_cycles, stats.errors);
  print_region("dwc2_isr", &dwc2_isr); print_region("dwc2_thread", &dwc2_thread);
  print_region("usbd_thread", &usbd_thread);
}
#endif
static void usb_message(struct usbd_context *context, const struct usbd_msg *message) {
  if (usbd_can_detect_vbus(context) && message->type == USBD_MSG_VBUS_READY) (void)usbd_enable(context);
  if (usbd_can_detect_vbus(context) && message->type == USBD_MSG_VBUS_REMOVED) (void)usbd_disable(context);
}
int main(void) {
  wl_environment_t environment = wl_platform_environment();
  uint64_t instance;
  int error = wl_session_next(environment.session, 0, &instance);
  if (error != WL_OK) { printk("Fresh USB instance unavailable: %d\n", error); return 1; }
  wl_packet_config_t packet = {.instance = instance, .rx_storage = &rx[0][0],
    .rx_unit_size = sizeof(rx[0]), .rx_storage_size = sizeof(rx), .submit = wl_kernel_usb_bulk_submit,
    .notify = wl_kernel_usb_bulk_notify, .user = &adapter};
  if (wl_packet_init(&port, &packet) != WL_OK) return 1;
  wl_kernel_config_t kernel = {.port = &port, .route = usb_sample_route, .user = &application};
  for (unsigned i = 0; i < WL_KERNEL_TX_COUNT; ++i)
    kernel.tx_storage[i] = (wl_span_t){snapshots[i], sizeof(snapshots[i])};
  if (wl_kernel_init(&endpoint, &kernel) != WL_OK) return 1;
  wl_kernel_usb_bulk_config_t physical = {.port = &port, .maximum_frame_size = USB_SAMPLE_FRAME_MAX,
    .wake_consumer = wake,
#if defined(CONFIG_SAMPLE_WIRELINK_USB_CPU_STATS)
    .cycle_counter = adapter_cycles,
#endif
  };
  error = wl_kernel_usb_bulk_init(&adapter, &physical);
  if (error != WL_OK) { printk("USB adapter init: %d\n", error); return 1; }
  struct usbd_context *usb = sample_usbd_init_device(usb_message);
  if (usb == NULL || (!usbd_can_detect_vbus(usb) && usbd_enable(usb) != 0)) {
    error = WL_ERR_IO;
  } else printk("Wirelink kernel native/NONE raw echo + application object sink ready payload=%u chunk=%u\n",
    USB_SAMPLE_PAYLOAD_MAX, USB_SAMPLE_CHUNK_MAX);
#if defined(CONFIG_SAMPLE_WIRELINK_USB_CPU_STATS)
  uint32_t next_stats = k_uptime_get_32() + 1000;
#endif
  while (error == WL_OK) {
    uint32_t now = k_uptime_get_32();
    error = wl_kernel_usb_bulk_service(&adapter);
    if (error == WL_ERR_WOULD_BLOCK) error = WL_OK;
    if (error == WL_ERR_IO && wl_kernel_usb_bulk_rx_fault(&adapter) != WL_KERNEL_USB_RX_FAULT_NONE) {
      printk("USB RX fault=%d; explicit re-enumeration\n", wl_kernel_usb_bulk_rx_fault(&adapter));
      error = wl_kernel_usb_bulk_recover(&adapter);
      if (error == WL_ERR_WOULD_BLOCK) error = WL_OK;
    }
    if (error != WL_OK) break;
#if defined(CONFIG_SAMPLE_WIRELINK_USB_CPU_STATS)
    uint32_t began = wirelink_usb_cpu_cycle_count();
#endif
    error = usb_sample_pass(&application, &endpoint, now);
#if defined(CONFIG_SAMPLE_WIRELINK_USB_CPU_STATS)
    uint32_t cycles = wirelink_usb_cpu_cycle_count() - began;
    ++pass_calls; pass_cycles += cycles; if (cycles > pass_max) pass_max = cycles;
#endif
    if (error == WL_ERR_CORRUPT_PAYLOAD || error == WL_ERR_NOT_SUPPORTED) {
      printk("USB rejected malformed/unsupported packet: %d\n", error); error = WL_OK;
    }
    if (error != WL_OK) break;
    if (application.report_pending && (int32_t)(now - application.report_due) >= 0 &&
        !application.output_pending && !wl_packet_tx_active(&port)) {
      printk("wirelink_object_rx_v1,transfer_id,%u,bytes,%llu,elapsed_ms,%u,crc32c,0x%08x,write_calls,%u\n",
        application.completed_id, (unsigned long long)application.completed_length,
        application.elapsed, application.completed_crc, application.completed_writes);
      application.report_pending = false;
    }
#if defined(CONFIG_SAMPLE_WIRELINK_USB_CPU_STATS)
    if ((int32_t)(now - next_stats) >= 0) { print_stats(); next_stats = now + 1000; }
#endif
    uint32_t maximum = usb_sample_deadline(&application, now);
    if (application.report_pending && (int32_t)(application.report_due - now) > 0 &&
        application.report_due - now < maximum) maximum = application.report_due - now;
#if defined(CONFIG_SAMPLE_WIRELINK_USB_CPU_STATS)
    if (next_stats - now < maximum) maximum = next_stats - now;
#endif
    uint32_t elapsed = k_uptime_get_32() - now;
    if (usb_sample_ready(&application, &endpoint, now) || wl_kernel_usb_bulk_ready(&adapter) ||
        (maximum != UINT32_MAX && elapsed >= maximum)) continue;
    (void)k_sem_take(&owner_wake, wl_kernel_timeout_ms(maximum == UINT32_MAX ? maximum : maximum - elapsed));
  }
  printk("USB owner stopping: %d\n", error);
  usb_sample_close(&application); (void)wl_kernel_close(&endpoint);
  (void)wl_kernel_usb_bulk_stop(&adapter);
  while (!wl_kernel_usb_bulk_stopped(&adapter) || !wl_kernel_quiescent(&endpoint)) {
    (void)wl_kernel_usb_bulk_service(&adapter);
    (void)wl_kernel_step(&endpoint, k_uptime_get_32(), 16);
    if (!wl_kernel_usb_bulk_stopped(&adapter) || !wl_kernel_quiescent(&endpoint))
      (void)k_sem_take(&owner_wake, K_MSEC(1));
  }
  return wl_kernel_usb_bulk_deinit(&adapter) == WL_OK ? 0 : 1;
}
