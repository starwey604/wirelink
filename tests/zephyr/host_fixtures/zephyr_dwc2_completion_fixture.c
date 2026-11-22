/* SPDX-License-Identifier: Apache-2.0 */
/* Host model of registers/buffers around the actual two DWC2 driver functions.
 * Tests software completion decisions, not controller electrical behavior. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define USB_CONTROL_EP_IN 0x80
#define USB_EP_DIR_IN 0x80
#define USB_DWC2_DOEPTSIZN_RXDPID_DATA0 0
#define USB_DWC2_DOEPTSIZN_RXDPID_DATA1 1
#define USB_DWC2_DOEPTSIZN_RXDPID_DATA2 2
#define USB_DWC2_DOEPTSIZN_RXDPID_MDATA 3
#define UDC_EVT_ERROR 1
#define DWC2_DRV_EVT_EP_FINISHED 1
#define BIT(n) (1UL << (n))
#define LOG_ERR(...)                                                                               \
  do {                                                                                             \
  } while (0)
typedef uintptr_t mem_addr_t;
struct udc_ep_config {
  unsigned addr, mps;
};
struct net_buf {
  size_t len, size;
  bool zlp;
};
struct udc_dwc2_data {
  uint32_t rx_siz[2], xfer_finished;
  unsigned ep_disabled, drv_evt;
};
struct usb_dwc2_reg {
  struct {
    uint32_t doeptsiz;
  } out_ep[2];
};
struct device {
  struct udc_dwc2_data *data;
  struct usb_dwc2_reg *regs;
};
static struct udc_ep_config endpoint;
static struct net_buf buffer;
static unsigned continuations, completions;
static bool dma;
static inline struct udc_ep_config *udc_get_ep_cfg(const struct device *d, uint8_t e) {
  (void)d;
  (void)e;
  return &endpoint;
}
static inline struct udc_dwc2_data *udc_get_private(const struct device *d) { return d->data; }
static inline struct usb_dwc2_reg *dwc2_get_base(const struct device *d) { return d->regs; }
static inline bool dwc2_ep_is_iso(const struct udc_ep_config *e) {
  (void)e;
  return false;
}
static inline uint32_t sys_read32(mem_addr_t address) { return *(uint32_t *)address; }
static inline struct net_buf *udc_buf_peek(const struct udc_ep_config *e) {
  (void)e;
  return &buffer;
}
static inline struct net_buf *udc_buf_get(const struct udc_ep_config *e) {
  (void)e;
  return &buffer;
}
static inline int udc_submit_event(const struct device *d, int event, int error) {
  (void)d;
  (void)event;
  (void)error;
  assert(false);
  return 0;
}
static inline uint32_t usb_dwc2_get_doeptsizn_xfersize(uint32_t x) { return x & 0x7ffffU; }
static inline uint32_t usb_dwc2_get_doeptsizn_pktcnt(uint32_t x) { return (x >> 19) & 0x3ffU; }
static inline uint32_t usb_dwc2_get_doeptsizn_rxdpid(uint32_t x) { return x >> 29; }
static inline void net_buf_remove_mem(struct net_buf *b, size_t n) { b->len -= n; }
static inline void net_buf_add(struct net_buf *b, size_t n) { b->len += n; }
static inline size_t net_buf_tailroom(const struct net_buf *b) { return b->size - b->len; }
static inline bool dwc2_in_buffer_dma_mode(const struct device *d) {
  (void)d;
  return dma;
}
static inline bool dwc2_in_completer_mode(const struct device *d) {
  (void)d;
  return !dma;
}
static inline unsigned udc_mps_ep_size(const struct udc_ep_config *e) { return e->mps; }
static inline void dwc2_prep_rx(const struct device *d, struct net_buf *b,
                                struct udc_ep_config *e) {
  (void)d;
  (void)b;
  (void)e;
  ++continuations;
}
static inline void atomic_set_bit(uint32_t *bits, unsigned n) { *bits |= BIT(n); }
static inline void k_event_post(unsigned *events, unsigned bits) { *events |= bits; }
static inline int dwc2_tx_fifo_write(const struct device *d, struct udc_ep_config *e,
                                     struct net_buf *b) {
  (void)d;
  (void)e;
  (void)b;
  ++continuations;
  return 0;
}
static inline bool udc_ep_buf_has_zlp(const struct net_buf *b) { return b->zlp; }
static inline void udc_ep_buf_clear_zlp(struct net_buf *b) { b->zlp = false; }
static inline void udc_ep_set_busy(struct udc_ep_config *e, bool busy) {
  (void)e;
  (void)busy;
}
static inline int udc_submit_ep_event(const struct device *d, struct net_buf *b, int result) {
  (void)d;
  (void)b;
  assert(result == 0);
  ++completions;
  return 0;
}
#include "dwc2_functions.inc"
static void out_test(size_t bytes, unsigned packets, bool continues) {
  struct udc_dwc2_data data = {0};
  struct usb_dwc2_reg registers = {0};
  struct device dev = {&data, &registers};
  endpoint = (struct udc_ep_config){1, 64};
  buffer = (struct net_buf){dma ? 0 : bytes, 512, false};
  continuations = completions = 0;
  data.rx_siz[1] = 512U | (8U << 19);
  registers.out_ep[1].doeptsiz = (512U - (unsigned)bytes) | ((8U - packets) << 19);
  dwc2_handle_out_xfercompl(&dev, 1);
  assert(buffer.len == bytes);
  assert(continuations == (unsigned)continues);
  assert(!!(data.xfer_finished & BIT(17)) == !continues);
}
static void in_test(unsigned ep) {
  struct udc_dwc2_data data = {0};
  struct usb_dwc2_reg registers = {0};
  struct device dev = {&data, &registers};
  endpoint = (struct udc_ep_config){ep, 64};
  buffer = (struct net_buf){0, 512, true};
  continuations = completions = 0;
  assert(dwc2_handle_evt_din(&dev, &endpoint) == 0);
  assert(continuations == 1 && completions == 0 && !buffer.zlp);
  assert(dwc2_handle_evt_din(&dev, &endpoint) == 0);
  assert(continuations == 1 && completions == 1);
}
int main(int argc, char **argv) {
  bool all = argc == 1;
  if (all || strcmp(argv[1], "out") == 0)
    for (unsigned mode = 0; mode < 2; ++mode) {
      dma = mode != 0;
      out_test(0, 1, false);
      out_test(63, 1, false);
      out_test(64, 1, true);
      out_test(64, 2, false);
      out_test(65, 2, false);
      out_test(127, 2, false);
      out_test(128, 2, true);
      out_test(128, 3, false);
      out_test(129, 3, false);
      out_test(511, 8, false);
      out_test(512, 8, false);
    }
  if (all || strcmp(argv[1], "in") == 0) {
    in_test(0x80);
    in_test(0x81);
  }
  puts("DWC2 full-packet + ZLP OUT and requested Bulk IN ZLP decisions passed");
  return 0;
}
