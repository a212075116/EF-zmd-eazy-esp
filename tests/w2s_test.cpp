// W2S 数学层冒烟测试（平台无关，可在任意 C++17 环境编译运行）
// 编译运行: g++ -std=c++17 -Wall -I . tests/w2s_test.cpp -o /tmp/w2st && /tmp/w2st
#include "src/esp.hpp"
#include "config/offsets.hpp"
#include <cassert>
#include <cstdio>
#include <cmath>

int main() {
    // 相机位于 (10,0,0) 朝 -Z（Unity 左手系, view = 无旋转 + 平移[-cam]）
    Mat4 v{};
    v.m[0][0] = v.m[1][1] = v.m[2][2] = v.m[3][3] = 1.f;
    v.m[0][3] = -10.f;
    const int W = 1920, H = 1080;
    Vec2 s;

    // 1) 与相机重合的点深度≈0 → 应被近平面剔除
    assert(!W2SView(v, 60.f, Vec3{10, 0, 0}, W, H, s));
    // 2) 正前方 5m → 屏幕中心
    assert(W2SView(v, 60.f, Vec3{10, 0, -5}, W, H, s));
    assert(std::abs(s.x - W/2) < 1 && std::abs(s.y - H/2) < 1);
    // 3) 相机后方 → 剔除
    assert(!W2SView(v, 60.f, Vec3{10, 0, +5}, W, H, s));
    // 4) 深度 1m 处横偏 1m：offset = k = (H/2)/tan30° ≈ 935.3px
    assert(W2SView(v, 60.f, Vec3{11, 0, -1}, W, H, s));
    printf("1m offset @1m depth: %.1fpx (expect ~935.3)\n", s.x - W/2);
    assert(std::abs((s.x - W/2) - 935.3f) < 2.f);
    //    深度 5m 处横偏 1m → 935.3/5 ≈ 187.1px（透视收缩）
    assert(W2SView(v, 60.f, Vec3{11, 0, -5}, W, H, s));
    printf("1m offset @5m depth: %.1fpx (expect ~187.1)\n", s.x - W/2);
    assert(std::abs((s.x - W/2) - 187.1f) < 2.f);
    // 5) CamPosFromView 逆推相机位置 = (10,0,0)
    Vec3 cam = CamPosFromView(v);
    assert(std::abs(cam.x - 10.f) < 0.01f && std::abs(cam.z) < 0.01f);
    // 6) Clip 模式：标准 GL 透视（列主序 m[col][row]），中心点与后方剔除
    Mat4 p{}; float f = 1.f / std::tanf(30.f * 3.14159265f / 180.f), aspect = (float)W / H;
    p.m[0][0] = f / aspect; p.m[1][1] = f; p.m[2][3] = -1.f;   // clipW = -z
    assert(W2SClip(p, Vec3{0, 0, -2}, W, H, s));
    assert(std::abs(s.x - W/2) < 1 && std::abs(s.y - H/2) < 1);
    assert(!W2SClip(p, Vec3{0, 0, +2}, W, H, s));
    //    横偏 1m @2m：ndcX = (f/aspect)/2 = 0.487 → 960+0.487*960 ≈ 1427.5
    assert(W2SClip(p, Vec3{1, 0, -2}, W, H, s));
    printf("clip offset: %.1fpx (expect ~1427.5)\n", s.x);
    assert(std::abs(s.x - 1427.5f) < 2.f);

    puts("W2S math: all assertions passed");
    return 0;
}
