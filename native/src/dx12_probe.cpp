#include "uvdg/dx12_probe.h"

#include "uvdg/vulkan_probe.h"

#include <algorithm>
#include <cctype>
#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#endif

namespace uvdg {
namespace {

std::string Trim(std::string value) {
    const auto nonSpace = [](const unsigned char c) { return !std::isspace(c); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), nonSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), nonSpace).base(), value.end());
    return value;
}

#if defined(_WIN32)

using PFN_D3D12CreateDevice = HRESULT (*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
using PFN_CreateDXGIFactory1 = HRESULT (*)(REFIID, void**);

constexpr D3D_FEATURE_LEVEL kFeatureLevels[] = {
    D3D_FEATURE_LEVEL_12_1,
    D3D_FEATURE_LEVEL_12_0,
    D3D_FEATURE_LEVEL_11_1,
    D3D_FEATURE_LEVEL_11_0,
};

class DynamicLibrary {
public:
    explicit DynamicLibrary(const wchar_t* name) { handle_ = LoadLibraryW(name); }

    ~DynamicLibrary() {
        if (handle_) FreeLibrary(handle_);
    }

    void* Symbol(const char* name) const {
        if (!handle_) return nullptr;
        return reinterpret_cast<void*>(GetProcAddress(handle_, name));
    }

    explicit operator bool() const { return handle_ != nullptr; }

private:
    HMODULE handle_ = nullptr;
};

std::string Utf8(const wchar_t* value) {
    if (!value || !*value) return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string result(static_cast<std::size_t>(count - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), count, nullptr, nullptr);
    return result;
}

D3D12ProbeResult ProbeD3D12Windows() {
    D3D12ProbeResult result;
    DynamicLibrary d3d12(L"d3d12.dll");
    if (!d3d12) {
        result.failure = FailureKind::D3D12Unavailable;
        result.reason = "The Direct3D 12 runtime (d3d12.dll) is not installed.";
        return result;
    }

    const auto createDevice = reinterpret_cast<PFN_D3D12CreateDevice>(
        d3d12.Symbol("D3D12CreateDevice"));
    if (!createDevice) {
        result.failure = FailureKind::D3D12Unavailable;
        result.reason = "The Direct3D 12 runtime does not export D3D12CreateDevice.";
        return result;
    }

    std::uint32_t best = 0;
    GpuInfo bestGpu;

    DynamicLibrary dxgi(L"dxgi.dll");
    const PFN_CreateDXGIFactory1 createFactory = dxgi
        ? reinterpret_cast<PFN_CreateDXGIFactory1>(dxgi.Symbol("CreateDXGIFactory1"))
        : nullptr;

    if (createFactory) {
        IDXGIFactory1* factory = nullptr;
        if (SUCCEEDED(createFactory(__uuidof(IDXGIFactory1),
                                    reinterpret_cast<void**>(&factory))) &&
            factory) {
            for (UINT index = 0;; ++index) {
                IDXGIAdapter1* adapter = nullptr;
                if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND) break;
                if (!adapter) continue;

                DXGI_ADAPTER_DESC1 desc{};
                const bool hasDesc = SUCCEEDED(adapter->GetDesc1(&desc));
                const bool isSoftware =
                    hasDesc && (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;

                std::uint32_t adapterBest = 0;
                if (!isSoftware) {
                    for (const auto level : kFeatureLevels) {
                        IUnknown* device = nullptr;
                        const HRESULT hr = createDevice(
                            adapter, level, __uuidof(ID3D12Device),
                            reinterpret_cast<void**>(&device));
                        if (SUCCEEDED(hr) && device) {
                            adapterBest = static_cast<std::uint32_t>(level);
                            device->Release();
                            break;
                        }
                    }
                }

                if (hasDesc && adapterBest > best) {
                    best = adapterBest;
                    bestGpu.vendorId = desc.VendorId;
                    bestGpu.deviceId = desc.DeviceId;
                    bestGpu.deviceName = Utf8(desc.Description);
                }
                adapter->Release();
            }
            factory->Release();
        }
    }

    // Fallback: try the default adapter when adapter enumeration was unavailable.
    if (best == 0) {
        for (const auto level : kFeatureLevels) {
            IUnknown* device = nullptr;
            const HRESULT hr = createDevice(nullptr, level, __uuidof(ID3D12Device),
                                            reinterpret_cast<void**>(&device));
            if (SUCCEEDED(hr) && device) {
                best = static_cast<std::uint32_t>(level);
                device->Release();
                break;
            }
        }
    }

    if (best == 0) {
        result.failure = FailureKind::D3D12Unavailable;
        result.reason = "No Direct3D 12-capable graphics adapter was found.";
        return result;
    }

    result.featureLevel = best;
    result.gpu = std::move(bestGpu);
    ApplyPlatformDriverVersion(result.gpu);
    return result;
}

#endif  // defined(_WIN32)

}  // namespace

D3D12ProbeResult ProbeD3D12() {
#if defined(_WIN32)
    return ProbeD3D12Windows();
#else
    D3D12ProbeResult result;
    result.failure = FailureKind::D3D12Unavailable;
    result.reason = "Direct3D 12 is only supported on Windows.";
    return result;
#endif
}

std::uint32_t ParseFeatureLevel(const std::string& text) {
    const std::string value = Trim(text);
    if (value == "9_1" || value == "9.1") return kFeatureLevel9_1;
    if (value == "9_2" || value == "9.2") return kFeatureLevel9_2;
    if (value == "9_3" || value == "9.3") return kFeatureLevel9_3;
    if (value == "10_0" || value == "10.0" || value == "a000" || value == "A000") {
        return kFeatureLevel10_0;
    }
    if (value == "10_1" || value == "10.1" || value == "a100" || value == "A100") {
        return kFeatureLevel10_1;
    }
    if (value == "11_0" || value == "11.0" || value == "b000" || value == "B000") {
        return kFeatureLevel11_0;
    }
    if (value == "11_1" || value == "11.1" || value == "b100" || value == "B100") {
        return kFeatureLevel11_1;
    }
    if (value == "12_0" || value == "12.0" || value == "c000" || value == "C000") {
        return kFeatureLevel12_0;
    }
    if (value == "12_1" || value == "12.1" || value == "c100" || value == "C100") {
        return kFeatureLevel12_1;
    }

    try {
        std::size_t consumed = 0;
        const auto parsed = std::stoull(value, &consumed, 0);
        if (consumed == value.size()) return static_cast<std::uint32_t>(parsed);
    } catch (...) {
    }
    return 0;
}

std::string FormatFeatureLevel(const std::uint32_t featureLevel) {
    switch (featureLevel) {
        case kFeatureLevel9_1: return "9_1";
        case kFeatureLevel9_2: return "9_2";
        case kFeatureLevel9_3: return "9_3";
        case kFeatureLevel10_0: return "10_0";
        case kFeatureLevel10_1: return "10_1";
        case kFeatureLevel11_0: return "11_0";
        case kFeatureLevel11_1: return "11_1";
        case kFeatureLevel12_0: return "12_0";
        case kFeatureLevel12_1: return "12_1";
        default: return std::to_string(featureLevel);
    }
}

}  // namespace uvdg
