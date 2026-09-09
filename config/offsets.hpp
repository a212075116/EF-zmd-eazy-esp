// ============================================================================
//  offsets.hpp —— ⑤ 实体字段已换成 1.5 真机 dump 实测值（@desirepro 注入式 dumper 产出）
//  ②静态 RVA：需要 dump 里 EntityManager.s_manager 的 static 基址，注入通道拿到后再填
//  RPM 被内核剥权期间：用 ini `dump = 目录` 走 FileFeed 模式（零内存读取）
// ============================================================================
#pragma once
#include <cstdint>
#include <cstddef>

#define CHAIN_N(a) (sizeof(a) / sizeof((a)[0]))

namespace off {

// ① 实测(proclist)：主进程 = Endfield.exe。也可在 ini 用 proc=/module= 覆盖，免重编译
inline constexpr const wchar_t* ProcName  = L"Endfield.exe";
inline constexpr const char*    ProcNameA = "Endfield.exe";
inline constexpr const char*    ModuleName = "GameAssembly.dll";

// 实测(mapname)：GameAssembly.dll 常驻 247.8MB(0xF7CC000)。基址 ASLR 每次变，attach 自动恢复
inline constexpr uintptr_t ManualModuleBase = 0;

// ② 管理器静态 RVA —— 待注入通道读出 EntityManager 类 static 区地址后填（0=未配置）
inline constexpr uintptr_t EntityList_RVA  = 0;
inline constexpr uintptr_t ViewMatrix_RVA  = 0;
inline constexpr uintptr_t CamFov_RVA      = 0;

// ③ 静态区→实体表 的指针链（同上，未配置）
inline constexpr uintptr_t EntityList_Chain[] = { 0 };
inline constexpr uintptr_t ViewMatrix_Chain[] = { 0 };
inline constexpr uintptr_t CamFov_Chain[]     = { 0 };

// ④ il2cpp 容器固定布局（不用改）
inline constexpr uintptr_t List_items   = 0x10;
inline constexpr uintptr_t List_size    = 0x18;
inline constexpr uintptr_t Array_data   = 0x20;
inline constexpr uintptr_t PtrStep      = 8;
inline constexpr uintptr_t Str_length   = 0x10;
inline constexpr uintptr_t Str_chars    = 0x14;

// ⑤ 实体字段 —— 全部 1.5 dump 实测（IL2CPP_Dump_AI/Gameplay.Beyond.dll.cs）
//    真实链（未来驱动/DMA 通道按此走）：
//      EntityManager.s_manager(static@0x8) -> m_dataStorage 0x88
//        -> EntityDataStorage.m_containers 0x10 (Il2CppArray*, 按 EntityDataType 分桶, Enemy=2)
//        -> DataContainer.m_dataDict 0x10 (Dictionary<ul64,BaseEntityData>)
//      或 m_nodes 0xD0 (DynamicFastLookupCollection) -> EntityNode:
//        EntityNode.entity 0x48 / EntityNode.data 0x50 / EntityNode.m_isDead 0x29
//      BaseEntityData(SIZE 0xF0): id 0x60 / templateId(String) 0x68
//        bornPos 0x74 / position(Vector3) 0x8C / rotation 0x98
//    EntityDataType: Invalid=0 Character=1 Enemy=2 Interactive=3 Npc=4 AbilityEntity=5
inline constexpr uintptr_t Ent_Pos       = 0x8C;   // BaseEntityData.position（注意：先到 data 节点）
inline constexpr uintptr_t Ent_Name      = 0x68;   // BaseEntityData.templateId (Il2CppString*)
inline constexpr uintptr_t Ent_Height    = 0;      // dump 无身高字段 → 用 EspConfig.defHeight
inline constexpr uintptr_t Ent_TypeId    = 0;      // 类型由所在 container 桶号决定，非字段

// ⑤½ 实时模式（首选）：dumper 里按 F4 → 命名节 "Local\EndfieldEsp"
//     60Hz：EntityManager.s_manager→m_nodes→EntityNode.data→position(全部运行时按名推导)
//     ini `shm = false` 可关闭回落。协议 src/shm.hpp ↔ Dump/include/esp_feed.hxx

// ⑥ FileFeed 模式（当前唯一可用通道，RPM 被剥权）：
//    ini 填 `dump = dumper输出目录` → ESP 解析 IL2CPP_World_Dump_*.txt + IL2CPP_Camera_*.txt
//    相机 = 相机文件 或 世界快照 tag="MainCamera" 的 pos+quat（FOV 实测 47°）
//    敌人过滤 = ini `include = enemy;monster;boss`（战斗图拿到真命名后填）

inline constexpr const char* ViewMatrixSig = "";
inline constexpr int MaxEntities = 4096;

} // namespace off
