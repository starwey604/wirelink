#include <stddef.h>

#include "control.h"
#include "control_runtime.h"

#if !ARM_MIT_COMMAND_HAS_MAX_ENCODED_SIZE
#error "the fixed MIT command must expose a static encoded-size bound"
#endif

_Static_assert(ARM_MIT_COMMAND_MAX_ENCODED_SIZE <= SIZE_MAX,
               "the generated payload bound must fit host size_t");

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
