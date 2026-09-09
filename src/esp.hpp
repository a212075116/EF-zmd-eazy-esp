// ============================================================================
//  esp.hpp —— 数学层：两种投影模式 + 实体结构 + 运行期配置
// ============================================================================
#pragma once
#include <string>
#include <vector>
#include <cmath>
#include <cstdint>
#include <string>

struct Vec2 { float x = 0, y = 0; };
struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3 operator+(const Vec3& o) const { return { x + o.x, y + o.y, z + o.z }; }
    Vec3 operator-(const Vec3& o) const { return { x - o.x, y - o.y, z - o.z }; }
    float len() const { return std::sqrtf(x * x + y * y + z * z); }
};
struct Vec4 { float x = 0, y = 0, z = 0, w = 0; };   // 四元数 (x,y,z,w)
struct Mat4 { float m[4][4] = {}; };   // 行优先

enum class W2SMode { View, Clip };     // worldToCamera / worldToClip(URP VP)

struct EspConfig {
    bool  enable = true, menu = true;
    bool  boxes = true, names = true, dists = true, snap = false, corners = true;
    float maxDist = 200.0f;
    float defHeight = 1.8f;             // Ent_Height 未配置时的身长（米）
    float boxWidthRatio = 0.42f;
    W2SMode mode = W2SMode::View;
    bool  fontChinese = true;
    bool  useShm = true;             // ini: shm = false 可关共享内存实时feed
    // GUI/类型过滤开关（palette 类：1=敌人 3=NPC 7=其它）
    bool showEnemy = true, showNpc = true, showOther = false;
    char exclude[256] = "";           // GUI 黑名单（分号子串，优先于白名单）
    int  fontSize = 14;
    bool attach = true;            // 覆盖层贴合游戏窗口客户区（非整桌面）               // 覆盖层文字字号（像素高）
    std::vector<std::string> saves;   // 收藏: "name,r,g,b"（GUI 书签页）
    char  procName[64]   = {};          // ini: proc = Endfield.exe（非空则覆盖 offsets.hpp）
    char  moduleName[64] = {};          // ini: module = GameAssembly.dll
    char  dumpDir[260]   = {};          // ini: dump = 目录 → FileFeed 模式（读 dumper 文件，不碰 RPM）
    char  feedInclude[256] = {};        // ini: include = enemy;monster;boss（小写子串分号分隔；空=skinned 全选）
    int   feedMs         = 1500;        // ini: feedMs = 重新读文件间隔
    float feedFov        = 0;           // ini: feedFov = 47（0=用相机文件/内建值）
    uint32_t palette[8] = {
        0xFFFFFFFF, 0xFF4B4BE8 /*敌人红*/, 0xFF3CDC3C /*资源绿*/, 0xFFE8C832 /*NPC黄*/,
        0xFFB450E6, 0xFF40E0D0, 0xFF808080, 0xFFD0D0D0,
    }; // ABGR(ImGui)
};

struct EspEntry {
    Vec3  pos;          // 世界坐标（脚底/根）
    Vec2  head, feet;   // 屏幕坐标
    float dist = 0;
    int   type = 0;
    std::string name;
    bool  vis = false;
};

inline bool finiteV3(const Vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// Unity worldToCamera：行主序 4x4，相机空间 -Z 为前方，y-up
inline bool W2SView(const Mat4& v, float fovVertDeg, const Vec3& w, int W, int H, Vec2& out) {
    float cx = w.x * v.m[0][0] + w.y * v.m[0][1] + w.z * v.m[0][2] + v.m[0][3];
    float cy = w.x * v.m[1][0] + w.y * v.m[1][1] + w.z * v.m[1][2] + v.m[1][3];
    float cz = w.x * v.m[2][0] + w.y * v.m[2][1] + w.z * v.m[2][2] + v.m[2][3];
    float depth = -cz;
    if (depth < 0.15f) return false;
    float k = (float)H / (2.0f * std::tanf(fovVertDeg * 0.0174532925199433f * 0.5f));
    out.x = W * 0.5f + cx * k / depth;
    out.y = H * 0.5f - cy * k / depth;
    return out.x > -2000 && out.x < W + 2000 && out.y > -2000 && out.y < H + 2000;
}

// worldToClip（若 dump 出的矩阵已是 VP 变换，用这个）
inline bool W2SClip(const Mat4& p, const Vec3& w, int W, int H, Vec2& out) {
    float x = w.x * p.m[0][0] + w.y * p.m[1][0] + w.z * p.m[2][0] + p.m[3][0];
    float y = w.x * p.m[0][1] + w.y * p.m[1][1] + w.z * p.m[2][1] + p.m[3][1];
    float ww = w.x * p.m[0][3] + w.y * p.m[1][3] + w.z * p.m[2][3] + p.m[3][3];
    if (ww < 0.1f) return false;
    out.x = (1.0f + x / ww) * 0.5f * W;
    out.y = (1.0f - y / ww) * 0.5f * H;
    return out.x > -2000 && out.x < W + 2000 && out.y > -2000 && out.y < H + 2000;
}

// 相机世界原点 = -R^T * t（只需 view 矩阵，省一个偏移）
inline Vec3 CamPosFromView(const Mat4& v) {
    float tx = v.m[0][3], ty = v.m[1][3], tz = v.m[2][3];
    return { -(v.m[0][0] * tx + v.m[1][0] * ty + v.m[2][0] * tz),
             -(v.m[0][1] * tx + v.m[1][1] * ty + v.m[2][1] * tz),
             -(v.m[0][2] * tx + v.m[1][2] * ty + v.m[2][2] * tz) };
}
