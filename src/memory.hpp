// ============================================================================
//  memory.hpp —— 纯外部读进程：RPM 封装 / 指针链 / UTF-16 字符串 / 特征码扫描
// ============================================================================
#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace mem {

// —— 读取原语唯一切换点 ——
// RPM 被内核剥权时，替换本函数体即可整体换后端：
//   驱动:   DeviceIoControl(hDev, IOCTL_READ_PM, &req, ...)
//   DMA:    leechcore/ftd3xx 的 Read(addr)（h 忽略）
//   SYSTEM: 保持不动，以 SYSTEM 身份重跑
inline BOOL RawRead(HANDLE h, LPCVOID a, void* d, SIZE_T n, PSIZE_T got) {
    SIZE_T g = 0;
    BOOL ok = ::ReadProcessMemory(h, a, d, n, &g);
    if (got) *got = g;
    return ok;
}

struct Proc {
    HANDLE    hProcess = nullptr;
    DWORD     pid = 0;
    uintptr_t moduleBase = 0;
    size_t    moduleSize = 0;
    char      failReason[128] = {};   // ASCII 失败原因（probe 诊断打印用）
    bool      autoRecovered   = false; // 模块基址来自内存映射恢复（枚举被保护挡了）

    bool valid() const { return hProcess && moduleBase; }
    void close() { if (hProcess) { CloseHandle(hProcess); hProcess = nullptr; } }

    bool attach(const wchar_t* exeName, const char* exeNameA, const char* moduleName, uintptr_t manualBase = 0) {
        close(); moduleBase = 0; pid = 0; failReason[0] = 0; autoRecovered = false;
        PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return false;
        if (Process32FirstW(snap, &pe)) {
            do { if (_wcsicmp(pe.szExeFile, exeName) == 0) { pid = pe.th32ProcessID; break; } }
            while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
        if (!pid) { snprintf(failReason, sizeof failReason, "process '%s' NOT FOUND (proclist to discover)", exeNameA); return false; }
        hProcess = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
        if (!hProcess) { snprintf(failReason, sizeof failReason, "OpenProcess pid=%lu DENIED (admin?)", pid); return false; }
        int rc = resolveModule(moduleName);
        if (rc == 1) return true;
        if (manualBase) { moduleBase = manualBase; moduleSize = 0x8000000; autoRecovered = true; return true; }
        if (findModuleByMappedName(moduleName)) return true;  // 兜底1: 文件映射名查询（另一类内核路径）
        if (findIl2CppModule()) return true;                  // 兜底2: 头部扫描 + il2cpp 字符串锚
        if (rc == 0) snprintf(failReason, sizeof failReason,
            "no '%s' via enum/mapname/anchor (probe mapname %lu; scanstr %lu il2cpp_domain_get)", moduleName, pid, pid);
        else snprintf(failReason, sizeof failReason,
            "enum DENIED + mapname & anchor blind for pid=%lu (probe mapname %lu)", pid, pid);
        return false;
    }

    // 1=找到  0=枚举成功但无此模块  -1=枚举通道本身被拒
    int resolveModule(const char* name) {
        // 路径A：句柄侧枚举（吃已拿到的 hProcess，绕过 toolhelp 的独立受限路径）
        HMODULE mods[2048]; DWORD need = 0;
        if (hProcess && EnumProcessModulesEx(hProcess, mods, sizeof(mods), &need, LIST_MODULES_ALL)) {
            DWORD cnt = need / sizeof(HMODULE);
            for (DWORD i = 0; i < cnt; i++) {
                char a[MAX_PATH]{};
                if (GetModuleBaseNameA(hProcess, mods[i], a, MAX_PATH) && _stricmp(a, name) == 0) {
                    MODULEINFO mi{};
                    if (GetModuleInformation(hProcess, mods[i], &mi, sizeof(mi))) {
                        moduleBase = (uintptr_t)mi.lpBaseOfDll; moduleSize = mi.SizeOfImage;
                    } else {
                        moduleBase = (uintptr_t)mods[i]; moduleSize = 0;
                    }
                    return 1;
                }
            }
            return 0;
        }
        // 路径B：TH32 快照兜底（旧逻辑）
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        if (snap == INVALID_HANDLE_VALUE) return -1;
        MODULEENTRY32W me{}; me.dwSize = sizeof(me);
        bool found = false;
        if (Module32FirstW(snap, &me)) {
            do {
                char a[MAX_PATH]{};
                WideCharToMultiByte(CP_UTF8, 0, me.szModule, -1, a, MAX_PATH, 0, 0);
                if (_stricmp(a, name) == 0) {
                    moduleBase = (uintptr_t)me.modBaseAddr;
                    moduleSize = me.modBaseSize;
                    found = true;
                    break;
                }
            } while (Module32NextW(snap, &me));
        }
        CloseHandle(snap);
        return found ? 1 : (GetLastError() == ERROR_BAD_EXE_FORMAT ? -1 : 0);
    }

    bool regionHasString(uintptr_t base, uint32_t size, const char* needle) const {
        size_t nl = strlen(needle);
        std::vector<unsigned char> b((4u << 20) + nl + 1);
        for (uint32_t off = 0; off < size; off += (4u << 20)) {
            SIZE_T want = (size - off < (uint32_t)(b.size() - 1)) ? (SIZE_T)(size - off) : b.size() - 1;
            SIZE_T got = 0;
            if (!RawRead(hProcess, (LPCVOID)(base + off), b.data(), want, &got) || got <= nl) continue;
            for (SIZE_T i = 0; i + nl <= got; i++)
                if (memcmp(&b[i], needle, nl) == 0) return true;
        }
        return false;
    }

    // 枚举通道被保护挡死时的兜底：走内存映射，找「MZ/PE64 + SizeOfImage合理 + 含il2cpp导出名」的映像
    bool findIl2CppModule() {
        MEMORY_BASIC_INFORMATION mbi;
        uintptr_t addr = 0;
        std::vector<unsigned char> hdr(0x1000);
        for (; addr < ((uintptr_t)1 << 47) &&
             VirtualQueryEx(hProcess, (LPCVOID)addr, &mbi, sizeof(mbi)) == sizeof(mbi);
             addr = (uintptr_t)mbi.BaseAddress + mbi.RegionSize) {
            uintptr_t va = (uintptr_t)mbi.BaseAddress;
            if (mbi.State != MEM_COMMIT || mbi.RegionSize < 0x1000) continue;
            SIZE_T got = 0;
            if (!RawRead(hProcess, (LPCVOID)va, hdr.data(), hdr.size(), &got) || got < 0x40) continue;
            if (hdr[0] != 'M' || hdr[1] != 'Z') continue;
            int32_t elf = *(int32_t*)(hdr.data() + 0x3C);
            if (elf < 0x40 || elf > 0x900 || (size_t)elf + sizeof(IMAGE_NT_HEADERS64) > hdr.size()) continue;
            auto* nt = (IMAGE_NT_HEADERS64*)(hdr.data() + elf);
            if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != 0x8664) continue;
            uint32_t soi = nt->OptionalHeader.SizeOfImage;
            if (soi < (4u << 20) || soi > (512u << 20)) continue;
            if (va + soi > ((uintptr_t)1 << 47)) continue;
            if (!regionHasString(va, soi, "il2cpp_domain_get")) continue;
            moduleBase = va; moduleSize = soi; autoRecovered = true;
            return true;
        }
        // pass2: 字符串锚点恢复（针对手动映射/擦头：MZ 不在任何区首）
        addr = 0;
        for (; addr < ((uintptr_t)1 << 47) &&
             VirtualQueryEx(hProcess, (LPCVOID)addr, &mbi, sizeof(mbi)) == sizeof(mbi);
             addr = (uintptr_t)mbi.BaseAddress + mbi.RegionSize) {
            uintptr_t va = (uintptr_t)mbi.BaseAddress;
            if (mbi.State != MEM_COMMIT || mbi.RegionSize < (1u << 20)) continue;
            if (!(mbi.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE))) continue;
            uint32_t scan = (uint32_t)(mbi.RegionSize > (512u << 20) ? (512u << 20) : mbi.RegionSize);
            if (!regionHasString(va, scan, "il2cpp_domain_get")) continue;
            uintptr_t start = va;                                  // 回退到 reservation 起点
            for (;;) {
                if (start < 0x10000) break;
                MEMORY_BASIC_INFORMATION prev;
                if (VirtualQueryEx(hProcess, (LPCVOID)(start - 1), &prev, sizeof(prev)) != sizeof(prev)) break;
                if ((uintptr_t)prev.BaseAddress + prev.RegionSize != start || prev.State == MEM_FREE) break;
                start = (uintptr_t)prev.BaseAddress;
            }
            uintptr_t stop = va + mbi.RegionSize;                  // 前进合并连续已提交区
            for (uintptr_t p = stop; p < ((uintptr_t)1 << 47) && stop - start <= (512u << 20); p = stop) {
                MEMORY_BASIC_INFORMATION nx;
                if (VirtualQueryEx(hProcess, (LPCVOID)p, &nx, sizeof(nx)) != sizeof(nx)) break;
                if ((uintptr_t)nx.BaseAddress != p || nx.State != MEM_COMMIT) break;
                stop = p + nx.RegionSize;
            }
            moduleBase = start; moduleSize = stop - start; autoRecovered = true;
            return true;
        }
        return false;
    }

    // 兜底1 实现：GetMappedFileName 走 NtQueryVirtualMemory(FileBasedMemoryInformation)，
    // 与模块枚举是不同查询类，保护常见漏堵；同文件多个视图取最低地址=模块基址
    bool findModuleByMappedName(const char* name) {
        MEMORY_BASIC_INFORMATION mbi;
        uintptr_t addr = 0, low = ~(uintptr_t)0, high = 0;
        for (; addr < ((uintptr_t)1 << 47) &&
             VirtualQueryEx(hProcess, (LPCVOID)addr, &mbi, sizeof(mbi)) == sizeof(mbi);
             addr = (uintptr_t)mbi.BaseAddress + mbi.RegionSize) {
            if (mbi.State != MEM_COMMIT) continue;
            char path[MAX_PATH]{};
            if (!GetMappedFileNameA(hProcess, mbi.BaseAddress, path, sizeof(path))) continue;
            char* slash = strrchr(path, '\\');
            if (_stricmp(slash ? slash + 1 : path, name) != 0) continue;
            uintptr_t va = (uintptr_t)mbi.BaseAddress;
            if (va < low) low = va;
            if (va + mbi.RegionSize > high) high = va + mbi.RegionSize;
        }
        if (!high) return false;
        moduleBase = low; moduleSize = high - low; autoRecovered = true;
        return true;
    }

    template <class T>
    T read(uintptr_t addr, T def = T{}) const {
        T v{}; SIZE_T got = 0;
        if (addr > 0x10000 &&
            RawRead(hProcess, (LPCVOID)addr, &v, sizeof(T), &got) && got == sizeof(T))
            return v;
        return def;
    }
    uintptr_t readPtr(uintptr_t addr) const { return read<uintptr_t>(addr); }

    // *(start + chain[0]) 起逐级解引用；n=0 时等价 readPtr(start)
    uintptr_t follow(uintptr_t start, const uintptr_t* chain, size_t n) const {
        uintptr_t p = readPtr(start);
        for (size_t i = 0; i < n; i++) {
            if (p < 0x10000) return 0;
            p = readPtr(p + chain[i]);
        }
        return p;
    }

    std::string readIl2CppStr(uintptr_t strObj) const {
        if (strObj < 0x10000) return {};
        int len = read<int>(strObj + 0x10);            // off::Str_length
        if (len <= 0 || len > 128) return {};
        wchar_t w[129]{}; SIZE_T got = 0;
        if (!RawRead(hProcess, (LPCVOID)(strObj + 0x14), w,   // off::Str_chars
                               (SIZE_T)len * 2, &got) || got < (SIZE_T)len * 2)
            return {};
        char s[512]{};
        int n = WideCharToMultiByte(CP_UTF8, 0, w, len, s, sizeof(s) - 1, 0, 0);
        return n > 0 ? std::string(s, n) : std::string();
    }

    // "48 8B 05 ?? ?? ?? ?? C3" 风格特征码，分块扫主模块；命中返回地址，否则0
    uintptr_t findPattern(const char* sig) const {
        if (!valid() || !sig || !*sig) return 0;
        std::vector<int> pat;
        for (const char* p = sig; *p;) {
            while (*p == ' ') ++p;
            if (!*p) break;
            if (*p == '?') { pat.push_back(-1); p += (p[1] == '?') ? 2 : 1; }
            else           { pat.push_back((int)strtol(p, (char**)&p, 16)); }
        }
        if (pat.empty()) return 0;
        const size_t CHUNK = 4u << 20, OVL = pat.size() - 1;
        std::vector<uint8_t> buf(CHUNK + OVL);
        const uintptr_t end = moduleBase + moduleSize;
        for (uintptr_t a = moduleBase; a < end; a += CHUNK) {
            SIZE_T want = buf.size(), got = 0;
            if (a + want > end) want = (SIZE_T)(end - a);
            if (!RawRead(hProcess, (LPCVOID)a, buf.data(), want, &got)) continue;
            for (SIZE_T i = 0; i + pat.size() <= got; i++) {
                size_t k = 0;
                for (; k < pat.size(); k++)
                    if (pat[k] >= 0 && buf[i + k] != (uint8_t)pat[k]) break;
                if (k == pat.size()) return a + i;
            }
        }
        return 0;
    }
};

inline void Widen(const char* a, wchar_t* w, int cap) { MultiByteToWideChar(CP_UTF8, 0, a, -1, w, cap); }

} // namespace mem
