// ============================================================================
//  main.cpp —— endfield-esp 外部 ESP：读线程 + GDI 全透覆盖层
//  热键：INSERT 开关绘制   END 退出
// ============================================================================
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <mutex>
#include <thread>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "memory.hpp"
#include "esp.hpp"
#include "config.hpp"
#include "feed.hpp"
#include "shm.hpp"
#include <ctime>
#include "../config/offsets.hpp"
#include "launcher.hpp"
#include "gui.hpp"

// ------------------------------- 全局 -------------------------------
static mem::Proc           g_proc;
EspConfig           g_cfg;   // gui.hpp extern
std::mutex          g_mtx;
std::vector<EspEntry> g_ents;
static std::atomic<bool>   g_run{ true };
int g_screenW = 0, g_screenH = 0;
static HWND                g_hwnd = nullptr;
HFONT                      g_font = nullptr;   // gui 字号滑条重建

// ------------------------------- 读取 -------------------------------
static void ReadFrame(std::vector<EspEntry>& out) {
    out.clear();
    if (!off::EntityList_RVA) return;            // 静态链未配置 → 仅 FileFeed 模式可用
    if (!g_proc.valid()) return;

    uintptr_t base = g_proc.moduleBase;

    uintptr_t list = g_proc.follow(base + off::EntityList_RVA,
                                   off::EntityList_Chain, CHAIN_N(off::EntityList_Chain));
    uintptr_t vmAddr = 0;
    if (off::ViewMatrixSig && *off::ViewMatrixSig)
        vmAddr = g_proc.findPattern(off::ViewMatrixSig);            // 特征码优先
    if (!vmAddr)
        vmAddr = g_proc.follow(base + off::ViewMatrix_RVA,
                               off::ViewMatrix_Chain, CHAIN_N(off::ViewMatrix_Chain));
    if (!list || !vmAddr) return;

    Mat4 view = g_proc.read<Mat4>(vmAddr);
    if (std::isnan(view.m[0][0]) && std::isnan(view.m[3][3])) return;

    float fov = 60.f;
    if (off::CamFov_RVA) {
        uintptr_t fa = g_proc.follow(base + off::CamFov_RVA,
                                     off::CamFov_Chain, CHAIN_N(off::CamFov_Chain));
        if (fa) fov = g_proc.read<float>(fa, 60.f);
    }
    if (!(fov > 1.f && fov < 179.f)) fov = 60.f;

    Vec3 cam = CamPosFromView(view);

    int count = g_proc.read<int>(list + off::List_size, 0);
    uintptr_t items = g_proc.readPtr(list + off::List_items);
    if (count <= 0 || count > off::MaxEntities || !items) return;

    for (int i = 0; i < count; i++) {
        uintptr_t obj = g_proc.readPtr(items + off::Array_data + (uintptr_t)i * off::PtrStep);
        if (obj < 0x10000) continue;

        EspEntry e;
        e.pos = g_proc.read<Vec3>(obj + off::Ent_Pos);
        if (!finiteV3(e.pos)) continue;

        if (off::Ent_Name) {
            e.name = g_proc.readIl2CppStr(g_proc.readPtr(obj + off::Ent_Name));
        }
        if (e.name.empty()) { char b[24]; snprintf(b, sizeof b, "ENT#%d", i); e.name = b; }

        float hgt = off::Ent_Height ? g_proc.read<float>(obj + off::Ent_Height, 0.f) : 0.f;
        if (!(hgt > 0.1f && hgt < 10.f)) hgt = g_cfg.defHeight;

        e.type = off::Ent_TypeId ? (g_proc.read<int>(obj + off::Ent_TypeId, 0) & 7) : 0;
        e.dist = (e.pos - cam).len();
        if (e.dist > g_cfg.maxDist) continue;

        Vec3 headW = e.pos + Vec3{ 0.f, hgt, 0.f };                 // Unity y-up
        auto proj = [&](const Vec3& w, Vec2& s) {
            return g_cfg.mode == W2SMode::View
                       ? W2SView(view, fov, w, g_screenW, g_screenH, s)
                       : W2SClip(view, w, g_screenW, g_screenH, s);
        };
        e.vis = proj(e.pos, e.feet) && proj(headW, e.head);
        if (!e.vis) continue;
        out.push_back(std::move(e));
    }
}


// ------------------------------- FileFeed 数据源 -------------------------------
static std::string g_lastWorld, g_lastCam;                      // 已加载文件名（变化才重解析）
static std::vector<feed::WorldObj> g_objs;
static feed::CamState g_cam;

static std::string newestIn(const std::string& dir, const char* pattern) {
    WIN32_FIND_DATAA fd;
    std::string pat = dir + "\\" + pattern;
    HANDLE h = FindFirstFileA(pat.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return "";
    std::string best; FILETIME bt{}; bool have = false;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            (!have || CompareFileTime(&fd.ftLastWriteTime, &bt) > 0)) {
            bt = fd.ftLastWriteTime; best = fd.cFileName; have = true;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return best.empty() ? "" : dir + "\\" + best;
}

static void ReadFrameFeed(std::vector<EspEntry>& out) {
    std::string dir = g_cfg.dumpDir;
    while (!dir.empty() && (dir.back() == '\\' || dir.back() == '/')) dir.pop_back();
    std::string w = newestIn(dir, "IL2CPP_World_Dump_*.txt");
    std::string k = newestIn(dir, "IL2CPP_Camera_*.txt");
    if (w.empty()) return;
    if (w != g_lastWorld) { if (!feed::LoadWorldFile(g_objs, w)) return; g_lastWorld = w; }
    bool camOk = !k.empty() && k == g_lastCam ? g_cam.ok : false;
    if (k != g_lastCam) {
        g_lastCam = k;
        g_cam = feed::CamState{};
        camOk = !k.empty() && feed::LoadCameraFile(g_cam, k);
    }
    if (!camOk && !g_objs.empty()) { g_cam.fov = 0; camOk = feed::CamFromWorld(g_objs, g_cam); }
    if (!camOk) return;
    feed::FeedOpts o;
    o.include = "";   // include 统一在 espui::ApplyFilter 处理
    o.maxDist = g_cfg.maxDist; o.defHeight = g_cfg.defHeight;
    o.fovOverride = g_cfg.feedFov; o.screenW = g_screenW; o.screenH = g_screenH;
    feed::BuildEntries(g_objs, g_cam, o, out);
}

// ------------------------- SHM 实时 feed（dumper 内 F4） -------------------------
static shm::Reader g_shmR;
// ---- SHM funnel blackbox: where do NPC records die? writes endfield_esp_dbg.txt ----
static struct { int recs, npc, distOK, visOK, near30, near30Vis; } g_fdbg;
static char g_vfail[6][200]; static int g_nvfail = 0;
static char g_drawn[6][120]; static int g_ndrawn = 0;
static char g_camself[200] = {0};
static void FeedDbgDump(int drawn) {
    static int tick = 0;
    if (++tick < 240) return;            // one file every ~4 s
    tick = 0;
    char hid[8][64]; int nh = espui::HiddenNpcNames(hid, 8);
    FILE* f = fopen("endfield_esp_dbg.txt", "w");
    if (!f) return;
    fprintf(f, "recs=%d type2=%d distOK=%d visOK=%d afterFilter=%d\n",
        g_fdbg.recs, g_fdbg.npc, g_fdbg.distOK, g_fdbg.visOK, drawn);
    fprintf(f, "npcNear30=%d visNear30=%d\n", g_fdbg.near30, g_fdbg.near30Vis);
    for (int i = 0; i < g_nvfail && i < 6; i++) fprintf(f, "%s\n", g_vfail[i]);
    g_nvfail = 0;
    fprintf(f, "%s\n", g_camself);
    for (int i = 0; i < g_ndrawn && i < 6; i++) fprintf(f, "%s\n", g_drawn[i]);
    g_ndrawn = 0;
    fprintf(f, "maxDist=%.0f include='%s' exclude='%s' showEnemy=%d showNpc=%d showOther=%d\n",
        g_cfg.maxDist, g_cfg.feedInclude, g_cfg.exclude,
        (int)g_cfg.showEnemy, (int)g_cfg.showNpc, (int)g_cfg.showOther);
    for (int i = 0; i < nh && i < 8; i++) fprintf(f, "hid '%s'\n", hid[i]);
    fclose(f);
}

static bool ReadFrameShm(std::vector<EspEntry>& out) {
    if (!g_shmR.opened() && !g_shmR.open()) return false;
    shm::Header hd; std::vector<shm::Rec> recs;
    if (!g_shmR.grab(hd, recs) || !g_shmR.fresh(hd)) return false;
    if (!hd.camOk) return false;
    feed::CamState c{};
    c.pos   = { hd.camPos[0],   hd.camPos[1],   hd.camPos[2] };
    c.fwd   = { hd.camFwd[0],   hd.camFwd[1],   hd.camFwd[2] };
    c.right = { hd.camRight[0], hd.camRight[1], hd.camRight[2] };
    c.up    = { hd.camUp[0],    hd.camUp[1],    hd.camUp[2] };
    c.fov   = g_cfg.feedFov > 1.f ? g_cfg.feedFov : hd.fov;
    c.ok    = true;
    Mat4 v = feed::ViewFromCam(c);
    {   // camera self-check: is the view cam sitting behind the hero, looking his way?
        float hx = (float)hd.charPos[0], hy = (float)hd.charPos[1], hz = (float)hd.charPos[2];
        float vx = hx - c.pos.x, vy = hy - c.pos.y, vz = hz - c.pos.z;
        float vh = std::sqrt(vx * vx + vy * vy + vz * vz);
        float ahead = (vh > 0.01f) ? (vx * c.fwd.x + vy * c.fwd.y + vz * c.fwd.z) : 0.f;
        snprintf(g_camself, sizeof g_camself,
            "cam p(%.1f,%.1f,%.1f) f(%.2f,%.2f,%.2f) hero d=%.1fm ahead=%+.1fm fov=%.1f tick=%llu",
            c.pos.x, c.pos.y, c.pos.z, c.fwd.x, c.fwd.y, c.fwd.z, vh, ahead, c.fov,
            (unsigned long long)hd.tick);
    }
    g_fdbg.recs = (int)recs.size(); g_fdbg.npc = g_fdbg.distOK = g_fdbg.visOK = 0;
    g_fdbg.near30 = g_fdbg.near30Vis = 0;
    for (auto& r : recs) {
        if (r.type == 3) continue;                    // 主控自己不画
        const bool isNpc = (r.type == 2);
        if (isNpc) g_fdbg.npc++;
        EspEntry e;
        e.pos  = { r.pos[0], r.pos[1], r.pos[2] };
        e.name.assign(r.name, strnlen(r.name, 48));       // 防撕裂行未终止
        e.type = r.type == 1 ? 1 : (r.type == 2 ? 3 : 7);      // 敌人红/NPC黄/其它灰
        e.dist = (e.pos - c.pos).len();
        if (isNpc && e.dist < 30.f) g_fdbg.near30++;
        if (e.dist > g_cfg.maxDist) continue;
        if (isNpc) g_fdbg.distOK++;
        Vec3 headW = e.pos + Vec3{ 0.f, g_cfg.defHeight, 0.f };
        e.vis = W2SView(v, c.fov, e.pos, g_screenW, g_screenH, e.feet)
             && W2SView(v, c.fov, headW, g_screenW, g_screenH, e.head);
        if (!e.vis && isNpc && e.dist < 30.f && g_nvfail < 6) {   // autopsy the kill
            Vec2 fp, hp;
            bool fk = W2SView(v, c.fov, e.pos, g_screenW, g_screenH, fp);
            bool hk = W2SView(v, c.fov, headW, g_screenW, g_screenH, hp);
            float fd = -(e.pos.x * v.m[2][0] + e.pos.y * v.m[2][1] + e.pos.z * v.m[2][2] + v.m[2][3]);
            float hd = -(headW.x * v.m[2][0] + headW.y * v.m[2][1] + headW.z * v.m[2][2] + v.m[2][3]);
            snprintf(g_vfail[g_nvfail++], 199,
                "visfail %.30s d=%.1fm fd=%.1f hd=%.1f f=(%.0f,%.0f)%s h=(%.0f,%.0f)%s pos(%.1f,%.1f,%.1f)",
                e.name.empty() ? "(noname)" : e.name.c_str(), e.dist, fd, hd,
                fp.x, fp.y, fk ? "ok" : "X", hp.x, hp.y, hk ? "ok" : "X",
                e.pos.x, e.pos.y, e.pos.z);
        }
        if (!e.vis) continue;
        if (isNpc) { g_fdbg.visOK++; if (e.dist < 30.f) g_fdbg.near30Vis++; }
        if (e.dist < 30.f && g_ndrawn < 6) {
            snprintf(g_drawn[g_ndrawn++], 119, "drawn %.30s d=%.1fm s=(%.0f,%.0f)",
                e.name.empty() ? "(noname)" : e.name.c_str(), e.dist, e.feet.x, e.feet.y);
        }
        out.push_back(std::move(e));
    }
    return true;
}

static void ReadLoop() {
    while (g_run) {
        auto t0 = std::chrono::steady_clock::now();
        if (g_cfg.useShm) {                                            // ===== SHM 实时优先 =====
            std::vector<EspEntry> ls;
            if (ReadFrameShm(ls)) {
                espui::ApplyFilter(ls);
                int dnpc = 0; for (auto& e : ls) if (e.type == 3) dnpc++;
                FeedDbgDump(dnpc);
                espui::SetSource("SHM 实时(60Hz)");
                { std::lock_guard<std::mutex> lk(g_mtx); g_ents.swap(ls); }
                Sleep(6); continue;
            }
        }
        if (g_cfg.dumpDir[0]) {                                      // ===== FileFeed 模式：零 RPM =====
            std::vector<EspEntry> local;
            ReadFrameFeed(local);
            espui::ApplyFilter(local);
            espui::SetSource("FileFeed(兜底)");
            { std::lock_guard<std::mutex> lk(g_mtx); g_ents.swap(local); }
            Sleep((DWORD)g_cfg.feedMs);
            continue;
        }
        if (!g_proc.valid()) {                                       // 游戏重启自动重连
            wchar_t wp[64];
            const wchar_t* exeW = off::ProcName;
            const char* exeA = off::ProcNameA;
            const char* modN = off::ModuleName;
            if (g_cfg.procName[0])   { mem::Widen(g_cfg.procName, wp, 64); exeW = wp; exeA = g_cfg.procName; }
            if (g_cfg.moduleName[0]) modN = g_cfg.moduleName;
            if (!g_proc.attach(exeW, exeA, modN, off::ManualModuleBase)) { Sleep(1000); continue; }
        }
        std::vector<EspEntry> local;
        ReadFrame(local);
        espui::ApplyFilter(local);
        espui::SetSource("RPM");
        { std::lock_guard<std::mutex> lk(g_mtx); g_ents.swap(local); }

        auto cost = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0).count();
        Sleep((DWORD)(cost < 6 ? 6 - cost : 0));                    // ~160Hz 读频
    }
}

// --------------------------- attach 游戏窗口 ---------------------------
static HWND    g_gameWin = nullptr;
static int     g_avX = -1, g_avY = -1, g_avW = -1, g_avH = -1;   // 上次摆放，防抖
static DWORD   GamePid() {                                       // 不依赖 RPM attach
    static ULONGLONG next = 0; static DWORD pid = 0;
    if (GetTickCount64() < next) return pid;
    next = GetTickCount64() + 1000;
    wchar_t wp[64]; const wchar_t* exeW = off::ProcName;
    if (g_cfg.procName[0]) { mem::Widen(g_cfg.procName, wp, 64); exeW = wp; }
    pid = 0;
    HANDLE sn = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (sn != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe{ sizeof pe };
        if (Process32FirstW(sn, &pe)) do
            if (_wcsicmp(pe.szExeFile, exeW) == 0) { pid = pe.th32ProcessID; break; }
        while (Process32NextW(sn, &pe));
        CloseHandle(sn);
    }
    return pid;
}
static HWND  g_enumBest = nullptr;
static ULONGLONG g_enumArea = 0;
static BOOL CALLBACK EnumPidWin(HWND hw, LPARAM lp) {
    DWORD pid = 0; GetWindowThreadProcessId(hw, &pid);
    if (pid != (DWORD)lp || !IsWindowVisible(hw)) return TRUE;
    if (GetWindow(hw, GW_OWNER)) return TRUE;                 // 只要主窗
    if (GetWindowTextLengthW(hw) == 0) return TRUE;           // 必须有标题
    RECT rc{}; GetWindowRect(hw, &rc);
    ULONGLONG a = (ULONGLONG)(rc.right - rc.left) * (rc.bottom - rc.top);
    if (a > g_enumArea && a >= 200ull * 150) { g_enumArea = a; g_enumBest = hw; }
    return TRUE;                                              // 全枚举取最大
}
static void Place(int x, int y, int w, int h) {
    if (x == g_avX && y == g_avY && w == g_avW && h == g_avH) return;
    g_avX = x; g_avY = y; g_avW = w; g_avH = h;
    SetWindowPos(g_hwnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
    g_screenW = w; g_screenH = h;                             // 投影跟随视口尺寸
    char ab[64]; snprintf(ab, sizeof ab, "Attach %dx%d@%d,%d", w, h, x, y);
    espui::SetAttach(ab);
}
static void UpdateAttach(int fullW, int fullH) {
    static ULONGLONG refind = 0;
    if (!g_cfg.attach) { Place(0, 0, fullW, fullH); ShowWindow(g_hwnd, SW_SHOWNOACTIVATE); return; }
    DWORD pid = g_proc.valid() ? g_proc.pid : GamePid();
    if (!pid) { Place(0, 0, fullW, fullH); return; }          // 游戏没开：整桌面待命
    if ((!g_gameWin || !IsWindow(g_gameWin)) && GetTickCount64() >= refind) {
        refind = GetTickCount64() + 500;
        g_gameWin = nullptr; g_enumBest = nullptr; g_enumArea = 0;
        EnumWindows(EnumPidWin, (LPARAM)pid);
        g_gameWin = g_enumBest;
    }
    if (!g_gameWin) { Place(0, 0, fullW, fullH); return; }
    if (IsIconic(g_gameWin)) { ShowWindow(g_hwnd, SW_HIDE); return; }
    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    RECT rc{}; GetClientRect(g_gameWin, &rc);
    POINT tl{ rc.left, rc.top }, br{ rc.right, rc.bottom };
    ClientToScreen(g_gameWin, &tl); ClientToScreen(g_gameWin, &br);
    int w = br.x - tl.x, h = br.y - tl.y;
    if (w >= 100 && h >= 100) Place(tl.x, tl.y, w, h);
}

// ------------------------------- 绘制 -------------------------------
void EspRebuildOverlayFont() {
    if (g_font) DeleteObject(g_font);
    g_font = CreateFontW(-g_cfg.fontSize, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                         GB2312_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                         ANTIALIASED_QUALITY, FF_DONTCARE | DEFAULT_PITCH,
                         L"Microsoft YaHei");
}
static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_PAINT) { PAINTSTRUCT ps; BeginPaint(h, &ps); EndPaint(h, &ps); return 0; }
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(h, m, w, l);
}

static void DrawCornerBox(HDC dc, const RECT& r, COLORREF c, int thick) {
    HPEN pen = CreatePen(PS_SOLID, thick, c);
    HGDIOBJ old = SelectObject(dc, pen);
    int w = r.right - r.left, hgt = r.bottom - r.top;
    int lx = w > 8 ? w / 4 : 2, ly = hgt > 8 ? hgt / 5 : 2;
    MoveToEx(dc, r.left, r.top, nullptr);            LineTo(dc, r.left + lx, r.top);
    MoveToEx(dc, r.left, r.top, nullptr);            LineTo(dc, r.left, r.top + ly);
    MoveToEx(dc, r.right, r.top, nullptr);           LineTo(dc, r.right - lx, r.top);
    MoveToEx(dc, r.right, r.top, nullptr);           LineTo(dc, r.right, r.top + ly);
    MoveToEx(dc, r.left, r.bottom, nullptr);         LineTo(dc, r.left + lx, r.bottom);
    MoveToEx(dc, r.left, r.bottom, nullptr);         LineTo(dc, r.left, r.bottom - ly);
    MoveToEx(dc, r.right, r.bottom, nullptr);        LineTo(dc, r.right - lx, r.bottom);
    MoveToEx(dc, r.right, r.bottom, nullptr);        LineTo(dc, r.right, r.bottom - ly);
    SelectObject(dc, old); DeleteObject(pen);
}

static void DrawFullBox(HDC dc, const RECT& r, COLORREF c) {
    HPEN pen = CreatePen(PS_SOLID, 1, c);
    HGDIOBJ old = SelectObject(dc, pen);
    HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, r.left, r.top, r.right, r.bottom);
    SelectObject(dc, old); SelectObject(dc, ob); DeleteObject(pen);
}

static void RenderLoop() {
    MSG msg;
    while (g_run) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { g_run = false; break; }
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
        if (!g_run) break;
        if (GetAsyncKeyState(VK_INSERT) & 1) g_cfg.enable = !g_cfg.enable;
        if (GetAsyncKeyState(VK_F7) & 1)     { EspConfig_Load(g_cfg, "endfield-esp.ini"); espui::SyncFromCfg(); } // 热改配置
        if (GetAsyncKeyState(VK_F8) & 1)     espui::Toggle();                        // 控制台面板
        if (GetAsyncKeyState(VK_END) & 1)    { g_run = false; break; }

        HDC sdc = GetDC(nullptr);
        HDC mdc = CreateCompatibleDC(sdc);
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = g_screenW; bi.bmiHeader.biHeight = -g_screenH;
        bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP dib = CreateDIBSection(mdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        HGDIOBJ oldBmp = SelectObject(mdc, dib);

        RECT full{ 0, 0, g_screenW, g_screenH };
        HBRUSH kb = CreateSolidBrush(RGB(0, 0, 0));                 // 纯黑 = colorkey 透明
        FillRect(mdc, &full, kb); DeleteObject(kb);
        SetBkMode(mdc, TRANSPARENT);
        if (g_font) SelectObject(mdc, g_font);

        if (g_cfg.enable) {
            std::vector<EspEntry> ents;
            { std::lock_guard<std::mutex> lk(g_mtx); ents = g_ents; }

            for (const EspEntry& e : ents) {
                uint32_t abgr = g_cfg.palette[e.type & 7];
                COLORREF col = RGB(abgr & 0xFF, (abgr >> 8) & 0xFF, (abgr >> 16) & 0xFF);
                if (COLORREF cc = espui::CustomColor(e.name)) col = cc;   // 书签自定义色

                int hh = (int)(e.feet.y - e.head.y);
                if (hh < 4) continue;
                int ww = (int)(hh * g_cfg.boxWidthRatio); if (ww < 2) ww = 2;
                RECT box{ (LONG)(e.feet.x - ww / 2.f), (LONG)e.head.y,
                          (LONG)(e.feet.x + ww / 2.f), (LONG)e.feet.y };

                if      (g_cfg.corners) DrawCornerBox(mdc, box, col, 2);
                else if (g_cfg.boxes)   DrawFullBox(mdc, box, col);

                if (g_cfg.names) {
                    int tw = MultiByteToWideChar(CP_UTF8, 0, e.name.c_str(), -1, nullptr, 0);
                    std::wstring wn(tw > 0 ? tw - 1 : 0, 0);
                    MultiByteToWideChar(CP_UTF8, 0, e.name.c_str(), -1,
                                        wn.empty() ? nullptr : &wn[0], tw);
                    SIZE sz{}; GetTextExtentPoint32W(mdc, wn.c_str(), (int)wn.size(), &sz);
                    SetTextColor(mdc, col);
                    TextOutW(mdc, box.left + (box.right - box.left - sz.cx) / 2,
                             (int)e.head.y - sz.cy - 2, wn.c_str(), (int)wn.size());
                }
                if (g_cfg.dists) {
                    char db[24]; snprintf(db, sizeof db, "%.0fm", e.dist);
                    SIZE sz{}; GetTextExtentPoint32A(mdc, db, (int)strlen(db), &sz);
                    SetTextColor(mdc, RGB(210, 210, 210));
                    TextOutA(mdc, box.left + (box.right - box.left - sz.cx) / 2,
                             box.bottom + 2, db, (int)strlen(db));
                }
                if (g_cfg.snap) {
                    HPEN p = CreatePen(PS_SOLID, 1, col);
                    HGDIOBJ o = SelectObject(mdc, p);
                    MoveToEx(mdc, g_screenW / 2, g_screenH, nullptr);
                    LineTo(mdc, (int)(box.left + box.right) / 2, box.bottom);
                    SelectObject(mdc, o); DeleteObject(p);
                }
            }
        }

        HDC wdc = GetDC(g_hwnd);
        BitBlt(wdc, 0, 0, g_screenW, g_screenH, mdc, 0, 0, SRCCOPY);
        ReleaseDC(g_hwnd, wdc);

        SelectObject(mdc, oldBmp); DeleteObject(dib);
        DeleteDC(mdc); ReleaseDC(nullptr, sdc);
        UpdateAttach(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
        Sleep(8);                                                    // ~120fps 绘制
    }
}

// ------------------------------- 入口 -------------------------------
int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    // DPI 感知（笔记本 150% 缩放下坐标必须物理像素，否则 attach/全屏全部错位）
    if (HMODULE u32 = GetModuleHandleA("user32")) {
        typedef BOOL(WINAPI* SPDAC)(HANDLE);
        if (auto f = (SPDAC)GetProcAddress(u32, "SetProcessDpiAwarenessContext"))
            f((HANDLE)-4);                                  // PER_MONITOR_AWARE_V2
        else SetProcessDPIAware();                          // 老系统兜底
    }
    g_screenW = GetSystemMetrics(SM_CXSCREEN);
    g_screenH = GetSystemMetrics(SM_CYSCREEN);

    // ---- v17c all-in-one bootstrap BEFORE any window: pick/launch game,
    //      gate on GameAssembly.dll, inject embedded feed, report outcome ----
    {
        std::string lmsg;
        int lst = EspLaunchBootstrap(lmsg);
        if (lst != 2)   // anything but "user cancelled selection" -> one honest status line
            MessageBoxA(nullptr, lmsg.c_str(), "endfield-esp feed",
                        (lst == 0) ? MB_OK | MB_ICONINFORMATION : MB_OK | MB_ICONWARNING);
    }

    WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc; wc.hInstance = hInst;
    wc.lpszClassName = L"EndfieldEspOverlay";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    g_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW,
        wc.lpszClassName, L"endfield-esp", WS_POPUP,
        0, 0, g_screenW, g_screenH, nullptr, nullptr, hInst, nullptr);
    SetLayeredWindowAttributes(g_hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);
    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);

    EspConfig_Load(g_cfg, "endfield-esp.ini");                       // 同目录 ini，缺省用默认值
    espui::SyncFromCfg();

    INITCOMMONCONTROLSEX icc{ sizeof icc, ICC_STANDARD_CLASSES | ICC_BAR_CLASSES | ICC_TAB_CLASSES };
    InitCommonControlsEx(&icc);
    espui::Create(hInst);                                             // F8 显隐控制台

        EspRebuildOverlayFont();                                        // 按 ini 字号建覆盖层字体
    std::thread(ReadLoop).detach();
    RenderLoop();

    g_run = false;
    if (g_font) DeleteObject(g_font);
    g_proc.close();
    return 0;
}
