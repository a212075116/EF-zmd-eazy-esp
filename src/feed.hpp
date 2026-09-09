// ============================================================================
//  feed.hpp —— FileFeed 数据源：解析注入式 dumper 的产出文件
//    IL2CPP_World_Dump_*.txt ：全场景 @go 对象（id/name/pos/rot/parent/comp）
//    IL2CPP_Camera_*.txt     ：主相机 pos+forward/right/up+fov
//  意义：外部 ReadProcessMemory 被内核 AC 剥权时，数据面走文件桥照样出 ESP。
//  纯 C++，无 Win32 —— 可在任何平台单测。
// ============================================================================
#pragma once
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <cctype>
#include "esp.hpp"

namespace feed {

struct WorldObj {
    long long     id = 0, parentId = 0;
    std::string   name, tag;
    bool          active = true;
    bool          hasPos = false, hasRot = false;
    Vec3          pos{};
    Vec4          quat{};                       // (x,y,z,w)
    bool          skinned = false, isCamera = false, isMainCam = false;
};

struct CamState {
    Vec3  pos{}, fwd{}, right{}, up{};
    float fov = 0.f;
    bool  ok = false;
};

// ---------- 小工具 ----------
inline std::string lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }

inline bool parseV3(const char* s, Vec3& v) {
    return sscanf(s, "(%f, %f, %f)", &v.x, &v.y, &v.z) == 3;
}
inline bool parseV4(const char* s, Vec4& v) {
    return sscanf(s, "(%f, %f, %f, %f)", &v.x, &v.y, &v.z, &v.w) == 4;
}
inline bool extractQuoted(const char* line, const char* key, std::string& out) {
    const char* p = strstr(line, key);
    if (!p) return false;
    p += strlen(key);
    const char* a = strchr(p, '"'); if (!a) return false; ++a;
    const char* b = strchr(a, '"'); if (!b) return false;
    out.assign(a, b); return true;
}

// ---------- 世界文件 ----------
inline bool LoadWorldFile(std::vector<WorldObj>& objs, const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    objs.clear();
    char line[2048];
    WorldObj* cur = nullptr;
    while (fgets(line, sizeof line, f)) {
        if (!strncmp(line, "@go ", 4)) {
            objs.push_back(WorldObj{});
            cur = &objs.back();
            char idb[32] = {}; sscanf(line, "@go id=%31s", idb);
            cur->id = strtoll(idb, nullptr, 10);
            extractQuoted(line, "name=", cur->name);
            extractQuoted(line, "tag=",  cur->tag);
            cur->active = strstr(line, "active=true") != nullptr;
            cur->isMainCam = (cur->tag == "MainCamera" || cur->name == "MainCamera");
            continue;
        }
        if (!cur) continue;
        const char* t = line; while (*t == ' ' || *t == '\t') ++t;
        if (!strncmp(t, "pos=(", 5))            cur->hasPos = parseV3(t + 4, cur->pos);
        else if (!strncmp(t, "rot=(", 5))       cur->hasRot = parseV4(t + 4, cur->quat);
        else if (!strncmp(t, "parent_id=", 10)) cur->parentId = strtoll(t + 10, nullptr, 10);
        else if (strstr(t, "SkinnedMeshRenderer")) cur->skinned = true;
        else if (strstr(t, "comp \"UnityEngine.Camera\"")) cur->isCamera = true;
    }
    fclose(f);
    return true;
}

// ---------- 相机文件 ----------
inline bool LoadCameraFile(CamState& c, const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    c = CamState{};
    char line[512];
    while (fgets(line, sizeof line, f)) {
        const char* t = line; while (*t == ' ') ++t;
        if (!strncmp(t, "pos=", 4))            parseV3(t + 4, c.pos);
        else if (!strncmp(t, "forward=", 8))   parseV3(t + 8, c.fwd);
        else if (!strncmp(t, "right=", 6))     parseV3(t + 6, c.right);
        else if (!strncmp(t, "up=", 3))        parseV3(t + 3, c.up);
        else if (!strncmp(t, "fov_deg", 7))    sscanf(strchr(t, '='), "= %f", &c.fov);
    }
    fclose(f);
    // identity 快照（未进场/菜单态）视为不可用
    bool identity = c.pos.len() < 0.01f && c.fwd.z > 0.999f && c.right.x > 0.999f;
    c.ok = !identity && c.fwd.len() > 0.5f && c.fov > 1.f;
    return c.ok;
}

// 四元数(x,y,z,w) → 相机三轴（Unity：local->world 旋转的列）
inline void QuatToAxes(const Vec4& q, Vec3& right, Vec3& up, Vec3& fwd) {
    auto x = q.x, y = q.y, z = q.z, w = q.w;
    right = { 1 - 2 * (y * y + z * z), 2 * (x * y - z * w),       2 * (x * z + y * w) };
    up    = { 2 * (x * y + z * w),     1 - 2 * (x * x + z * z),   2 * (y * z - x * w) };
    fwd   = { 2 * (x * z - y * w),     2 * (y * z + x * w),       1 - 2 * (x * x + y * y) };
}

// 从世界文件里的 MainCamera 构造相机
inline bool CamFromWorld(std::vector<WorldObj>& objs, CamState& c) {
    for (auto& o : objs) {
        if (o.isMainCam && o.hasPos && o.hasRot) {
            c.pos = o.pos;
            QuatToAxes(o.quat, c.right, c.up, c.fwd);
            if (!c.fov || c.fov < 1) c.fov = 47.f;         // Endfield 主摄实测 47°
            c.ok = true;
            return true;
        }
    }
    return false;
}

// world->view 矩阵（行优先；与 W2SView 约定一致：cz 行 = -forward）
inline Mat4 ViewFromCam(const CamState& c) {
    Mat4 v{};
    v.m[0][0] = c.right.x;  v.m[0][1] = c.right.y;  v.m[0][2] = c.right.z;
    v.m[1][0] = c.up.x;     v.m[1][1] = c.up.y;     v.m[1][2] = c.up.z;
    v.m[2][0] = -c.fwd.x;   v.m[2][1] = -c.fwd.y;   v.m[2][2] = -c.fwd.z;
    v.m[0][3] = -(c.right.x * c.pos.x + c.right.y * c.pos.y + c.right.z * c.pos.z);
    v.m[1][3] = -(c.up.x * c.pos.x + c.up.y * c.pos.y + c.up.z * c.pos.z);
    v.m[2][3] =  (c.fwd.x * c.pos.x + c.fwd.y * c.pos.y + c.fwd.z * c.pos.z);
    v.m[3][3] = 1.f;
    return v;
}

// include = 分号分隔小写子串；返回 true 表示入选（filter 为空则取 skinned+active）
inline bool MatchFilter(const WorldObj& o, const std::string& includeLower) {
    if (o.isMainCam || o.tag == "MainCamera") return false;
    std::string n = lower(o.name);
    if (includeLower.empty())
        return o.skinned && o.active;
    size_t a = 0;
    while (a < includeLower.size()) {
        size_t b = includeLower.find(';', a); if (b == std::string::npos) b = includeLower.size();
        if (b - a > 0 && n.find(includeLower.substr(a, b - a)) != std::string::npos) return true;
        a = b + 1;
    }
    return false;
}

// "_lod3"/"(Clone)" 归一化 → 同一实体的多级 LOD/克隆实例只保留一个框
inline std::string BaseName(const std::string& raw) {
    std::string s = raw;
    size_t c = s.find("(Clone)"); if (c != std::string::npos) s.erase(c, 7);
    std::string l = lower(s);
    size_t p = l.rfind("_lod");
    if (p != std::string::npos) {
        size_t q = p + 4; while (q < s.size() && isdigit((unsigned char)s[q])) ++q;
        if (q == s.size() || s[q] == '.') s.erase(p, q - p);      // 尾部的 _lodN 才剥
    }
    return s;
}

struct FeedOpts {
    std::string include;          // 已 lower 的过滤串
    float maxDist = 200.f, defHeight = 1.8f, fovOverride = 0.f;
    int   screenW = 1920, screenH = 1080;
};

inline void BuildEntries(std::vector<WorldObj>& objs, const CamState& c,
                         const FeedOpts& o, std::vector<EspEntry>& out) {
    Mat4 v = ViewFromCam(c);
    float fov = (o.fovOverride > 1.f) ? o.fovOverride : (c.fov > 1.f ? c.fov : 47.f);
    std::vector<std::string> seen;
    for (auto& ob : objs) {
        if (!ob.active || !ob.hasPos || !MatchFilter(ob, o.include)) continue;
        std::string base = BaseName(ob.name);
        char key[192];
        snprintf(key, sizeof key, "%s|%.2f,%.2f,%.2f", base.c_str(),
                 ob.pos.x * 8, ob.pos.y * 8, ob.pos.z * 8);
        bool dup = false;
        for (auto& k : seen) if (k == key) { dup = true; break; }
        if (dup) continue;
        if (seen.size() < 8192) seen.emplace_back(key);

        EspEntry e;
        e.pos = ob.pos;
        e.name = ob.name;
        e.dist = (ob.pos - c.pos).len();
        if (e.dist > o.maxDist) continue;
        e.type = ob.skinned ? 1 : 0;
        Vec3 headW = ob.pos + Vec3{ 0.f, o.defHeight, 0.f };
        e.vis = W2SView(v, fov, ob.pos, o.screenW, o.screenH, e.feet)
             && W2SView(v, fov, headW,  o.screenW, o.screenH, e.head);
        if (!e.vis) continue;
        out.push_back(std::move(e));
    }
}

} // namespace feed
