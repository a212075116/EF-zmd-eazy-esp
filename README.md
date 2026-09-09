# endfield-esp — 外部 Unity/IL2CPP ESP 框架

纯外部实现：`ReadProcessMemory` 读实体链 + 视图矩阵投影 + 分层窗口覆盖层。零注入、零 hook，改偏移即可跨版本复用。
配置全部集中在 `config/offsets.hpp`，当前为示例占位值，需用下面的流程从你手上的客户端 dump 后替换。

## 目录结构

```
endfield-esp/
├── CMakeLists.txt
├── config/
│   ├── offsets.hpp       ← 所有偏移/指针链/特征码（示例占位，必换）
│   └── endfield-esp.ini  ← 运行期参数（F7 热重载）
├── src/
│   ├── memory.hpp        ← RPM 封装、指针链、il2cpp String 解码、特征码扫描
│   ├── esp.hpp           ← Vec/Mat、WorldToScreen（View/Clip 双模式）
│   ├── config.hpp        ← ini 解析
│   ├── main.cpp          ← 读线程(~160Hz) + GDI 覆盖层(~120fps)
│   └── probe_main.cpp    ← probe.exe 偏移定位工具(矩阵扫描/链验证/实体解读/特征码)
├── tools/
│   └── find_offsets.py   ← 从 Il2CppDumper 的 dump.cs 自动挖候选偏移
└── tests/
    ├── w2s_test.cpp      ← 投影数学冒烟测试: g++ -std=c++17 -I . tests/w2s_test.cpp -o t && ./t
    └── sample_dump.cs    ← find_offsets.py 自测样例
```

## 编译（Windows）

PowerShell（5.1 不支持 `&&`，分行执行）：
```powershell
cd endfield-esp
cmake -S . -B build -A x64
cmake --build build --config Release
```
一行版（带短路判断，等价 &&）：
```powershell
cmake -S . -B build -A x64; if ($LASTEXITCODE -eq 0) { cmake --build build --config Release }
```
产物：`build/Release/endfield_esp.exe`（覆盖层）+ `build/Release/probe.exe`（偏移定位）。需要 VS2022「使用 C++ 的桌面开发」负载 + CMake（缺则 `winget install Kitware.CMake`，或在「Developer PowerShell for VS」里跑，PATH 已配好）。
MinGW-w64 亦可：
```
x86_64-w64-mingw32-cmake -S . -B build && cmake --build build
```
以管理员运行（OpenProcess 权限）。多显示器时按主屏分辨率覆盖。

## 偏移量填充流程（关键步骤）

1. **进程名**：任务管理器 → 详细信息，确认游戏主进程，改 `off::ProcName`。IL2CPP 游戏逻辑在 `GameAssembly.dll`，元数据在 `GameData/Managed/Metadata/global-metadata.dat`。
2. **dump 类结构**：
   ```
   Il2CppDumper.exe GameAssembly.dll global-metadata.dat out/
   ```
   在 `out/dump.cs` 里搜 `List<`、`Enemy`、`Actor`、`Manager` 定位实体管理器单例（Endfield 的实体类命名以实际 dump 为准）。每个字段的 `// 0x... Offset:` 注释即 `Ent_*` 直接填。
3. **实体列表链**：从 static 字段 RVA 出发，用 CE 多级指针扫描（已知坐标 → 指针扫描 → 跨重启稳定过滤）得到 `EntityList_Chain`。
4. **视图矩阵**：经典做法 —— dump 出的 Camera 类里找 `m_WorldToCamera`/`m_ProjectionMatrix`，或用特征码：矩阵首行四个 float 定位一个静态候选地址，再 `findPattern` 固化。Unity 多数版本矩阵是 worldToCamera → 用 `W2SMode::View`；若拿到的是 VP 矩阵 → 切 `W2SMode::Clip`（菜单里热切，投影不对就换）。
5. **验证**：游戏里站定，看覆盖层框是否跟随；移动/转向时框抖动 → 矩阵链或模式不对；框大小恒定但位置偏 → FOV 读错，改走 `CamFov_RVA`。

## 运行

- 运行后自动弹出 **F8 控制台**（原生 Win32，Tab 四页）：
  - **绘制**：总开关、**贴合游戏窗口**（覆盖层只盖游戏客户区，Alt+Tab/移动/缩放窗口自动跟随，投影自动切客户区尺寸；取消勾选=整桌面）、角框/方框/名字/距离/吸附、文字字号滑条（覆盖层字体实时重建）
  - **视图**：显示距离 5–500m / 模型高度 / FOV（0=游戏值）——全部实时生效
  - **过滤**：敌人/NPC/其它物件类型开关；白名单+黑名单（分号子串，黑名单优先）；
    **实体清单**点行=显式显隐（优先一切），重建时滚动/选中位置保持
  - **书签**：收藏任意名称并配自定义颜色（系统取色器，双击行换色），绘框/文字即用该色；
    `保存ini` 全部落盘（save = name,r,g,b 多行）
- `INSERT` 开关绘制，`F7` 重载 ini，`F8` 显隐控制台，`END` 退出。
- 绘制参数放 `endfield-esp.ini`（示例见 `config/`），运行时热改。
- 游戏建议无边框窗口模式（独占全屏会盖住 TOPMOST 层）。
- 按 `Ent_TypeId` 分类上色（`palette[8]`），Endfield 场景里主要是敌人/采集点/互动物三类。

## 检测面（工程事实）

- 客户端侧对 `OpenProcess(PROCESS_VM_READ)` 句柄枚举是常见检测手段；覆盖层窗口按类名/风格（WS_EX_LAYERED|TRANSPARENT|TOPMOST）也能被扫到。规避路线：驱动读、DMA 读卡、窗口去特征（owner-draw、混入正常 UI）。本项目保持最小可用骨架，未内置这些。
- 号损自负；配置仅在你自己的测试实例上使用。

## TODO（骨架预留的扩展点）

- `Ent_TypeId` 分类过滤（敌人/资源点/掉落各一色，Endfield 主要是物资/采集点透视）
- 骨骼线（读 Transform 子节点链）
- 小地图雷达（独立窗口，同一份 g_ents）
- ImGui + D3D9 hook 版内嵌覆盖层（替换 GDI，解决 colorkey 边缘锯齿）


## 偏移定位工具链（占位值必须经此流程换成实测值）

偏移唯一真值来源 = 你自己版本装机客户端里的二进制，聊天/论坛给的"现成偏移"随补丁必失效。

1. **dump 元数据**。Endfield 为 IL2CPP metadata v27~29，通用 Il2CppDumper 常报错；
   用社区专为其做的 [IL2CPP-Dumper（metadata 27–29）](https://github.com/DeftSolutions-dev/IL2CPP-Dumper)：
   `IL2CPP-Dumper.exe GameAssembly.dll global-metadata.dat out/`
   （若 metadata 头部被加密，先按社区流程 dump 解密后的 global-metadata.dat）
2. **实例字段自动挖掘**：`python tools/find_offsets.py out/dump.cs`
   → 候选类排名 + 生成 `config/offsets.suggested.hpp`；`Ent_Pos/Height/TypeId/Name` 即实测值，
   static 字段 dump.cs 只能给 bucket 索引，工具如实标 TODO 交下一步。
3. **视图矩阵定位（正交扫描 + 旋转差分，Unity 社区标准手法）**：
   ```
   probe mat A.txt              # 全模块扫「行0-2单位正交+末行000±1」4x4 候选
   （游戏内转动视角）
   probe mat B.txt
   probe matdiff A.txt B.txt    # 两次都在且内容变动 = 活跃视图矩阵
   probe sig <矩阵地址>         # 生成特征码填 ViewMatrixSig，跨版本免重找
   ```
4. **链与结构验证**：
   ```
   probe chain <RVA> 0x20 0x8 0x38   # 逐跳打印，断在哪跳一眼可见
   probe list <管理器地址> 0x60 16    # 打印16个实体坐标+类名 = 链终验
   probe ent <实体地址>               # klass类名 + 可疑Vector3字段自动扫描
   ```
5. 实测值合入 `offsets.hpp`，重编译。跑不通时把 dump.cs 或 probe 输出贴回来定位。

## 实时 Feed（终态方案，首选）

dumper 已内置 F4 实时喂送（fork 自 @desirepro，`Dump/include/esp_feed.hxx`）：
游戏内按 **F4** → 60Hz 走 `EntityManager.s_manager → m_nodes → EntityNode.data → BaseEntityData.position`，
连同主相机（get_main + Transform 三轴 + FOV）写入共享内存 `Local\EndfieldEsp`。
**所有偏移运行时按字段名推导**（field_get_offset），版本更新免疫；读取侧 `src/shm.hpp` 经
OpenFileMapping 映射——句柄剥权管不到自己创建的命名节，全程零 RPM。

启用：① 把 `esp_feed.hxx` + 打过补丁的 `main.cxx` 拷回 dumper 工程重编译 ② 注入后按 F4
（控制台会打印实测偏移，应与本 README 下方数值一致）③ 跑 endfield_esp.exe（管理员，同会话）。
数据优先级：SHM 实时 > `dump=` 文件桥 > RPM 直读（静态链配好后）。ini `shm = false` 可关。

## FileFeed 模式（备用文件桥，零内存读取）

内核 AC 剥掉 PROCESS_VM_READ 后（readtest 全 err=5），数据面改走文件桥：
注入式 dumper（@desirepro 工具）周期性产出 `IL2CPP_World_Dump_*.txt`（全场景对象+坐标+四元数）与
`IL2CPP_Camera_*.txt`（pos/forward/right/up/fov）。ESP 只需 ini 里：

```ini
dump    = E:\Program Files        # dumper 输出目录（英文路径）
include = enemy;monster;boss      # 战斗图 dump 一次，看真命名后收紧
feedMs  = 1500                    # 文件轮询间隔
```

即可渲染框/名字/距离，完全不碰游戏内存。管线已用真实 1.5 dump（82,941 对象）通过端到端测试
（tests/feed_test.cpp）：LOD 去重 3851→970 框，MainCamera 四元数→视图矩阵→W2S 全链路断言通过。

相机 FOV 实测 47°；世界快照自带 tag="MainCamera" 对象，相机文件缺失/identity 时自动降级使用它。

## 真实显示名（feed 侧解析，协议不变）
`Rec.name` 现在优先输出 `BaseEntityData.templateData@0xC8 → BaseTemplateData.name@0x18`
（游戏内真实名称），拿不到时回退 templateId；dumper 进程内按 tid 缓存，60Hz 无额外开销。
偏移仍全部运行时按名推导（feed 日志新增 `tmpl=0xC8 nm=0x18` 应可核对）。

## 实测敌人命名（1.5，真实 game dump + F4 联调确认）
敌人 templateId 前缀 `eny_`：`eny_0057_dog`、`eny_0050_hound`、`eny_0072_slimeml`、`eny_0099_slimeml2`、`eny_0094_hsfly`。
类型着色以 `EntityDataType==Enemy(2)` 为准（比名字更可靠）；include 留空即可，GUI 类型开关过滤。

## 实测偏移（1.5，来自 dump，已写入 offsets.hpp）

EntityManager.m_dataStorage=0x88 / m_nodes=0xD0；EntityNode.entity=0x48 data=0x50 isDead=0x29；
BaseEntityData.position=0x8C rotation=0x98 templateId=0x68 id=0x60；EntityDataType.Enemy=2；
GameAssembly.dll 恒 247.8MB，基址由 mapname 通道自动恢复。静态 RVA 待 s_manager 解析后补。

## 第 0 步：attach 失败先定位名字

`attach` 报失败 ≠ 代码坏了，按提示分三种：
- `process 'X' NOT FOUND` → 进程名不对（占位值）：`probe proclist end`（换 `ark`/`hyper` 等子串试），拿到真实名后**同时改** `offsets.hpp` 的 `ProcName` 和 `ProcNameA`，重新编译；游戏也得先开着。
- `OpenProcess DENIED` → 用管理员 PowerShell 重跑。
- `module 'GameAssembly.dll' absent` → 该游戏可能不是 IL2CPP 或模块名特殊：`probe modules <pid>` 看真实模块清单，改 `ModuleName`。

**枚举被保护挡掉（winerr=5 两条通道全拒）时**：新版 `attach` 会自动回退到「内存映射走查恢复」——遍历 VirtualQueryEx 区域，用 MZ/PE64 头 + SizeOfImage 合理 + 含 `il2cpp_domain_get` 导出名字符串三重条件认出 IL2CPP 主模块，成功时打印 `(recovered via memory-map walk)`。手动验证：`probe findmod <pid>`。无需驱动，前提是 OpenProcess+ReadProcessMemory 可用（通常外部挂都满足）。

进程名/模块名也可以在 `endfield-esp.ini` 里用 `proc =` / `module =` 覆盖，改完 F7 热重载，不用重编译。

## 排障：C1083 找不到 memory.hpp 等

手工跨机拷贝漏文件时，把项目根目录的 `setup.ps1` 拷过去运行，一次重建全部文件：
```powershell
powershell -ExecutionPolicy Bypass -File setup.ps1
```
然后删除旧配置缓存并重新构建（configure 结果缓存了旧文件树）：
```powershell
Remove-Item -Recurse -Force build
cmake -S . -B build -A x64
cmake --build build --config Release
```

## v16: merged layout (ESP-only)
- `feed/`   — game-side DLL: minimal IL2CPP bootstrap (wait domain, 12s settle, GC-safe attach) + `esp_feed.hxx` (stable59) + F4/F6. Static dump, scene dumper, mapper: removed.
- `src/`    — overlay GUI (unchanged behavior, incl. endfield_esp_dbg.txt blackbox).
- Build: `cmake -S . -B build -A x64 && cmake --build build --config Release`
  → `build\Release\endfield_feed.dll` (inject, F4 starts feed) + `build\Release\endfield_esp.exe` (overlay).
- Diagnostics without rebuilds: `esp_feed.cfg` next to the game exe, line `diag=1`, then F4 off/on.


## v17d: all-in-one overlay (proven mapper flow)
endfield_esp.exe IS the launcher, using the very same manual-map injection that
the standalone dumper Mapper always used (vendored into src/mapper/):
  1) elevates itself via UAC once (mapper proven flow needs admin)
  2) picks/launches the game CREATE_SUSPENDED -> ManualMapDLLBytes -> ResumeThread
  3) if the game is already running: attempts a live manual map (same code)
  4) receipt = named event Local\EndfieldEspBoot published by the feed's DllMain
     (manual-mapped modules never appear in toolhelp, so the event is the proof)
Feed side: endfield_feed.dll works mapped AND LoadLibrary'd; F6 now ExitThread
(FreeLibrary would crash a manually mapped host).
## v17: all-in-one overlay
endfield_esp.exe now IS the launcher: on start it finds the game process (or
launches the game exe you pick in a file dialog; path saved to
endfield-esp-game.txt), extracts the embedded endfield_feed.dll to %TEMP%,
injects it and the feed auto-starts (cfg handshake: %TEMP%\esp_feed.cfg
auto=1 enable=1). No console, no F4, no manual steps.
- feed code updated but game still runs the OLD injected one -> restart game (warned once).
- game running elevated -> run the overlay as administrator.
- manual/hotkey mode untouched: delete the temp cfg, inject endfield_feed.dll yourself, F4/F6 as before.
