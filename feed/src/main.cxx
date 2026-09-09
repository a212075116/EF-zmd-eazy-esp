// endfield-esp feed loader: IL2CPP bootstrap + ESP feed hotkeys ONLY.
// (merged from IL2CPP-Dumper; static dump / scene dumper / mapper removed)
#define _CRT_SECURE_NO_WARNINGS

#include "../include/il2cpp_api.hxx"
#include "../include/esp_feed.hxx"
#include "../include/utils.hxx"
#include <cstring>
#include <string>
#include <windows.h>

static void* g_il2cppThread = nullptr;

static bool WaitForIl2CppReady() {
    const DWORD totalTimeoutMs = 90'000;
    DWORD start = GetTickCount();

    while (!GetModuleHandleA("GameAssembly.dll")) {
        if (GetTickCount() - start > totalTimeoutMs) {
            Log("[error] GameAssembly.dll never loaded");
            return false;
        }
        Sleep(500);
    }

    if (!api::initialized)
        api::init();
    if (!api::initialized) {
        Log("[error] api init failed");
        return false;
    }

    while (!(api::get_domain && api::get_domain())) {
        if (GetTickCount() - start > totalTimeoutMs) {
            Log("[error] il2cpp_domain_get never returned non-null");
            return false;
        }
        Sleep(500);
    }

    Log("Domain ready, settling for 12s before touching IL2CPP...");
    Sleep(12000);
    return true;
}

static void AttachToRuntime() {
    if (g_il2cppThread || !api::thread_attach)
        return;
    void* domain = api::get_domain ? api::get_domain() : nullptr;
    if (!domain)
        return;

    for (int attempt = 0; attempt < 3 && !g_il2cppThread; ++attempt) {
        if (attempt > 0) {
            Log("attach retry " + std::to_string(attempt));
            Sleep(3000);
        }

        // GC-safe attach: freeze the collector around thread_attach (proven recipe)
        int savedDontGc = -1;
        bool usedBoehmFn = false;
        bool usedIl2cppFn = false;

        if (api::gc_dont_gc_ptr) {
            __try {
                savedDontGc = *api::gc_dont_gc_ptr;
                *api::gc_dont_gc_ptr = savedDontGc + 1;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                savedDontGc = -1;
            }
        }
        if (savedDontGc < 0 && api::gc_disable_boehm) {
            __try { api::gc_disable_boehm(); usedBoehmFn = true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { usedBoehmFn = false; }
        }
        if (savedDontGc < 0 && !usedBoehmFn && api::gc_disable) {
            __try { api::gc_disable(); usedIl2cppFn = true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { usedIl2cppFn = false; }
        }

        __try { g_il2cppThread = api::thread_attach(domain); }
        __except (EXCEPTION_EXECUTE_HANDLER) { g_il2cppThread = nullptr; }

        if (savedDontGc >= 0 && api::gc_dont_gc_ptr) {
            __try { *api::gc_dont_gc_ptr = savedDontGc; }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        if (usedBoehmFn && api::gc_enable_boehm) {
            __try { api::gc_enable_boehm(); }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        if (usedIl2cppFn && api::gc_enable) {
            __try { api::gc_enable(); }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
    }

    Log(g_il2cppThread ? "thread attached to runtime"
                       : "running unattached - ESP feed still guards walks with SEH");
}

// %TEMP%\esp_feed.cfg (or game CWD copy): written by endfield_esp.exe => silent mode
static std::string CfgFirst() {
    char tmp[MAX_PATH] = { 0 };
    std::string p0;
    if (GetTempPathA(MAX_PATH, tmp) > 0) p0 = std::string(tmp) + "esp_feed.cfg";
    const char* cand[2] = { p0.c_str(), "esp_feed.cfg" };
    for (const char* p : cand) {
        if (!p || !*p) continue;
        HANDLE h = CreateFileA(p, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) { CloseHandle(h); return p; }
    }
    return "";
}

static bool CfgHas(const std::string& path, const char* key) {
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return false;
    char line[128]; bool hit = false;
    while (fgets(line, sizeof line, f)) if (!strncmp(line, key, strlen(key))) hit = true;
    fclose(f);
    return hit;
}

DWORD WINAPI EntryPoint(LPVOID lpParam) {
    HMODULE hModule = (HMODULE)lpParam;

    std::string cfg = CfgFirst();
    bool autoMode = !cfg.empty() && CfgHas(cfg, "auto=1");

    if (!autoMode) {
        AllocConsole();
        FILE* fDummy = nullptr;
        freopen_s(&fDummy, "CONOUT$", "w", stdout);
        freopen_s(&fDummy, "CONIN$", "r", stdin);
        SetConsoleTitleA("endfield-esp feed");
    }

    Log("feed DLL injected. waiting for IL2CPP runtime...");

    if (WaitForIl2CppReady()) {
        AttachToRuntime();

        if (autoMode) {
            // all-in-one: no console, no hotkeys -- GUI drives ON/OFF via cfg "enable"
            espfeed::Toggle();
            int last = 1;
            for (;;) {
                Sleep(1000);
                std::string c2 = CfgFirst();
                if (c2.empty()) continue;
                int e = CfgHas(c2, "enable=1") ? 1 : (CfgHas(c2, "enable=0") ? 0 : -1);
                if (e >= 0 && e != last) { espfeed::Toggle(); last = e; }
            }
        }

        Log("");
        Log("Hotkeys:");
        Log("  F4   ESP live feed ON/OFF (shared mem 'Local\\EndfieldEsp', 60 Hz)");
        Log("  F6   exit & unload");
        Log("");

        bool exitRequested = false;
        bool prev[2] = { false, false };
        while (!exitRequested) {
            bool d4 = (GetAsyncKeyState(VK_F4) & 0x8000) != 0;
            if (d4 && !prev[0]) espfeed::Toggle();
            prev[0] = d4;
            bool d6 = (GetAsyncKeyState(VK_F6) & 0x8000) != 0;
            if (d6) exitRequested = true;
            prev[1] = d6;
            Sleep(50);
        }

        espfeed::Stop();

        if (g_il2cppThread && api::thread_detach) {
            api::thread_detach(g_il2cppThread);
            g_il2cppThread = nullptr;
        }
    }

    Log("unloading...");
    FreeConsole();
    // plain ExitThread: under manual mapping FreeLibrary would crash the target
    ExitThread(0);
    return 0;
}

static HANDLE g_bootBeat = nullptr;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        // receipt beacon for the all-in-one injector (manual-mapped modules never
        // show up in toolhelp, so an always-signaled named event IS the proof)
        g_bootBeat = CreateEventA(nullptr, TRUE, TRUE, "Local\\EndfieldEspBoot");
        CreateThread(nullptr, 0, EntryPoint, hModule, 0, nullptr);
    }
    return TRUE;
}
