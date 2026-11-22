/* SPDX-License-Identifier: Apache-2.0 */
#include "tutorial_host.h"
#include "wirelink/platform.h"
#include <errno.h>
#include <signal.h>
#include <stdlib.h>

static volatile sig_atomic_t running = 1;

static void stopped(int signal_number) {
  (void)signal_number;
  running = 0;
}

void example_install_signals(void) {
  signal(SIGINT, stopped);
  signal(SIGTERM, stopped);
}

bool example_running(void) { return running != 0; }

uint32_t example_now_ms(void) {
  const wl_environment_t environment = wl_platform_environment();
  return environment.clock.now_ms(environment.clock.user_data);
}

wl_err_t example_instance(uint32_t *instance) {
  return wl_session_next(wl_platform_environment().session, 0, instance);
}

static bool parse_port(const char *text, uint16_t *port) {
  char *end;
  errno = 0;
  const long value = strtol(text, &end, 10);
  if (errno || end == text || *end || value <= 0 || value > UINT16_MAX) return false;
  *port = (uint16_t)value;
  return true;
}

bool example_udp_args(int argc, char **argv, wl_udp_config_t *config) {
  if (argc == 1) return true;
  uint16_t local, peer;
  if (argc == 5 && parse_port(argv[2], &local) && parse_port(argv[4], &peer)) {
    config->bind_address = argv[1];
    config->bind_port = local;
    config->peer_address = argv[3];
    config->peer_port = peer;
    return true;
  }
  fprintf(stderr, "usage: %s [LOCAL_IPV4 LOCAL_PORT PEER_IPV4 PEER_PORT]\n", argv[0]);
  return false;
}
