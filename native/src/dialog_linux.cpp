#include "uvdg/preflight.h"

#include "uvdg/localization.h"
#include "uvdg/dx12_probe.h"
#include "uvdg/vulkan_probe.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace uvdg {
namespace {

int Run(const std::vector<std::string>& arguments, std::string* output = nullptr) {
    if (arguments.empty()) return -1;
    int outputPipe[2] = {-1, -1};
    if (output && pipe(outputPipe) != 0) return -1;
    const pid_t child = fork();
    if (child < 0) {
        if (output) {
            close(outputPipe[0]);
            close(outputPipe[1]);
        }
        return -1;
    }
    if (child == 0) {
        if (output) {
            close(outputPipe[0]);
            dup2(outputPipe[1], STDOUT_FILENO);
            close(outputPipe[1]);
        }
        std::vector<char*> argv;
        argv.reserve(arguments.size() + 1);
        for (const auto& value : arguments) argv.push_back(const_cast<char*>(value.c_str()));
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        _exit(127);
    }
    if (output) {
        close(outputPipe[1]);
        output->clear();
        std::array<char, 256> buffer{};
        for (;;) {
            const ssize_t count = read(outputPipe[0], buffer.data(), buffer.size());
            if (count <= 0) break;
            output->append(buffer.data(), static_cast<std::size_t>(count));
        }
        close(outputPipe[0]);
        while (!output->empty() && (output->back() == '\n' || output->back() == '\r')) {
            output->pop_back();
        }
    }
    int status = 0;
    if (waitpid(child, &status, 0) < 0 || !WIFEXITED(status)) return -1;
    return WEXITSTATUS(status);
}

Language SystemLanguage() {
    const char* locale = std::getenv("LC_ALL");
    if (!locale || !*locale) locale = std::getenv("LC_MESSAGES");
    if (!locale || !*locale) locale = std::getenv("LANG");
    return LanguageFromLocale(locale ? locale : "en");
}

std::string Details(const PreflightResult& result, const Language language) {
    const LocalizedText& text = Text(language);
    std::ostringstream stream;
    stream << LocalizedReason(language, result);
    if (!result.gpu.deviceName.empty()) {
        stream << "\n\n" << text.gpu << ": " << result.gpu.deviceName
               << "\n" << text.installedDriver << ": "
               << result.gpu.unifiedDriverVersion.ToString();
        if (result.checkVulkan) {
            stream << "\n" << text.vulkanApi << ": " << FormatVulkanVersion(result.gpu.apiVersion)
                   << "\n" << text.requiredVulkanApi << ": "
                   << result.requiredVulkanMajor << '.' << result.requiredVulkanMinor
                   << ' ' << text.orNewer;
        }
        if (result.checkD3D11) {
            stream << "\n" << text.dx11FeatureLevel << ": "
                   << FormatFeatureLevel(result.gpu.d3d11FeatureLevel)
                   << "\n" << text.requiredD3D11FeatureLevel << ": "
                   << FormatFeatureLevel(result.requiredD3D11FeatureLevel) << ' ' << text.orNewer;
        }
        if (result.checkD3D12) {
            stream << "\n" << text.dx12FeatureLevel << ": "
                   << FormatFeatureLevel(result.gpu.d3d12FeatureLevel)
                   << "\n" << text.requiredFeatureLevel << ": "
                   << FormatFeatureLevel(result.requiredFeatureLevel) << ' ' << text.orNewer;
        }
    }
    if (!result.suggestedVersion.empty()) {
        stream << "\n" << text.recommendedDriver << ": " << result.suggestedVersion;
    }
    return stream.str();
}

}  // namespace

std::string ExecutableDirectory() {
    std::array<char, 4096> path{};
    const ssize_t length = readlink("/proc/self/exe", path.data(), path.size() - 1);
    if (length <= 0) return ".";
    std::string result(path.data(), static_cast<std::size_t>(length));
    const auto slash = result.find_last_of('/');
    return slash == std::string::npos ? "." : result.substr(0, slash);
}

bool ShowFailureDialog(const PreflightResult& result) {
    const Language language = SystemLanguage();
    const LocalizedText& text = Text(language);
    const bool dx11Failure = result.failure == FailureKind::D3D11Unavailable ||
                             result.failure == FailureKind::D3D11FeatureLevelUnsupported;
    const bool dx12Failure = result.failure == FailureKind::D3D12Unavailable ||
                             result.failure == FailureKind::D3D12FeatureLevelUnsupported;
    const std::string title = result.failure == FailureKind::DriverDenied
        ? text.driverTitle
        : (dx11Failure ? text.dx11Title : (dx12Failure ? text.dx12Title : text.vulkanTitle));
    const std::string details = Details(result, language);

    const bool canContinue = result.CanContinue();
    std::vector<std::string> zenity{"zenity", "--question", "--title=" + title,
                                    "--text=" + details,
                                    "--ok-label=" + std::string(canContinue
                                        ? text.continueRunning : text.updateDriver),
                                    "--cancel-label=" + std::string(text.exit), "--width=520"};
    if (canContinue && !result.downloadUrl.empty()) {
        zenity.push_back("--extra-button=" + std::string(text.updateDriver));
    }
    std::string zenityOutput;
    int selected = Run(zenity, &zenityOutput);
    bool usedKdialog = false;
    if (selected == 127) {
        usedKdialog = true;
        if (canContinue) {
            selected = Run({"kdialog", "--title", title, "--yesnocancel", details,
                            "--yes-label", text.continueRunning,
                            "--no-label", text.updateDriver,
                            "--cancel-label", text.exit});
        } else {
            selected = Run({"kdialog", "--title", title, "--yesno", details,
                            "--yes-label", text.updateDriver, "--no-label", text.exit});
        }
    }
    if (selected < 0 || selected == 127) {
        std::fprintf(stderr, "%s\n%s\n", title.c_str(), details.c_str());
        return false;
    }
    if (canContinue && selected == 0 && zenityOutput == text.updateDriver) {
        Run({"xdg-open", result.downloadUrl});
        return false;
    }
    if (!canContinue && selected == 0 && !result.downloadUrl.empty()) {
        Run({"xdg-open", result.downloadUrl});
    }
    if (canContinue && usedKdialog && selected == 1 && !result.downloadUrl.empty()) {
        Run({"xdg-open", result.downloadUrl});
    }
    return canContinue && selected == 0;
}

}  // namespace uvdg
