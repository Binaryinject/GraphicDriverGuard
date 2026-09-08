using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using UnityEditor;
using UnityEditor.Build;
using UnityEditor.Build.Reporting;
using UnityEngine;

namespace GraphicDriverGuard.Editor
{
    internal sealed class DriverGuardBuildProcessor : IPostprocessBuildWithReport
    {
        public int callbackOrder => 10000;

        public void OnPostprocessBuild(BuildReport report)
        {
            var target = report.summary.platform;
            if (target != BuildTarget.StandaloneWindows64 && target != BuildTarget.StandaloneLinux64)
                return;

            var outputPath = Path.GetFullPath(report.summary.outputPath);
            var buildDirectory = Directory.Exists(outputPath)
                ? outputPath
                : Path.GetDirectoryName(outputPath);
            if (string.IsNullOrEmpty(buildDirectory))
                throw new BuildFailedException("Cannot determine the standalone player output directory.");

            var windows = target == BuildTarget.StandaloneWindows64;
            var playerName = windows ? "UnityPlayer.dll" : "UnityPlayer.so";
            var originalPlayerName = windows ? "UnityPlayer_.dll" : "UnityPlayer_.so";

            if (!DriverGuardSettings.instance.enableBuildIntegration)
            {
                RemoveGeneratedArtifacts(buildDirectory, playerName, originalPlayerName);
                Debug.Log("[GraphicDriverGuard] Build integration is disabled; no proxy or configuration was added.");
                return;
            }

            var architecture = "x86_64";

            var packageRoot = Environment.GetEnvironmentVariable("UVDG_PACKAGE_ROOT");
            if (string.IsNullOrWhiteSpace(packageRoot))
            {
                var package = UnityEditor.PackageManager.PackageInfo.FindForAssembly(typeof(DriverGuardBuildProcessor).Assembly);
                if (package == null)
                    throw new BuildFailedException("Cannot resolve the Graphic Driver Guard package path.");
                packageRoot = package.resolvedPath;
            }

            var platform = windows ? "Windows" : "Linux";
            var proxySource = Path.Combine(packageRoot, "Native~", platform, architecture, playerName);
            var playerPath = Path.Combine(buildDirectory, playerName);
            var originalPlayerPath = Path.Combine(buildDirectory, originalPlayerName);
            var temporaryProxy = playerPath + ".uvdg.tmp";

            if (!File.Exists(proxySource))
                throw new BuildFailedException($"Driver Guard native proxy is missing: {proxySource}");
            if (!File.Exists(playerPath))
                throw new BuildFailedException($"Unity player library is missing: {playerPath}");
            if (File.Exists(originalPlayerPath))
                throw new BuildFailedException($"Build output was already patched: {originalPlayerPath}");

            File.Copy(proxySource, temporaryProxy, true);
            try
            {
                File.Move(playerPath, originalPlayerPath);
                File.Move(temporaryProxy, playerPath);
            }
            catch
            {
                if (!File.Exists(playerPath) && File.Exists(originalPlayerPath))
                    File.Move(originalPlayerPath, playerPath);
                throw;
            }
            finally
            {
                if (File.Exists(temporaryProxy))
                    File.Delete(temporaryProxy);
            }

            var configPath = Path.Combine(buildDirectory, "DriverGuard.ini");
            File.WriteAllText(configPath, CreateConfig(DriverGuardSettings.instance, windows),
                new UTF8Encoding(false));
            Debug.Log($"[GraphicDriverGuard] Installed pre-render proxy: {playerPath}");
        }

        private static void RemoveGeneratedArtifacts(string buildDirectory, string playerName,
            string originalPlayerName)
        {
            var configPath = Path.Combine(buildDirectory, "DriverGuard.ini");
            var playerPath = Path.Combine(buildDirectory, playerName);
            var originalPlayerPath = Path.Combine(buildDirectory, originalPlayerName);
            var temporaryProxyPath = playerPath + ".uvdg.tmp";

            // A previous enabled build may have replaced UnityPlayer and left the original beside it.
            // Restore that original before removing the generated configuration.
            if (File.Exists(originalPlayerPath))
            {
                if (File.Exists(playerPath))
                    File.Delete(playerPath);
                File.Move(originalPlayerPath, playerPath);
            }

            if (File.Exists(configPath))
                File.Delete(configPath);
            if (File.Exists(temporaryProxyPath))
                File.Delete(temporaryProxyPath);
        }

        internal static string CreateConfig(DriverGuardSettings settings, bool windows)
        {
            var output = new StringBuilder();
            output.AppendLine("; Generated by Graphic Driver Guard.");
            output.AppendLine("; DriverDenyList uses UE5-style constraints plus Vulkan ID extensions.");
            output.AppendLine("[Global]");
            output.AppendLine($"MinimumVulkanVersion={settings.minimumVulkanMajor}.{settings.minimumVulkanMinor}");
            var renderApis = new List<string>();
            if (settings.checkVulkan) renderApis.Add("Vulkan");
            // Direct3D 11/12 are Windows-only. A Linux player cannot use them, so
            // never emit a D3D check (or a feature-level requirement) for Linux builds.
            if (windows && settings.checkD3D11) renderApis.Add("D3D11");
            if (windows && settings.checkD3D12) renderApis.Add("D3D12");
            if (renderApis.Count == 0) renderApis.Add("Vulkan");
            output.AppendLine($"RenderAPI={string.Join(",", renderApis)}");
            if (windows && settings.checkD3D11)
                output.AppendLine($"MinimumD3D11FeatureLevel={FeatureLevelName11(settings.minimumD3D11FeatureLevel)}");
            if (windows)
                output.AppendLine($"MinimumFeatureLevel={FeatureLevelName(settings.minimumFeatureLevel)}");
            AppendPolicy(output, "GPU_NVIDIA", settings.nvidia, windows);
            AppendPolicy(output, "GPU_AMD", settings.amd, windows);
            AppendPolicy(output, "GPU_INTEL", settings.intel, windows);
            return output.ToString();
        }

        private static void AppendPolicy(StringBuilder output, string section,
            VendorDriverPolicy policy, bool windows)
        {
            output.AppendLine();
            output.AppendLine($"[{section}]");
            var suggestedVersion = windows ? policy.windowsSuggestedVersion : policy.linuxSuggestedVersion;
            var minimumVersion = windows ? policy.windowsMinimumVersion : policy.linuxMinimumVersion;
            if (string.IsNullOrWhiteSpace(minimumVersion))
                minimumVersion = suggestedVersion;
            output.AppendLine($"MinimumDriverVersion={Safe(minimumVersion)}");
            output.AppendLine($"SuggestedDriverVersion={Safe(suggestedVersion)}");
            output.AppendLine($"DownloadURL={Safe(policy.downloadUrl)}");

            var platform = windows ? DriverRulePlatform.Windows : DriverRulePlatform.Linux;
            foreach (var rule in policy.denyList.Where(rule =>
                         rule != null &&
                         rule.enabled &&
                         (rule.platform == DriverRulePlatform.All || rule.platform == platform) &&
                         (windows || (rule.rhi != DriverRuleRhi.D3D11 && rule.rhi != DriverRuleRhi.D3D12)) &&
                         !string.IsNullOrWhiteSpace(rule.comparison)))
            {
                output.Append("+DriverDenyList=(DriverVersion=\"");
                output.Append(Quoted(rule.comparison));
                output.Append('"');
                if (rule.rhi == DriverRuleRhi.Vulkan)
                    output.Append(",RHIName=\"Vulkan\"");
                else if (rule.rhi == DriverRuleRhi.D3D11)
                    output.Append(",RHIName=\"D3D11\"");
                else if (rule.rhi == DriverRuleRhi.D3D12)
                    output.Append(",RHIName=\"D3D12\"");
                AppendField(output, "AdapterNameRegex", rule.adapterNameRegex);
                AppendField(output, "DeviceId", rule.deviceIds);
                if (rule.rhi == DriverRuleRhi.Vulkan)
                    AppendField(output, "DriverId", rule.driverIds);
                AppendField(output, "SuggestedDriverVersion",
                    string.IsNullOrWhiteSpace(rule.suggestedVersion)
                        ? suggestedVersion
                        : rule.suggestedVersion);
                AppendField(output, "DownloadURL", rule.downloadUrlOverride);
                AppendField(output, "Reason", rule.reason);
                output.AppendLine(")");
            }
        }

        private static void AppendField(StringBuilder output, string name, string value)
        {
            if (string.IsNullOrWhiteSpace(value))
                return;
            output.Append(',').Append(name).Append("=\"").Append(Quoted(value)).Append('"');
        }

        private static string FeatureLevelName11(D3D11FeatureLevel level)
        {
            switch (level)
            {
                case D3D11FeatureLevel.Level9_1: return "9_1";
                case D3D11FeatureLevel.Level9_2: return "9_2";
                case D3D11FeatureLevel.Level9_3: return "9_3";
                case D3D11FeatureLevel.Level10_0: return "10_0";
                case D3D11FeatureLevel.Level10_1: return "10_1";
                case D3D11FeatureLevel.Level11_1: return "11_1";
                default: return "11_0";
            }
        }

        private static string FeatureLevelName(D3D12FeatureLevel level)
        {
            switch (level)
            {
                case D3D12FeatureLevel.Level11_1: return "11_1";
                case D3D12FeatureLevel.Level12_0: return "12_0";
                case D3D12FeatureLevel.Level12_1: return "12_1";
                default: return "11_0";
            }
        }

        private static string Quoted(string value)
        {
            return Safe(value).Replace("\\", "\\\\").Replace("\"", "\\\"");
        }

        private static string Safe(string value)
        {
            return (value ?? string.Empty).Replace("\r", " ").Replace("\n", " ").Trim();
        }
    }
}
