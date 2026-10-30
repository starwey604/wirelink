/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/packet_loopback.h"
#include <string.h>
wl_err_t wl_packet_loopback_init(wl_packet_loopback_t *pair, wl_packet_port_t *a,
                                 wl_packet_port_t *b, wl_packet_notify_fn notify, void *user) {
  if (pair == NULL || a == NULL || b == NULL || a == b)
    return WL_ERR_INVALID_ARG;
  memset(pair, 0, sizeof(*pair));
  pair->ports[0] = a;
  pair->ports[1] = b;
  pair->notify = notify;
  pair->user = user;
  for (unsigned i = 0U; i < 2U; ++i) {
    pair->directions[i].owner = pair;
    pair->directions[i].side = (uint8_t)i;
  }
  return WL_OK;
}
wl_packet_submit_t wl_packet_loopback_submit(void *user, const uint8_t *data, size_t length,
                                             wl_packet_token_t token) {
  wl_packet_loopback_direction_t *d = user;
  if (d == NULL || d->owner == NULL || d->owner->stopped || data == NULL || length == 0U)
    return WL_PACKET_FAILED;
  if (d->pending)
    return WL_PACKET_BUSY;
  d->data = data;
  d->length = length;
  d->token = token;
  d->pending = 1U;
  return WL_PACKET_STARTED;
}
void wl_packet_loopback_notify(void *user) {
  wl_packet_loopback_direction_t *d = user;
  if (d != NULL && d->owner != NULL && d->owner->notify)
    d->owner->notify(d->owner->user);
}
int wl_packet_loopback_ready(const wl_packet_loopback_t *pair) {
  if (pair == NULL || pair->stopped)
    return 0;
  for (unsigned i = 0U; i < 2U; ++i)
    if (pair->directions[i].pending && wl_packet_rx_available(pair->ports[1U - i]))
      return 1;
  return 0;
}
wl_err_t wl_packet_loopback_service(wl_packet_loopback_t *pair, unsigned budget) {
  if (pair == NULL || budget == 0U || budget > 16U)
    return WL_ERR_INVALID_ARG;
  if (pair->stopped)
    return WL_ERR_INVALID_STATE;
  for (unsigned n = 0U; n < budget; ++n) {
    unsigned i = pair->cursor;
    pair->cursor ^= 1U;
    wl_packet_loopback_direction_t *d = &pair->directions[i];
    if (!d->pending)
      continue;
    wl_packet_claim_t claim;
    wl_err_t error = wl_packet_rx_claim(pair->ports[1U - i], d->length, &claim);
    if (error == WL_ERR_WOULD_BLOCK)
      continue;
    if (error == WL_OK) {
      memcpy(claim.span.data, d->data, d->length);
      error = wl_packet_rx_commit(pair->ports[1U - i], &claim, d->length);
      if (error != WL_OK)
        (void)wl_packet_rx_abort(pair->ports[1U - i], &claim);
    }
    d->pending = 0U;
    d->data = NULL;
    wl_err_t completed = wl_packet_tx_publish(
        pair->ports[i], d->token, error == WL_OK ? WL_PACKET_DRAINED : WL_PACKET_TX_ERROR);
    if (error != WL_OK)
      return error;
    if (completed != WL_OK)
      return completed;
    ++d->delivered;
  }
  return WL_OK;
}
void wl_packet_loopback_stop(wl_packet_loopback_t *pair) {
  if (pair == NULL || pair->stopped)
    return;
  pair->stopped = 1U;
  for (unsigned i = 0U; i < 2U; ++i) {
    wl_packet_stop(pair->ports[i]);
    wl_packet_loopback_direction_t *d = &pair->directions[i];
    if (d->pending) {
      d->pending = 0U;
      d->data = NULL;
      (void)wl_packet_tx_publish(pair->ports[i], d->token, WL_PACKET_TX_ERROR);
    }
  }
}
