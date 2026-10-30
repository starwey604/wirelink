/* SPDX-License-Identifier: Apache-2.0 */
/* Keep the owner execution bridge resident across UART interrupts and sleep.
 * Framing, codecs and RPC dispatch retain their normal flash placement. */
*(.literal.wl_link_get_readiness .text.wl_link_get_readiness)
*(.literal.wl_pump_step .text.wl_pump_step)
*(.literal.wl_pump_get_readiness .text.wl_pump_get_readiness)
*(.literal.wl_endpoint_step .text.wl_endpoint_step)
*(.literal.wl_endpoint_get_readiness .text.wl_endpoint_get_readiness)
*(.literal.wl_endpoint_get_readiness_at .text.wl_endpoint_get_readiness_at)
*(.literal.wl_endpoint_now .text.wl_endpoint_now)
*(.literal.endpoint_hooks .text.endpoint_hooks)
