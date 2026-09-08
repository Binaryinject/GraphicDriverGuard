#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace uvdg {

constexpr std::uint32_t kVendorNvidia = 0x10de;
constexpr std::uint32_t kVendorAmd = 0x1002;
constexpr std::uint32_t kVendorIntel = 0x8086;

// D3D_FEATURE_LEVEL values. D3D11 spans 9_1 through 11_1; D3D12 requires 11_0+.
constexpr std::uint32_t kFeatureLevel9_1 = 0x9100;
constexpr std::uint32_t kFeatureLevel9_2 = 0x9200;
constexpr std::uint32_t kFeatureLevel9_3 = 0x9300;
constexpr std::uint32_t kFeatureLevel10_0 = 0xA000;
constexpr std::uint32_t kFeatureLevel10_1 = 0xA100;
constexpr std::uint32_t kFeatureLevel11_0 = 0xB000;
constexpr std::uint32_t kFeatureLevel11_1 = 0xB100;
constexpr std::uint32_t kFeatureLevel12_0 = 0xC000;
constexpr std::uint32_t kFeatureLevel12_1 = 0xC100;

struct Version {
    std::vector<std::uint32_t> components;

    static Version Parse(const std::string& text);
    std::string ToString() const;
};

int Compare(const Version& left, const Version& right);

enum class Comparison {
    Less,
    LessOrEqual,
    Equal,
    GreaterOrEqual,
    Greater
};

struct DriverRule {
    struct IdRange {
        std::uint32_t first = 0;
        std::uint32_t last = 0;
    };

    std::uint32_t vendorId = 0;
    std::string rhiName;
    std::string adapterNameRegex;
    bool allDeviceIds = false;
    bool allDriverIds = false;
    std::vector<IdRange> deviceIds;
    std::vector<std::uint32_t> driverIds;
    Comparison comparison = Comparison::Less;
    Version version;
    std::string reason;
    std::string suggestedVersion;
    std::string downloadUrl;
    // True for the vendor-wide MinimumDriverVersion rule synthesized by
    // AppendMinimumRule, as opposed to an explicit deny-list entry.
    bool fromMinimumVersion = false;
};

struct VendorPolicy {
    std::string name;
    Version minimumVersion;
    std::string minimumVersionText;
    Version suggestedVersion;
    std::string suggestedVersionText;
    std::string downloadUrl;
    std::vector<DriverRule> denyList;
};

struct Config {
    std::uint32_t minimumVulkanMajor = 1;
    std::uint32_t minimumVulkanMinor = 1;
    bool checkVulkan = true;
    bool checkD3D11 = false;
    bool checkD3D12 = false;
    std::uint32_t minimumD3D11FeatureLevel = kFeatureLevel11_0;
    std::uint32_t minimumFeatureLevel = kFeatureLevel11_0;
    VendorPolicy nvidia;
    VendorPolicy amd;
    VendorPolicy intel;
    VendorPolicy other;
    std::vector<DriverRule> driverDenyList;
};

struct GpuInfo {
    std::uint32_t apiVersion = 0;
    std::uint32_t rawDriverVersion = 0;
    std::uint32_t vendorId = 0;
    std::uint32_t deviceId = 0;
    std::uint32_t deviceType = 0;
    std::uint32_t driverId = 0;
    std::string deviceName;
    std::string driverName;
    std::string driverInfo;
    Version unifiedDriverVersion;
    std::uint32_t d3d11FeatureLevel = 0;
    std::uint32_t d3d12FeatureLevel = 0;
};

enum class FailureKind {
    None,
    VulkanLoaderMissing,
    VulkanInitializationFailed,
    NoPhysicalDevice,
    VulkanVersionUnsupported,
    DriverDenied,
    D3D11Unavailable,
    D3D11FeatureLevelUnsupported,
    D3D12Unavailable,
    D3D12FeatureLevelUnsupported
};

struct PreflightResult {
    FailureKind failure = FailureKind::None;
    GpuInfo gpu;
    std::uint32_t requiredVulkanMajor = 1;
    std::uint32_t requiredVulkanMinor = 1;
    bool checkVulkan = true;
    bool checkD3D11 = false;
    bool checkD3D12 = false;
    std::uint32_t requiredD3D11FeatureLevel = kFeatureLevel11_0;
    std::uint32_t requiredFeatureLevel = kFeatureLevel11_0;
    bool deniedByMinimumVersion = false;
    std::string reason;
    std::string suggestedVersion;
    std::string downloadUrl;

    bool Passed() const { return failure == FailureKind::None; }
    bool CanContinue() const {
        if (failure != FailureKind::DriverDenied) return false;
        if (checkVulkan) {
            const std::uint32_t major = (gpu.apiVersion >> 22) & 0x7f;
            const std::uint32_t minor = (gpu.apiVersion >> 12) & 0x3ff;
            if (major < requiredVulkanMajor ||
                (major == requiredVulkanMajor && minor < requiredVulkanMinor)) {
                return false;
            }
        }
        if (checkD3D11 && gpu.d3d11FeatureLevel < requiredD3D11FeatureLevel) return false;
        if (checkD3D12 && gpu.d3d12FeatureLevel < requiredFeatureLevel) return false;
        return true;
    }
};

}  // namespace uvdg
