// ============================================================================
//  probe_main.cpp —— 偏移定位辅助（控制台，和 find_offsets.py 配套）
//  probe proclist <substr>   找真实进程名
//  probe modules <pid>       列模块（句柄枚举优先，toolhelp 兜底）
//  probe regions <pid>       内存映射走查：枚举全被挡时靠 MZ 头找主模块基址
//  probe attach              测试 ProcName/ModuleName 能否挂上
//  probe chain <rva> [offs]  指针链逐跳打印
//  probe mat <file>          正交4x4矩阵候选扫描
//  probe matdiff <A> <B>     转视角后差分 → 活跃视图矩阵
//  probe list <addr> [off]   按 List<T> 布局验证实体链
//  probe ent <addr>          klass类名 + 可疑Vector3字段扫描
//  probe sig <addr> [len]    生成特征码
// ============================================================================
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstddef>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <array>
#include <map>
#include <string>
#include <vector>
#include "memory.hpp"
#include "esp.hpp"
#include "config.hpp"
#include "../config/offsets.hpp"

static mem::Proc P;
static uintptr_t hx(const char* s) { return (uintptr_t)_strtoui64(s, nullptr, 16); }
static bool readb(uintptr_t a, void* d, size_t n) {
    SIZE_T g = 0;
    return P.valid() && mem::RawRead(P.hProcess, (LPCVOID)a, d, n, &g) && g == n;
}
static std::string cstr(uintptr_t a, int cap = 48) {
    std::string s; char c = 0;
    for (int i = 0; i < cap && readb(a + i, &c, 1) && c; i++) s += c;
    return s;
}

static bool sections(std::vector<std::pair<uintptr_t, size_t>>& out) {
    IMAGE_DOS_HEADER dos = P.read<IMAGE_DOS_HEADER>(P.moduleBase);
    if (dos.e_magic != IMAGE_DOS_SIGNATURE) return false;
    IMAGE_NT_HEADERS64 nt = P.read<IMAGE_NT_HEADERS64>(P.moduleBase + dos.e_lfanew);
    if (nt.Signature != IMAGE_NT_SIGNATURE) return false;
    uintptr_t sa = P.moduleBase + dos.e_lfanew +
                   offsetof(IMAGE_NT_HEADERS64, OptionalHeader) + nt.FileHeader.SizeOfOptionalHeader;
    std::vector<IMAGE_SECTION_HEADER> secs(nt.FileHeader.NumberOfSections);
    if (!readb(sa, secs.data(), secs.size() * sizeof(IMAGE_SECTION_HEADER))) return false;
    for (auto& s : secs)
        if (s.Characteristics & IMAGE_SCN_MEM_READ)
            out.push_back({ P.moduleBase + s.VirtualAddress,
                            (size_t)(s.Misc.VirtualSize > s.SizeOfRawData ? s.Misc.VirtualSize : s.SizeOfRawData) });
    return true;
}

static bool ifind(const char* hay, const char* needle) {   // ASCII 大小写不敏感 substring
    if (!*needle) return true;
    for (const char* h = hay; *h; h++) {
        const char *a = h, *b = needle;
        while (*b && *a && ((*a | 32) == (*b | 32))) { a++; b++; }
        if (!*b) return true;
    }
    return false;
}

static const char* kUsage =
  "usage:\n"
  "  probe proclist <substr>        list processes (find the real exe name)\n"
  "  probe modules <pid>            list modules of a pid (find IL2CPP module)\n"
  "  probe findmod <pid>            map-walk: recover IL2CPP module base when enum is blocked\n"
  "  probe mapname <pid>            file names behind mapped regions (different kernel path!)\n"
  "  probe scanstr <pid> <needle>   string anchor scan across readable memory\n"
  "  probe readtest                 handle-rights + read-channel matrix (diagnose RPM blocking)\n"
  "  probe attach                   test attach with ProcName/ModuleName\n"
  "  probe chain <rvaHex> [offs...] verify a pointer chain hop by hop\n"
  "  probe mat <out.txt>            scan orthonormal 4x4 matrices\n"
  "  probe matdiff <A> <B>          which candidates moved after camera rotate\n"
  "  probe list <mgr> [posOff] [n]  dump n entity coords via List layout\n"
  "  probe ent <obj> [nameOff]      klass name + suspicious Vector3 scan\n"
  "  probe sig <addr> [len]         emit pattern + RVA hint\n";

static int cmdProclist(const char* filt) {
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (s == INVALID_HANDLE_VALUE) { puts("snapshot failed"); return 1; }
    PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
    int n = 0;
    if (Process32FirstW(s, &pe)) do {
        char a[MAX_PATH]{};
        WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1, a, MAX_PATH, 0, 0);
        if (ifind(a, filt)) { printf("  %-8lu %s\n", pe.th32ProcessID, a); n++; }
    } while (Process32NextW(s, &pe));
    CloseHandle(s);
    printf("%d match(es) for '%s'. Put the real name into offsets.hpp ProcName/ProcNameA.\n", n, filt);
    return 0;
}

static int cmdModules(const char* pidS) {
    DWORD pid = (DWORD)strtoul(pidS, nullptr, 10);
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!h) { printf("OpenProcess failed winerr=%lu (admin? protected?)\n", GetLastError()); return 1; }
    HMODULE mods[2048]; DWORD need = 0;
    if (EnumProcessModulesEx(h, mods, sizeof(mods), &need, LIST_MODULES_ALL)) {
        DWORD cnt = need / sizeof(HMODULE);
        int hit = 0;
        printf("[via handle EnumProcessModulesEx] %lu modules:\n", cnt);
        for (DWORD i = 0; i < cnt; i++) {
            char a[MAX_PATH]{}; MODULEINFO mi{};
            GetModuleBaseNameA(h, mods[i], a, MAX_PATH);
            GetModuleInformation(h, mods[i], &mi, sizeof(mi));
            bool mark = ifind(a, "assembly") || ifind(a, "mono") || ifind(a, "unity");
            if (mark || i < 60)
                printf("  %-36s base=%llX size=%.1fMB%s\n", a, (unsigned long long)mi.lpBaseOfDll,
                       mi.SizeOfImage / 1048576.0, mark ? "   <== candidate main module" : "");
            hit += mark ? 1 : 0;
        }
        if (!hit) puts("no assembly/mono/unity-named module seen -- check full list beyond first 60");
        CloseHandle(h);
        return 0;
    }
    printf("[handle enum] denied winerr=%lu, fallback to toolhelp snapshot...\n", GetLastError());
    CloseHandle(h);
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (s == INVALID_HANDLE_VALUE) {
        printf("[toolhelp] also failed winerr=%lu -- both channels blocked by protection.\n", GetLastError());
        puts("  next diagnostic: .\\probe.exe regions <pid>  (memory-map walk needs only VM_READ)");
        return 1;
    }
    MODULEENTRY32W me{}; me.dwSize = sizeof(me);
    int n = 0, hit = 0;
    if (Module32FirstW(s, &me)) do {
        char a[MAX_PATH]{};
        WideCharToMultiByte(CP_UTF8, 0, me.szModule, -1, a, MAX_PATH, 0, 0);
        bool mark = ifind(a, "assembly") || ifind(a, "mono");
        if (mark || n < 60)
            printf("  %-36s base=%llX size=%.1fMB%s\n", a, (unsigned long long)me.modBaseAddr,
                   me.modBaseSize / 1048576.0, mark ? "   <== candidate main module" : "");
        hit += mark ? 1 : 0; n++;
    } while (Module32NextW(s, &me));
    CloseHandle(s);
    if (!hit) printf("no *assembly*/*mono* among %d modules\n", n);
    return 0;
}

// 兜底通道：只用 VM_READ 句柄走内存映射，找私有映像型大分配（典型被藏起来的 DLL）
static int cmdRegions(const char* pidS) {
    DWORD pid = (DWORD)strtoul(pidS, nullptr, 10);
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!h) { printf("OpenProcess failed winerr=%lu\n", GetLastError()); return 1; }
    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t addr = 0, end = (uintptr_t)1 << 47;
    size_t shown = 0, big = 0;
    while (addr < end && VirtualQueryEx(h, (LPCVOID)addr, &mbi, sizeof(mbi)) == sizeof(mbi)) {
        if (mbi.State == MEM_COMMIT &&
            (mbi.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_READONLY | PAGE_READWRITE)) &&
            mbi.Type != MEM_MAPPED && mbi.RegionSize >= 512u << 10) {
            char head[8]{}; SIZE_T got = 0;
            mem::RawRead(h, mbi.BaseAddress, head, 2, &got);
            bool pe = got == 2 && head[0] == 'M' && head[1] == 'Z';
            printf("  %s%llX  size=%.1fMB  prot=%lX type=%lX%s\n", pe ? "*IMG*" : "     ",
                   (unsigned long long)(uintptr_t)mbi.BaseAddress, mbi.RegionSize / 1048576.0,
                   mbi.Protect, mbi.Type, pe ? "  <== MZ: likely hidden/relocated module" : "");
            big++;
            if (++shown > 120) { puts("  ... truncated"); break; }
        }
        addr = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
        if (addr < (uintptr_t)mbi.BaseAddress) break;
    }
    printf("large private regions=%zu  (only 1 *IMG* => that's the main module; 2+ => extra module hidden from enum)\n", big);
    CloseHandle(h);
    return 0;
}

static int cmdMapname(const char* pidS) {
    DWORD pid = (DWORD)strtoul(pidS, nullptr, 10);
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!h) { printf("OpenProcess failed winerr=%lu\n", GetLastError()); return 1; }
    MEMORY_BASIC_INFORMATION mbi; uintptr_t addr = 0;
    struct Ent { char path[MAX_PATH]; uintptr_t low, high; } ents[512]; int n = 0;
    for (; addr < ((uintptr_t)1 << 47) &&
         VirtualQueryEx(h, (LPCVOID)addr, &mbi, sizeof(mbi)) == sizeof(mbi);
         addr = (uintptr_t)mbi.BaseAddress + mbi.RegionSize) {
        if (mbi.State != MEM_COMMIT) continue;
        char path[MAX_PATH]{};
        if (!GetMappedFileNameA(h, mbi.BaseAddress, path, sizeof(path))) continue;
        uintptr_t va = (uintptr_t)mbi.BaseAddress, hi = va + mbi.RegionSize;
        int i;
        for (i = 0; i < n; i++) if (!strcmp(ents[i].path, path)) break;
        if (i == n) { if (n >= 512) break; snprintf(ents[i].path, MAX_PATH, "%s", path); ents[i].low = va; ents[i].high = hi; n++; }
        else { if (va < ents[i].low) ents[i].low = va; if (hi > ents[i].high) ents[i].high = hi; }
    }
    if (!n) puts("GetMappedFileName returned NOTHING -> file-name queries also blocked; use scanstr anchor route");
    int hit = 0;
    for (int i = 0; i < n; i++) {
        bool mark = ifind(ents[i].path, "assembly") || ifind(ents[i].path, "mono")
                 || ifind(ents[i].path, "unity") || ifind(ents[i].path, "metadata");
        hit += mark ? 1 : 0;
        if (mark || n < 80)
            printf("  %llX  %7.1fMB  %s%s\n", (unsigned long long)ents[i].low,
                   (double)(ents[i].high - ents[i].low) / 1048576.0, ents[i].path,
                   mark ? "   <== KEY FILE (attach auto-fallback uses lowest view as base)" : "");
    }
    printf("%d mapped file(s), key-name hits: %d\n", n, hit);
    CloseHandle(h);
    return 0;
}

static int cmdScanstr(const char* pidS, const char* needle) {
    if (!needle || !*needle) { puts("usage: probe scanstr <pid> <ascii-needle>"); return 1; }
    DWORD pid = (DWORD)strtoul(pidS, nullptr, 10);
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!h) { printf("OpenProcess failed winerr=%lu\n", GetLastError()); return 1; }
    size_t nl = strlen(needle);
    std::vector<unsigned char> b(1u << 20);
    MEMORY_BASIC_INFORMATION mbi; uintptr_t addr = 0; int hits = 0, chunks = 0;
    puts("scanning (can take ~30s on big processes)...");
    for (; addr < ((uintptr_t)1 << 47) &&
         VirtualQueryEx(h, (LPCVOID)addr, &mbi, sizeof(mbi)) == sizeof(mbi);
         addr = (uintptr_t)mbi.BaseAddress + mbi.RegionSize) {
        if (mbi.State != MEM_COMMIT ||
            !(mbi.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_READONLY | PAGE_READWRITE)))
            continue;
        uintptr_t va = (uintptr_t)mbi.BaseAddress, end = va + mbi.RegionSize;
        for (uintptr_t p = va; p < end; p += b.size() - 64) {
            SIZE_T want = (SIZE_T)((end - p < b.size()) ? (end - p) : b.size()), got = 0;
            if (!mem::RawRead(h, (LPCVOID)p, b.data(), want, &got) || got <= nl) continue;
            chunks++;
            for (SIZE_T i = 0; i + nl <= got; i++)
                if (!memcmp(&b[i], needle, nl)) {
                    uintptr_t hit = p + i;
                    MEMORY_BASIC_INFORMATION q;
                    if (VirtualQueryEx(h, (LPCVOID)hit, &q, sizeof(q)) == sizeof(q)) {
                        char path[MAX_PATH]{};
                        GetMappedFileNameA(h, (LPVOID)hit, path, sizeof(path));
                        printf("  hit#%d @%llX  region_start=%llX type=%lX %s\n", ++hits,
                               (unsigned long long)hit, (unsigned long long)(uintptr_t)q.BaseAddress,
                               q.Type, path[0] ? path : "(no file name)");
                    }
                    if (hits >= 20) { puts("  (cap 20)"); CloseHandle(h); return 0; }
                    break;
                }
        }
    }
    if (!hits) printf("NOTHING found in %d readable chunks -> RPM content also filtered (headers stripped + read hooked)\n", chunks);
    else puts("First hit inside the game's big image/heap region: region_start there = module base candidate.");
    CloseHandle(h);
    return hits ? 0 : 1;
}

static int cmdAttach();   // fwd

// ================= readtest: 句柄权限 + 读通道矩阵 =================
typedef LONG NTSTATUS;
// 完整 OBJECT_BASIC_INFORMATION = 56 字节；传短内核回 C0000004，GrantedAccess 显示不作数
typedef struct { ULONG Attributes; ACCESS_MASK GrantedAccess; ULONG pad[12]; } MY_OBJ_BASIC;
typedef NTSTATUS (NTAPI* PFN_NtQueryObject)(HANDLE, ULONG, PVOID, ULONG, PULONG);
typedef NTSTATUS (NTAPI* PFN_NtReadVirtualMemory)(HANDLE, PVOID, PVOID, SIZE_T, PSIZE_T);

static void showGranted(const char* tag, HANDLE h) {
    HMODULE nt = GetModuleHandleA("ntdll.dll");
    auto qo = (PFN_NtQueryObject)GetProcAddress(nt, "NtQueryObject");
    if (!qo) { printf("  %-30s no NtQueryObject?\n", tag); return; }
    MY_OBJ_BASIC ob{}; ULONG rl = 0;
    NTSTATUS st = qo(h, 0, &ob, sizeof(ob), &rl);
    printf("  %-30s granted=%08X  VM_READ:%s  QUERY:%s (st=%08X)\n", tag, ob.GrantedAccess,
           (ob.GrantedAccess & 0x10)   ? "OK" : "STRIPPED!!",
           (ob.GrantedAccess & 0x400)  ? "OK" : "no", (ULONG)st);
}
static void tryRpm(const char* tag, HANDLE h, uintptr_t a) {
    unsigned char b[16]; SIZE_T got = 0;
    BOOL ok = mem::RawRead(h, (LPCVOID)a, b, 16, &got);
    printf("  %-30s RPM@%llX %s got=%llu err=%lu", tag, (unsigned long long)a,
           (ok && got) ? "OK  " : "FAIL", (unsigned long long)got, ok ? 0UL : GetLastError());
    if (ok && got) for (SIZE_T i = 0; i < got; i++) printf(" %02X", b[i]);
    putchar('\n');
}

static int cmdReadtest() {
    if (cmdAttach() != 0) { puts("readtest needs a successful attach first"); return 1; }
    uintptr_t mb = P.moduleBase;
    puts("--- handle rights ---");
    showGranted("attach handle(VREAD|QUERY)", P.hProcess);
    HANDLE h2 = OpenProcess(PROCESS_VM_READ, FALSE, P.pid);
    if (h2) { showGranted("fresh open[VM_READ only]", h2); tryRpm("fresh open[VM_READ only]", h2, mb + 0x1000); CloseHandle(h2); }
    else printf("  fresh open[VM_READ only] FAILED err=%lu\n", GetLastError());
    HANDLE h3 = OpenProcess(PROCESS_ALL_ACCESS, FALSE, P.pid);
    if (h3) { showGranted("fresh open[ALL_ACCESS]", h3); tryRpm("fresh open[ALL_ACCESS]", h3, mb + 0x1000); CloseHandle(h3); }
    else printf("  fresh open[ALL_ACCESS] FAILED err=%lu\n", GetLastError());
    puts("--- read channels @ module+0x1000 ---");
    tryRpm("attach handle", P.hProcess, mb);
    tryRpm("attach handle", P.hProcess, mb + 0x1000);
    {   // 裸 syscall，绕过 kernel32 任何用户态 hook
        HMODULE nt = GetModuleHandleA("ntdll.dll");
        auto ntread = (PFN_NtReadVirtualMemory)GetProcAddress(nt, "NtReadVirtualMemory");
        if (ntread) {
            unsigned char b[16]; SIZE_T got = 0;
            NTSTATUS st = ntread(P.hProcess, (PVOID)(mb + 0x1000), b, 16, &got);
            printf("  %-30s NtReadVM st=%08X got=%llu", "ntdll NtReadVirtualMemory", (ULONG)st, (unsigned long long)got);
            if (st == 0) for (SIZE_T i = 0; i < got; i++) printf(" %02X", b[i]);
            putchar('\n');
        }
    }
    {   // Win11 24H2+ 新 API（走的 syscall 类不同，部分保护没堵它）
        typedef struct { PVOID Address; PVOID Buffer; SIZE_T Size; ULONG Flags; SIZE_T BytesRead; } MRR;
        typedef SIZE_T (WINAPI* PFN_RPMEx)(HANDLE, MRR*);
        auto rpmx = (PFN_RPMEx)GetProcAddress(GetModuleHandleA("kernelbase.dll"), "ReadProcessMemoryEx");
        if (rpmx) {
            unsigned char b[16]; MRR r{ (PVOID)(mb + 0x1000), b, 16, 0, 0 };
            SIZE_T res = rpmx(P.hProcess, &r);
            printf("  %-30s ret=%llu bytesRead=%llu", "ReadProcessMemoryEx(win11)",
                   (unsigned long long)res, (unsigned long long)r.BytesRead);
            if (r.BytesRead) for (SIZE_T i = 0; i < r.BytesRead; i++) printf(" %02X", b[i]);
            putchar('\n');
        } else puts("  ReadProcessMemoryEx: API absent on this OS (Win11 24H2+ only)");
    }
    puts("--- heap target (first RW private >=1MB) ---");
    {
        MEMORY_BASIC_INFORMATION mbi; uintptr_t addr = 0;
        while (addr < ((uintptr_t)1 << 47) &&
               VirtualQueryEx(P.hProcess, (LPCVOID)addr, &mbi, sizeof(mbi)) == sizeof(mbi)) {
            if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE &&
                (mbi.Protect & PAGE_READWRITE) && mbi.RegionSize >= (1u << 20)) {
                tryRpm("heap RW private", P.hProcess, (uintptr_t)mbi.BaseAddress);
                break;
            }
            addr = (uintptr_t)mbi.BaseAddress + mbi.RegionSize;
        }
    }
    puts("\nverdicts: VM_READ:STRIPPED on every handle -> kernel callback cuts rights (driver/DMA needed for RPM).");
    puts("          any one channel OK -> switch that reader in and ESP is alive.");
    puts("          rights OK but reads fail only in image pages -> content filter; heap may still work.");
    return 0;
}

static int cmdFindmod(const char* pidS) {
    DWORD pid = (DWORD)strtoul(pidS, nullptr, 10);
    mem::Proc T; T.pid = pid;
    T.hProcess = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!T.hProcess) { printf("OpenProcess failed winerr=%lu (admin?)\n", GetLastError()); return 1; }
    MEMORY_BASIC_INFORMATION mbi; uintptr_t addr = 0; int n = 0, hit = 0;
    std::vector<unsigned char> hdr(0x1000);
    for (; addr < ((uintptr_t)1 << 47) &&
         VirtualQueryEx(T.hProcess, (LPCVOID)addr, &mbi, sizeof(mbi)) == sizeof(mbi);
         addr = (uintptr_t)mbi.BaseAddress + mbi.RegionSize) {
        uintptr_t va = (uintptr_t)mbi.BaseAddress;
        if (mbi.State != MEM_COMMIT || mbi.RegionSize < 0x1000) continue;
        SIZE_T got = 0;
        if (!mem::RawRead(T.hProcess, (LPCVOID)va, hdr.data(), hdr.size(), &got) || got < 0x40) continue;
        if (hdr[0] != 'M' || hdr[1] != 'Z') continue;
        int32_t elf = *(int32_t*)(hdr.data() + 0x3C);
        if (elf < 0x40 || elf > 0x900 || (size_t)elf + sizeof(IMAGE_NT_HEADERS64) > hdr.size()) continue;
        auto* nt = (IMAGE_NT_HEADERS64*)(hdr.data() + elf);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != 0x8664) continue;
        uint32_t soi = nt->OptionalHeader.SizeOfImage;
        if (soi < (4u << 20)) continue;
        bool il = (va + soi < ((uintptr_t)1 << 47)) && T.regionHasString(va, soi, "il2cpp_domain_get");
        printf("  base=%llX  SizeOfImage=%.1fMB  type=%lX%s\n", (unsigned long long)va,
               soi / 1048576.0, mbi.Type, il ? "   <== IL2CPP MAIN MODULE" : "");
        n++; hit += il ? 1 : 0;
    }
    printf("%d PE64 module(s) >=4MB; IL2CPP candidate(s): %d%s\n", n, hit,
           hit ? "  (attach's map-recovery will pick this one automatically)" : "");
    CloseHandle(T.hProcess);
    return hit ? 0 : 1;
}

static EspConfig g_pc;          // probe.ini 可选覆盖
static bool g_pcLoaded = false;

static int cmdAttach() {
    printf("[self] probe.exe is %d-bit\n", (int)(sizeof(void*) * 8));
    if (!g_pcLoaded) { EspConfig_Load(g_pc, "probe.ini"); g_pcLoaded = true; }
    wchar_t wp[64];
    const wchar_t* exeW = off::ProcName;
    const char* exeA = off::ProcNameA;
    const char* modN = off::ModuleName;
    if (g_pc.procName[0])   { mem::Widen(g_pc.procName, wp, 64); exeW = wp; exeA = g_pc.procName; }
    if (g_pc.moduleName[0]) modN = g_pc.moduleName;
    if (!P.attach(exeW, exeA, modN, off::ManualModuleBase)) {
        DWORD e = GetLastError();
        printf("[attach] FAILED at: %s  (winerr=%lu)\n", P.failReason[0] ? P.failReason : "?", e);
        puts("  -> game running? admin? then: probe mapname <pid>  /  probe scanstr <pid> il2cpp_domain_get");
        return 1;
    }
    printf("pid=%lu  module base=%llX size=%.1fMB  -> attach OK%s\n", P.pid,
           (unsigned long long)P.moduleBase, P.moduleSize / 1048576.0,
           P.autoRecovered ? (off::ManualModuleBase ? "  (NOTE: base = ManualModuleBase override)"
                             : "  (NOTE: enum blocked; base recovered via mapname/anchor walk)") : "");
    return 0;
}

static int cmdChain(int ac, char** av) {
    if (ac < 3 || cmdAttach()) return 1;
    std::vector<uintptr_t> offs;
    for (int i = 3; i < ac; i++) offs.push_back(hx(av[i]));
    uintptr_t slot = P.moduleBase + hx(av[2]);
    uintptr_t p = 0;
    printf("slot %llX -> ", (unsigned long long)slot);
    if (!readb(slot, &p, 8) || !p) { puts("NULL (RVA 不对或进程状态不对)"); return 1; }
    printf("%llX\n", (unsigned long long)p);
    for (size_t i = 0; i < offs.size(); i++) {
        uintptr_t at = p + offs[i], nxt = 0;
        if (!readb(at, &nxt, 8) || nxt < 0x10000) {
            printf("  hop%zu +0x%llX @%llX 失败 (当前 p=%llX)\n", i,
                   (unsigned long long)offs[i], (unsigned long long)at, (unsigned long long)p);
            return 1;
        }
        printf("  hop%zu +0x%llX -> %llX\n", i, (unsigned long long)offs[i], (unsigned long long)nxt);
        p = nxt;
    }
    printf("final = %llX  → 用 probe list %llX 验证\n", (unsigned long long)p, (unsigned long long)p);
    return 0;
}

// 正交性扫描：行0-2 单位正交 + 末行 [0 0 0 ±1] → 世界↔相机型矩阵
static int cmdMat(const char* outFile) {
    if (cmdAttach()) return 1;
    std::vector<std::pair<uintptr_t, size_t>> secs;
    if (!sections(secs)) return 1;
    FILE* fo = fopen(outFile, "w");
    if (!fo) return 1;
    std::vector<float> buf((4u << 20) + 16);
    size_t found = 0;
    for (auto& [va, sz] : secs) {
        for (size_t base = 0; base + 64 <= sz; base += (4u << 20)) {
            size_t want = sz - base < buf.size() - 16 ? sz - base : buf.size() - 16;
            if (!mem::RawRead(P.hProcess, (LPCVOID)(va + base), buf.data(), want, nullptr)) continue;
            for (size_t b = 0; b + 64 <= want; b += 4) {
                const float* f = buf.data() + b / 4;
                auto nrm = [&](const float* r){ return r[0]*r[0] + r[1]*r[1] + r[2]*r[2]; };
                auto dt  = [&](const float* a, const float* c){ return a[0]*c[0] + a[1]*c[1] + a[2]*c[2]; };
                if (fabsf(nrm(f) - 1) < .03f && fabsf(nrm(f+4) - 1) < .03f && fabsf(nrm(f+8) - 1) < .03f &&
                    fabsf(dt(f, f+4)) < .01f && fabsf(dt(f, f+8)) < .01f && fabsf(dt(f+4, f+8)) < .01f &&
                    fabsf(f[12]) < 1e-3f && fabsf(f[13]) < 1e-3f && fabsf(f[14]) < 1e-3f &&
                    fabsf(fabsf(f[15]) - 1) < 1e-3f)
                {
                    fprintf(fo, "%llX", (unsigned long long)(va + base + b));
                    for (int k = 0; k < 16; k++) fprintf(fo, " %.6f", f[k]);
                    fputc('\n', fo); found++;
                }
            }
        }
    }
    fclose(fo);
    printf("候选矩阵 %zu 个 → %s\n", found, outFile);
    puts("下一步: 游戏里转动视角/前进 → probe mat B.txt → probe matdiff 本文件 B.txt");
    return 0;
}

static bool loadMats(const char* f, std::map<unsigned long long, std::array<float, 16>>& m) {
    FILE* fp = fopen(f, "r");
    if (!fp) return false;
    char line[512];
    while (fgets(line, sizeof line, fp)) {
        char* t = line;
        unsigned long long a = _strtoui64(t, &t, 16);
        std::array<float, 16> v{};
        for (int i = 0; i < 16; i++) v[i] = (float)strtod(t, &t);
        m[a] = v;
    }
    fclose(fp);
    return true;
}
static int cmdMatdiff(const char* fa, const char* fb) {
    std::map<unsigned long long, std::array<float, 16>> A, B;
    if (!loadMats(fa, A) || !loadMats(fb, B)) { puts("文件读取失败"); return 1; }
    size_t moved = 0;
    for (auto& [a, va] : B) {
        auto it = A.find(a);
        if (it == A.end()) continue;
        float maxd = 0;
        for (int k = 0; k < 16; k++) maxd = max(maxd, fabsf(va[k] - it->second[k]));
        if (maxd > 1e-4f) { printf("MOVED %llX  maxΔ=%.5f  行0: %.3f %.3f %.3f\n",
                                   a, maxd, va[0], va[1], va[2]); moved++; }
    }
    printf("共同候选=%zu 移动=%zu  ← 移动者为活跃矩阵\n", A.size(), moved);
    puts("对锁定地址: probe sig <addr> 生成特征码写入 offsets.hpp 的 ViewMatrixSig，或反推 RVA 填 ViewMatrix_Chain");
    return 0;
}

static int cmdList(int ac, char** av) {
    if (ac < 3 || cmdAttach()) return 1;
    uintptr_t mgr = hx(av[2]);
    uintptr_t posOff = ac > 3 ? hx(av[3]) : 0x60;
    int n = ac > 4 ? atoi(av[4]) : 16;
    int cnt = P.read<int>(mgr + off::List_size, -1);
    uintptr_t items = P.readPtr(mgr + off::List_items);
    printf("count=%d items=%llX (List布局: size@+0x%llX items@+0x%llX)\n",
           cnt, (unsigned long long)items, (unsigned long long)off::List_size, (unsigned long long)off::List_items);
    if (cnt <= 0 || cnt > 4096 || !items) { puts("链或 List_* 布局不对"); return 1; }
    if (n > cnt) n = cnt;
    for (int i = 0; i < n; i++) {
        uintptr_t o = P.readPtr(items + off::Array_data + (uintptr_t)i * off::PtrStep);
        Vec3 v = o ? P.read<Vec3>(o + posOff) : Vec3{};
        uintptr_t kl = o ? P.readPtr(o) : 0;
        uintptr_t kn = kl ? P.readPtr(kl + 0xB0) : 0;   // Il2CppClass.name 偏移随版本变
        printf("  [%02d] obj=%llX klass@%llX(%s) pos(+0x%llX)=(%.2f, %.2f, %.2f)%s\n",
               i, (unsigned long long)o, (unsigned long long)kl,
               kn ? cstr(kn).c_str() : "?", (unsigned long long)posOff, v.x, v.y, v.z,
               finiteV3(v) ? (fabsf(v.x) + fabsf(v.y) + fabsf(v.z) < 0.01f ? " [全零→字段不对]" : "") : " [NaN→字段不对]");
    }
    return 0;
}

static int cmdEnt(int ac, char** av) {
    if (ac < 3 || cmdAttach()) return 1;
    uintptr_t o = hx(av[2]);
    uintptr_t nameOff = ac > 3 ? hx(av[3]) : 0xB0;
    uintptr_t kl = P.readPtr(o), kn = kl ? P.readPtr(kl + nameOff) : 0;
    printf("obj=%llX  klass=%llX  name=%s (klass.nameOff=0x%llX 不对就换)\n",
           (unsigned long long)o, (unsigned long long)kl, kn ? cstr(kn).c_str() : "?",
           (unsigned long long)nameOff);
    float raw[80]{};
    if (!readb(o, raw, sizeof raw)) { puts("读取失败"); return 1; }
    puts("可疑 Vector3 (u64偏移: x,y,z):");
    for (int i = 0; i + 3 <= 80; i++) {
        float x = raw[i], y = raw[i+1], z = raw[i+2];
        if (std::isfinite(x) && std::isfinite(y) && std::isfinite(z) &&
            fabsf(x) < 1e6f && fabsf(y) < 1e4f && fabsf(z) < 1e6f &&
            (fabsf(x) > 0.01f || fabsf(z) > 0.01f) && y > -500 && y < 5000 &&
            !(fabsf(x) < 1 && fabsf(y) < 1 && fabsf(z) < 1))   // 排除单位向量/小常量
            printf("  +0x%02X: %.3f %.3f %.3f\n", i * 4, x, y, z);
    }
    return 0;
}

static int cmdSig(int ac, char** av) {
    if (ac < 3 || cmdAttach()) return 1;
    uintptr_t a = hx(av[2]);
    int len = ac > 3 ? atoi(av[3]) : 24;
    if (len > 64) len = 64;
    std::vector<unsigned char> b(len);
    if (!readb(a, b.data(), len)) { puts("读取失败"); return 1; }
    printf("\"");
    for (int i = 0; i < len; i++) printf("%s%02X", i ? " " : "", b[i]);
    printf("\"\n(RVA hint: %llX)  提示: call/jmp 相对操作数 4 字节请改 ??\n",
           (unsigned long long)(a - P.moduleBase));
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) { puts(kUsage); return 0; }
    std::string c = argv[1];
    if (c == "proclist") return cmdProclist(argc > 2 ? argv[2] : "");
    if (c == "modules")  return argc > 2 ? cmdModules(argv[2]) : (puts("usage: probe modules <pid>"), 1);
    if (c == "regions")  return argc > 2 ? cmdRegions(argv[2]) : (puts("usage: probe regions <pid>"), 1);
    if (c == "findmod")  return argc > 2 ? cmdFindmod(argv[2]) : (puts("usage: probe findmod <pid>"), 1);
    if (c == "mapname")  return argc > 2 ? cmdMapname(argv[2]) : (puts("usage: probe mapname <pid>"), 1);
    if (c == "scanstr")  return argc > 3 ? cmdScanstr(argv[2], argv[3]) : (puts("usage: probe scanstr <pid> <needle>"), 1);
    if (c == "readtest") return cmdReadtest();
    if (c == "attach")   return cmdAttach();
    if (c == "chain")    return cmdChain(argc, argv);
    if (c == "mat")      return argc > 2 ? cmdMat(argv[2]) : (puts("usage: probe mat A.txt"), 1);
    if (c == "matdiff")  return argc > 3 ? cmdMatdiff(argv[2], argv[3]) : (puts("usage: probe matdiff A B"), 1);
    if (c == "list")     return cmdList(argc, argv);
    if (c == "ent")      return cmdEnt(argc, argv);
    if (c == "sig")      return cmdSig(argc, argv);
    puts(kUsage);
    return 1;
}
