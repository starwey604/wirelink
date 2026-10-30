/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WIRELINK_USB_SAMPLE_APPLICATION_H
#define WIRELINK_USB_SAMPLE_APPLICATION_H
#include "control.h"
#include "wirelink/kernel.h"
#include <stdbool.h>
#define USB_SAMPLE_PAYLOAD_MAX 2048U
#define USB_SAMPLE_FRAME_MAX (USB_SAMPLE_PAYLOAD_MAX + 4U)
#define USB_SAMPLE_CHUNK_MAX 2016U
#define USB_SAMPLE_OBJECT_MAX (UINT64_C(64) * 1024 * 1024)

typedef struct {
  uint8_t pending[USB_SAMPLE_PAYLOAD_MAX];
  size_t pending_length;
  uint16_t pending_id;
  bool output_pending, active, report_pending;
  uint32_t now, transfer_id, expected_crc, crc, touched_at, started_at;
  uint32_t writes, commits, aborts, duplicates, chunk_size;
  uint64_t total, next;
  uint32_t completed_id, completed_crc, completed_writes, elapsed, report_due;
  uint64_t completed_length;
} usb_sample_application_t;
wl_err_t usb_sample_route(wl_kernel_t *, const wl_kernel_frame_t *);
wl_err_t usb_sample_pass(usb_sample_application_t *, wl_kernel_t *, uint32_t now);
int usb_sample_ready(const usb_sample_application_t *, const wl_kernel_t *, uint32_t now);
uint32_t usb_sample_deadline(const usb_sample_application_t *, uint32_t now);
void usb_sample_close(usb_sample_application_t *);
#endif
