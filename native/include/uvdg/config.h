#pragma once

#include "uvdg/types.h"

#include <string>

namespace uvdg {

Config DefaultConfig();
Config LoadConfig(const std::string& path);
bool Matches(const Version& installed, const DriverRule& rule);
bool Matches(const GpuInfo& gpu, const DriverRule& rule);
// True when a deny rule's RHIName selector applies to one of the render APIs
// that the current configuration checks. An empty RHIName applies to every API.
bool RhiIsActive(const std::string& ruleRhi, bool checkVulkan, bool checkD3D11,
                 bool checkD3D12);
const VendorPolicy& PolicyFor(const Config& config, std::uint32_t vendorId);

}  // namespace uvdg
