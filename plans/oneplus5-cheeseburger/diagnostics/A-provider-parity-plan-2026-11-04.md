# A 线：H103cg display provider 对齐计划（2026-11-04）

## 0. 目的与边界

目标：在**保留 H103cg 可用基线**的前提下，让 `backboardd` 真正打开 Legacy
display，得到 `NATIVE_LEGACY_STATE calls>0`（理想情况下出现首个 iOS UI 帧）。

本计划**不包含**：

- 切换 OS（iPadOS 18 / iOS 18 / 16K XNU）；
- GPU 硬件提交、Adreno 固件、QcomGPU 完整栈；
- 新增 boot-arg、`sheng-disable-jobs`、Data 容量或 AMFI/TrustCache 诊断；
- display timing、ordering、delay、pump、方块/灰条/亮度常量实验；
- 修改 `unit-scale`、`sheng-vsync`、`IOClass`/`newUserClient` 这些已被
  H103at/H103be/H103bf 排除的路径（除非 A3 明确要求做 identity parity）。

约束：

- 每次只改一个变量；
- 每次构建保留 H103cg 镜像和 hash，失败可回退；
- 任何显示 archive 改动都可能重新触发 Data/APFS IOMedia 发布 race；
  如果启动停在 `Waiting on IOMedia UUID 04769D26-...`，不能直接判定
  provider 失败，只能记录并回退；
- 不提交 IPSW、kernelcache、userdata、jpg 等私有文件。

> **停止记录（2026-11-04）**：A2/A2c 已越界。用户要求停止
> display archive、方块/灰条、display timing 和 ordering 实验。
> H103cg 是唯一基线；以下 A2/A3/A4 仅保留历史，不再执行。
>
> H103cg-1（只移动三个 fill 坐标，其他完全按 H103cg）仍然 root wait
> `04769D26`；display archive 的任何二进制/布局变化都会触发 race。
> 指示块只能从 display archive 外部覆盖/绘制，或先修复 race。

## 1. 当前门

H103cg 是唯一接受的稳定基线：

```text
boot:    D:\Users\ls\Documents\OnePlus5_ios\boot-H103cg-depid.img
size:    81,147,904 bytes
sha256:  4560ac5b64662aa321348d530617894ef85bf27252eae04396f8a2048b1cea8a
payload: 79,691,776 bytes
sha256:  a12f5fc9859da698cf41b27d4172c456201b8a5dcde18ba671b984d71234da0f
```

状态：

```text
NATIVE_LEGACY_STATE:
  load=0 stage=10 reg=1 surface=0/1 probe=1 dt=1 start=1 calls=0 pattern=1
```

- Legacy 后端、GOP 映射、DT 节点、服务注册都正常；
- `IOMFB_FDR_Loader` 偶发 `calls=1`，说明 display 可以被打开；
- `backboardd` 不打开：`calls=0 pid=0 comm=`；
- 面板左上白色 = SpringBoard + backboardd 已 exec，但 backboardd headless。

## 2. A 只读对照结论

### 2.1 三个同 SoC 参考

| 项目 | OS / 设备 | 显示做法 | 结果 |
| --- | --- | --- | --- |
| `sagit-ios-port` | iPadOS 18.7.10（iPad7,11/12）+ iPhone10,3 身份 | 原生 `IOMobileFramebufferLegacy`，物理 DPU 节点 + 完整属性 | 已到 `CADisplay: LCD primary` |
| `hackios-xnu-kernel` | iOS 16.5，OnePlus 5 | 自写最小 `IOMobileFramebuffer` + `IOAcceleratorES` stub | 完整 UI |
| `DinnerABC/UniXNU-cheeseburger` | iPadOS 17.7.11，OnePlus 5 | 同 `NativeLegacyDisplay`，CPU 呈现 | 显示可用，`backboardd calls=77/97`，首帧偶发 |

### 2.2 关键差异

| 维度 | sagit | H103cg | DinnerABC |
| --- | --- | --- | --- |
| DT 节点 | 物理 DPU：`device_type=display-subsystem`、`compatible=qcom,msm8998-dpu`、`reg`、`interrupts` | 合成 nub `gop-display`，只有 `device_type` | 同 H103cg（无 compatible/canvas） |
| provider 类 | kext：`IOClass=QcomDisplay`、`IOProviderClass=AppleARMIODevice`、`IONameMatch=qcom,msm8998-dpu` | 手工 `NativeLegacyDisplay` + `IOPlatformDevice` nub | 同 H103cg |
| display 属性 | `DisplayWidth/Height/Width/Height`、`RowBytes`、`BitsPerPixel`、`PixelFormat`、`Location`、`IOMFBDisplayRefresh`、`IOMFBMaxSrcPixels`、alignment/stride/scaling、`IOFBDependentID=0` | 仅 `IOFBDependentID/Index`、`NativeSandboxGrantProcess`、`IOClass` override | 不设置这些 identity props |
| `IOFBDependentID` | `0` | `1`（display 和 nub 都写） | 未设置 |
| `TimingElements`/`ColorElements` | 源码未显式发布（依赖原始 family/platform 路径） | 源码未显式发布；服务属性只有 `IOMFB Debug Info` | 未显式发布 |
| accelerator | `QcomGPU` / IOGPU 服务 | **`NativeA540AcceleratorProbe` 已链接但从未调用** | GPU/IOAccelerator 服务 |
| 父节点 canvas | `canvas-width/height`（iOS 18 `IOMobileFramebufferOpen` 读取） | 无 | 无 |
| PM | 硬件 PM 注册 | 逻辑 PM（`offline-legacy-pm=1`） | 逻辑 PM |

### 2.3 证据

- sagit：
  - `drivers/display/QcomDisplay/Info.plist`
  - `drivers/display/QcomDisplay/QcomDisplay.cpp:1313` 起的
    `publishDisplayProperties()`
  - `boot/bridge/display_handoff.c:196-203`（`canvas-width/height`）
  - `device_trees/msm8998.dtsi:197-209`（`qcom,msm8998-dpu` +
    `device_type=display-subsystem`）
  - `docs/display/README.md:1026`：只发布 IOService 不足以完成枚举
- HackiOS：
  - `xnu/files/iokit/Kernel/MiniIOMobileFramebuffer.cpp:333-383`
    （`TimingElements`/`ColorElements`，注释：without a mode it draws nothing）
  - 同文件 `1438-1465`（`/arm-io` 下 `display-subsystem` 节点）
  - 同文件 `1468` 起（`IOAcceleratorES` stub，CA 等待约 30 秒）
- 我们的 H103cg：
  - `drivers/NativeMobileDisplay/LegacyDisplayProbe.cpp:504-507,570-571,597`
    （`NativeSandboxGrantProcess`、`IOFBDependentID=1`、`IOClass` override）
  - `drivers/NativeMobileDisplay/NativeLegacyDisplay.cpp:304-313,579-607`
    （`registerService`、`startHardware`）
  - `drivers/NativeMobileDisplay/NativeA540AcceleratorProbe.cpp`
    （`native_a540_accelerator_probe()` 有定义，但全树无调用点）
  - H103cg 镜像 strings：`IOFBDependentID`、`NativeSandboxGrantProcess`、
    `TimingElements`、`ColorElements`、`IOMFBDisplayRefresh` 存在；
    `DisplayWidth`/`DisplayHeight`/`canvas-width` 不存在
- DinnerABC：
  - `package/BoardRuntime/LegacyDisplayProbe.cpp`：不设置 `IOClass`、
    `NativeSandboxGrantProcess`、`IOFBDependentID/Index`
  - `package/BoardRuntime/NativeLegacyDisplay.cpp`：核心 start/register/
    isMainDisplay/newUserClient 与 H103cg 一致，额外有 cacheable scanout、
    MDP direct scanout、GPU early retire、scale

### 2.4 结论

H103cg 的问题不是 `Legacy newUserClient` 拒绝，也不是 AMFI/TrustCache；
而是 **CADisplay provider 不完整**。按证据强度排序：

1. **accelerator 服务缺失**：H103cg 完全没有 `IOAcceleratorES`/IOGPU；
   三个工作参考都有。H103bg 只改 CA 偏好，没有发布服务，不能算已试。
2. **display 属性/mode 缺失**：H103cg 服务上几乎只有 `IOMFB Debug Info`；
   sagit 在 `registerService()` 前发布完整身份，HackiOS 显式发布 mode。
3. **identity 差异**：`IOFBDependentID=1`、`NativeSandboxGrantProcess`、
   `IOClass` override；DinnerABC 同 OS 但都不设置。H103at 已单独排除
   `IOClass`/`NativeSandboxGrantProcess`，所以这项优先级最低。
4. **父节点 DT/canvas 差异**：合成 nub 只有 `device_type`；sagit 的物理
   DPU 节点有 `compatible`/`canvas-width/height`。iOS 18 已确认读取 canvas，
   17.7.11 尚未确认。

## 3. 执行顺序

### A1：最小 `IOAcceleratorES` 服务（第一优先）

目的：回答 backboardd/CoreAnimation 是否因为没有 accelerator 而进入 headless。

改动：

- 在 `native_legacy_display_late_probe()` 的早期调用
  `native_a540_accelerator_probe()`；
- 发布一个**裸** `IOAcceleratorES`：
  - 不设置 `MetalPluginName` / `MetalPluginClassName`；
  - 避免 Metal 去加载不存在的 `NativeAdrenoMetal`；
  - 保留 `IOClass=IOAcceleratorES` 或实际类名，按原始 family 的匹配方式；
- 不增加 delay、不调整 ordering、不改 display 属性。

预期：

```text
NATIVE_A540_ACCEL: published IOAcceleratorES (probe only)
native_a540_accel_state = 6
NATIVE_LEGACY_STATE ... calls>0 ... comm=backboardd
```

判定：

- `accel_state=6` 但 `calls=0`：accelerator 不是唯一缺口，进入 A2；
- `calls>0` 且 `comm=backboardd`：provider 路径前进，记录并检查首帧；
- 启动停在 `Waiting on IOMedia UUID 04769D26-...`：Data/APFS race，
  记录后回退，不据此判定 A1 失败。

#### A1 结果（2026-11-04）

候选：

```text
boot-H103cw-a1.img
size:    81,147,904 bytes
boot:    43f831fb07fe5ad9f639a4147a26137d51f7972cb7fb74c4041639422245e28c
kernel:  4950bfa6c93a248f3e584b05f60e93705c0603f9eb366827004f8fcedaa99d19
payload: 3ac218e47edc83a80d2e746ea8ff75e320d6930c1826f7772eada4a925e82342
```

上机结果：**panic**。

```text
NATIVE_LEGACY: late probe enter
Can't load kext com.apple.iokit.IOGPUFamily - requests to user space are disabled.
OSMetaClass::preModLoad() wasn't called for class NativeA540AcceleratorProbe
```

原因：

1. `native_a540_accelerator_probe()` 里
   `if (!OSKextLoadKextWithIdentifier("com.apple.iokit.IOGPUFamily"))`
   的返回值判断写反；加载失败后反而继续
   `native_a540_accelerator_static_init()`，触发 OSMetaClass panic。
2. 即使修正判断，pre-root late probe 阶段也无法加载 `IOGPUFamily`
   （`requests to user space are disabled`）；原始 `IOAcceleratorES`
   基类依赖该 family。

结论：**A1 作为 pre-root 裸 accelerator stub 不可行**。原始
`IOAcceleratorES` 需要 `IOGPUFamily` 已加载；可选后续是 A1b（把
`IOGPUFamily` 放进 prelink roots）或 post-root publication，两者都会改变
prelink 布局/启动时序，暂不执行。A1 源码已回退，H103cg 保持。

### A2：最小 display 属性（A1 不足时）

目的：让原始 family/CA 看到完整的固定模式。

改动（只加属性，不改 timing）：

- 在 `NativeLegacyDisplay::startHardware()` / `registerService()` 之前发布：
  - `DisplayWidth` / `DisplayHeight`（GOP 尺寸）
  - `RowBytes`、`BitsPerPixel=32`、`PixelFormat="BGRA"`
  - `Location="Internal"`
  - `IOMFBDisplayRefresh`（固定 60 Hz，或调用原始
    `native_legacy_set_refresh_properties()`）
- 仍不发布 `TimingElements`/`ColorElements`；如果 A2 后 `calls>0` 但
  没有画面，再做 A2b 单独补 mode。

判定：

- `calls>0`：属性/mode 路径有效；
- `calls=0`：进入 A3；
- root wait：记录并回退。

#### A2 结果（2026-11-04）

候选：

```text
boot-H103cx-a2.img
size:    81,147,904 bytes
boot:    571d77b74176a89c60f773bca36b364ec90172b782ce142cb948360a79cbc822
kernel:  4fbd7673243bc9aae455017d7d8258a6dcb37d028808ce26343a9849689c526c
payload: 9a6efa2eb7cd371941b225590a9901c6c8ba9240204e49d7545356317732cf53
```

上机结果：显示属性发布成功，但随后停在 root wait：

```text
NATIVE_LEGACY: display properties 1080x1920 row=4320
NATIVE_LEGACY: registerService object=...
NATIVE_LEGACY: base start ok primary=1 ...
NATIVE_LEGACY: phase=4 dt=1 probe=1 attach=1 start=1 reg=1

Waiting on <dict ... IOProviderClass=IOMedia ... UUID=04769D26-...>
```

结论：A2 的属性代码在显示服务上生效，但 Legacy archive 变大再次触发了
Data/APFS IOMedia 发布 race；启动未到 backboardd，`calls` 无法评估。
A2 镜像不可用，H103cg 保持。

#### A2c：已停止（2026-11-04，越界）

A2 改了 display 属性/`IOMFBDisplayRefresh`；A2c 又移除了
`drawTestPattern()` 的 accel 方块并改变了 archive 成员。这违反了
“不再做方块/灰条/display 时序/ordering 实验”的边界。已恢复 H103cg
源码和 7 成员 builder；A2/A2c 镜像不使用，A3/A4 也不执行。

### A3：identity parity（A2 仍不足时）

改动（一次只改一项）：

1. `IOFBDependentID` 1 → 0（display 和 nub）；
2. 去掉 `NativeSandboxGrantProcess`；
3. 去掉 `IOClass` override。

说明：H103at 已单独试过 `IOClass=IOMobileFramebuffer` 和
`NativeSandboxGrantProcess`，backboardd 仍未打开；因此这项只作为
DinnerABC 同 OS 对照，不作为第一优先。

### A4：父节点 DT/canvas（最后）

改动：

- 在 `gop-display` nub 上补 `compatible`（按实际 GOP/DPU 描述，不伪造
  Apple 面板 timing）；
- 补 `canvas-width`/`canvas-height`（如果确认 17.7.11 的
  `IOMobileFramebufferOpen` 读取）。

说明：sagit 的 iOS 18 已确认读取；17.7.11 尚未验证，不应先改。

## 4. 每次构建的记录模板

```text
ID:            A1 / A2 / A2b / A3.1 / ...
变更:          单变量描述
kernel:        <sha256>
payload:       <sha256>
boot:          <sha256> / <bytes>
H103cg 基线:   是否保留
NATIVE_LEGACY_STATE: ...
NATIVE_A540_ACCEL: ...
root:          是否通过 / Waiting UUID
结果:          calls>0? comm=? 首帧?
结论/下一步:
```

## 5. 风险

- 任何 display archive 改动都可能改变 H103cg 的精确时序，触发 Data/APFS
  IOMedia 发布 race；这不是 provider 结论，需要单独记录。
- `NativeA540AcceleratorProbe` 当前会设置 `MetalPluginName`/
  `MetalPluginClassName`；直接调用可能让 Metal 去加载不存在的插件并
  崩溃。A1 必须先改为裸 accelerator。
- 不要因为 A1/A2 失败就恢复 display timing/ordering/delay/pump 实验；
  失败只说明 provider 还缺别的层。
- 不要切 OS；A 线的目的就是先用最小改动验证 provider 假设。

## 6. 参考文件

- `plans/oneplus5-cheeseburger/STATUS.md`
- `plans/oneplus5-cheeseburger/diagnostics/H103-preboot-layout-audit-2026-11-01.md`
  §44/§45
- `drivers/NativeMobileDisplay/LegacyDisplayProbe.cpp`
- `drivers/NativeMobileDisplay/NativeLegacyDisplay.cpp`
- `drivers/NativeMobileDisplay/NativeA540AcceleratorProbe.cpp`
- `scripts/build-op5-legacy-display.py`
- `../sagit-ios-port/drivers/display/QcomDisplay/`
- `../hackios-xnu-kernel/xnu/files/iokit/Kernel/MiniIOMobileFramebuffer.cpp`
- `../DinnerABC/UniXNU-cheeseburger/package/BoardRuntime/`
