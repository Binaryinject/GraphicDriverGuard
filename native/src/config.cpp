#include "uvdg/config.h"

#include "uvdg/dx11_probe.h"
#include "uvdg/dx12_probe.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <regex>
#include <unordered_map>

namespace uvdg {
namespace {

std::string Trim(std::string value) {
    const auto nonSpace = [](const unsigned char c) { return !std::isspace(c); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), nonSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), nonSpace).base(), value.end());
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.size() - 2);
    }
    return value;
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string Upper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](const unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return value;
}

bool ParseId(const std::string& text, std::uint32_t& result) {
    const auto value = Trim(text);
    if (value.empty()) return false;
    try {
        std::size_t consumed = 0;
        const auto parsed = std::stoull(value, &consumed, 0);
        if (consumed != value.size() || parsed > std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        result = static_cast<std::uint32_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

std::vector<std::string> SplitSelectors(const std::string& value) {
    std::vector<std::string> result;
    std::size_t start = 0;
    for (std::size_t index = 0; index <= value.size(); ++index) {
        if (index != value.size() && value[index] != ',' && value[index] != ';') continue;
        const auto item = Trim(value.substr(start, index - start));
        if (!item.empty()) result.push_back(item);
        start = index + 1;
    }
    return result;
}

void ParseDeviceIds(const std::string& value, DriverRule& rule) {
    for (const auto& selector : SplitSelectors(value)) {
        if (selector == "*") {
            rule.allDeviceIds = true;
            continue;
        }

        const auto dash = selector.find('-');
        std::uint32_t first = 0;
        std::uint32_t last = 0;
        if (dash == std::string::npos) {
            if (!ParseId(selector, first)) continue;
            last = first;
        } else {
            if (!ParseId(selector.substr(0, dash), first) ||
                !ParseId(selector.substr(dash + 1), last)) {
                continue;
            }
            if (first > last) std::swap(first, last);
        }
        rule.deviceIds.push_back({first, last});
    }
}

std::uint32_t NamedDriverId(std::string value) {
    value = Upper(Trim(value));
    constexpr const char* prefix = "VK_DRIVER_ID_";
    if (value.rfind(prefix, 0) == 0) value.erase(0, 13);
    if (value.size() > 4 && value.compare(value.size() - 4, 4, "_KHR") == 0) {
        value.resize(value.size() - 4);
    }

    static const std::unordered_map<std::string, std::uint32_t> ids{
        {"AMD_PROPRIETARY", 1}, {"AMD_OPEN_SOURCE", 2}, {"MESA_RADV", 3},
        {"NVIDIA_PROPRIETARY", 4}, {"INTEL_PROPRIETARY_WINDOWS", 5},
        {"INTEL_OPEN_SOURCE_MESA", 6}, {"IMAGINATION_PROPRIETARY", 7},
        {"QUALCOMM_PROPRIETARY", 8}, {"ARM_PROPRIETARY", 9},
        {"GOOGLE_SWIFTSHADER", 10}, {"GGP_PROPRIETARY", 11},
        {"BROADCOM_PROPRIETARY", 12}, {"MESA_LLVMPIPE", 13}, {"MOLTENVK", 14},
        {"COREAVI_PROPRIETARY", 15}, {"JUICE_PROPRIETARY", 16},
        {"VERISILICON_PROPRIETARY", 17}, {"MESA_TURNIP", 18}, {"MESA_V3DV", 19},
        {"MESA_PANVK", 20}, {"SAMSUNG_PROPRIETARY", 21}, {"MESA_VENUS", 22},
        {"MESA_DOZEN", 23}, {"MESA_NVK", 24}, {"IMAGINATION_OPEN_SOURCE_MESA", 25}};
    const auto found = ids.find(value);
    return found == ids.end() ? 0 : found->second;
}

void ParseDriverIds(const std::string& value, DriverRule& rule) {
    for (const auto& selector : SplitSelectors(value)) {
        if (selector == "*") {
            rule.allDriverIds = true;
            continue;
        }
        std::uint32_t id = 0;
        if (!ParseId(selector, id)) id = NamedDriverId(selector);
        if (id != 0) rule.driverIds.push_back(id);
    }
}

std::unordered_map<std::string, std::string> ParseFields(std::string value) {
    std::unordered_map<std::string, std::string> fields;
    value = Trim(value);
    if (value.size() < 2 || value.front() != '(' || value.back() != ')') return fields;
    value = value.substr(1, value.size() - 2);

    std::size_t index = 0;
    while (index < value.size()) {
        while (index < value.size() &&
               (std::isspace(static_cast<unsigned char>(value[index])) || value[index] == ',')) {
            ++index;
        }
        const auto equals = value.find('=', index);
        if (equals == std::string::npos) break;
        const auto key = Lower(Trim(value.substr(index, equals - index)));
        index = equals + 1;

        std::string field;
        if (index < value.size() && value[index] == '"') {
            ++index;
            while (index < value.size()) {
                const char character = value[index++];
                if (character == '"') break;
                if (character == '\\' && index < value.size()) field.push_back(value[index++]);
                else field.push_back(character);
            }
        } else {
            const auto comma = value.find(',', index);
            field = Trim(value.substr(index, comma - index));
            index = comma == std::string::npos ? value.size() : comma + 1;
        }
        if (!key.empty()) fields[key] = field;
    }
    return fields;
}

bool ParseComparison(std::string value, DriverRule& result) {
    value = Trim(value);
    if (value.rfind("<=", 0) == 0) {
        result.comparison = Comparison::LessOrEqual;
        value.erase(0, 2);
    } else if (value.rfind(">=", 0) == 0) {
        result.comparison = Comparison::GreaterOrEqual;
        value.erase(0, 2);
    } else if (value.rfind("<", 0) == 0) {
        result.comparison = Comparison::Less;
        value.erase(0, 1);
    } else if (value.rfind(">", 0) == 0) {
        result.comparison = Comparison::Greater;
        value.erase(0, 1);
    } else if (value.rfind("=", 0) == 0) {
        result.comparison = Comparison::Equal;
        value.erase(0, 1);
    } else {
        return false;
    }
    result.version = Version::Parse(Trim(value));
    return !result.version.components.empty();
}

bool ParseRule(const std::string& value, const std::uint32_t vendorId, DriverRule& result) {
    const auto fields = ParseFields(value);
    const auto driverVersion = fields.find("driverversion");
    if (vendorId == 0 || driverVersion == fields.end() ||
        !ParseComparison(driverVersion->second, result)) {
        return false;
    }
    result.vendorId = vendorId;

    const auto rhiName = fields.find("rhiname");
    if (rhiName != fields.end()) {
        result.rhiName = rhiName->second;
        const auto rhi = Lower(result.rhiName);
        if (rhi != "vulkan" && rhi != "d3d11" && rhi != "d3d12") return false;
    }
    const auto adapterName = fields.find("adapternameregex");
    if (adapterName != fields.end()) {
        result.adapterNameRegex = adapterName->second;
        try {
            std::regex test(result.adapterNameRegex, std::regex::ECMAScript | std::regex::icase);
        } catch (const std::regex_error&) {
            return false;
        }
    }
    const auto deviceIds = fields.find("deviceid");
    if (deviceIds != fields.end()) ParseDeviceIds(deviceIds->second, result);
    const auto driverIds = fields.find("driverid");
    if (driverIds != fields.end()) ParseDriverIds(driverIds->second, result);

    const auto reason = fields.find("reason");
    if (reason != fields.end()) result.reason = reason->second;
    const auto suggested = fields.find("suggesteddriverversion");
    if (suggested != fields.end()) result.suggestedVersion = suggested->second;
    const auto url = fields.find("downloadurl");
    if (url != fields.end()) result.downloadUrl = url->second;
    return true;
}

VendorPolicy* FindSection(Config& config, const std::string& section, std::uint32_t& vendorId) {
    const auto name = Lower(section);
    if (name == "gpu_nvidia" || name == "nvidia" || name == "gpu_0x10de") {
        vendorId = kVendorNvidia;
        return &config.nvidia;
    }
    if (name == "gpu_amd" || name == "amd" || name == "gpu_0x1002") {
        vendorId = kVendorAmd;
        return &config.amd;
    }
    if (name == "gpu_intel" || name == "intel" || name == "gpu_0x8086") {
        vendorId = kVendorIntel;
        return &config.intel;
    }
    if (name == "gpu_other" || name == "other") return &config.other;
    return nullptr;
}

bool Contains(const std::vector<std::uint32_t>& values, const std::uint32_t value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

void AppendMinimumRule(Config& config, const VendorPolicy& policy,
                       const std::uint32_t vendorId) {
    if (policy.minimumVersion.components.empty()) return;
    DriverRule rule;
    rule.vendorId = vendorId;
    // A vendor minimum applies to every active render API (the installed driver
    // serves both Vulkan and D3D12), so leave the RHI name empty.
    rule.rhiName.clear();
    rule.comparison = Comparison::Less;
    rule.version = policy.minimumVersion;
    rule.reason = "The installed graphics driver is below this game's minimum version.";
    rule.fromMinimumVersion = true;
    rule.suggestedVersion = policy.suggestedVersionText.empty()
        ? policy.minimumVersionText : policy.suggestedVersionText;
    rule.downloadUrl = policy.downloadUrl;
    config.driverDenyList.push_back(std::move(rule));
}

}  // namespace

Config DefaultConfig() {
    Config config;
    config.nvidia.name = "NVIDIA";
    config.nvidia.downloadUrl = "https://www.nvidia.com/Download/index.aspx";
    config.amd.name = "AMD";
    config.amd.downloadUrl = "https://www.amd.com/en/support/download/drivers.html";
    config.intel.name = "Intel";
    config.intel.downloadUrl = "https://www.intel.com/content/www/us/en/download-center/home.html";
    config.other.name = "GPU vendor";
    return config;
}

Config LoadConfig(const std::string& path) {
    Config config = DefaultConfig();
    std::ifstream input(path);
    if (!input) return config;

    VendorPolicy* policy = nullptr;
    std::uint32_t sectionVendorId = 0;
    std::string line;
    while (std::getline(input, line)) {
        line = Trim(line);
        if (line.empty() || line.front() == ';' || line.front() == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            sectionVendorId = 0;
            policy = FindSection(config, Trim(line.substr(1, line.size() - 2)), sectionVendorId);
            continue;
        }

        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;
        const auto key = Lower(Trim(line.substr(0, equals)));
        const auto value = Trim(line.substr(equals + 1));

        if (!policy) {
            if (key == "minimumvulkanversion") {
                const auto version = Version::Parse(value);
                if (!version.components.empty()) config.minimumVulkanMajor = version.components[0];
                if (version.components.size() > 1) config.minimumVulkanMinor = version.components[1];
            } else if (key == "renderapi") {
                config.checkVulkan = false;
                config.checkD3D11 = false;
                config.checkD3D12 = false;
                for (const auto& item : SplitSelectors(value)) {
                    const auto api = Lower(item);
                    if (api == "vulkan") {
                        config.checkVulkan = true;
                    } else if (api == "d3d11" || api == "direct3d11" || api == "dx11") {
                        config.checkD3D11 = true;
                    } else if (api == "d3d12" || api == "direct3d12" || api == "dx12") {
                        config.checkD3D12 = true;
                    }
                }
                if (!config.checkVulkan && !config.checkD3D11 && !config.checkD3D12) {
                    config.checkVulkan = true;
                }
            } else if (key == "minimumd3d11featurelevel") {
                const auto level = ParseFeatureLevel(value);
                if (level != 0) config.minimumD3D11FeatureLevel = level;
            } else if (key == "minimumfeaturelevel") {
                const auto level = ParseFeatureLevel(value);
                if (level != 0) config.minimumFeatureLevel = level;
            }
            continue;
        }

        if (key == "minimumdriverversion") {
            policy->minimumVersionText = value;
            policy->minimumVersion = Version::Parse(value);
        } else if (key == "suggesteddriverversion") {
            policy->suggestedVersionText = value;
            policy->suggestedVersion = Version::Parse(value);
        } else if (key == "downloadurl") {
            policy->downloadUrl = value;
        } else if (key == "driverdenylist" || key == "+driverdenylist") {
            DriverRule rule;
            if (ParseRule(value, sectionVendorId, rule)) {
                config.driverDenyList.push_back(std::move(rule));
            }
        }
    }
    AppendMinimumRule(config, config.nvidia, kVendorNvidia);
    AppendMinimumRule(config, config.amd, kVendorAmd);
    AppendMinimumRule(config, config.intel, kVendorIntel);
    return config;
}

bool Matches(const Version& installed, const DriverRule& rule) {
    const int comparison = Compare(installed, rule.version);
    switch (rule.comparison) {
        case Comparison::Less: return comparison < 0;
        case Comparison::LessOrEqual: return comparison <= 0;
        case Comparison::Equal: return comparison == 0;
        case Comparison::GreaterOrEqual: return comparison >= 0;
        case Comparison::Greater: return comparison > 0;
    }
    return false;
}

bool RhiIsActive(const std::string& ruleRhi, const bool checkVulkan, const bool checkD3D11,
                 const bool checkD3D12) {
    if (ruleRhi.empty()) return true;
    const auto rhi = Lower(ruleRhi);
    if (rhi == "vulkan") return checkVulkan;
    if (rhi == "d3d11") return checkD3D11;
    if (rhi == "d3d12") return checkD3D12;
    return false;
}

bool Matches(const GpuInfo& gpu, const DriverRule& rule) {
    if (gpu.vendorId != rule.vendorId) return false;
    if (!rule.adapterNameRegex.empty()) {
        try {
            const std::regex pattern(rule.adapterNameRegex,
                                     std::regex::ECMAScript | std::regex::icase);
            if (!std::regex_search(gpu.deviceName, pattern)) return false;
        } catch (const std::regex_error&) {
            return false;
        }
    }
    if (!rule.allDeviceIds && !rule.deviceIds.empty()) {
        const bool matched = std::any_of(rule.deviceIds.begin(), rule.deviceIds.end(),
            [&gpu](const DriverRule::IdRange& range) {
                return gpu.deviceId >= range.first && gpu.deviceId <= range.last;
            });
        if (!matched) return false;
    }
    if (!rule.allDriverIds && !rule.driverIds.empty() && !Contains(rule.driverIds, gpu.driverId)) {
        return false;
    }
    return Matches(gpu.unifiedDriverVersion, rule);
}

const VendorPolicy& PolicyFor(const Config& config, const std::uint32_t vendorId) {
    if (vendorId == kVendorNvidia) return config.nvidia;
    if (vendorId == kVendorAmd) return config.amd;
    if (vendorId == kVendorIntel) return config.intel;
    return config.other;
}

}  // namespace uvdg
