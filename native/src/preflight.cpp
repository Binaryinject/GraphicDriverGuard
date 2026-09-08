#include "uvdg/preflight.h"

#include "uvdg/config.h"
#include "uvdg/dx11_probe.h"
#include "uvdg/dx12_probe.h"
#include "uvdg/vulkan_probe.h"

#include <sstream>

namespace uvdg {

PreflightResult RunPreflight(const std::string& configPath) {
    const Config config = LoadConfig(configPath);

    PreflightResult result;
    result.requiredVulkanMajor = config.minimumVulkanMajor;
    result.requiredVulkanMinor = config.minimumVulkanMinor;
    result.checkVulkan = config.checkVulkan;
    result.checkD3D11 = config.checkD3D11;
    result.checkD3D12 = config.checkD3D12;
    result.requiredD3D11FeatureLevel = config.minimumD3D11FeatureLevel;
    result.requiredFeatureLevel = config.minimumFeatureLevel;

    bool haveGpu = false;

    if (config.checkVulkan) {
        const ProbeResult probe = ProbeVulkan();
        result.gpu = probe.gpu;
        haveGpu = true;
        if (probe.failure != FailureKind::None) {
            result.failure = probe.failure;
            result.reason = probe.reason;
            return result;
        }

        const std::uint32_t major = (probe.gpu.apiVersion >> 22) & 0x7f;
        const std::uint32_t minor = (probe.gpu.apiVersion >> 12) & 0x3ff;
        if (major < config.minimumVulkanMajor ||
            (major == config.minimumVulkanMajor && minor < config.minimumVulkanMinor)) {
            result.failure = FailureKind::VulkanVersionUnsupported;
            std::ostringstream reason;
            reason << "This GPU exposes Vulkan " << FormatVulkanVersion(probe.gpu.apiVersion)
                   << ", but this game requires Vulkan " << config.minimumVulkanMajor
                   << '.' << config.minimumVulkanMinor << " or newer.";
            result.reason = reason.str();
        }
    }

    if (config.checkD3D11 && result.failure == FailureKind::None) {
        const D3D11ProbeResult dx11 = ProbeD3D11();
        if (dx11.failure != FailureKind::None) {
            result.failure = dx11.failure;
            result.reason = dx11.reason;
            if (!haveGpu) result.gpu = dx11.gpu;
            return result;
        }
        if (!haveGpu) {
            result.gpu = dx11.gpu;
            haveGpu = true;
        }
        result.gpu.d3d11FeatureLevel = dx11.featureLevel;
        if (dx11.featureLevel < config.minimumD3D11FeatureLevel) {
            result.failure = FailureKind::D3D11FeatureLevelUnsupported;
            std::ostringstream reason;
            reason << "This GPU supports Direct3D 11 feature level "
                   << FormatFeatureLevel(dx11.featureLevel)
                   << ", but this game requires feature level "
                   << FormatFeatureLevel(config.minimumD3D11FeatureLevel) << " or newer.";
            result.reason = reason.str();
        }
    }

    if (config.checkD3D12 && result.failure == FailureKind::None) {
        const D3D12ProbeResult dx12 = ProbeD3D12();
        if (dx12.failure != FailureKind::None) {
            result.failure = dx12.failure;
            result.reason = dx12.reason;
            if (!haveGpu) result.gpu = dx12.gpu;
            return result;
        }
        if (!haveGpu) {
            result.gpu = dx12.gpu;
            haveGpu = true;
        }
        result.gpu.d3d12FeatureLevel = dx12.featureLevel;
        if (dx12.featureLevel < config.minimumFeatureLevel) {
            result.failure = FailureKind::D3D12FeatureLevelUnsupported;
            std::ostringstream reason;
            reason << "This GPU supports Direct3D 12 feature level "
                   << FormatFeatureLevel(dx12.featureLevel)
                   << ", but this game requires feature level "
                   << FormatFeatureLevel(config.minimumFeatureLevel) << " or newer.";
            result.reason = reason.str();
        }
    }

    const VendorPolicy& policy = PolicyFor(config, result.gpu.vendorId);
    result.suggestedVersion = policy.suggestedVersionText;
    result.downloadUrl = policy.downloadUrl;
    if (result.failure != FailureKind::None) return result;

    for (const DriverRule& rule : config.driverDenyList) {
        if (!RhiIsActive(rule.rhiName, config.checkVulkan, config.checkD3D11,
                         config.checkD3D12)) {
            continue;
        }
        if (!Matches(result.gpu, rule)) continue;
        result.failure = FailureKind::DriverDenied;
        result.deniedByMinimumVersion = rule.fromMinimumVersion;
        result.reason = rule.reason.empty()
            ? "The installed graphics driver is on this game's deny list."
            : rule.reason;
        if (!rule.suggestedVersion.empty()) result.suggestedVersion = rule.suggestedVersion;
        if (!rule.downloadUrl.empty()) result.downloadUrl = rule.downloadUrl;
        break;
    }
    return result;
}

}  // namespace uvdg
