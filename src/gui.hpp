// ============================================================================
//  gui.hpp —— 原生 Win32 控制台（F8 显隐），Tab 四页：绘制/视图/过滤/书签
//  实体清单点行=显式显隐（优先白/黑名单与类型）；书签=名称自定义颜色。
//  main.cpp 在全局定义后 include；EspRebuildOverlayFont 由 main.cpp 提供。
// ============================================================================
#pragma once
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <string>
#include <cstring>
#include <vector>
#include <map>
#include <mutex>
#include <cstdio>
#include <cwchar>
#include <cctype>
#include "esp.hpp"

bool EspConfig_Save(const EspConfig& c, const char* path);   // config.hpp inline 定义
void EspConfig_Load(EspConfig& c, const char* path);
extern EspConfig             g_cfg;      // main.cpp
extern std::mutex            g_mtx;
extern std::vector<EspEntry> g_ents;
void EspRebuildOverlayFont();            // main.cpp：字号滑条回调

namespace espui {

inline std::string lo(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }
inline std::wstring u8w(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 1 ? n - 1 : 0, 0);
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}
inline std::string w2u(const wchar_t* w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 1 ? n - 1 : 0, 0);
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    return s;
}
inline bool subHit(const std::string& lowSemi, const std::string& name) {
    if (lowSemi.empty()) return false;
    std::string n = lo(name); size_t a = 0;
    while (a < lowSemi.size()) {
        size_t b = lowSemi.find(';', a); if (b == std::string::npos) b = lowSemi.size();
        if (b - a > 0 && n.find(lowSemi.substr(a, b - a)) != std::string::npos) return true;
        a = b + 1;
    }
    return false;
}
inline bool typeOk(int paletteType) {
    return paletteType == 1 ? g_cfg.showEnemy : (paletteType == 3 ? g_cfg.showNpc : g_cfg.showOther);
}

struct Obs { bool explicitSet = false; bool vis = true; int type = 0; int count = 0; };
inline std::map<std::string, Obs> g_obs;
inline std::mutex g_obsMtx;
inline bool shown(const Obs& o, const std::string& key) {
    if (o.explicitSet) return o.vis;
    if (subHit(lo(g_cfg.exclude), key)) return false;                 // 黑名单
    return (!g_cfg.feedInclude[0] || subHit(lo(g_cfg.feedInclude), key))
           && typeOk(o.type);                                          // 白名单+类型
}
inline void ApplyFilter(std::vector<EspEntry>& v) {
    std::lock_guard<std::mutex> lk(g_obsMtx);
    for (auto& kv : g_obs) kv.second.count = 0;
    for (auto& e : v) { Obs& o = g_obs[e.name]; if (o.type == 0) o.type = e.type; o.count++; }
    size_t w = 0;
    for (size_t i = 0; i < v.size(); i++)
        if (shown(g_obs[v[i].name], v[i].name)) { if (w != i) v[w] = std::move(v[i]); w++; }
    v.resize(w);
}

inline int HiddenNpcNames(char (*out)[64], int cap) {   // names hidden by explicit clicks
    std::lock_guard<std::mutex> lk(g_obsMtx);
    int n = 0;
    for (auto& kv : g_obs)
        if (kv.second.explicitSet && !kv.second.vis) {
            if (n < cap) { strncpy(out[n], kv.first.c_str(), 63); out[n][63] = 0; }
            n++;
        }
    return n;
}

inline std::map<std::string, COLORREF> g_col;   // 书签：小写名 -> 颜色
inline std::mutex g_colMtx;
inline COLORREF CustomColor(const std::string& name) {
    std::lock_guard<std::mutex> lk(g_colMtx);
    auto it = g_col.find(lo(name));
    return it == g_col.end() ? 0 : it->second;
}
inline void ParseSaves() {
    std::lock_guard<std::mutex> lk(g_colMtx);
    g_col.clear();
    for (auto& s : g_cfg.saves) {
        char nm[128]; int r, g, b;
        if (sscanf(s.c_str(), "%127[^,],%d,%d,%d", nm, &r, &g, &b) == 4)
            g_col[lo(nm)] = RGB(r & 255, g & 255, b & 255);
    }
}
inline void DumpSaves() {
    std::lock_guard<std::mutex> lk(g_colMtx);
    g_cfg.saves.clear();
    for (auto& kv : g_col) {
        char buf[192];
        snprintf(buf, sizeof buf, "%s,%d,%d,%d", kv.first.c_str(),
                 (int)GetRValue(kv.second), (int)GetGValue(kv.second), (int)GetBValue(kv.second));
        g_cfg.saves.push_back(buf);
    }
}
inline void SyncFromCfg() { ParseSaves(); }

enum {
    IDC_TAB = 2000,
    IDC_ENABLE, IDC_ATTACH, IDC_BOXES, IDC_CORNERS, IDC_NAMES, IDC_DISTS, IDC_SNAP, IDC_TR_FONT, IDC_LBL_FONT,
    IDC_TR_DIST, IDC_TR_HEIGHT, IDC_TR_FOV, IDC_LBL_DIST, IDC_LBL_HEIGHT, IDC_LBL_FOV, IDC_STATUS,
    IDC_ENEMY, IDC_NPC, IDC_OTHER, IDC_EDIT_INC, IDC_EDIT_EXC, IDC_BTN_APPLY,
    IDC_LIST, IDC_BTN_SHOWALL, IDC_BTN_HIDEALL,
    IDC_SAVEEDIT, IDC_SWATCH, IDC_BTN_ADD, IDC_BTN_CHGC, IDC_BTN_DEL, IDC_SVLIST,
    IDC_BTN_SAVEINI, IDC_BTN_RELOAD,
    IDT = 0x5E
};
struct Ctl { HWND hw; int page; };
inline std::vector<Ctl> g_ctls;
inline HWND g_hMain = nullptr, g_tab = nullptr, g_list = nullptr, g_svList = nullptr,
            g_status = nullptr, g_edInc = nullptr, g_edExc = nullptr, g_edSave = nullptr,
            g_swatch = nullptr;
inline COLORREF g_curCol = RGB(255, 80, 80);
inline float g_sc = 1.0f;                       // DPI 缩放（150% 面板=1.5）
inline std::string g_attachText;
inline std::mutex g_attMtx;
inline void SetAttach(const std::string& s) { std::lock_guard<std::mutex> lk(g_attMtx); g_attachText = s; }
inline std::vector<std::string> g_rowKeys;
inline std::string g_srcText = "init";
inline std::mutex g_srcMtx;
inline void SetSource(const std::string& s) { std::lock_guard<std::mutex> lk(g_srcMtx); g_srcText = s; }
inline bool Checked(HWND b) { return SendMessageW(b, BM_GETCHECK, 0, 0) == BST_CHECKED; }

inline HWND Mk(HINSTANCE hi, HWND parent, const wchar_t* cls, const wchar_t* txt,
               DWORD style, int x, int y, int w, int h, int id, int page) {
    HWND hw = CreateWindowExW(0, cls, txt, WS_CHILD | WS_VISIBLE | style,
                              (int)(x * g_sc), (int)(y * g_sc), (int)(w * g_sc), (int)(h * g_sc),
                              parent, (HMENU)(INT_PTR)id, hi, nullptr);
    if (page >= 0) g_ctls.push_back({ hw, page });
    if (page > 0) ShowWindow(hw, SW_HIDE);
    return hw;
}
inline BOOL CALLBACK SetFontCb(HWND c, LPARAM f) { SendMessageW(c, WM_SETFONT, f, TRUE); return TRUE; }
inline void PullEdits() {
    wchar_t buf[300] = {};
    GetWindowTextW(g_edInc, buf, 299);
    snprintf(g_cfg.feedInclude, sizeof g_cfg.feedInclude, "%s", w2u(buf).c_str());
    GetWindowTextW(g_edExc, buf, 299);
    snprintf(g_cfg.exclude, sizeof g_cfg.exclude, "%s", w2u(buf).c_str());
}
inline void RefreshEdits() {
    SetWindowTextW(g_edInc, u8w(g_cfg.feedInclude).c_str());
    SetWindowTextW(g_edExc, u8w(g_cfg.exclude).c_str());
}
inline void UpdateSwatchText() {
    wchar_t b[64];
    swprintf(b, 64, L"当前色 #%02X%02X%02X", (unsigned)GetRValue(g_curCol),
             (unsigned)GetGValue(g_curCol), (unsigned)GetBValue(g_curCol));
    SetWindowTextW(g_swatch, b);
}
inline void RebuildSaves();
inline void RebuildList();
inline void PickColor() {
    static COLORREF cust[16] = { RGB(255,80,80), RGB(0,255,255), RGB(255,255,0), RGB(0,255,0) };
    CHOOSECOLORW cc{ sizeof cc };
    cc.hwndOwner = g_hMain; cc.rgbResult = g_curCol; cc.lpCustColors = cust;
    cc.Flags = CC_RGBINIT | CC_SOLIDCOLOR | CC_ANYCOLOR;
    if (ChooseColorW(&cc)) { g_curCol = cc.rgbResult; UpdateSwatchText(); }
}
inline void AddOrUpdateSave() {
    wchar_t b[200] = {}; GetWindowTextW(g_edSave, b, 199);
    std::string n = w2u(b);
    while (!n.empty() && (n.back() == ' ' || n.back() == '\t')) n.pop_back();
    if (n.empty()) return;
    { std::lock_guard<std::mutex> lk(g_colMtx); g_col[lo(n)] = g_curCol; }
    RebuildSaves();
}
inline std::string SelSaveName() {
    int sel = (int)SendMessageW(g_svList, LB_GETCURSEL, 0, 0);
    if (sel < 0) return "";
    wchar_t b[256] = {}; SendMessageW(g_svList, LB_GETTEXT, sel, (LPARAM)b);
    std::wstring s(b); size_t p = s.find(L"  #");
    if (p == std::wstring::npos) p = s.size();
    while (p > 0 && s[p - 1] == L' ') p--;
    std::wstring nm(s, 0, p);
    return lo(w2u(nm.c_str()));
}
inline void DelSaveSel() {
    std::string k = SelSaveName(); if (k.empty()) return;
    std::lock_guard<std::mutex> lk(g_colMtx); g_col.erase(k);
    RebuildSaves();
}
inline void ChangeSelSaveColor() {
    std::string k = SelSaveName(); if (k.empty()) return;
    PickColor();
    { std::lock_guard<std::mutex> lk(g_colMtx); auto it = g_col.find(k);
      if (it != g_col.end()) it->second = g_curCol; }
    RebuildSaves();
}

inline void RebuildList() {
    std::vector<std::pair<std::string, bool>> rows;
    { std::lock_guard<std::mutex> lk(g_obsMtx);
      for (auto& kv : g_obs) rows.push_back({ kv.first, shown(kv.second, kv.first) }); }
    static std::vector<std::pair<std::string, bool>> last;
    if (rows == last) return;
    int top = (int)SendMessageW(g_list, LB_GETTOPINDEX, 0, 0);
    int sel = (int)SendMessageW(g_list, LB_GETCURSEL, 0, 0);
    std::string selKey = (sel >= 0 && sel < (int)g_rowKeys.size()) ? g_rowKeys[sel] : "";
    last = rows;
    SendMessageW(g_list, LB_RESETCONTENT, 0, 0);
    g_rowKeys.clear();
    for (auto& r : rows) {
        std::wstring t = r.second ? L"[x] " : L"[ ] ";
        t += u8w(r.first);
        SendMessageW(g_list, LB_ADDSTRING, 0, (LPARAM)t.c_str());
        g_rowKeys.push_back(r.first);
    }
    SendMessageW(g_list, LB_SETTOPINDEX, (WPARAM)top, 0);          // 滚动位置保持
    if (!selKey.empty())
        for (size_t i = 0; i < g_rowKeys.size(); i++)
            if (g_rowKeys[i] == selKey) { SendMessageW(g_list, LB_SETCURSEL, (WPARAM)i, 0); break; }
}
inline void RebuildSaves() {
    std::vector<std::pair<std::string, COLORREF>> rows;
    { std::lock_guard<std::mutex> lk(g_colMtx);
      for (auto& kv : g_col) rows.push_back({ kv.first, kv.second }); }
    SendMessageW(g_svList, LB_RESETCONTENT, 0, 0);
    for (auto& r : rows) {
        wchar_t b[256];
        swprintf(b, 256, L"%s  #%02X%02X%02X", u8w(r.first).c_str(),
                 (unsigned)GetRValue(r.second), (unsigned)GetGValue(r.second), (unsigned)GetBValue(r.second));
        SendMessageW(g_svList, LB_ADDSTRING, 0, (LPARAM)b);
    }
}

inline LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    HINSTANCE hi = (HINSTANCE)GetWindowLongPtrW(h, GWLP_HINSTANCE);
    if (m == WM_CREATE) {
        if (HMODULE u = GetModuleHandleW(L"user32.dll")) {
            typedef UINT(WINAPI*GDFW)(HWND);
            auto f = (GDFW)GetProcAddress(u, "GetDpiForWindow");
            if (f) { UINT dpi = f(h); if (dpi >= 96) g_sc = dpi / 96.f; }
        }
        NONCLIENTMETRICSW nc{ sizeof nc };
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof nc, &nc, 0);
        // PMv2 下 SPI 返回的 lfHeight 已含系统 DPI 放大，不能再乘 g_sc（双重放大=断腿字）
        HFONT uf = CreateFontIndirectW(&nc.lfMessageFont);
        g_tab = CreateWindowExW(0, L"sysTabControl32", L"", WS_CHILD | WS_VISIBLE,
                                (int)(8*g_sc), (int)(6*g_sc), (int)(400*g_sc), (int)(32*g_sc),
                                h, (HMENU)(INT_PTR)IDC_TAB, hi, nullptr);
        const wchar_t* pages[] = { L"绘制", L"视图", L"过滤", L"书签" };
        for (int i = 0; i < 4; i++) {
            TCITEMW ti{ }; ti.mask = TCIF_TEXT; ti.pszText = (LPWSTR)pages[i];
            SendMessageW(g_tab, TCM_INSERTITEMW, (WPARAM)i, (LPARAM)&ti);  // 宏在 ANSI TU 下会走 A 版→乱码，显式 W
        }
        int y = 46; HWND b;
        // -------- 页0 绘制 --------
        b = Mk(hi, h, L"button", L"启用 ESP（INSERT 同效）", BS_AUTOCHECKBOX, 14, y, 180, 20, IDC_ENABLE, 0);
        SendMessageW(b, BM_SETCHECK, g_cfg.enable ? BST_CHECKED : BST_UNCHECKED, 0);
        b = Mk(hi, h, L"button", L"贴合游戏窗口", BS_AUTOCHECKBOX, 200, y, 130, 20, IDC_ATTACH, 0);
        SendMessageW(b, BM_SETCHECK, g_cfg.attach ? BST_CHECKED : BST_UNCHECKED, 0); y += 26;
        { const wchar_t* t[] = { L"角框", L"方框", L"名字", L"距离", L"吸附" };
          int id[] = { IDC_CORNERS, IDC_BOXES, IDC_NAMES, IDC_DISTS, IDC_SNAP };
          const bool* v[] = { &g_cfg.corners, &g_cfg.boxes, &g_cfg.names, &g_cfg.dists, &g_cfg.snap };
          int cx = 14;
          for (int i = 0; i < 5; i++) {
              b = Mk(hi, h, L"button", t[i], BS_AUTOCHECKBOX, cx, y, 64, 20, id[i], 0);
              SendMessageW(b, BM_SETCHECK, *v[i] ? BST_CHECKED : BST_UNCHECKED, 0);
              cx += 70; if (cx > 330) { cx = 14; y += 24; }
          } y += 30; }
        Mk(hi, h, L"static", L"文字字号", 0, 14, y + 2, 80, 18, IDC_LBL_FONT, 0);
        b = Mk(hi, h, L"msctls_trackbar32", L"", TBS_AUTOTICKS, 100, y - 2, 220, 26, IDC_TR_FONT, 0);
        SendMessageW(b, TBM_SETRANGE, TRUE, MAKELONG(8, 36));
        SendMessageW(b, TBM_SETPOS, TRUE, g_cfg.fontSize); y += 34;
        Mk(hi, h, L"static", L"默认颜色：敌人红/NPC黄/其它灰。\n书签名单（过滤/书签页）优先用自定义色。",
           0, 14, y, 380, 40, 0, 0);
        // -------- 页1 视图 --------
        y = 50;
        auto slider = [&](int idLbl, const wchar_t* cap, int idTr, int lo_, int hi_, int pos) {
            Mk(hi, h, L"static", cap, 0, 14, y + 2, 110, 18, idLbl, 1);
            HWND t = Mk(hi, h, L"msctls_trackbar32", L"", TBS_AUTOTICKS, 128, y - 2, 250, 26, idTr, 1);
            SendMessageW(t, TBM_SETRANGE, TRUE, MAKELONG(lo_, hi_));
            SendMessageW(t, TBM_SETPOS, TRUE, pos);
            y += 40;
        };
        slider(IDC_LBL_DIST,   L"显示距离",   IDC_TR_DIST,   5, 500, (int)g_cfg.maxDist);
        slider(IDC_LBL_HEIGHT, L"模型高度",   IDC_TR_HEIGHT, 5, 40,  (int)(g_cfg.defHeight * 10));
        slider(IDC_LBL_FOV,    L"FOV(0=自动)", IDC_TR_FOV,   0, 120, (int)g_cfg.feedFov);
        Mk(hi, h, L"static", L"滑条实时生效；范围外实体不进清单", 0, 14, y + 2, 380, 18, 0, 1); y += 30;
        g_status = Mk(hi, h, L"static", L"数据源 -", SS_LEFT, 14, y, 380, 20, IDC_STATUS, 1);
        // -------- 页2 过滤 --------
        y = 50;
        { const wchar_t* t[] = { L"敌人", L"NPC", L"其它物件" };
          int id[] = { IDC_ENEMY, IDC_NPC, IDC_OTHER };
          const bool* v[] = { &g_cfg.showEnemy, &g_cfg.showNpc, &g_cfg.showOther };
          int cx = 14;
          for (int i = 0; i < 3; i++) {
              b = Mk(hi, h, L"button", t[i], BS_AUTOCHECKBOX, cx, y, 104, 20, id[i], 2);
              SendMessageW(b, BM_SETCHECK, *v[i] ? BST_CHECKED : BST_UNCHECKED, 0); cx += 112;
          } y += 30; }
        Mk(hi, h, L"static", L"白名单", 0, 14, y + 3, 56, 18, 0, 2);
        g_edInc = Mk(hi, h, L"edit", L"", ES_AUTOHSCROLL | WS_BORDER, 74, y, 248, 22, IDC_EDIT_INC, 2);
        Mk(hi, h, L"button", L"应用", BS_PUSHBUTTON, 328, y - 1, 50, 24, IDC_BTN_APPLY, 2); y += 28;
        Mk(hi, h, L"static", L"黑名单", 0, 14, y + 3, 56, 18, 0, 2);
        g_edExc = Mk(hi, h, L"edit", L"", ES_AUTOHSCROLL | WS_BORDER, 74, y, 304, 22, IDC_EDIT_EXC, 2); y += 32;
        Mk(hi, h, L"button", L"全显示", BS_PUSHBUTTON, 14, y, 60, 24, IDC_BTN_SHOWALL, 2);
        Mk(hi, h, L"button", L"全隐藏", BS_PUSHBUTTON, 78, y, 60, 24, IDC_BTN_HIDEALL, 2);
        Mk(hi, h, L"static", L"点行=显式显隐（优先于一切）", 0, 156, y + 4, 230, 18, 0, 2); y += 28;
        g_list = Mk(hi, h, L"listbox", L"", LBS_NOTIFY | WS_BORDER | WS_VSCROLL, 14, y, 364, 200, IDC_LIST, 2);
        // -------- 页3 书签 --------
        y = 50;
        Mk(hi, h, L"static", L"名称", 0, 14, y + 3, 44, 18, 0, 3);
        g_edSave = Mk(hi, h, L"edit", L"", ES_AUTOHSCROLL | WS_BORDER, 62, y, 200, 22, IDC_SAVEEDIT, 3);
        g_swatch = Mk(hi, h, L"button", L"选色", BS_PUSHBUTTON, 268, y - 1, 110, 24, IDC_SWATCH, 3);
        y += 30;
        Mk(hi, h, L"button", L"添加/更新书签", BS_PUSHBUTTON, 14, y, 116, 24, IDC_BTN_ADD, 3);
        Mk(hi, h, L"button", L"改选中色", BS_PUSHBUTTON, 136, y, 84, 24, IDC_BTN_CHGC, 3);
        Mk(hi, h, L"button", L"删除选中", BS_PUSHBUTTON, 226, y, 84, 24, IDC_BTN_DEL, 3);
        y += 30;
        g_svList = Mk(hi, h, L"listbox", L"", LBS_NOTIFY | WS_BORDER | WS_VSCROLL, 14, y, 364, 196, IDC_SVLIST, 3);
        y += 202;
        Mk(hi, h, L"static", L"双击书签行 = 立即换色；名字从实体清单复制即可", 0, 14, y, 380, 18, 0, 3);
        // -------- 底部公共 --------
        Mk(hi, h, L"button", L"保存ini", BS_PUSHBUTTON, 14, 600, 90, 26, IDC_BTN_SAVEINI, -1);
        Mk(hi, h, L"button", L"重载ini", BS_PUSHBUTTON, 112, 600, 90, 26, IDC_BTN_RELOAD, -1);
        Mk(hi, h, L"static", L"F8 显隐 | END 退出", 0, 250, 604, 160, 18, 0, -1);
        EnumChildWindows(h, SetFontCb, (LPARAM)uf);
        RefreshEdits(); UpdateSwatchText(); RebuildSaves();
        SetTimer(h, IDT, 500, nullptr);
        return 0;
    }
    if (m == WM_TIMER) {
        RebuildList();
        size_t n = 0; { std::lock_guard<std::mutex> lk(g_mtx); n = g_ents.size(); }
        std::string src; { std::lock_guard<std::mutex> lk(g_srcMtx); src = g_srcText; }
        std::string att; { std::lock_guard<std::mutex> lk(g_attMtx); att = g_attachText; }
        wchar_t buf[256];
        swprintf(buf, 256, L"数据源 %s | 屏幕实体 %zu | %s",
                 u8w(src).c_str(), n, u8w(att).c_str());
        if (g_status) SetWindowTextW(g_status, buf);
        return 0;
    }
    if (m == WM_HSCROLL && l) {
        HWND src = (HWND)l;
        int id = (int)(INT_PTR)GetWindowLongPtrW(src, GWLP_ID);
        int pos = (int)SendMessageW(src, TBM_GETPOS, 0, 0);
        wchar_t buf[64];
        if (id == IDC_TR_DIST)        { g_cfg.maxDist = (float)pos;
            swprintf(buf, 64, L"显示距离 %dm", pos); SetWindowTextW(GetDlgItem(h, IDC_LBL_DIST), buf); }
        else if (id == IDC_TR_HEIGHT) { g_cfg.defHeight = pos / 10.f;
            swprintf(buf, 64, L"模型高度 %.1fm", g_cfg.defHeight); SetWindowTextW(GetDlgItem(h, IDC_LBL_HEIGHT), buf); }
        else if (id == IDC_TR_FONT)   { g_cfg.fontSize = pos; EspRebuildOverlayFont(); }
        else { g_cfg.feedFov = (float)pos;
            if (pos <= 1) swprintf(buf, 64, L"FOV(0=自动)"); else swprintf(buf, 64, L"FOV %d\u00b0", pos);
            SetWindowTextW(GetDlgItem(h, IDC_LBL_FOV), buf); }
        return 0;
    }
    if (m == WM_NOTIFY) {
        NMHDR* nm = (NMHDR*)l;
        if (nm->idFrom == IDC_TAB && nm->code == TCN_SELCHANGE) {
            int cur = (int)SendMessageW(g_tab, TCM_GETCURSEL, 0, 0);
            for (auto& c : g_ctls) if (c.page >= 0)
                ShowWindow(c.hw, (c.page == cur) ? SW_SHOW : SW_HIDE);
        }
        return 0;
    }
    if (m == WM_COMMAND) {
        int id = LOWORD(w), code = HIWORD(w);
        HWND srcw = (HWND)l;
        if (code == BN_CLICKED) {
            switch (id) {
            case IDC_ENABLE:   g_cfg.enable = Checked(srcw); break;
            case IDC_ATTACH:   g_cfg.attach = Checked(srcw); break;
            case IDC_BOXES:    g_cfg.boxes = Checked(srcw); break;
            case IDC_CORNERS:  g_cfg.corners = Checked(srcw); break;
            case IDC_NAMES:    g_cfg.names = Checked(srcw); break;
            case IDC_DISTS:    g_cfg.dists = Checked(srcw); break;
            case IDC_SNAP:     g_cfg.snap = Checked(srcw); break;
            case IDC_ENEMY:    g_cfg.showEnemy = Checked(srcw); break;
            case IDC_NPC:      g_cfg.showNpc = Checked(srcw); break;
            case IDC_OTHER:    g_cfg.showOther = Checked(srcw); break;
            case IDC_BTN_APPLY:   PullEdits(); break;
            case IDC_BTN_RELOAD:  EspConfig_Load(g_cfg, "endfield-esp.ini"); SyncFromCfg();
                                  EspRebuildOverlayFont(); RefreshEdits(); RebuildSaves(); break;
            case IDC_BTN_SAVEINI: DumpSaves(); EspConfig_Save(g_cfg, "endfield-esp.ini"); break;
            case IDC_SWATCH: case IDC_BTN_CHGC: PickColor(); return 0;
            case IDC_BTN_ADD:   AddOrUpdateSave(); return 0;
            case IDC_BTN_DEL:   DelSaveSel(); return 0;
            case IDC_BTN_SHOWALL: case IDC_BTN_HIDEALL: {
                std::lock_guard<std::mutex> lk(g_obsMtx);
                for (auto& kv : g_obs) { kv.second.explicitSet = true;
                                         kv.second.vis = (id == IDC_BTN_SHOWALL); }
                break; }
            }
            RebuildList();
            return 0;
        }
        if (id == IDC_LIST && code == LBN_SELCHANGE) {
            int sel = (int)SendMessageW(g_list, LB_GETCURSEL, 0, 0);
            if (sel >= 0 && sel < (int)g_rowKeys.size()) {
                std::string key = g_rowKeys[sel];
                std::lock_guard<std::mutex> lk(g_obsMtx);
                Obs& o = g_obs[key];
                bool cur = shown(o, key);
                o.explicitSet = true; o.vis = !cur;
            }
            return 0;
        }
        if (id == IDC_SVLIST && code == LBN_DBLCLK) { ChangeSelSaveColor(); return 0; }
    }
    if (m == WM_CLOSE) { ShowWindow(h, SW_HIDE); return 0; }
    return DefWindowProcW(h, m, w, l);
}

inline void Create(HINSTANCE hi) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = Proc; wc.hInstance = hi; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); wc.lpszClassName = L"espui_cfg";
    RegisterClassW(&wc);
    float sc = 1.0f;
    if (HMODULE u = GetModuleHandleW(L"user32.dll")) {
        typedef UINT(WINAPI*GDFS)(void);
        auto f = (GDFS)GetProcAddress(u, "GetDpiForSystem");
        if (f) { UINT dpi = f(); if (dpi >= 96) sc = dpi / 96.f; }
    }
    g_hMain = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, wc.lpszClassName,
        L"Endfield ESP 控制台（F8 显隐）", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        40, 50, (int)(418 * sc), (int)(700 * sc), nullptr, nullptr, hi, nullptr);
    ShowWindow(g_hMain, SW_SHOW);
}
inline void Toggle() {
    if (g_hMain) ShowWindow(g_hMain, IsWindowVisible(g_hMain) ? SW_HIDE : SW_SHOW);
}

} // namespace espui
