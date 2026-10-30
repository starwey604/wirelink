/* SPDX-License-Identifier: Apache-2.0 */

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef enum { UART_DATA_5_BITS, UART_DATA_6_BITS, UART_DATA_7_BITS,
               UART_DATA_8_BITS, UART_DATA_BITS_MAX } uart_word_length_t;
typedef enum { UART_STOP_BITS_1 = 1, UART_STOP_BITS_1_5, UART_STOP_BITS_2 } uart_stop_bits_t;
typedef enum { UART_PARITY_DISABLE, UART_PARITY_EVEN, UART_PARITY_ODD } uart_parity_t;
enum { UART_CFG_DATA_BITS_5 = 5, UART_CFG_DATA_BITS_6, UART_CFG_DATA_BITS_7,
       UART_CFG_DATA_BITS_8 };
enum { UART_CFG_STOP_BITS_1 = 1, UART_CFG_STOP_BITS_1_5, UART_CFG_STOP_BITS_2 };
enum { UART_CFG_PARITY_NONE, UART_CFG_PARITY_EVEN, UART_CFG_PARITY_ODD };
enum { UART_CFG_FLOW_CTRL_NONE, UART_CFG_FLOW_CTRL_RTS_CTS, UART_CFG_FLOW_CTRL_RS485 };
enum { UART_SCLK_XTAL, UART_SCLK_DEFAULT, UART_MODE_UART, UART_MODE_RS485_HALF_DUPLEX,
       UART_HW_FLOWCTRL_DISABLE, UART_HW_FLOWCTRL_CTS_RTS,
       UART_SIGNAL_TXD_INV = 1, UART_SIGNAL_RXD_INV = 2,
       ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED };
#define UART_RX_FIFO_THRESH 22
#define UART_TX_FIFO_THRESH 1
typedef int soc_module_clk_t;
typedef struct {
  uart_word_length_t data;
  uart_stop_bits_t stop;
  uart_parity_t parity;
  uart_mem_conf_reg_t timeout;
  uint16_t max_threshold;
  unsigned writes;
} mock_uart_t;
typedef struct { mock_uart_t *dev; } uart_hal_context_t;
struct uart_esp32_data { uart_hal_context_t hal; };
struct uart_esp32_config { bool tx_invert; bool rx_invert; };
struct device { const void *config; void *data; };
struct uart_config { uint32_t baudrate; int data_bits, stop_bits, parity, flow_ctrl; };

static void uart_ll_get_data_bit_num(mock_uart_t *d, uart_word_length_t *out) { *out = d->data; }
static void uart_ll_get_stop_bits(mock_uart_t *d, uart_stop_bits_t *out) { *out = d->stop; }
static void uart_ll_get_parity(mock_uart_t *d, uart_parity_t *out) { *out = d->parity; }
static uint16_t uart_ll_max_tout_thrd(mock_uart_t *d) { return d->max_threshold; }
static void uart_ll_set_rx_tout(mock_uart_t *d, uint16_t bits) {
  d->timeout.rx_tout_thrhd = bits;
  ++d->writes;
}
static void uart_hal_set_data_bit_num(uart_hal_context_t *h, uart_word_length_t v) {
  h->dev->data = v;
  ++h->dev->writes;
}
static void uart_hal_set_stop_bits(uart_hal_context_t *h, uart_stop_bits_t v) {
  h->dev->stop = v;
  ++h->dev->writes;
}
static void uart_hal_set_parity(uart_hal_context_t *h, uart_parity_t v) {
  h->dev->parity = v;
  ++h->dev->writes;
}
/* Clock selection does not change the S3's threshold capacity. */
static void uart_hal_set_sclk(uart_hal_context_t *h, int v) { (void)h; (void)v; }
static void uart_hal_get_sclk(uart_hal_context_t *h, soc_module_clk_t *v) { (void)h; *v = 0; }
static void esp_clk_tree_src_get_freq_hz(int clk, int precision, uint32_t *v) {
  (void)clk; (void)precision; *v = 80000000;
}
static void uart_hal_set_rxfifo_full_thr(uart_hal_context_t *h, int v) { (void)v; ++h->dev->writes; }
static void uart_hal_set_txfifo_empty_thr(uart_hal_context_t *h, int v) { (void)v; ++h->dev->writes; }
static void uart_hal_rxfifo_rst(uart_hal_context_t *h) { ++h->dev->writes; }
static void uart_hal_txfifo_rst(uart_hal_context_t *h) { ++h->dev->writes; }
static void uart_hal_set_mode(uart_hal_context_t *h, int v) { (void)v; ++h->dev->writes; }
static void uart_hal_set_hw_flow_ctrl(uart_hal_context_t *h, int v, int n) {
  (void)v; (void)n; ++h->dev->writes;
}
static void uart_hal_set_baudrate(uart_hal_context_t *h, uint32_t baud, uint32_t freq) {
  (void)baud; (void)freq; ++h->dev->writes;
}
static void uart_hal_inverse_signal(uart_hal_context_t *h, uint32_t mask) {
  (void)mask; ++h->dev->writes;
}

/* DRIVER */

int main(void) {
  unsigned cases = 0;
  const struct uart_esp32_config config = {0};
  mock_uart_t reg = { .data = UART_DATA_8_BITS, .stop = UART_STOP_BITS_1,
                     .parity = UART_PARITY_DISABLE, .max_threshold = 1023 };
  struct uart_esp32_data data = { .hal = { .dev = &reg } };
  const struct device dev = { .config = &config, .data = &data };
  struct uart_config cfg = { .baudrate = 1000000, .flow_ctrl = UART_CFG_FLOW_CTRL_NONE };
  for (int bits = 5; bits <= 8; ++bits) {
    for (int parity = UART_CFG_PARITY_NONE; parity <= UART_CFG_PARITY_ODD; ++parity) {
      for (int stop = UART_CFG_STOP_BITS_1; stop <= UART_CFG_STOP_BITS_2; ++stop) {
        cfg.data_bits = bits;
        cfg.parity = parity;
        cfg.stop_bits = stop;
        unsigned symbol = 1U + (unsigned)bits + (parity != UART_CFG_PARITY_NONE) +
                          (stop == UART_CFG_STOP_BITS_1 ? 1U : 2U);
        mock_uart_t before = reg;
        int result = uart_esp32_configure(&dev, &cfg);
        if (CONFIG_UART_ESP32_RX_TIMEOUT > 1023U / symbol) {
          assert(result == -EINVAL);
          assert(memcmp(&reg, &before, sizeof(reg)) == 0);
        } else {
          assert(result == 0);
          assert(uart_hal_get_symb_len(&data.hal) == symbol);
          assert(CONFIG_UART_ESP32_RX_TIMEOUT <= uart_hal_get_max_rx_timeout_thrd(&data.hal));
          assert(reg.timeout.rx_tout_thrhd == symbol * CONFIG_UART_ESP32_RX_TIMEOUT);
        }
        ++cases;
      }
    }
  }
  /* A rejected runtime change preserves the previously accepted 8N1 format. */
  cfg = (struct uart_config){1000000, UART_CFG_DATA_BITS_8, UART_CFG_STOP_BITS_1,
                            UART_CFG_PARITY_NONE, UART_CFG_FLOW_CTRL_NONE};
  if (CONFIG_UART_ESP32_RX_TIMEOUT <= 102) {
    assert(uart_esp32_configure(&dev, &cfg) == 0);
    mock_uart_t before = reg;
    cfg.parity = UART_CFG_PARITY_EVEN;
    cfg.stop_bits = UART_CFG_STOP_BITS_2;
    int result = uart_esp32_configure(&dev, &cfg);
    if (CONFIG_UART_ESP32_RX_TIMEOUT > 85) {
      assert(result == -EINVAL);
      assert(memcmp(&reg, &before, sizeof(reg)) == 0);
    } else {
      assert(result == 0);
      assert(reg.timeout.rx_tout_thrhd == 12U * CONFIG_UART_ESP32_RX_TIMEOUT);
    }
    ++cases;
  }
  /* The limit is read from the hardware abstraction, rather than fixed to S3. */
  reg.max_threshold = 15;
  mock_uart_t before = reg;
  assert(uart_esp32_configure(&dev, &cfg) == -EINVAL);
  assert(memcmp(&reg, &before, sizeof(reg)) == 0);
  ++cases;
  for (unsigned invalid = 0; invalid < 3; ++invalid) {
    struct uart_config bad = cfg;
    if (invalid == 0) bad.data_bits = 9;
    if (invalid == 1) bad.parity = 99;
    if (invalid == 2) bad.stop_bits = 99;
    assert(uart_esp32_configure(&dev, &bad) == -ENOTSUP);
    assert(memcmp(&reg, &before, sizeof(reg)) == 0);
    ++cases;
  }
  printf("timeout=%u: %u format/boundary cases passed\n", CONFIG_UART_ESP32_RX_TIMEOUT, cases);
  return 0;
}
