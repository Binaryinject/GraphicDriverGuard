#include "tests.h"

#include "uvdg/dx12_probe.h"
#include "uvdg/localization.h"
#include "uvdg/vulkan_probe.h"

#include <stdexcept>

namespace {

void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

}  // namespace

void RunDriverVersionTests() {
    const std::uint32_t nvidiaRaw = (516u << 22) | (25u << 14) | (3u << 6) | 7u;
    const auto nvidia = uvdg::DecodeDriverVersion(uvdg::kVendorNvidia, nvidiaRaw);
    Require(nvidia.ToString() == "516.25.3.7", "NVIDIA Vulkan encoding is wrong");

    const std::uint32_t standardRaw = (24u << 22) | (3u << 12) | 1u;
    const auto standard = uvdg::DecodeDriverVersion(uvdg::kVendorAmd, standardRaw);
    Require(standard.ToString() == "24.3.1", "standard Vulkan encoding is wrong");

    Require(uvdg::Compare(uvdg::Version::Parse("1.2"), uvdg::Version::Parse("1.2.0")) == 0,
            "missing version components should compare as zero");
    Require(uvdg::FormatVulkanVersion((1u << 22) | (1u << 12)) == "1.1.0",
            "Vulkan API version formatting is wrong");

    Require(uvdg::ParseFeatureLevel("12_1") == uvdg::kFeatureLevel12_1,
            "feature level 12_1 parsing is wrong");
    Require(uvdg::ParseFeatureLevel("11.0") == uvdg::kFeatureLevel11_0,
            "feature level 11.0 parsing is wrong");
    Require(uvdg::ParseFeatureLevel("0xC000") == uvdg::kFeatureLevel12_0,
            "feature level hex parsing is wrong");
    Require(uvdg::ParseFeatureLevel("9_1") == uvdg::kFeatureLevel9_1,
            "feature level 9_1 parsing is wrong");
    Require(uvdg::ParseFeatureLevel("10.0") == uvdg::kFeatureLevel10_0,
            "feature level 10.0 parsing is wrong");
    Require(uvdg::ParseFeatureLevel("garbage") == 0,
            "unknown feature level must parse to zero");
    Require(uvdg::FormatFeatureLevel(uvdg::kFeatureLevel12_0) == "12_0",
            "feature level formatting is wrong");
    Require(uvdg::FormatFeatureLevel(uvdg::kFeatureLevel10_1) == "10_1",
            "feature level 10_1 formatting is wrong");

    uvdg::PreflightResult denied;
    denied.failure = uvdg::FailureKind::DriverDenied;
    denied.gpu.apiVersion = (1u << 22) | (1u << 12);
    Require(denied.requiredVulkanMajor == 1 && denied.requiredVulkanMinor == 1,
            "default dialog requirement should be Vulkan 1.1");
    Require(denied.CanContinue(), "Vulkan 1.1 driver warning should allow continuing");
    denied.gpu.apiVersion = (1u << 22);
    Require(!denied.CanContinue(), "Vulkan 1.0 driver warning must remain blocking");
    denied.failure = uvdg::FailureKind::VulkanVersionUnsupported;
    denied.gpu.apiVersion = (1u << 22) | (2u << 12);
    Require(!denied.CanContinue(), "Vulkan capability failure must remain blocking");

    uvdg::PreflightResult dx11Denied;
    dx11Denied.failure = uvdg::FailureKind::DriverDenied;
    dx11Denied.checkVulkan = false;
    dx11Denied.checkD3D11 = true;
    dx11Denied.requiredD3D11FeatureLevel = uvdg::kFeatureLevel11_0;
    dx11Denied.gpu.d3d11FeatureLevel = uvdg::kFeatureLevel11_1;
    Require(dx11Denied.CanContinue(), "D3D11 feature level above minimum should allow continuing");
    dx11Denied.gpu.d3d11FeatureLevel = uvdg::kFeatureLevel10_0;
    Require(!dx11Denied.CanContinue(), "D3D11 feature level below minimum must block");

    uvdg::PreflightResult dx12Denied;
    dx12Denied.failure = uvdg::FailureKind::DriverDenied;
    dx12Denied.checkVulkan = false;
    dx12Denied.checkD3D12 = true;
    dx12Denied.requiredFeatureLevel = uvdg::kFeatureLevel12_0;
    dx12Denied.gpu.d3d12FeatureLevel = uvdg::kFeatureLevel12_1;
    Require(dx12Denied.CanContinue(), "D3D12 feature level above minimum should allow continuing");
    dx12Denied.gpu.d3d12FeatureLevel = uvdg::kFeatureLevel11_0;
    Require(!dx12Denied.CanContinue(), "D3D12 feature level below minimum must block");

    Require(uvdg::LanguageFromLocale("zh-CN") == uvdg::Language::Chinese,
            "Chinese locale detection is wrong");
    Require(uvdg::LanguageFromLocale("ja_JP.UTF-8") == uvdg::Language::Japanese,
            "Japanese locale detection is wrong");
    Require(uvdg::LanguageFromLocale("ko-KR") == uvdg::Language::Korean,
            "Korean locale detection is wrong");
    Require(uvdg::LanguageFromLocale("fr-FR") == uvdg::Language::English,
            "non-CJK locales must fall back to English");
}
