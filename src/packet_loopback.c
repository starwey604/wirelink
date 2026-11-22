/* SPDX-License-Identifier: Apache-2.0 */
#include "wirelink/packet_loopback.h"
#include <string.h>
static wl_err_t attach(wl_transport_t *transport, const wl_transport_attach_t *info) {
  wl_packet_loopback_direction_t *direction = (wl_packet_loopback_direction_t *)transport;
  wl_packet_loopback_t *pair = direction->owner;
  if (info->stream || pair->stopped || pair->ports[direction->side]) return WL_ERR_INVALID_STATE;
  pair->ports[direction->side] = info->port;
  return WL_OK;
}
static wl_packet_submit_t submit(wl_transport_t *transport, const uint8_t *data, size_t length,
                                 wl_packet_token_t token) {
  wl_packet_loopback_direction_t *d = (wl_packet_loopback_direction_t *)transport;
  if (d->owner->stopped || !data || !length)
    return WL_PACKET_FAILED;
  if (d->pending)
    return WL_PACKET_BUSY;
  d->data = data;
  d->length = length;
  d->token = token;
  d->pending = 1U;
  return WL_PACKET_STARTED;
}
static void notify_owner(wl_transport_t *transport) {
  wl_packet_loopback_direction_t *d = (wl_packet_loopback_direction_t *)transport;
  if (d->owner->notify)
    d->owner->notify(d->owner->user);
}
static wl_err_t service(wl_transport_t *transport, uint32_t now) {
  (void)now;
  wl_packet_loopback_t *pair = ((wl_packet_loopback_direction_t *)transport)->owner;
  if (pair->stopped || !pair->ports[0] || !pair->ports[1]) return WL_OK;
  return wl_packet_loopback_service(pair, 2U);
}
static wl_io_readiness_t readiness(const wl_transport_t *transport, uint32_t now) {
  (void)now;
  const wl_packet_loopback_direction_t *direction = (const wl_packet_loopback_direction_t *)transport;
  return wl_io_readiness_at(wl_packet_loopback_ready(direction->owner) ? 0U : UINT32_MAX);
}
static wl_err_t stop(wl_transport_t *transport) {
  wl_packet_loopback_stop(((wl_packet_loopback_direction_t *)transport)->owner);
  return WL_OK;
}
static const wl_transport_ops_t ops = {
  .attach = attach, .submit = submit, .notify = notify_owner, .service = service,
  .readiness = readiness, .stop = stop
};
wl_transport_t *wl_packet_loopback_transport(wl_packet_loopback_t *pair, unsigned side) {
  return pair && side < 2U ? &pair->directions[side].transport : NULL;
}
wl_err_t wl_packet_loopback_init(wl_packet_loopback_t *pair, wl_packet_notify_fn notify, void *user) {
  if (pair == NULL) return WL_ERR_INVALID_ARG;
  memset(pair, 0, sizeof(*pair));
  pair->notify = notify;
  pair->user = user;
  for (unsigned i = 0U; i < 2U; ++i) {
    pair->directions[i].transport.ops = &ops;
    pair->directions[i].owner = pair;
    pair->directions[i].side = (uint8_t)i;
  }
  return WL_OK;
}
int wl_packet_loopback_ready(const wl_packet_loopback_t *pair) {
  if (!pair || pair->stopped || !pair->ports[0] || !pair->ports[1])
    return 0;
  for (unsigned i = 0U; i < 2U; ++i)
    if (pair->directions[i].pending && wl_packet_rx_available(pair->ports[1U - i]))
      return 1;
  return 0;
}
wl_err_t wl_packet_loopback_service(wl_packet_loopback_t *pair, unsigned budget) {
  if (pair == NULL || budget == 0U || budget > 16U)
    return WL_ERR_INVALID_ARG;
  if (pair->stopped || !pair->ports[0] || !pair->ports[1])
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
    if (!pair->ports[i]) continue;
    wl_packet_stop(pair->ports[i]);
    wl_packet_loopback_direction_t *d = &pair->directions[i];
    if (d->pending) {
      d->pending = 0U;
      d->data = NULL;
      (void)wl_packet_tx_publish(pair->ports[i], d->token, WL_PACKET_TX_ERROR);
    }
  }
}
