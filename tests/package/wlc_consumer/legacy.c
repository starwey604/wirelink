#include "control_runtime.h"

int main(void) {
  control_runtime_config_t config;

  return control_runtime_config_defaults(&config) == WL_OK ? 0 : 1;
}
