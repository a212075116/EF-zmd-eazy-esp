// ============================================================================
//  config.hpp —— 运行期热加载配置：endfield-esp.ini 与 exe 同目录
//  格式 key = value，# 注释；改参数不用重编译
// ============================================================================
#pragma once
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <string>
#include "esp.hpp"

static inline std::string trimV(std::string s) {
    auto h = s.find('#'); if (h != std::string::npos) s = s.substr(0, h);
    size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

inline void EspConfig_Load(EspConfig& c, const char* path) {
    std::ifstream f(path);
    if (!f) return;                                   // 无 ini 用默认值
    c.saves.clear();
    std::string line;
    auto valOf = [](const std::string& l) { return l.substr(l.find('=') + 1); };
    while (std::getline(f, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq);
        auto gb = k.find_first_not_of(" \t#"); if (gb != std::string::npos) k = k.substr(gb);
        auto ge = k.find_last_not_of(" \t");   if (ge != std::string::npos) k.erase(ge + 1);
        if (k.empty() || k[0] == '#') continue;
        auto v = valOf(line);
        if      (k == "enable")   c.enable  = v.find("true")  != std::string::npos;
        else if (k == "boxes")    c.boxes   = v.find("true")  != std::string::npos;
        else if (k == "corners")  c.corners = v.find("true")  != std::string::npos;
        else if (k == "names")    c.names   = v.find("true")  != std::string::npos;
        else if (k == "dists")    c.dists   = v.find("true")  != std::string::npos;
        else if (k == "snap")     c.snap    = v.find("true")  != std::string::npos;
        else if (k == "shm")      c.useShm  = v.find("false") == std::string::npos;
        else if (k == "maxDist")  c.maxDist        = (float)std::atof(v.c_str());
        else if (k == "defHeight") c.defHeight     = (float)std::atof(v.c_str());
        else if (k == "boxWidth")  c.boxWidthRatio = (float)std::atof(v.c_str());
        else if (k == "mode")      c.mode = v.find("clip") != std::string::npos
                                                ? W2SMode::Clip : W2SMode::View;
        else if (k == "proc")   { strncpy(c.procName,   trimV(v).c_str(), 63); }
        else if (k == "module") { strncpy(c.moduleName, trimV(v).c_str(), 63); }
        else if (k == "dump")     { snprintf(c.dumpDir, sizeof c.dumpDir, "%s", trimV(v).c_str()); }
        else if (k == "include")  { snprintf(c.feedInclude, sizeof c.feedInclude, "%s", trimV(v).c_str()); }
        else if (k == "feedMs")   { int x = atoi(v.c_str()); if (x >= 200) c.feedMs = x; }
        else if (k == "feedFov")  { float x = (float)atof(v.c_str()); if (x > 1 && x < 179) c.feedFov = x; }
        else if (k == "showEnemy") c.showEnemy = v.find("true") != std::string::npos;
        else if (k == "showNpc")   c.showNpc   = v.find("true") != std::string::npos;
        else if (k == "showOther") c.showOther = v.find("true") != std::string::npos;
        else if (k == "exclude")  { snprintf(c.exclude, sizeof c.exclude, "%s", trimV(v).c_str()); }
        else if (k == "attach")     c.attach  = v.find("false") == std::string::npos;
        else if (k == "fontSize") { int x = atoi(v.c_str()); if (x >= 8 && x <= 48) c.fontSize = x; }
        else if (k == "save")     { std::string sv = trimV(v); if (!sv.empty()) c.saves.push_back(sv); }
    }
}

// GUI「保存ini」：把当前全部可调项写回（注释头保留一份说明）
inline bool EspConfig_Save(const EspConfig& c, const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    fprintf(f, "# endfield-esp.ini —— GUI 保存于此刻；手改后 F7/GUI重载ini 生效\n"
               "# shm=true 用 dumper F4 实时流；dump= 有值则 FileFeed 兜底；都要留一\n");
    fprintf(f, "enable  = %s\n", c.enable  ? "true" : "false");
    fprintf(f, "boxes   = %s\n", c.boxes   ? "true" : "false");
    fprintf(f, "corners = %s\n", c.corners ? "true" : "false");
    fprintf(f, "names   = %s\n", c.names   ? "true" : "false");
    fprintf(f, "dists   = %s\n", c.dists   ? "true" : "false");
    fprintf(f, "snap    = %s\n", c.snap    ? "true" : "false");
    fprintf(f, "shm     = %s\n", c.useShm  ? "true" : "false");
    fprintf(f, "maxDist = %.0f\n", c.maxDist);
    fprintf(f, "defHeight = %.1f\n", c.defHeight);
    fprintf(f, "boxWidth = %.2f\n", c.boxWidthRatio);
    fprintf(f, "feedMs = %d\n", c.feedMs);
    fprintf(f, "feedFov = %.0f\n", c.feedFov);
    fprintf(f, "showEnemy = %s\n", c.showEnemy ? "true" : "false");
    fprintf(f, "showNpc   = %s\n", c.showNpc   ? "true" : "false");
    fprintf(f, "showOther = %s\n", c.showOther ? "true" : "false");
    fprintf(f, "proc   = %s\n", c.procName);
    fprintf(f, "module = %s\n", c.moduleName);
    fprintf(f, "dump = %s\n", c.dumpDir);
    fprintf(f, "include = %s\n", c.feedInclude);
    fprintf(f, "exclude = %s\n", c.exclude);
    fprintf(f, "fontSize = %d\n", c.fontSize);
    fprintf(f, "attach = %s\n", c.attach ? "true" : "false");
    for (const std::string& sv : c.saves) fprintf(f, "save = %s\n", sv.c_str());
    fclose(f);
    return true;
}
