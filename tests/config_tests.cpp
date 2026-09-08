#include "tests.h"

#include "uvdg/config.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace {

void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

}  // namespace

void RunConfigTests() {
    const auto path = std::filesystem::temp_directory_path() / "uvdg-config-test.ini";
    {
        std::ofstream output(path);
        output << "[Global]\nMinimumVulkanVersion=1.1\n"
                  "[GPU_NVIDIA]\nMinimumDriverVersion=516.25\nSuggestedDriverVersion=551.76\n"
                  "+DriverDenyList=<999.0|Legacy syntax must be ignored\n"
                  "+DriverDenyList=(DriverVersion=\"<551.76\",RHIName=\"Vulkan\",AdapterNameRegex=\".*GTX 10[0-9]0.*\",DeviceId=\"0x1B80\",DriverId=\"NVIDIA_PROPRIETARY\",SuggestedDriverVersion=\"551.76\",Reason=\"Known Vulkan issue\")\n"
                  "DownloadURL=https://example.invalid/nvidia\n";
    }

    const auto config = uvdg::LoadConfig(path.string());
    std::filesystem::remove(path);
    Require(config.minimumVulkanMajor == 1 && config.minimumVulkanMinor == 1,
            "minimum Vulkan version was not parsed");
    Require(config.checkVulkan && !config.checkD3D12,
            "default render API must be Vulkan only");
    Require(config.driverDenyList.size() == 2,
            "deny-list and minimum driver rules were not parsed");
    Require(config.driverDenyList[0].deviceIds.size() == 1 &&
            config.driverDenyList[0].driverIds.size() == 1 &&
            config.driverDenyList[0].rhiName == "Vulkan" &&
            config.driverDenyList[0].suggestedVersion == "551.76",
            "deny-list selectors were not parsed");
    Require(!config.driverDenyList[0].fromMinimumVersion &&
            config.driverDenyList[1].fromMinimumVersion,
            "minimum-version flag must distinguish synthesized minimum rules");
    Require(uvdg::Matches(uvdg::Version::Parse("546.33"), config.driverDenyList[0]),
            "older driver should match deny-list rule");
    Require(!uvdg::Matches(uvdg::Version::Parse("551.76"), config.driverDenyList[0]),
            "boundary driver should not match a strict less-than rule");

    uvdg::GpuInfo gpu;
    gpu.vendorId = uvdg::kVendorNvidia;
    gpu.deviceId = 0x1B80;
    gpu.driverId = 4;
    gpu.unifiedDriverVersion = uvdg::Version::Parse("546.33");
    gpu.deviceName = "NVIDIA GeForce GTX 1080";
    Require(uvdg::Matches(gpu, config.driverDenyList[0]), "matching GPU should be denied");
    gpu.deviceId = 0x2206;
    Require(!uvdg::Matches(gpu, config.driverDenyList[0]), "different GPU must not be denied");
    gpu.deviceId = 0x1B80;
    gpu.driverId = 3;
    Require(!uvdg::Matches(gpu, config.driverDenyList[0]), "different Vulkan driver must not be denied");
    gpu.driverId = 4;
    gpu.deviceName = "NVIDIA GeForce RTX 4090";
    Require(!uvdg::Matches(gpu, config.driverDenyList[0]), "adapter regex mismatch must not deny");
    gpu.unifiedDriverVersion = uvdg::Version::Parse("500.0");
    Require(uvdg::Matches(gpu, config.driverDenyList[1]),
            "vendor minimum must deny an older driver without device selectors");
    gpu.unifiedDriverVersion = uvdg::Version::Parse("516.25");
    Require(!uvdg::Matches(gpu, config.driverDenyList[1]),
            "vendor minimum boundary must be accepted");

    // D3D12 render API selection and minimum feature level.
    {
        std::ofstream output(path);
        output << "[Global]\nRenderAPI=D3D12\nMinimumFeatureLevel=12_0\n"
                  "[GPU_NVIDIA]\nMinimumDriverVersion=551.76\n"
                  "+DriverDenyList=(DriverVersion=\"<561.0\",RHIName=\"D3D12\","
                  "DeviceId=\"0x1B80\",Reason=\"Known D3D12 issue\")\n";
    }
    const auto dx12 = uvdg::LoadConfig(path.string());
    std::filesystem::remove(path);
    Require(!dx12.checkVulkan && dx12.checkD3D12,
            "RenderAPI=D3D12 must enable only D3D12");
    Require(dx12.minimumFeatureLevel == uvdg::kFeatureLevel12_0,
            "MinimumFeatureLevel was not parsed");
    Require(dx12.driverDenyList.size() == 2,
            "D3D12 deny rule and vendor minimum must be parsed");
    Require(dx12.driverDenyList[0].rhiName == "D3D12" &&
            dx12.driverDenyList[0].comparison == uvdg::Comparison::Less,
            "D3D12 deny rule was not parsed");
    Require(dx12.driverDenyList[1].rhiName.empty(),
            "vendor minimum must apply to every render API");

    // D3D11 render API selection and minimum feature level.
    {
        std::ofstream output(path);
        output << "[Global]\nRenderAPI=D3D11\nMinimumD3D11FeatureLevel=10_0\n"
                  "[GPU_NVIDIA]\nMinimumDriverVersion=551.76\n"
                  "+DriverDenyList=(DriverVersion=\"<561.0\",RHIName=\"D3D11\","
                  "DeviceId=\"0x1B80\",Reason=\"Known D3D11 issue\")\n";
    }
    const auto dx11 = uvdg::LoadConfig(path.string());
    std::filesystem::remove(path);
    Require(!dx11.checkVulkan && dx11.checkD3D11 && !dx11.checkD3D12,
            "RenderAPI=D3D11 must enable only D3D11");
    Require(dx11.minimumD3D11FeatureLevel == uvdg::kFeatureLevel10_0,
            "MinimumD3D11FeatureLevel was not parsed");
    Require(dx11.driverDenyList.size() == 2,
            "D3D11 deny rule and vendor minimum must be parsed");
    Require(dx11.driverDenyList[0].rhiName == "D3D11",
            "D3D11 deny rule was not parsed");

    // Both render APIs.
    {
        std::ofstream output(path);
        output << "[Global]\nRenderAPI=Vulkan,D3D12\n";
    }
    const auto both = uvdg::LoadConfig(path.string());
    std::filesystem::remove(path);
    Require(both.checkVulkan && both.checkD3D12,
            "RenderAPI=Vulkan,D3D12 must enable both APIs");

    // RHI selector activation.
    Require(uvdg::RhiIsActive("", true, false, false),
            "empty RHIName must apply to every render API");
    Require(uvdg::RhiIsActive("Vulkan", true, false, false),
            "Vulkan rule must be active when Vulkan is checked");
    Require(!uvdg::RhiIsActive("Vulkan", false, true, false),
            "Vulkan rule must be inactive when only D3D11 is checked");
    Require(uvdg::RhiIsActive("d3d11", false, true, false),
            "D3D11 rule must be active when D3D11 is checked");
    Require(!uvdg::RhiIsActive("d3d11", true, false, false),
            "D3D11 rule must be inactive when only Vulkan is checked");
    Require(uvdg::RhiIsActive("d3d12", false, false, true),
            "D3D12 rule must be active when D3D12 is checked");
    Require(!uvdg::RhiIsActive("d3d12", true, false, false),
            "D3D12 rule must be inactive when only Vulkan is checked");
    Require(uvdg::RhiIsActive("D3D11", true, true, true),
            "RHI matching must be case-insensitive");
    Require(!uvdg::RhiIsActive("opengl", true, true, true),
            "unknown RHI must never be active");
}
