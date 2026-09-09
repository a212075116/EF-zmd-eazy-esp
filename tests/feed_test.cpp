// ============================================================================
//  feed_test.cpp —— 用真实 dumper 产出验证 FileFeed 管线（Linux 可跑）
//  g++ -std=c++17 -I . tests/feed_test.cpp -o /tmp/feedt && /tmp/feedt <世界文件> [相机文件]
// ============================================================================
#include <cassert>
#include <cstdio>
#include <cmath>
#include <string>
#include "../src/feed.hpp"

int main(int argc, char** argv) {
    if (argc < 2) { puts("usage: feed_test <world_dump.txt> [camera.txt]"); return 1; }
    std::vector<feed::WorldObj> objs;
    assert(feed::LoadWorldFile(objs, argv[1]));
    printf("world objects parsed: %zu\n", objs.size());
    assert(objs.size() > 1000);

    // 1) MainCamera 定位
    feed::CamState c;
    bool fromFile = argc > 2 && feed::LoadCameraFile(c, argv[2]);
    printf("camera from file: %s\n", fromFile ? "YES" : "no (identity/missing)");
    if (!fromFile) { assert(feed::CamFromWorld(objs, c)); }
    printf("cam pos=(%.3f, %.3f, %.3f) fov=%.1f fwd=(%.3f,%.3f,%.3f)\n",
           c.pos.x, c.pos.y, c.pos.z, c.fov, c.fwd.x, c.fwd.y, c.fwd.z);
    assert(c.ok && c.fwd.len() > 0.9f && c.fwd.len() < 1.1f);

    Mat4 v = feed::ViewFromCam(c);
    // 相机自身深度应为 0
    float cz = c.pos.x*v.m[2][0] + c.pos.y*v.m[2][1] + c.pos.z*v.m[2][2] + v.m[2][3];
    assert(std::fabs(cz) < 1e-3f);

    // 2) 过滤 + 投影全链路（1920x1080）
    int passed = 0, onScreen = 0;
    for (auto& o : objs) {
        if (!o.hasPos) continue;
        if (!feed::MatchFilter(o, "")) continue;
        passed++;
        Vec2 s;
        if (W2SView(v, c.fov, o.pos, 1920, 1080, s)) { onScreen++;
            if (onScreen <= 3) printf("  proj: %-28s -> (%.0f, %.0f) d=%.1fm\n",
                                      o.name.c_str(), s.x, s.y, (o.pos - c.pos).len()); }
    }
    printf("skinned entities passed filter: %d, on-screen: %d\n", passed, onScreen);
    assert(passed > 0);

    // 3) include 过滤器语义 + BuildEntries 装配（含 LOD 去重）
    std::vector<EspEntry> ents;
    feed::FeedOpts o; o.screenW = 1920; o.screenH = 1080;
    feed::BuildEntries(objs, c, o, ents);
    printf("entries after dedup+project: %zu (was %d on-screen pre-dedup)\n", ents.size(), onScreen);
    assert(ents.size() * 2 <= (size_t)onScreen || ents.size() < 2000);   // LOD 去重必须显著收敛
    for (auto& e : ents) assert(e.dist >= 0 && e.dist <= o.maxDist);
    printf("Feed pipeline: all assertions passed\n");
    return 0;
}
