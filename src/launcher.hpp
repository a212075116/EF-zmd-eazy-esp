// launcher.hpp -- v17 all-in-one bootstrap (Windows only)
//   1) locate a running game process (or launch the configured one)
//   2) drop the embedded feed DLL into %TEMP% and inject it (LoadLibrary via
//      CreateRemoteThread; the loader itself waits for the IL2CPP runtime)
//   3) %TEMP%\esp_feed.cfg carries the silent "auto=1" handshake so the feed
//      starts without any console / hotkey
// The generated build/gen/feed_dll.h provides g_feedDll[] / g_feedDllLen /
// g_feedDllTag (content hash -> module file name -> stale-build detection).
#pragma once
#if defined(_WIN32) || defined(LAUNCHER_ALLOW_STUB)

#include <windows.h>
#include <tlhelp32.h>
#include <commdlg.h>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifdef LAUNCHER_ALLOW_STUB
// Linux check: hand-declared interface mirroring mapper/injector.hxx
bool IsAdmin();
bool EnableDebugPrivilege();
bool CheckProcessArchitecture(HANDLE);
bool ManualMapDLLBytes(HANDLE, const std::vector<unsigned char>&, const std::string& = "");
#else
#include "mapper/injector.hxx"   // proven manual mapper (vendored from the dumper Mapper)
#endif
#include "feed_dll.h"   // generated (Linux stub: tools/stubs/feed_dll.h)

namespace launcher {

inline std::string W2A(const wchar_t* w) {
    std::string s;
    if (!w) return s;
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n > 1) { s.resize((size_t)n - 1); WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr); }
    return s;
}

inline std::string Lower(std::string s) {
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}
inline std::string BaseName(const std::string& p) {
    size_t k = p.find_last_of("\\/");
    return k == std::string::npos ? p : p.substr(k + 1);
}
inline std::string DirName(const std::string& p) {
    size_t k = p.find_last_of("\\/");
    return k == std::string::npos ? std::string(".") : p.substr(0, k);
}
inline std::string ExeDir() {
    char buf[MAX_PATH] = { 0 };
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    return DirName(buf);
}
inline bool PathExists(const std::string& p) {
    DWORD a = GetFileAttributesA(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
inline std::string TempDir() {
    char tmp[MAX_PATH] = { 0 };
    if (GetTempPathA(MAX_PATH, tmp) > 0) return tmp;   // trailing slash included
    return ExeDir() + "\\";
}

// game path persistence: sidecar next to the overlay exe (survives ini saves)
inline std::string GameFile() { return ExeDir() + "\\endfield-esp-game.txt"; }
inline std::string LoadGamePath() {
    FILE* f = fopen(GameFile().c_str(), "r");
    if (!f) return "";
    char line[512] = { 0 };
    if (fgets(line, sizeof line, f)) line[strcspn(line, "\r\n")] = 0;
    fclose(f);
    return line;
}
inline void SaveGamePath(const std::string& p) {
    FILE* f = fopen(GameFile().c_str(), "w");
    if (f) { fputs(p.c_str(), f); fclose(f); }
}

inline DWORD FindPidByImage(const std::string& imagePath) {
    if (imagePath.empty()) return 0;
    const std::string wantBase = Lower(BaseName(imagePath));
    const std::string wantFull = Lower(imagePath);
    DWORD nameHit = 0;
    HANDLE sn = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (sn != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe{}; pe.dwSize = sizeof pe;
        if (Process32FirstW(sn, &pe)) {
            do {
                std::string base = Lower(W2A(pe.szExeFile));
                if (base != wantBase) continue;
                DWORD pid = pe.th32ProcessID;
                if (!nameHit) nameHit = pid;                    // base-name fallback
                HANDLE hp = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
                if (hp) {
                    typedef BOOL(WINAPI* QFP)(HANDLE, DWORD, LPWSTR, LPDWORD);
                    QFP qfp = (QFP)GetProcAddress(GetModuleHandleA("kernel32.dll"), "QueryFullProcessImageNameW");
                    wchar_t wbuf[512] = { 0 }; DWORD cn = 512;
                    if (qfp && qfp(hp, 0, wbuf, &cn) && Lower(W2A(wbuf)) == wantFull) {
                        CloseHandle(hp); CloseHandle(sn); return pid;   // exact path wins
                    }
                    CloseHandle(hp);
                }
            } while (Process32NextW(sn, &pe));
        }
        CloseHandle(sn);
    }
    return nameHit;
}

// any endfield_feed* module already loaded in the game? (stale-build awareness)
inline bool HasModulePrefix(DWORD pid, const char* prefixLower, std::string& nameOut) {
    bool found = false;
    HANDLE sn = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (sn != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W me{}; me.dwSize = sizeof me;
        if (Module32FirstW(sn, &me)) {
            do {
                std::string n = W2A(me.szModule);
                if (Lower(n).rfind(prefixLower, 0) == 0) { nameOut = n; found = true; }
            } while (Module32NextW(sn, &me));
        }
        CloseHandle(sn);
    }
    return found;
}

inline bool PidAlive(DWORD pid) {
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!h) return false;
    bool alive = (WaitForSingleObject(h, 0) == WAIT_TIMEOUT);
    CloseHandle(h);
    return alive;
}

#ifndef EVENT_QUERY_STATE
#define EVENT_QUERY_STATE 0x0001   // older SDK headers may not expose it
#endif

// the feed loader publishes this event the moment it lands in the target;
// manual-mapped modules never appear in toolhelp, so THIS is the receipt.
inline bool WaitBootHeartbeat(DWORD /*pid*/, int timeoutMs) {
    HANDLE h = nullptr;
    DWORD t0 = GetTickCount();
    for (;;) {
        h = OpenEventA(EVENT_QUERY_STATE, FALSE, "Local\\EndfieldEspBoot");
        if (h) { CloseHandle(h); return true; }
        if ((int)(GetTickCount() - t0) >= timeoutMs) return false;
        Sleep(500);
    }
}

inline HANDLE OpenTarget(DWORD pid) {
    return OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
}

inline std::string BrowseGame() {
    char file[512] = { 0 };
    OPENFILENAMEA ofn{}; ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = nullptr;
    ofn.lpstrFilter = "Game executable (*.exe)\0*.exe\0All files\0*.*\0";
    ofn.lpstrFile = file; ofn.nMaxFile = (DWORD)sizeof file;
    ofn.lpstrTitle = "Select the Endfield executable";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    return GetOpenFileNameA(&ofn) ? std::string(file) : std::string();
}

// status: 0 = feed live   2 = game not configured (caller browses)
//         3 = failure (msg explains)   4 = relaunching elevated (caller must exit NOW)
inline int Ensure(const std::string& gamePath, std::string& msg) {
    if (gamePath.empty()) return 2;

    EnableDebugPrivilege();

    const std::string want = std::string("endfield_feed_") + g_feedDllTag + ".dll";
    std::vector<unsigned char> bytes(g_feedDll, g_feedDll + g_feedDllLen);

    // silent-mode handshake BEFORE mapping (loader reads cfg at attach)
    {
        FILE* fc = fopen((TempDir() + "esp_feed.cfg").c_str(), "w");
        if (fc) { fputs("auto=1\nenable=1\ndiag=0\n", fc); fclose(fc); }
    }

    DWORD pid = FindPidByImage(gamePath);
    HANDLE hProc = nullptr;
    bool suspended = false;
    PROCESS_INFORMATION pi{};

    if (!pid) {
        if (!PathExists(gamePath)) { msg = "saved game path no longer exists"; return 2; }
        STARTUPINFOA si{}; si.cb = sizeof si;
        std::string cmd = "\"" + gamePath + "\"";
        if (!CreateProcessA(gamePath.c_str(), &cmd[0], nullptr, nullptr, FALSE,
                            CREATE_SUSPENDED, nullptr, DirName(gamePath).c_str(), &si, &pi)) {
            msg = "could not launch the game (code " + std::to_string(GetLastError()) + ")";
            return 3;
        }
        pid = pi.dwProcessId;
        hProc = pi.hProcess;
        suspended = true;
    } else {
        hProc = OpenTarget(pid);
        if (!hProc) {
            msg = "game running but OpenProcess failed (code " + std::to_string(GetLastError()) +
                  ") - run the overlay as administrator";
            return 3;
        }
    }

    if (!CheckProcessArchitecture(hProc)) {
        msg = "architecture mismatch (target must be x64)";
        if (suspended) { TerminateProcess(hProc, 1); CloseHandle(pi.hThread); CloseHandle(hProc); }
        else CloseHandle(hProc);
        return 3;
    }

    // map it (proven injector; works suspended-by-design, attempts live too)
    if (!ManualMapDLLBytes(hProc, bytes, "")) {
        msg = "manual map failed (code " + std::to_string(GetLastError()) + ")";
        if (suspended) { TerminateProcess(hProc, 1); CloseHandle(pi.hThread); CloseHandle(hProc); }
        else CloseHandle(hProc);
        return 3;
    }
    if (suspended) {
        if (ResumeThread(pi.hThread) == (DWORD)-1) {
            msg = "ResumeThread failed";
            TerminateProcess(hProc, 1);
        }
        CloseHandle(pi.hThread);
    }
    CloseHandle(hProc);

    // receipt: the loader's boot event (needs its 90s GameAssembly wait window)
    if (WaitBootHeartbeat(pid, 30000)) {
        msg = "feed mapped + alive (pid " + std::to_string(pid) + ", tag " + g_feedDllTag + ") - " +
              (suspended ? "boxes after the game finishes loading" : "boxes within a few seconds");
        return 0;
    }
    msg = "payload mapped but no heartbeat from the game side yet - if the game is still booting that "
          "is normal: boxes should follow; if they never do, close the game and start it with this exe again";
    return 0;   // optimistic: the loader may simply be inside its wait windows
}

} // namespace launcher

#include <shellapi.h>

inline void RelaunchElevatedSelf() {
    wchar_t exe[MAX_PATH] = { 0 };
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    SHELLEXECUTEINFOW sei{}; sei.cbSize = sizeof sei;
    sei.lpVerb = L"runas"; sei.lpFile = exe; sei.nShow = SW_SHOWNORMAL;
    ShellExecuteExW(&sei);
}

// one-shot entry used by WinMain: returns the status for optional reporting
inline int EspLaunchBootstrap(std::string& outMsg) {
    if (!IsAdmin()) {            // mapper proven flow demands admin; bounce once via UAC
        RelaunchElevatedSelf();
        outMsg = "relaunching elevated";
        return 4;
    }
    std::string gp = launcher::LoadGamePath();
    int st = launcher::Ensure(gp, outMsg);
    while (st == 2) {
        std::string pick = launcher::BrowseGame();
        if (pick.empty()) { outMsg = "no game selected - overlay runs in manual mode"; return 2; }
        gp = pick;
        launcher::SaveGamePath(gp);
        st = launcher::Ensure(gp, outMsg);
    }
    return st;
}

#endif // _WIN32 || LAUNCHER_ALLOW_STUB
