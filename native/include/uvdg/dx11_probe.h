#pragma once

#include "uvdg/types.h"

#include <string>

namespace uvdg {

struct D3D11ProbeResult {
    FailureKind failure = FailureKind::None;
    // Highest D3D11 feature level the adapter exposes (9_1 .. 11_1).
    std::uint32_t featureLevel = 0;
    GpuInfo gpu;
    std::string reason;
};

// Tries to create a real D3D11 device on the best hardware adapter and reports
// the highest supported feature level. On non-Windows platforms this always
// fails because Direct3D 11 is a Windows-only runtime.
D3D11ProbeResult ProbeD3D11();

}  // namespace uvdg
