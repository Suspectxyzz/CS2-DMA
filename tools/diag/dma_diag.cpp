// dma_diag.cpp — DMA 5E环境诊断工具
// 目标：定位5E环境下DMA读不到client.dll，到底卡在哪一层
//   层1: 模块可见性（DTB隐藏 / PEB模块链被抹）
//   层2: 数据加密（模块可见但读到乱码）
//   层3: IOMMU硬件封杀（用户已确认未开，阶段0会再次验证）
//
// 编译: cl /EHsc /std:c++17 /I"<includes>" dma_diag.cpp /link /LIBPATH:"<lib>" vmm.lib leechcore.lib
// 运行: dma_diag.exe [device]    device 默认 "fpga://algo=0"

#include <windows.h>
#include <vmmdll.h>
#include <leechcore.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <sstream>
#include <thread>
#include <chrono>

#pragma comment(lib, "vmm.lib")
#pragma comment(lib, "leechcore.lib")

static void Out(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fputc('\n', stdout);
    fflush(stdout);
}

// 读VFS文本文件（分块读，直到EOF或达到上限）
static bool VfsReadText(VMM_HANDLE h, const wchar_t* path, std::string& out) {
    char buf[4096];
    DWORD got = 0;
    ULONG64 off = 0;
    out.clear();
    while (true) {
        if (VMMDLL_VfsReadW(h, (LPWSTR)path, (PBYTE)buf, sizeof(buf), &got, off) != VMMDLL_STATUS_SUCCESS)
            break;
        if (got == 0) break;
        out.append(buf, got);
        off += got;
        if (out.size() > 4 * 1024 * 1024) break; // 4MB cap
    }
    return !out.empty();
}

// 试探某个DTB能否让目标模块可见
static bool TryDtb(VMM_HANDLE h, DWORD pid, uint64_t dtb, const char* modName,
                   bool fastMode, uint64_t* pBase = nullptr) {
    ULONG64 opt = fastMode ? (VMMDLL_OPT_PROCESS_DTB_FAST_LOWINTEGRITY | pid)
                           : (VMMDLL_OPT_PROCESS_DTB | pid);
    VMMDLL_ConfigSet(h, opt, dtb);
    VMMDLL_ConfigSet(h, VMMDLL_OPT_REFRESH_ALL, 1);
    PVMMDLL_MAP_MODULEENTRY pEntry = nullptr;
    BOOL ok = VMMDLL_Map_GetModuleFromNameU(h, pid, (LPSTR)modName, &pEntry, 0);
    if (ok && pEntry) {
        if (pBase) *pBase = pEntry->vaBase;
        Out("    [DTB 0x%llX%s] OK -> %s base=0x%llX size=0x%llX",
            (unsigned long long)dtb, fastMode ? "|FAST" : "", modName,
            (unsigned long long)pEntry->vaBase, (unsigned long long)pEntry->cbImageSize);
        VMMDLL_MemFree(pEntry);
        return true;
    }
    return false;
}

int main(int argc, char** argv) {
    Out("========== DMA 5E 环境诊断工具 ==========");
    const char* device = (argc > 1) ? argv[1] : "fpga://algo=0";
    Out("device = %s", device);

    // ---------- 阶段0: 连接DMA ----------
    Out("\n[阶段0] 连接DMA...");
    char exePath[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string mmapPath = exePath;
    auto p = mmapPath.rfind('\\');
    if (p != std::string::npos) mmapPath = mmapPath.substr(0, p + 1);
    mmapPath += "mmap.txt";
    bool hasMmap = (GetFileAttributesA(mmapPath.c_str()) != INVALID_FILE_ATTRIBUTES);
    Out("  mmap.txt: %s (%s)", hasMmap ? "存在" : "不存在", mmapPath.c_str());

    PLC_CONFIG_ERRORINFO pErr = nullptr;
    VMM_HANDLE h = nullptr;
    if (hasMmap) {
        LPSTR args[] = { (LPSTR)"", (LPSTR)"-device", (LPSTR)device, (LPSTR)"-memmap", (LPSTR)"-norefresh" };
        h = VMMDLL_InitializeEx(5, args, &pErr);
    } else {
        LPSTR args[] = { (LPSTR)"", (LPSTR)"-device", (LPSTR)device, (LPSTR)"-norefresh" };
        h = VMMDLL_InitializeEx(4, args, &pErr);
    }
    if (!h) {
        Out("[FAIL] DMA连接失败");
        if (pErr) {
            char buf[2048] = {0};
            if (pErr->cwszUserText > 0 && pErr->wszUserText) {
                WideCharToMultiByte(CP_UTF8, 0, pErr->wszUserText, (int)pErr->cwszUserText,
                                    buf, sizeof(buf) - 1, NULL, NULL);
                Out("  错误信息: %s", buf);
            }
            Out("  fUserInputRequest=%d (若为1通常意味着VT/IOMMU拒绝)", pErr->fUserInputRequest ? 1 : 0);
            LcMemFree(pErr);
        }
        Out("\n>>> 结论: 第0层失败 - DMA硬件连不上。检查FPGA/USB/驱动；若fUserInputRequest=1则IOMMU可能其实开着。");
        return 1;
    }
    Out("[OK] DMA已连接");
    ULONG64 vMaj = 0, vMin = 0;
    VMMDLL_ConfigGet(h, LC_OPT_FPGA_VERSION_MAJOR, &vMaj);
    VMMDLL_ConfigGet(h, LC_OPT_FPGA_VERSION_MINOR, &vMin);
    Out("  FPGA固件: %llu.%llu", (unsigned long long)vMaj, (unsigned long long)vMin);

    // ---------- 阶段1: 枚举进程 ----------
    Out("\n[阶段1] 枚举进程...");
    PVMMDLL_PROCESS_INFORMATION pAll = nullptr;
    DWORD cProc = 0;
    if (!VMMDLL_ProcessGetInformationAll(h, &pAll, &cProc)) {
        Out("[FAIL] ProcessGetInformationAll 失败");
        VMMDLL_Close(h);
        return 1;
    }
    Out("  共 %u 个进程", cProc);

    DWORD cs2Pid = 0;
    uint64_t cs2Dtb = 0, cs2Peb = 0;
    for (DWORD i = 0; i < cProc; i++) {
        const char* nm = pAll[i].szNameLong;
        if (!nm || !nm[0]) nm = pAll[i].szName;
        std::string name = nm ? nm : "";
        if (_stricmp(name.c_str(), "cs2.exe") == 0) {
            cs2Pid = pAll[i].dwPID;
            cs2Dtb = pAll[i].paDTB;
            cs2Peb = pAll[i].win.vaPEB;
        }
        // 找5E/反作弊相关进程（关键词：5e / anticheat / ace / protect / cheat）
        std::string low = name;
        for (auto& c : low) c = (char)tolower(c);
        if (low.find("5e") != std::string::npos ||
            low.find("anticheat") != std::string::npos ||
            low.find("anti-cheat") != std::string::npos ||
            low.find("ace-") != std::string::npos ||
            low.find("protect") != std::string::npos ||
            low.find("cheat") != std::string::npos) {
            Out("  发现相关进程: %-32s PID=%-6u DTB=0x%llX",
                name.c_str(), pAll[i].dwPID, (unsigned long long)pAll[i].paDTB);
        }
    }
    VMMDLL_MemFree(pAll);

    if (cs2Pid == 0) {
        Out("\n[FAIL] 未找到 cs2.exe");
        Out(">>> 结论: CS2没在运行。请在5E启动CS2后再跑本工具。");
        VMMDLL_Close(h);
        return 1;
    }
    Out("  cs2.exe: PID=%u, 默认DTB=0x%llX, PEB=0x%llX",
        cs2Pid, (unsigned long long)cs2Dtb, (unsigned long long)cs2Peb);

    // ---------- 阶段2: 模块可见性诊断（核心，对应日志卡点）----------
    Out("\n[阶段2] 模块可见性诊断 - client.dll");
    const char* modName = "client.dll";
    uint64_t clientBase = 0;

    PVMMDLL_MAP_MODULEENTRY pEntry = nullptr;
    BOOL found = VMMDLL_Map_GetModuleFromNameU(h, cs2Pid, (LPSTR)modName, &pEntry, 0);
    if (found && pEntry) {
        clientBase = pEntry->vaBase;
        Out("[OK] 默认DTB下 client.dll 可见: base=0x%llX size=0x%llX",
            (unsigned long long)pEntry->vaBase, (unsigned long long)pEntry->cbImageSize);
        VMMDLL_MemFree(pEntry);
    } else {
        Out("[X] 默认DTB下 client.dll 找不到 - 这就是你日志里的卡点");
        Out("    (对应日志: 'client.dll not found ... trying CR3 fix ... failed after CR3 fix')");

        // 2.1 初始化VFS插件（读dtb.txt需要）
        Out("\n  初始化VFS插件...");
        if (!VMMDLL_InitializePlugins(h)) {
            Out("  [FAIL] VMMDLL_InitializePlugins 失败");
            Out(">>> 结论: 第1层 - VFS插件初始化失败，无法获取候选DTB。");
            VMMDLL_Close(h);
            return 2;
        }
        // 2.2 等插件就绪（progress=100）
        Out("  等待procinfo插件就绪 (最多15s)...");
        bool ready = false;
        for (int i = 0; i < 150; i++) {
            std::string pct;
            if (VfsReadText(h, L"\\misc\\procinfo\\progress_percent.txt", pct)) {
                int v = atoi(pct.c_str());
                if (v >= 100) { ready = true; break; }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        Out("  插件就绪: %s", ready ? "是" : "否(超时)");

        // 2.3 读dtb.txt
        std::string dtbText;
        bool gotDtb = VfsReadText(h, L"\\misc\\procinfo\\dtb.txt", dtbText);
        Out("  dtb.txt: %s (%zu 字节)", gotDtb ? "读取成功" : "读取失败", dtbText.size());
        if (gotDtb && dtbText.size() < 300) {
            Out("  dtb.txt 内容:\n%s", dtbText.c_str());
        }

        // 2.4 解析候选DTB
        // dtb.txt 每行: <index:hex> <pid:dec> <dtb:hex> <kernelAddr:hex> <name>
        std::vector<uint64_t> cand;
        if (gotDtb) {
            std::istringstream iss(dtbText);
            std::string line;
            while (std::getline(iss, line)) {
                if (line.empty()) continue;
                std::istringstream ls(line);
                std::string idx, pidStr, dtbStr, kAddr, nm;
                if (ls >> idx >> pidStr >> dtbStr >> kAddr >> nm) {
                    uint64_t dtb = strtoull(dtbStr.c_str(), nullptr, 16);
                    uint64_t pid = strtoull(pidStr.c_str(), nullptr, 10);
                    std::string low = nm;
                    for (auto& c : low) c = (char)tolower(c);
                    if (dtb != 0 && (pid == cs2Pid || pid == 0 || low.find("cs2") != std::string::npos)) {
                        cand.push_back(dtb);
                    }
                }
            }
        }
        Out("  解析出候选DTB: %zu 个", cand.size());

        // 2.5 逐个试探（普通模式 + FAST_LOWINTEGRITY模式）
        bool anyOk = false;
        uint64_t workDtb = 0;
        Out("\n  [普通模式] 试探候选DTB...");
        for (size_t i = 0; i < cand.size(); i++) {
            if (TryDtb(h, cs2Pid, cand[i], modName, false, &clientBase)) {
                anyOk = true; workDtb = cand[i]; break;
            }
        }
        if (!anyOk) {
            Out("\n  [FAST_LOWINTEGRITY模式] 试探候选DTB...");
            for (size_t i = 0; i < cand.size(); i++) {
                if (TryDtb(h, cs2Pid, cand[i], modName, true, &clientBase)) {
                    anyOk = true; workDtb = cand[i]; break;
                }
            }
        }

        if (anyOk) {
            Out("\n[OK] 找到可用DTB: 0x%llX - client.dll 现在可见", (unsigned long long)workDtb);
            Out("    >>> 说明: 你的FixCr3漏了某些候选DTB(或没试FAST模式)。补上即可。");
        } else {
            Out("\n[X] 所有候选DTB都无法让client.dll可见");
            // 2.6 读PEB.Ldr，判断5E是否抹了模块链
            Out("\n  进一步: 读PEB.Ldr判断5E是否抹了模块链...");
            if (cs2Peb != 0) {
                BYTE pebBuf[0x100] = {0};
                DWORD flags = VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_NOPAGING | VMMDLL_FLAG_ZEROPAD_ON_FAIL | VMMDLL_FLAG_NOPAGING_IO;
                if (VMMDLL_MemReadEx(h, cs2Pid, cs2Peb, pebBuf, sizeof(pebBuf), 0, flags)) {
                    uint64_t ldr = *(uint64_t*)(pebBuf + 0x18); // x64 PEB.Ldr 偏移0x18
                    Out("  PEB(0x%llX).Ldr = 0x%llX", (unsigned long long)cs2Peb, (unsigned long long)ldr);
                    // PEB前16字节是否合理（PEB开头是BeingDebugged等，通常前几字节非全零）
                    bool pebAllZero = true;
                    for (int i = 0; i < 16; i++) if (pebBuf[i] != 0) { pebAllZero = false; break; }
                    if (pebAllZero) {
                        Out("  >>> PEB全零 - DTB完全不对，连PEB都读不到。");
                        Out("  >>> 需从物理内存暴力搜索正确的PML4页表基址(5E可能彻底换了DTB)。");
                    } else if (ldr == 0) {
                        Out("  >>> PEB.Ldr=0 - 5EAC很可能抹了PEB模块链，dtb.txt路径无效。");
                        Out("  >>> 需改用: VAD扫描 / 内存特征码扫描找client.dll，绕开PEB。");
                    } else {
                        Out("  PEB.Ldr非零但client.dll仍找不到 - 可能client.dll被单独抹除。");
                        Out("  >>> 需: VAD扫描 + 特征码定位client.dll基址。");
                    }
                } else {
                    Out("  读PEB失败 - DTB可能完全不对。");
                    Out("  >>> 需从物理内存暴力搜索正确的PML4页表基址。");
                }
            }
            Out("\n>>> 结论: 第1层 - 模块可见性问题，且现有FixCr3(dtb.txt候选)无法解决。");
            Out("    下一步对策: 暴力扫物理内存找PML4 / VAD扫描 / 特征码定位client.dll。");
            VMMDLL_Close(h);
            return 2;
        }
    }

    // ---------- 阶段3: 数据读取诊断（区分第1层半突破 vs 第2层加密）----------
    Out("\n[阶段3] 数据读取诊断 - client.dll头部 (排查数据加密)");
    if (clientBase == 0) {
        PVMMDLL_MAP_MODULEENTRY pE3 = nullptr;
        if (VMMDLL_Map_GetModuleFromNameU(h, cs2Pid, (LPSTR)modName, &pE3, 0) && pE3) {
            clientBase = pE3->vaBase;
            VMMDLL_MemFree(pE3);
        }
    }
    if (clientBase == 0) {
        Out("[X] 拿不到client.dll基址，跳过阶段3");
    } else {
        Out("  client.dll base = 0x%llX", (unsigned long long)clientBase);
        BYTE hdr[16] = {0};
        DWORD flags = VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_NOPAGING | VMMDLL_FLAG_ZEROPAD_ON_FAIL | VMMDLL_FLAG_NOPAGING_IO;
        if (VMMDLL_MemReadEx(h, cs2Pid, clientBase, hdr, sizeof(hdr), 0, flags)) {
            Out("  前16字节: %02X %02X %02X %02X %02X %02X %02X %02X  %02X %02X %02X %02X %02X %02X %02X %02X",
                hdr[0],hdr[1],hdr[2],hdr[3],hdr[4],hdr[5],hdr[6],hdr[7],
                hdr[8],hdr[9],hdr[10],hdr[11],hdr[12],hdr[13],hdr[14],hdr[15]);
            if (hdr[0] == 'M' && hdr[1] == 'Z') {
                Out("  [OK] MZ头正常 - 模块可见且数据可读，第1层已突破");
                Out("  >>> 结论: 模块OK。若ESP/雷达仍全零，问题在偏移或字段读取层(不是加密)。");
            } else {
                bool allZero = true;
                for (int i = 0; i < 16; i++) if (hdr[i] != 0) { allZero = false; break; }
                if (allZero) {
                    Out("  [X] 全零 - 模块列表能解析但实际页读不到。DTB部分正确(模块缓存)但页映射不对。");
                    Out("  >>> 结论: 第1层半突破 - 需更精确的DTB或页表修复。");
                } else {
                    Out("  [?] 非MZ非全零 - 可能是密文/乱码。这才是真正的'数据加密'特征。");
                    Out("  >>> 结论: 第2层 - 数据疑似被加密。需采集密文样本+对照明文进一步分析。");
                }
            }
        } else {
            Out("  [X] 读client.dll头部失败");
        }
    }

    Out("\n========== 诊断结束 ==========");
    VMMDLL_Close(h);
    return 0;
}
