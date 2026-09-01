#include <stddef.h>

#include "control.h"
#include "control_runtime.h"

int main(void) {
  arm_mit_command_t command;
  control_runtime_config_t config = {0};
  control_runtime_requirements_t requirements;

  arm_mit_command_clear(&command);
  config.joint_command_fifo_capacity = 1U;
  config.arm_mit_command_latest_initial_generation = 1U;
  if (control_runtime_requirements(&config, &requirements) != 0) {
    return 1;
  }
  return requirements.storage_size >= sizeof(command) ? 0 : 2;
}
