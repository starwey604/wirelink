/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <cfloat>
#include <cstddef>
#include <cstdint>
#include <wirelink/codec.h>
// Only passive values cross this namespace. Endpoint callbacks stay in C.
namespace device::wlc_native {
#include "device_values.h"
}
// C enum macros must not rewrite scoped C++ enum member names.
#undef MODE_IDLE
#undef MODE_ACTIVE
