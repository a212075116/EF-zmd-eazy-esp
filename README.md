# endfield-esp — 外部 Unity/IL2CPP ESP 框架

纯外部实现：`ReadProcessMemory` 读实体链 + 视图矩阵投影 + 分层窗口覆盖层。零注入、零 hook，改偏移即可跨版本复用。
配置全部集中在 `config/offsets.hpp`，当前为示例占位值，需用下面的流程从你手上的客户端 dump 后替换。

## 目录结构

```
endfield-esp/
├── CMakeLists.txt
├── config/
│   ├── offsets.hpp       ← 所有偏移/指针链/特征码
│   └── endfield-esp.ini  ← 运行期参数
├── src/
│   ├── memory.hpp        ← RPM 封装、指针链、il2cpp String 解码、特征码扫描
│   ├── esp.hpp           ← Vec/Mat、WorldToScreen
│   ├── config.hpp        ← ini 解析
│   ├── main.cpp          ← 读线程(~160Hz) + GDI 覆盖层(~120fps)
│   └── probe_main.cpp    ← probe.exe 偏移定位工具(矩阵扫描/链验证/实体解读/特征码)
├── tools/
│   └── find_offsets.py   ← 从 Il2CppDumper 的 dump.cs 自动挖候选偏移
└── tests/
    ├── w2s_test.cpp      ← 投影测试
    └── sample_dump.cs    ← find_offsets.py 自测样例
```

## 编译

PowerShell：
```powershell
cd endfield-esp
cmake -S . -B build -A x64
cmake --build build --config Release
```
产物：`build/Release/endfield_esp.exe`（覆盖层）+ `build/Release/probe.exe`（偏移定位）。需要 VS2022「使用 C++ 的桌面开发」负载 + CMake（缺则 `winget install Kitware.CMake`，或在「Developer PowerShell for VS」里跑，PATH 已配好）。
MinGW-w64 亦可：
```
x86_64-w64-mingw32-cmake -S . -B build && cmake --build build
```

## 运行
- 运行endfield_esp.exe后需要选择Endfield.exe打开游戏
- 成功后自动弹出 **F8显示/隐藏**：
  - **绘制**：总开关、**贴合游戏窗口**
  - **视图**：显示距离 5–500m / 模型高度 / FOV（0=游戏值）
  - **过滤**：敌人/NPC/其它物件类型开关；白名单+黑名单（分号子串，黑名单优先）；
    **实体清单**点行=显式显隐（优先一切），重建时滚动/选中位置保持
  - **书签**：收藏任意名称并配自定义颜色（系统取色器，双击行换色），绘框/文字即用该色；
    `保存ini` 全部落盘（save = name,r,g,b 多行）
- `INSERT` 开关绘制，`F7` 重载 ini，`F8` 显隐控制台，`END` 退出。
- 绘制参数放 `endfield-esp.ini`（示例见 `config/`），运行时热改。
- 游戏建议无边框窗口模式（独占全屏会盖住 TOPMOST 层）。
- 按 `Ent_TypeId` 分类上色（`palette[8]`），Endfield 场景里主要是敌人/采集点/互动物三类。

## TODO（骨架预留的扩展点）

- `Ent_TypeId` 分类过滤（敌人/资源点/掉落各一色，Endfield 主要是物资/采集点透视）
- 骨骼线（读 Transform 子节点链）
- 小地图雷达（独立窗口，同一份 g_ents）
- ImGui + D3D9 hook 版内嵌覆盖层（替换 GDI，解决 colorkey 边缘锯齿）

## Credits
1. **[DeftSolutions's IL2CPP-Dumper](https://github.com/DeftSolutions-dev/IL2CPP-Dumper)**


## 个人吐槽
1.这个项目全程为vibe coding快速实现。注入的方式是基于fork了IL2CPP-Dumper并进行改进而实现。
2.由于AI很喜欢添油加醋，且瞻前顾后，容易为了完成我提供的目标和对上我的信息。而把项目写得看着就像是失忆的人写得似的。。。。
3.使用Agent工具Deepseek harness搭载Qwen 3.8 flash完成