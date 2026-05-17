#pragma once

#include <string>
#include <cstdint>

namespace SystemInfo
{
    struct HardwareInfo
    {
        std::string cpuName;
        int cpuCores = 0;
        int cpuLogicalProcessors = 0;
        uint64_t totalMemoryMB = 0;
        std::string gpuName;
        uint64_t gpuMemoryMB = 0;
        std::string diskInfo;
    };

    struct OSInfo
    {
        std::string version;
        std::string build;
        std::string arch;
    };

    struct SoftwareInfo
    {
        std::string systemLanguage;
        std::string systemLocale;
    };

    struct FullInfo
    {
        HardwareInfo hardware;
        OSInfo os;
        SoftwareInfo software;
    };

    std::string GetMachineCode();

    FullInfo CollectInfo();

    std::string FormatInfoLog(const FullInfo& info);
}
