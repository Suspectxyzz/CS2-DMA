#include "SystemInfo.h"
#include "Logger.h"

#include <vector>
#include <map>
#include <sstream>
#include <algorithm>
#include <cstring>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <comdef.h>
#include <Wbemidl.h>
#include <intrin.h>

#pragma comment(lib, "wbemuuid.lib")

// =====================================================================
//  FNV-1a 64-bit hash
// =====================================================================
static uint64_t Fnv1a64(const std::string& data)
{
    uint64_t hash = 14695981039346656037ULL;
    for (unsigned char c : data)
    {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    return hash;
}

static std::string ToHex16(uint64_t value)
{
    char buf[17];
    snprintf(buf, sizeof(buf), "%016llX", (unsigned long long)value);
    return buf;
}

static bool IsValidUuid(const std::string& uuid)
{
    if (uuid.empty()) return false;
    if (uuid.find("00000000-0000-0000-0000-000000000000") != std::string::npos) return false;
    if (uuid.find("FFFFFFFF-FFFF-FFFF-FFFF-FFFFFFFFFFFF") != std::string::npos) return false;
    bool allZeroOrDash = true;
    for (char c : uuid)
    {
        if (c != '0' && c != '-') { allZeroOrDash = false; break; }
    }
    return !allZeroOrDash;
}

// =====================================================================
//  WMI session — RAII wrapper for COM + WMI
// =====================================================================
class WmiSession
{
public:
    WmiSession()
    {
        m_hrInit = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        m_shouldUninitialize = (m_hrInit == S_OK);

        if (FAILED(m_hrInit) && m_hrInit != RPC_E_CHANGED_MODE)
            return;

        HRESULT hr = CoCreateInstance(CLSID_WbemLocator, NULL, CLSCTX_INPROC_SERVER,
                                       IID_IWbemLocator, (LPVOID*)&m_pLoc);
        if (FAILED(hr)) return;

        hr = m_pLoc->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), NULL, NULL, NULL, 0, NULL, NULL, &m_pSvc);
        if (FAILED(hr)) { m_pLoc->Release(); m_pLoc = nullptr; return; }

        hr = CoSetProxyBlanket(m_pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL,
                               RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
                               NULL, EOAC_NONE);
        if (FAILED(hr)) { m_pSvc->Release(); m_pSvc = nullptr; m_pLoc->Release(); m_pLoc = nullptr; return; }

        m_ok = true;
    }

    ~WmiSession()
    {
        if (m_pSvc) m_pSvc->Release();
        if (m_pLoc) m_pLoc->Release();
        if (m_shouldUninitialize) CoUninitialize();
    }

    bool IsOk() const { return m_ok; }

    using Row = std::map<std::string, std::string>;

    std::vector<Row> Query(const char* queryStr, const std::vector<std::string>& properties)
    {
        std::vector<Row> results;
        if (!m_ok) return results;

        IEnumWbemClassObject* pEnum = nullptr;
        HRESULT hr = m_pSvc->ExecQuery(bstr_t("WQL"), bstr_t(queryStr),
                                        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                                        NULL, &pEnum);
        if (FAILED(hr)) return results;

        while (true)
        {
            IWbemClassObject* pObj = nullptr;
            ULONG ret = 0;
            hr = pEnum->Next(WBEM_INFINITE, 1, &pObj, &ret);
            if (ret == 0) break;

            Row row;
            for (const auto& prop : properties)
            {
                VARIANT vt;
                VariantInit(&vt);
                std::wstring wProp(prop.begin(), prop.end());
                hr = pObj->Get(wProp.c_str(), 0, &vt, NULL, NULL);
                if (SUCCEEDED(hr) && vt.vt != VT_NULL && vt.vt != VT_EMPTY)
                    row[prop] = VariantToUtf8(vt);
                VariantClear(&vt);
            }
            results.push_back(row);
            pObj->Release();
        }

        pEnum->Release();
        return results;
    }

private:
    static std::string VariantToUtf8(VARIANT& vt)
    {
        switch (vt.vt)
        {
        case VT_BSTR:
        {
            if (!vt.bstrVal) return "";
            char buf[1024] = {};
            WideCharToMultiByte(CP_UTF8, 0, vt.bstrVal, -1, buf, sizeof(buf) - 1, NULL, NULL);
            return buf;
        }
        case VT_I4:
        {
            char buf[32];
            snprintf(buf, sizeof(buf), "%d", vt.lVal);
            return buf;
        }
        case VT_UI4:
        {
            char buf[32];
            snprintf(buf, sizeof(buf), "%u", vt.ulVal);
            return buf;
        }
        case VT_I8:
        {
            char buf[32];
            snprintf(buf, sizeof(buf), "%lld", (long long)vt.llVal);
            return buf;
        }
        case VT_UI8:
        {
            char buf[32];
            snprintf(buf, sizeof(buf), "%llu", (unsigned long long)vt.ullVal);
            return buf;
        }
        case VT_BOOL:
            return vt.boolVal ? "true" : "false";
        default:
            return "";
        }
    }

    HRESULT m_hrInit = E_FAIL;
    IWbemLocator*  m_pLoc = nullptr;
    IWbemServices* m_pSvc = nullptr;
    bool m_shouldUninitialize = false;
    bool m_ok = false;
};

// =====================================================================
//  Machine code generation
// =====================================================================
std::string SystemInfo::GetMachineCode()
{
    std::string combined;

    {
        WmiSession wmi;
        if (wmi.IsOk())
        {
            auto rows = wmi.Query("SELECT UUID FROM Win32_ComputerSystemProduct", {"UUID"});
            if (!rows.empty() && rows[0].count("UUID"))
            {
                const std::string& uuid = rows[0].at("UUID");
                if (IsValidUuid(uuid))
                    combined += uuid;
            }

            rows = wmi.Query("SELECT ProcessorId FROM Win32_Processor", {"ProcessorId"});
            if (!rows.empty() && rows[0].count("ProcessorId") && !rows[0].at("ProcessorId").empty())
            {
                combined += "|" + rows[0].at("ProcessorId");
            }

            // 磁盘序列号（第一块物理硬盘）
            rows = wmi.Query("SELECT SerialNumber FROM Win32_DiskDrive WHERE MediaType LIKE '%Fixed%' OR MediaType LIKE '%Hard%' ORDER BY Index", {"SerialNumber"});
            if (!rows.empty() && rows[0].count("SerialNumber") && !rows[0].at("SerialNumber").empty())
            {
                combined += "|" + rows[0].at("SerialNumber");
            }

            // 网卡 MAC 地址（第一块物理网卡，排除虚拟适配器）
            rows = wmi.Query(
                "SELECT MACAddress FROM Win32_NetworkAdapter "
                "WHERE PhysicalAdapter = TRUE AND MACAddress IS NOT NULL "
                "AND NOT Description LIKE '%Virtual%' AND NOT Description LIKE '%VPN%' "
                "AND NOT Description LIKE '%Tunnel%' AND NOT Description LIKE '%Loop%' "
                "ORDER BY Index",
                {"MACAddress"});
            if (!rows.empty() && rows[0].count("MACAddress") && !rows[0].at("MACAddress").empty())
            {
                combined += "|" + rows[0].at("MACAddress");
            }
        }
    }

    if (combined.empty() || combined == "|")
    {
        combined.clear();

        DWORD volSerial = 0;
        if (GetVolumeInformationA("C:\\", NULL, 0, &volSerial, NULL, NULL, NULL, 0))
        {
            char buf[16];
            snprintf(buf, sizeof(buf), "%08X", volSerial);
            combined += buf;
        }

        char computerName[256] = {};
        DWORD size = sizeof(computerName);
        if (GetComputerNameA(computerName, &size))
        {
            combined += "|";
            combined += computerName;
        }

        int cpuInfo[4] = {};
        __cpuid(cpuInfo, 0);
        char vendor[13] = {};
        memcpy(vendor, &cpuInfo[1], 4);
        memcpy(vendor + 4, &cpuInfo[3], 4);
        memcpy(vendor + 8, &cpuInfo[2], 4);
        combined += "|";
        combined += vendor;
    }

    if (combined.empty())
        combined = "unknown";

    return ToHex16(Fnv1a64(combined));
}

// =====================================================================
//  System info collection
// =====================================================================
SystemInfo::FullInfo SystemInfo::CollectInfo()
{
    FullInfo info;

    {
        WmiSession wmi;
        if (wmi.IsOk())
        {
            auto cpuRows = wmi.Query(
                "SELECT Name, NumberOfCores, NumberOfLogicalProcessors FROM Win32_Processor",
                {"Name", "NumberOfCores", "NumberOfLogicalProcessors"});
            if (!cpuRows.empty())
            {
                auto& r = cpuRows[0];
                if (r.count("Name")) info.hardware.cpuName = r.at("Name");
                if (r.count("NumberOfCores") && !r.at("NumberOfCores").empty())
                    info.hardware.cpuCores = std::atoi(r.at("NumberOfCores").c_str());
                if (r.count("NumberOfLogicalProcessors") && !r.at("NumberOfLogicalProcessors").empty())
                    info.hardware.cpuLogicalProcessors = std::atoi(r.at("NumberOfLogicalProcessors").c_str());
            }

            auto memRows = wmi.Query(
                "SELECT TotalPhysicalMemory FROM Win32_ComputerSystem",
                {"TotalPhysicalMemory"});
            if (!memRows.empty() && memRows[0].count("TotalPhysicalMemory") && !memRows[0].at("TotalPhysicalMemory").empty())
            {
                try { info.hardware.totalMemoryMB = std::stoull(memRows[0].at("TotalPhysicalMemory")) / (1024 * 1024); }
                catch (...) {}
            }

            auto gpuRows = wmi.Query(
                "SELECT Name, AdapterRAM FROM Win32_VideoController",
                {"Name", "AdapterRAM"});
            if (!gpuRows.empty())
            {
                auto& r = gpuRows[0];
                if (r.count("Name")) info.hardware.gpuName = r.at("Name");
                if (r.count("AdapterRAM") && !r.at("AdapterRAM").empty())
                {
                    try { info.hardware.gpuMemoryMB = std::stoull(r.at("AdapterRAM")) / (1024 * 1024); }
                    catch (...) {}
                }
            }

            auto diskRows = wmi.Query(
                "SELECT Size, MediaType FROM Win32_DiskDrive",
                {"Size", "MediaType"});
            std::string diskSummary;
            for (auto& r : diskRows)
            {
                uint64_t sizeMB = 0;
                if (r.count("Size") && !r.at("Size").empty())
                {
                    try { sizeMB = std::stoull(r.at("Size")) / (1024 * 1024); }
                    catch (...) {}
                }
                std::string mediaType = r.count("MediaType") ? r.at("MediaType") : "Unknown";
                if (!diskSummary.empty()) diskSummary += "; ";
                char buf[256];
                snprintf(buf, sizeof(buf), "%s %lluMB", mediaType.c_str(), (unsigned long long)sizeMB);
                diskSummary += buf;
            }
            info.hardware.diskInfo = diskSummary;
        }
    }

    {
        typedef LONG(WINAPI* RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
        HMODULE hNtdll = GetModuleHandleA("ntdll.dll");
        if (hNtdll)
        {
            auto fn = (RtlGetVersionPtr)GetProcAddress(hNtdll, "RtlGetVersion");
            if (fn)
            {
                RTL_OSVERSIONINFOW rovi{};
                rovi.dwOSVersionInfoSize = sizeof(rovi);
                if (fn(&rovi) == 0)
                {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "%lu.%lu", rovi.dwMajorVersion, rovi.dwMinorVersion);
                    info.os.version = buf;
                    info.os.build = std::to_string(rovi.dwBuildNumber);
                }
            }
        }
    }

    {
        SYSTEM_INFO si{};
        GetNativeSystemInfo(&si);
        switch (si.wProcessorArchitecture)
        {
        case PROCESSOR_ARCHITECTURE_AMD64: info.os.arch = "x64"; break;
        case PROCESSOR_ARCHITECTURE_ARM64: info.os.arch = "ARM64"; break;
        case PROCESSOR_ARCHITECTURE_INTEL: info.os.arch = "x86"; break;
        default: info.os.arch = "Unknown"; break;
        }
    }

    {
        LANGID sysLang = GetSystemDefaultLangID();
        char langBuf[16];
        snprintf(langBuf, sizeof(langBuf), "0x%04X", sysLang);
        info.software.systemLanguage = langBuf;

        WCHAR localeName[LOCALE_NAME_MAX_LENGTH] = {};
        if (GetLocaleInfoW(LOCALE_SYSTEM_DEFAULT, LOCALE_SNAME, localeName, LOCALE_NAME_MAX_LENGTH))
        {
            char utf8[LOCALE_NAME_MAX_LENGTH] = {};
            WideCharToMultiByte(CP_UTF8, 0, localeName, -1, utf8, sizeof(utf8), NULL, NULL);
            info.software.systemLocale = utf8;
        }
    }

    return info;
}

// =====================================================================
//  Format info for logging
// =====================================================================
std::string SystemInfo::FormatInfoLog(const FullInfo& info)
{
    std::string result;

    result += "[System Info]\n";
    result += "  CPU: " + info.hardware.cpuName;
    if (info.hardware.cpuCores > 0)
    {
        result += " (" + std::to_string(info.hardware.cpuCores) + "C/" +
                  std::to_string(info.hardware.cpuLogicalProcessors) + "T)";
    }
    result += "\n";

    if (info.hardware.totalMemoryMB > 0)
        result += "  Memory: " + std::to_string(info.hardware.totalMemoryMB) + " MB\n";

    if (!info.hardware.gpuName.empty())
    {
        result += "  GPU: " + info.hardware.gpuName;
        if (info.hardware.gpuMemoryMB > 0)
            result += " (" + std::to_string(info.hardware.gpuMemoryMB) + " MB)";
        result += "\n";
    }

    if (!info.hardware.diskInfo.empty())
        result += "  Disk: " + info.hardware.diskInfo + "\n";

    if (!info.os.version.empty())
        result += "  OS: Windows " + info.os.version + " Build " + info.os.build + " " + info.os.arch + "\n";

    if (!info.software.systemLanguage.empty())
        result += "  Language: " + info.software.systemLanguage + "\n";

    if (!info.software.systemLocale.empty())
        result += "  Locale: " + info.software.systemLocale + "\n";

    return result;
}
