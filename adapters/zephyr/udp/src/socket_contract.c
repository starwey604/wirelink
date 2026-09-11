/* SPDX-License-Identifier: Apache-2.0 */
#include "socket_contract.h"

#include <errno.h>
#include <limits.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/fdtable.h>
#include <zephyr/zvfs/eventfd.h>

static wl_err_t check_open(const wl_udp_socket_t *io) {
  if (io == NULL) return WL_ERR_INVALID_ARG;
  if (k_is_in_isr()) return WL_ERR_REENTRANT;
  if (!io->opened) return WL_ERR_NOT_INITIALIZED;
  return WL_OK;
}

wl_err_t wl_udp_socket_open(wl_udp_socket_t *io,
    const struct net_sockaddr_in *local, const struct net_sockaddr_in *peer) {
  int socket_fd, wake_fd;
  if (io == NULL || local == NULL || peer == NULL) return WL_ERR_INVALID_ARG;
  if (k_is_in_isr()) return WL_ERR_REENTRANT;
  if (io->opened) return WL_ERR_INVALID_STATE;
  if (local->sin_family != NET_AF_INET || peer->sin_family != NET_AF_INET ||
      peer->sin_port == 0 || peer->sin_addr.s_addr == NET_INADDR_ANY)
    return WL_ERR_INVALID_ARG;
  socket_fd = zsock_socket(NET_AF_INET, NET_SOCK_DGRAM, NET_IPPROTO_UDP);
  if (socket_fd < 0) return WL_ERR_IO;
  if (zsock_bind(socket_fd, (const struct net_sockaddr *)local, sizeof(*local)) < 0) {
    (void)zsock_close(socket_fd);
    return WL_ERR_IO;
  }
  wake_fd = zvfs_eventfd(0, ZVFS_EFD_NONBLOCK);
  if (wake_fd < 0) {
    (void)zsock_close(socket_fd);
    return WL_ERR_NO_MEM;
  }
  io->socket_fd = socket_fd;
  io->wake_fd = wake_fd;
  io->peer = *peer;
  atomic_clear(&io->stopped);
  io->opened = true;
  return WL_OK;
}

wl_err_t wl_udp_socket_close(wl_udp_socket_t *io) {
  int result = 0;
  if (io == NULL) return WL_ERR_INVALID_ARG;
  if (k_is_in_isr()) return WL_ERR_REENTRANT;
  if (!io->opened) return WL_OK;
  atomic_set(&io->stopped, 1);
  if (zsock_close(io->socket_fd) < 0) result = -1;
  if (zvfs_close(io->wake_fd) < 0) result = -1;
  io->socket_fd = io->wake_fd = -1;
  io->opened = false;
  return result == 0 ? WL_OK : WL_ERR_IO;
}

static wl_err_t signal_activity(wl_udp_socket_t *io) {
  if (zvfs_eventfd_write(io->wake_fd, 1) == 0) return WL_OK;
  /* A saturated nonblocking counter is already readable: the wake is latched. */
  return errno == EAGAIN ? WL_OK : WL_ERR_IO;
}

wl_err_t wl_udp_socket_notify(wl_udp_socket_t *io) {
  const wl_err_t error = check_open(io);
  if (error != WL_OK) return error;
  if (atomic_get(&io->stopped)) return WL_ERR_CANCELLED;
  return signal_activity(io);
}

wl_err_t wl_udp_socket_request_stop(wl_udp_socket_t *io) {
  const wl_err_t error = check_open(io);
  if (error != WL_OK) return error;
  atomic_set(&io->stopped, 1);
  return signal_activity(io);
}

wl_err_t wl_udp_socket_wait(wl_udp_socket_t *io, bool receive_ready,
                           uint32_t maximum_ms) {
  int result;
  zvfs_eventfd_t count;
  const wl_err_t error = check_open(io);
  if (error != WL_OK) return error;
  if (atomic_get(&io->stopped)) return WL_ERR_CANCELLED;
  struct zsock_pollfd fds[2] = {
    {.fd = receive_ready ? io->socket_fd : -1, .events = ZSOCK_POLLIN},
    {.fd = io->wake_fd, .events = ZSOCK_POLLIN},
  };
  const int timeout = maximum_ms == UINT32_MAX ? -1 :
      maximum_ms > INT_MAX ? INT_MAX : (int)maximum_ms;
  result = zsock_poll(fds, 2, timeout);
  if (atomic_get(&io->stopped)) return WL_ERR_CANCELLED;
  if (result < 0) return errno == EINTR ? WL_ERR_NO_DATA : WL_ERR_IO;
  if (result == 0) return WL_ERR_NO_DATA;
  for (size_t i = 0; i < 2; ++i) {
    if ((fds[i].revents & (ZSOCK_POLLERR | ZSOCK_POLLHUP | ZSOCK_POLLNVAL)) != 0)
      return WL_ERR_IO;
  }
  if ((fds[1].revents & ZSOCK_POLLIN) != 0) {
    /* One read drains the accumulated notifications. Never drain in an
     * unbounded loop while other producers keep posting. Later writes stay
     * readable for the next pass, including writes immediately after read. */
    if (zvfs_eventfd_read(io->wake_fd, &count) < 0 && errno != EAGAIN)
      return WL_ERR_IO;
  }
  return WL_OK;
}

wl_err_t wl_udp_socket_receive(wl_udp_socket_t *io, uint8_t *buffer,
    size_t capacity, size_t maximum_frame, size_t *received) {
  struct net_sockaddr_in source = {0};
  ssize_t length;
  const wl_err_t error = check_open(io);
  if (received != NULL) *received = 0;
  if (error != WL_OK) return error;
  if (atomic_get(&io->stopped)) return WL_ERR_CANCELLED;
  if (buffer == NULL || received == NULL || maximum_frame == 0 ||
      maximum_frame > 65507U || capacity <= maximum_frame)
    return WL_ERR_INVALID_ARG;
  struct net_iovec vector = {.iov_base = buffer, .iov_len = maximum_frame + 1U};
  struct net_msghdr message = {.msg_name = &source, .msg_namelen = sizeof(source),
                           .msg_iov = &vector, .msg_iovlen = 1};
  length = zsock_recvmsg(io->socket_fd, &message, ZSOCK_MSG_DONTWAIT);
  if (length < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
      return WL_ERR_NO_DATA;
    return errno == EMSGSIZE ? WL_ERR_FRAME_TOO_LONG : WL_ERR_IO;
  }
  if ((message.msg_flags & ZSOCK_MSG_TRUNC) != 0 || (size_t)length > maximum_frame)
    return WL_ERR_FRAME_TOO_LONG;
  if (length == 0 || message.msg_namelen != sizeof(source) ||
      source.sin_family != NET_AF_INET || source.sin_port != io->peer.sin_port ||
      source.sin_addr.s_addr != io->peer.sin_addr.s_addr)
    return WL_ERR_BAD_FRAME;
  *received = (size_t)length;
  return WL_OK;
}

wl_sink_result_t wl_udp_socket_classify_send(ssize_t result, size_t length,
                                            int socket_error) {
  if (length == 0 || length > 65507U) return WL_SINK_FAILED;
  if (result >= 0) return (size_t)result == length ? WL_SINK_SENT : WL_SINK_FAILED;
  if (socket_error == EAGAIN || socket_error == EWOULDBLOCK ||
      socket_error == ENOBUFS || socket_error == ENOMEM || socket_error == EINTR)
    return WL_SINK_BUSY;
  return WL_SINK_FAILED;
}

wl_sink_result_t wl_udp_socket_send(wl_udp_socket_t *io,
                                   const uint8_t *buffer, size_t length) {
  ssize_t result;
  if (check_open(io) != WL_OK || atomic_get(&io->stopped) || buffer == NULL ||
      length == 0 || length > 65507U) return WL_SINK_FAILED;
  result = zsock_sendto(io->socket_fd, buffer, length, ZSOCK_MSG_DONTWAIT,
                        (const struct net_sockaddr *)&io->peer, sizeof(io->peer));
  return wl_udp_socket_classify_send(result, length, result < 0 ? errno : 0);
}
