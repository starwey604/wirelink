/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <cfloat>
#include <cstddef>
#include <cstdint>
#include <wirelink/codec.h>
#include <wirelink/node.h>
// Isolate schema names from system C++ typedefs.
namespace device::wlc_native {
#include "device.h"
}
// C enum macros must not rewrite scoped C++ enum member names.
#undef MODE_IDLE
#undef MODE_ACTIVE
