// ============================================================================
//  shm_proto.hpp —— 端游实时 feed 的共享内存协议（读取侧）
//  与 dumper 内 Dump/include/esp_feed.hxx 的写入侧逐字段镜像；改一边必须改两边！
//  命名节："Local\EndfieldEsp"（由游戏内 feed 线程 CreateFileMapping 创建）
//  seqlock：seq 奇数=写入中；读者校验 seq 前后一致才采信
// ============================================================================
#pragma once
#include <cstdint>
#include <cstring>
#include <windows.h>
#include <string>
#include <vector>

namespace shm {

static const uint32_t MAGIC = 0x31534645u;   // "EFS1"
static const uint32_t VER   = 1;
static const int      MAXE  = 2048;

struct Header {
    uint32_t      magic, ver;
    volatile LONG seq;
    LONG          count;          // LONG on both sides (same width, protocol unchanged)
    volatile LONG alive;
    int32_t       camOk;
    float         camPos[3], camFwd[3], camRight[3], camUp[3];
    float         fov;
    float         charPos[3];
    uint32_t      pad0;
    ULONGLONG     tick;
};
struct Rec {
    ULONGLONG     id;
    int32_t       type;            // 0 other | 1 enemy | 2 npc | 3 character
    float         pos[3];
    char          name[48];        // UTF-8 display name (BaseTemplateData.name，回退 templateId)
};
struct Block { Header h; Rec ents[MAXE]; };

static_assert(sizeof(Header) % 8 == 0, "Header alignment drifted");
static_assert(sizeof(Rec) == 8 + 4 + 12 + 48, "Rec size drifted");

// ------------------------------- reader ------------------------------------
class Reader {
public:
    bool open() {
        if (blk_) return true;
        HANDLE h = OpenFileMappingA(FILE_MAP_READ, FALSE, "Local\\EndfieldEsp");
        if (!h) return false;
        void* v = MapViewOfFile(h, FILE_MAP_READ, 0, 0, 0);
        CloseHandle(h);                       // 映射持有引用
        if (!v) return false;
        auto* b = (const Block*)v;
        if (b->h.magic != MAGIC || b->h.ver != VER) { UnmapViewOfFile(v); return false; }
        blk_ = b;
        return true;
    }
    void close() { if (blk_) { UnmapViewOfFile((void*)blk_); blk_ = nullptr; } }
    bool opened() const { return blk_ != nullptr; }

    // 拷贝一帧（seqlock 双读校验，重试 <=3 次）；false=拿不到稳定帧
    bool grab(Header& hd, std::vector<Rec>& ents) {
        if (!blk_) return false;
        for (int tryn = 0; tryn < 3; tryn++) {
            LONG s1 = blk_->h.seq;
            if (s1 & 1) { Sleep(1); continue; }                       // 写者临界
            ReadBarrier();
            if (blk_->h.count < 0 || blk_->h.count > MAXE) { Sleep(1); continue; }
            hd = blk_->h;
            ents.assign(blk_->ents, blk_->ents + hd.count);
            ReadBarrier();
            if (s1 == blk_->h.seq) return true;                        // 帧稳定
        }
        return false;
    }
    bool fresh(const Header& hd, DWORD maxAgeMs = 1000) const {
        return hd.alive && hd.count >= 0 && (GetTickCount64() - hd.tick) <= maxAgeMs;
    }

private:
    static void ReadBarrier() { MemoryBarrier(); }
    const Block* blk_ = nullptr;
};

} // namespace shm
