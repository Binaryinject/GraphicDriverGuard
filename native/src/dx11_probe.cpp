#include "uvdg/dx11_probe.h"

#include "uvdg/vulkan_probe.h"

#include <string>
#include <utility>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_4.h>
#endif

namespace uvdg {
namespace {

#if defined(_WIN32)

using PFN_D3D11CreateDevice = HRESULT (*)(IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE,
                                          UINT, const D3D_FEATURE_LEVEL*, UINT, UINT,
                                          ID3D11Device**, D3D_FEATURE_LEVEL*,
                                          ID3D11DeviceContext**);
using PFN_CreateDXGIFactory1 = HRESULT (*)(REFIID, void**);

constexpr D3D_FEATURE_LEVEL kFeatureLevels[] = {
    D3D_FEATURE_LEVEL_11_1,
    D3D_FEATURE_LEVEL_11_0,
    D3D_FEATURE_LEVEL_10_1,
    D3D_FEATURE_LEVEL_10_0,
    D3D_FEATURE_LEVEL_9_3,
    D3D_FEATURE_LEVEL_9_2,
    D3D_FEATURE_LEVEL_9_1,
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

D3D11ProbeResult ProbeD3D11Windows() {
    D3D11ProbeResult result;
    DynamicLibrary d3d11(L"d3d11.dll");
    if (!d3d11) {
        result.failure = FailureKind::D3D11Unavailable;
        result.reason = "The Direct3D 11 runtime (d3d11.dll) is not installed.";
        return result;
    }

    const auto createDevice = reinterpret_cast<PFN_D3D11CreateDevice>(
        d3d11.Symbol("D3D11CreateDevice"));
    if (!createDevice) {
        result.failure = FailureKind::D3D11Unavailable;
        result.reason = "The Direct3D 11 runtime does not export D3D11CreateDevice.";
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
                    ID3D11Device* device = nullptr;
                    ID3D11DeviceContext* context = nullptr;
                    D3D_FEATURE_LEVEL supported = D3D_FEATURE_LEVEL_9_1;
                    const HRESULT hr = createDevice(
                        adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, kFeatureLevels,
                        static_cast<UINT>(sizeof(kFeatureLevels) / sizeof(kFeatureLevels[0])),
                        D3D11_SDK_VERSION, &device, &supported, &context);
                    if (SUCCEEDED(hr) && device) {
                        adapterBest = static_cast<std::uint32_t>(supported);
                        device->Release();
                        if (context) context->Release();
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
        ID3D11Device* device = nullptr;
        ID3D11DeviceContext* context = nullptr;
        D3D_FEATURE_LEVEL supported = D3D_FEATURE_LEVEL_9_1;
        const HRESULT hr = createDevice(
            nullptr, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, kFeatureLevels,
            static_cast<UINT>(sizeof(kFeatureLevels) / sizeof(kFeatureLevels[0])),
            D3D11_SDK_VERSION, &device, &supported, &context);
        if (SUCCEEDED(hr) && device) {
            best = static_cast<std::uint32_t>(supported);
            device->Release();
            if (context) context->Release();
        }
    }

    if (best == 0) {
        result.failure = FailureKind::D3D11Unavailable;
        result.reason = "No Direct3D 11-capable graphics adapter was found.";
        return result;
    }

    result.featureLevel = best;
    result.gpu = std::move(bestGpu);
    ApplyPlatformDriverVersion(result.gpu);
    return result;
}

#endif  // defined(_WIN32)

}  // namespace

D3D11ProbeResult ProbeD3D11() {
#if defined(_WIN32)
    return ProbeD3D11Windows();
#else
    D3D11ProbeResult result;
    result.failure = FailureKind::D3D11Unavailable;
    result.reason = "Direct3D 11 is only supported on Windows.";
    return result;
#endif
}

}  // namespace uvdg
