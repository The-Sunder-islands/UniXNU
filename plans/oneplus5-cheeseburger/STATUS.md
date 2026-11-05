# OnePlus 5（cheeseburger）B 线状态

- 计划：`plans/oneplus5-cheeseburger/PLAN.md`
- 线路：B — UniXNU / 4K
- 路线/目标：iPad7,1 / iPadOS 17.7.11（21H461）
- 当前阶段：**H103/H103b/H103c Preboot/Cryptex 布局审计、构建与上机推进（2026-11-01）。H91 仍是最佳 Probe 基线；H101/H102 快照仍未工作；审计确认 OS 根 `/System/Cryptexes/OS` 指向 `/private/preboot/Cryptexes/OS`，而当前 `/private/preboot` 为空，SystemOS 被挂到 `/` 或 `/private/preboot` 后都没有命中该路径。首次上机证明 Preboot overlay 和 SystemOS 挂载生效；失败点为 owner cache carrier 的旧路径硬编码，H103b 已修复并构建，等待第二次上机。**
- 当前 gate：**G1 通过（2026-10-22）；B3 已到 BSD/IOKit；B5 root 已挂载（bring-up）；H91/H96a18 过 mount-phase-1；H99 停在 driverkitd active；H100 停在 pfd/accountsd/nsurlsessiond/routined；H101/H102 快照未触发；H103 布局审计完成；H103 boot `aed7e8d8...` 上机确认挂载生效但 shared region 路径检查失败；H103b 已越过 dyld cache 和 launchd boot task 序列；新停点 `tzinit` 缺 Data 时区目录；H103c 上机确认 Data UUID 与内核不匹配（`NATIVE_H82_DATA status=2 stage=0`）；H103d 已修复 UUID 并上机，`tzinit` 变为 EROFS；H103d 的 tzinit 变为 EROFS；H103f 已通过 tzinit，回到 `driverkitd active`；H103g 进程日志显示 driverkitd + 多个 daemon 后停在 FileSet KC；H103h 快照未输出，停在 UFS/SystemOS 读；H103i 证明跳过 driverkitd 会继续启动但无 cryptexd；H103j root union 因 Data UUID 又回旧值失败；H103k APFS root union 仍停在 driverkitd；H103l bindfs root union 破坏 launchd/libSystem，已回退；H103m2 确认 `driverkitd/62` 在读 SystemOS 并加载 FileSet KC；无 UFS stall。H103m3 首次到达 SpringBoard（`caller=SpringBoard/27`）；H103aa 显示后端已完整工作（GOP 自检色块 + calls=1）；H103af 正在确认 pid 70 是否为 IOMFB_FOR_Loader，当前阻塞是 backboardd/SpringBoard 未提交第一帧。userdata 已拆为 Android 64 GiB + userdata_ios 49.2 GiB；iOS SHNGROOT 已恢复，Android 已成功启动；Legacy display 后端已完成（GOP 自检色块 + start=1），backboardd 启动后不打开 display，下一步转向 CoreAnimation/IOGPU/Metal**
- 基线提交：H91 构建提交未记录；本地 UniXNU `main` `e4539ae`（H100 文档提交），设备仓库 `main` `5ad54e2`
- 当前代码提交：本地 `e4539ae`；H99/H100/H101/H102 干净构建树为 XNU `d8b8029` + 原始 patches + 远程诊断插桩；H96 脏源码保留在 `.cache/xnu-work.h96dirty-20261031`；设备仓库 `5ad54e2`；关键子模块见 `BASELINE.md`
- 远程仓库：
  - UniXNU `https://github.com/lss717/UniXNU`（`main`）
  - UEFI fork `https://github.com/lss717/edk2-msm8998`（分支 `unixnu-cheeseburger`）
  - 设备仓库 `https://github.com/lss717/UniXNU-cheeseburger`（`main`，`5ad54e2`）
  - NativeRamBlock fork `https://github.com/lss717/RamBlock`（submodule 路径仍为
    `drivers/NativeRamBlock`，fork 提交 `bc079a5`；`upstream` 指回 `UniXNU/RamBlock`）
- UEFI 分支：`edk2-msm8998` 的 `unixnu-cheeseburger`，提交 `0471194`、`708fa06`、`7f34479`、`91e17a8`、`8ef28f1`、`1ffb934`
- 最近更新：2026-11-01

> 本文件是 B 线（UniXNU / 4K）唯一的计划与状态记录。A 线（在 OP5 上用
> 自建 shim 引导 16K XNU）是另一个独立项目，与本线没有代码依赖。

## G1 设备实测（2026-10-22）

`fastboot boot out/cheeseburger/b1b/boot-op5-b1b-safe.img` 被 ABL 接受（`Booting OKAY`），设备随后从 USB 消失。屏幕确认：

- [x] UEFI 启动，有屏幕输出。
- [x] UniXNU 菜单出现：标题 `OnePlus 5 B1`、条目 `Exit to firmware`、倒计时可见。
- [x] payload 卷挂载成功（菜单来自 `\EFI\UniXNU\unixnu.toml`），说明 `CheeseburgerBootMemoryDxe` 的 DTB 地址 `0x83300000`、`/chosen/linux,initrd-*` 解析和 FAT 栈都工作。
- [x] `fastboot boot` 的 35 MiB boot image 与 `0x10000000/0x12000000/0x14000000` 布局被 ABL 接受。
- [x] DTB 地址 `0x83300000`、`/chosen/linux,initrd-*`、只读 BlockIo、FAT、`unixnu.toml` 全部工作。
- [x] 按键：Qualcomm `ButtonsDxe` 三个键都正常——音量上/下移动高亮，电源键选中（等价 Enter）。
- [x] UniXNU、DarwinBoot、BlockIo 都在：`CheeseburgerInfo` 查到 `UniXNULoader` 协议 `Success`（DarwinBoot 驱动已加载），并枚举到 8 个 BlockIo（没有用 Shell，等价于 `drivers`/`dh`）。
- [x] `UNIXNU:` 日志上屏：`console = "none"` 的 b1h 实测，`SerialDxe` + `FrameBufferSerialPortLib` 把 `UNIXNU:` 行写在屏幕上。

`fastboot getvar all` 已保存到 `.cache/firmware/op5-getvar.txt`（不提交）：hw-revision 20001、unlocked yes、userdata `0x1C4D8BB000`、boot `0x4000000`、没有 A/B slot。

### 诊断镜像 b1c（采集 B0 数据）

固件与 b1b 完全相同（FD `3b7a4c60…`），只换了 payload：加入 `EFI/UniXNU/Info.efi`（`CheeseburgerInfo`），并把 `unixnu.toml` 的默认项设为 `Board info`。该程序在屏上打印内存总量、GOP 模式、整盘 BlockIo 的逻辑块大小和 GPT 的 userdata 边界，停留 180 秒后重启。

G1 现场照片在 `assets/`：`g1-b1c-info-scrolled.jpg`（首版滚屏）、`g1-b1d-info-compact.jpg`（紧凑信息屏）、`g1-b1f-loader.jpg`（DarwinBoot 协议）、`g1-b1h-log.jpg`（`UNIXNU:` 日志）。

## B0 设备数据（2026-10-22 实测）

由 payload 里的 `CheeseburgerInfo.efi` 打印，数值与 `fastboot getvar` 交叉验证。

| 项目 | 值 |
| --- | --- |
| RAM | **8 GB**（UEFI 内存图 7039 MiB / 51 descriptors） |
| UFS 逻辑块 | **4096 B** |
| UFS 主 LUN | 29,763,584 块 = 116,264 MiB |
| `userdata` | `first_lba = 0x14F40` (85,824)，`last_lba = 0x1C627FA` (29,763,578)，29,677,755 块 = 115,928 MiB |
| `userdata` 交叉验证 | 29,677,755 × 4096 = `0x1C4D8BB000`，等于 `fastboot getvar partition-size:userdata` |
| GOP | 1080×1920，stride 1080，fmt 1（BGRX），base `0x9D400000`，size `0x7E9000` |
| 其他 LUN | 4 MiB ×2、128 MiB、4096 MiB、1536 MiB（带 GPT：toolsfv/logfs/sti/…/vendor/reserve3） |
| 无 A/B | `getvar current-slot` 不存在；`unlocked: yes`；`hw-revision: 20001` |

`userdata` 末尾 5 块是 GPT 备份表/头；`FirstUsableLBA` 在 `0x14F40` 之前留给主 GPT。

## B2 进展（2026-10-22）

本地仓库 `/home/ls/UniXNU-cheeseburger`（`main`，当前提交 `a18192e`）：

- `board.toml`：iPad7,1 / 17.7.11 路线、实测 userdata/RAM/GOP 值、`[payload]`；
- `configs/cheeseburger_defconfig`：计划里的最小内核配置；
- `scripts/make-bootimg.py`：调 edk2-msm8998 的 `mkbootimg.py`，地址 `0x10000000/0x12000000/0x14000000`，输出 JSON 报告；已用当前固件实测生成并预检通过；
- `scripts/check-cheeseburger.py`：B1 预检工具的仓库版本，默认读 edk2 构建产物；
- `scripts/make-cheeseburger-adt.py`：从 `make-sheng-adt.py` 改出，MSM8998 常量（GICD `0x17a00000`、GICR `0x17b00000`/stride `0x20000`、19.2 MHz、4×A73 + 4×A53、UFS 节点）；生成 13 KiB Apple 格式树；
- `scripts/check-adt.py`：解析 Apple DT 格式并检查 `/cpus`、`/arm-io/intc`、`/chosen`、`/defaults`、`/options`、`/buttons`、`/product`，生成结果全部通过。

GitHub Actions：`UniXNU/.github/workflows/macos-cheeseburger.yml`（kernel + payload，artifact 上传；固件仍由 edk2-msm8998 单独构建）。

设备仓库的 B3 内核暂时不含 legacy display / boot-progress（sheng 的两个 builder 绑定了它的 Adreno/触摸/背光探针）。这不影响 B3 看 panic：DarwinBoot 把 GOP framebuffer 放进 `boot_args->Video`，XNU 自己的 verbose/panic 控制台直接画到面板；legacy display 是 B7 给 `IOMobileFramebuffer`/SpringBoard 用的。

IPSW 缓存（`unixnu ipsw fetch`，HTTP range，只取需要的成员）：

- `out/ipsw/21H461/DeviceTree.j120ap.im4p`（35 KB）
- `out/ipsw/21H461/044-76040-038.dmg.trustcache`（59 KB）
- 副本在设备仓库 `.cache/firmware/17.7.11/`（不提交）
- `KernelCache` 是 LZFSE，`unixnu ipsw fetch` 需要 macOS `compression_tool`；在本机用 `pylib` 的 lzfse 解开了 DeviceTree 的 `bvx2` 流做校验，但内核/prelink 仍按计划走 macOS 或 GitHub Actions。

## B3 探针可信度（2026-10-23）

复核 `payload-stage3b.img` 里的 stage 3 cave：它等待 `384,000,000` 个
`CNTPCT_EL0` tick，19.2 MHz 下是 **20 秒**，然后执行 PSCI `SYSTEM_RESET`。
STATUS 里“挂几秒再重启”与这个 20 秒不一致，必须先分清那次重启是：

- 约 20 秒后的探针复位 → **stage 3 已执行，MMU 已过**，后面的问题在 C 初始化；
- 明显早于 20 秒的 crashdump/watchdog → 没到 stage 3；
- 没有重启 → 也可能是 PSCI reset 本身不可用，不能反推 MMU 失败。

为此新增一个只验证复位路径的对照镜像（**不写 SCTLR**，只画 stage 1 蓝条，
等待 8 秒后 PSCI 复位）：

| 文件 | SHA-256 |
| --- | --- |
| `out/cheeseburger/boot/boot-psci-control.img` | `b674019d7ed09e1e4e597ecbb2d40d02e091d3412c19ec07aa485b5c299e13b0` |
| `out/cheeseburger/boot/payload-psci-control.img` | `264156038b5795be205292df8714427d0a7cc7a4b2e502f70956081ac9dd2c40` |
| 修改后的内核 `kernel-psci-control.macho` | `b8a08bb17cbffc194c97a8fdbbb8c2941a6c5d22d853f66ecd27c919b4d6643a` |

生成方式（设备仓库）：

```sh
python3 scripts/patch-xnu-entry-probe.py kernel.macho --reset-before-mmu --reset-delay 8
python3 scripts/make-bootimg.py \
  --payload out/cheeseburger/boot/payload-psci-control.img \
  --out out/cheeseburger/boot/boot-psci-control.img
```

判读：

- 约 8 秒后重启 → PSCI reset 可用；stage 3/4 的“无复位”才有诊断意义。
- 不重启或延迟远超 8 秒 → 先修复位路径，不要把“无复位”当成 MMU 失败证据。

复核工具（设备仓库 `scripts/check-kernel-bases.py`）会从 Mach-O 重新计算
`physBase`、2 MiB 对齐和复位延时：

```sh
scripts/check-kernel-bases.py kernel.macho --address 0x1B007C000
# physBase 0x1b2e04000, 2 MiB offset congruent: yes
# reset probes: ... 384000000 ticks 20.000 s
```

## B3 MMU 实测结论（2026-10-23）

**MMU 已过。** 证据不是色带，而是把“复位延时”当成时钟：

| 镜像 | 探针位置 | 延时 | Android 出现 | 反推 kernel_start |
| --- | --- | ---: | ---: | ---: |
| `boot-psci-control.img` | MMU 前 | 8 s | 49.56 s | 未测 |
| `boot-psci-control-30.img` | MMU 前 | 30 s | 71.61 s | 未测 |
| `boot-psci-control-20.img` | MMU 前 | 20 s | 61.73 s | **+52.90 s** |
| `boot-physbase.img` | MMU 后 stage 3 | 20 s | 67.99 s | **+53.42 s** |

- 8 s → 30 s 对照：Android 出现时间相差 22.05 s，正好等于延时差 22 s，
  证明复位由探针控制，不是 watchdog。
- 20 s pre-MMU 对照与 `boot-physbase.img` 的 kernel_start 只差 **0.52 s**，
  说明 stage 3 cave 确实在 MMU 使能后执行并按时复位。
- Android 出现时间（61.73 vs 67.99）受 Android 自身启动速度影响，不能用来判
  MMU；`kernel_start = 主机时间 - /proc/uptime` 才是可靠指标。

因此：

- `PhysicalBase` / `VirtBase` 修正有效；
- 4 KiB bootstrap 页表、TCR/TTBR、SCTLR 切换都已通过；
- 当前卡点移到 **`arm_init` / 第一次 C 初始化**，不是 MMU。

## B3 根因：prelink 段在 bootstrap KVA 映射之外（2026-10-23）

二分链（stage 4 reset-after，10 s 复位；复位=越过该点）：

| 检查点 | 结果 |
| --- | --- |
| `_arm_set_kernel_tbi` 返回 | PASS |
| `_PE_init_platform(FALSE)` 返回 | PASS |
| `_cpu_bootstrap` 返回 | PASS |
| `_arm_vm_init` 返回 | **HANG** |
| `_vm_set_page_size` 返回 | PASS |
| `_arm_vm_prot_init` 返回 | **HANG** |
| 无条件调用 #4 `arm_vm_page_granular_RWNX(segPRELINKTEXTB, segSizePRELINKTEXT, ...)` 之前 | PASS |
| 同一调用返回后 | **HANG** |
| 把该调用 `size` 改成 0 | PASS |
| 把该调用 `size` 改成 4 KiB / 4 MiB / 2 MiB 对齐 | **HANG** |

结论：不是循环次数，而是 helper 访问 `segPRELINKTEXTB` 的第一页就失败。

`start.s` 的 KVA bootstrap 映射从 `VM_KERNEL_LINK_ADDRESS` 开始：

- `VM_KERNEL_LINK_ADDRESS = 0xfffffff007004000`（`__TEXT`）
- 当前 `__PRELINK_TEXT = 0xfffffff00427c000`，在 `__TEXT` **下方** 0x2d88000
- 映射只从 `__TEXT` 向上，`__PRELINK_TEXT` 不在 KVA bootstrap 表里
- `arm_vm_prot_init` 先保护 `__PRELINK_TEXT`，第一次 `arm_kva_to_tte`/TTE 访问就落到未映射页，表现为挂住

因此 `size=0` 能返回；4 KiB 也要访问第一页，照样挂。

进一步实测：

- `mapfix2-stage3`（把 bootstrap KVA 映射起点下移到 `__PRELINK_TEXT`）：
  **PASS**，kernel_start `+53.24 s` → 映射修复本身没有破坏 MMU。
- `mapfix2-prot4`（同样映射修复 + 调用 #4 返回后复位）：**HANG**。

剩余阻塞是 `arm_vm_page_granular_helper` 里的断言：

```c
assert((start - gVirtBase + gPhysBase - pa_offset) >= gPhysBase);
```

当前 `start = __PRELINK_TEXT = 0x427c000`，`gVirtBase = __TEXT = 0x7004000`，
`gPhysBase = Address + 0x2d88000`，代入得到 `Address >= Address + 0x2d88000`，
恒为假。XNU 假定 prelink 段在 kernel link 之上；只修映射不够。

修复方向（需要重建内核，不是再上机二分）：

1. **改 prelink 布局（首选）**：让 `__PRELINK_TEXT`/`__PLK_*` 落在
   `VM_KERNEL_LINK_ADDRESS`（`__TEXT`）之上，并避免与 native `__TEXT_EXEC`/
   `__DATA`/`__LINKEDIT` 重叠；需要原始 iPad7,1 `KernelCache` 与 macOS/CI
   来确认原始段地址和正确的 `slide`。
2. **改 `start.s` + 断言**：映射下移后，还要放宽
   `arm_vm_page_granular_helper` 的 `start >= gVirtBase` 假设；改动更大，
   不推荐先做。
3. 不建议只跳过 `__PRELINK_TEXT` 的保护；后续 kext/绑定仍会访问 prelink 段。

## B3 prelink 布局修复（2026-10-23 晚）

CI run `39951630215`（`macOS build (cheeseburger)`，success）用设备仓库
`07503a9` 的 `slide = "0x4100000"` 重建内核：

| 段 | 地址 |
| --- | --- |
| `__TEXT` / Base / VirtBase | `0xfffffff007004000` |
| `__PRELINK_INFO` | `0xfffffff0082ec000` |
| `__PRELINK_TEXT` | `0xfffffff00937c000` |
| `__PLK_TEXT_EXEC` | `0xfffffff009a20000` |
| `__PLK_DATA_CONST` | `0xfffffff00ada0000` |
| `__PRELINK_DATA` | `0xfffffff00bbdc000` |

所有 prelink 段都在 `__TEXT` 之上，且不与 native 段重叠；Base 与 VirtBase
相同（delta 0）。上机结果：

| 镜像 | 检查点 | 结果 |
| --- | --- | --- |
| `slide-prot4` | `arm_vm_prot_init` 的无条件调用 #4 返回 | **PASS**（kernel_start `+42.93 s`） |
| `slide-vmret` | `_arm_vm_init` 返回 | **PASS**（kernel_start `+45.23 s`） |
| `slide-pe1` | `_PE_init_platform(TRUE)` 返回 | **crashdump `05c6:900e`** |
| `slide-pe1-before` | `_PE_init_platform(TRUE)` 调用前 | **crashdump `05c6:900e`** |

因此崩溃在 `_arm_vm_init` 返回之后、`_PE_init_platform(TRUE)` 调用之前。该窗口
依次调用：

`_PE_consistent_debug_inherit` → `_serial_init` →
`_kernel_startup_initialize_upto` → `_PE_create_console` → `_PE_init_printf` →
`_cpu_machine_idle_init` → `_PE_init_platform(TRUE)`

下一步用 stage 4 检查点逐个测这段窗口；第一个 crashdump 的调用就是新的
阻塞点。设备仍只做 `fastboot boot`，不刷写。

## B3 收尾：XNU 进入 BSD/IOKit（2026-10-23 深夜）

在上面的 prelink 修复之后，又按顺序解决了三个启动阻塞：

1. **BCM2837 panic log 地址错误**：`PE_create_console` 先执行
   `PLATFORM_PANIC_LOG_PADDR=0x3c0fc000`，MSM8998 上无效。
   打开 `CONFIG_XNU_DT_PANIC_REGION=y` 后改走 ADT 的 `pram` 路径（我们的
   ADT 没有 `pram`，安全返回）。CI run `39956257439`。
2. **source corecrypto 的 FIPS POST**：prelink 后 HMAC 失效。改为
   `CONFIG_XNU_CRYPTO_BOOTSTRAP="source"`（CI `39960247193`），并在
   **prelink 之后**用 `scripts/seal-source-crypto-kernel.py` 重新封印。
   `sealed-panic` 的 FIPS 失败消失。
3. **trust cache runtime 过早初始化**：`CONFIG_XNU_TRUST_CACHE_RUNTIME`
   在 `img4if`/`amfi` 注册前调用 `trust_cache_runtime_init()`，panic
   `image4 interface not available @kern_trustcache.c:472`。B3 先关闭
   （CI `39968985568`），留到 B5/B6 做 root/AMFI 时再开。

stage 4 实测（全部 PASS）：

- `_kernel_bootstrap`：`printf`、`scale_setup`、`vm_mem_bootstrap`、
  第二次 `kernel_startup_initialize_upto`、`console_init`、`stackshot_init`、
  `sched_init`、`mac_policy_init`、`machine_init`、`clock_init`、`task_init`、
  `thread_init`、`kernel_thread_create`
- `_kernel_bootstrap_thread` 入口
- `_bsd_init` 入口
- `_IOKitBSDInit` 入口，**以及返回**

结论：MMU、prelink、crypto、DT panic、平台专家、IOKit 初始化都已越过。
普通启动没有可见控制台输出（只有 panic screen 能画），但没有 panic；
下一次阻塞应在 **root device/UFS**（B5）或控制台 `kprintf` 的显示状态。

另外：DarwinBoot 的 `ADT ...` 大标题已从诊断补丁移除；新固件 FD
`6da8ee30…`，无标题镜像 `boot-sourcecrypto-sealed-notitle.img`
`1c25c29a…`。大标题改动保存在
`plans/oneplus5-cheeseburger/patches/darwinboot-virtbase-physbase.patch`；
`drivers/DarwinBoot` 子模块保持干净，重建固件前先 apply 该补丁。

### 普通控制台输出（2026-10-23）

`console_init` 之后普通 `kprintf` 不上屏，但 panic screen 能画。原因是
`initialize_screen(kPEAcquireScreen)` 会 `gc_enable(FALSE)`，把 graphics
console 的 `gc_enabled` 清掉；`console_is_serial()` 因此返回 true。

在 `console_init` 返回后调用 `gc_enable(TRUE)`，普通 XNU 日志立即上屏。
测试镜像 `boot-console-fix.img`（`8d08d3b6…`）实测输出：

```text
standard timeslicing quantum is 10000 us
...
debug_log_init: Error!! gPanicBase is still not initialized
...
Kext com.apple.kec.Libm module start pointer is outside of kext range
  (start 0xfffffff0035b2e - kext starts at 0xfffffff00949b680)
...
lockdown_mode: lockdown mode in nvram is off
```

当前新的阻塞：**`emit_prelink.py` 没有把 `kmod_info` 的 start/stop
（+180/+188）随 slide 写入**，只写了 kmod address（+156）。
`Libm`/`corecrypto` 的 start 仍在 slide 前地址，OSKext 校验范围失败。
修复方向：在 `kits/NativePrelink/emit_prelink.py` 里同时写
`start + slide`/`stop + slide`。上游 `UniXNU/NativePrelink` 只有读权限，
需要在设备仓库放一个同名 kit 覆盖，或走 fork/上游 PR。

已做：

- 设备仓库同名 kit 写 start/stop + slide，并扩展 `kmod_info.size`
  （`aee6b08`，CI `39978443505`）。字段层面 start 已在
  `address..address+size` 内。
- 但 OSKext 仍走 **`LC_SEGMENT_SPLIT_INFO` 的 segment 遍历**，继续报
  `outside of kext range`。
- 试验把 19 个组件的 `LC_SEGMENT_SPLIT_INFO` 改成 `LC_NOTE`：
  内核 panic `kalloc_type_view_parse: No support for non-split seg KCs`
  ——不能去掉 split 信息。

进一步离线核对输出布局：

- `__PRELINK_INFO`：`0xfffffff00833c000..0xfffffff008381000`
- `__PRELINK_TEXT`：`0xfffffff00937c000` 起
- `__PLK_TEXT_EXEC`：`0xfffffff009a20000` 起
- `__PRELINK_DATA`：`0xfffffff00bbdc000..0xfffffff00bd30000`

`arm_vm_init` 在非 fileset 情况下把 `vm_kernel_slid_top` 设为
`vm_prelink_einfo`（`0xfffffff008381000`），因此
`ml_static_slide()` 判定所有组件段（`>= 0xfffffff00937c000`）
**不是 slid 地址并返回 0**；`OSKext::slidePrelinkedExecutable()`
随后把每个组的 segment `vmaddr` 清成 0，导致 start 指针全部越界。
这也解释了为什么 start/address 打印值已经是 slide 后的最终值，
但范围检查仍失败。

修复：`patches/xnu/native-prelink-slid-top.patch` 把
`vm_kernel_slid_top` 扩到 `MAX(vm_prelink_einfo, vm_prelink_etext,
vm_prelink_edata)`，覆盖所有 coalesced prelink 段。已提交
`707fde9`；CI `39987525491` 上机后 **kext 范围错误已消失**，
CoreAnalytics 等 kext 开始真正执行。

新阻塞：`corecrypto` 通过 source-corecrypto 早启动已经调用过
`corecrypto_kext_start()`，prelinked `com.apple.kec.corecrypto`
再次 start 时触发：

```
prng_random.c:267 Assertion failed: !prng_ready
```

修复已合并进设备侧
`kits/NativePrelink/patches/xnu-10063.141.1-security-init.patch`
（避免第二轮构建时 security-init patch 无法 reverse）：当
`source_corecrypto14_status == KERN_SUCCESS` 时，只跳过 prelinked
`com.apple.kec.corecrypto` 的第二次 `startfunc`，仍保留
`OSRuntimeInitializeCPP` 和 started 状态，依赖它的 kext 不受影响。
设备仓库提交 `17dd668`；CI `40056309030` success。

PRNG 修复分两步：

- `patches/xnu/zzz-prng-idempotent.patch`（`29e4e88`）：重复调用
  `register_and_init_prng()` 时直接返回，避免断言。
- source bootstrap 增加 `prng_ready` 早退保护，并合并进
  `source-corecrypto14-bootstrap.patch`（`8c30e45`），避免第二次初始化
  覆盖 prelinked corecrypto 的 PRNG 上下文。

上机 `boot-prng-sourceguard-panic.img`（SHA `400fe389…`）后：

- `prng_random.c` 断言和 `pc=0` 空回调都消失。
- XNU 进入 BSD/IOKit：`CoreAnalyticsHub` start 完成，
  `AppleFSCompressionTypeZlib`、`apfs_module_start`（v2236.140.6）启动，
  `apfs_sysctl_register` 完成。
- 最后停在：

```text
Waiting on <dict ... IOProviderClass IOMedia ...
  Content Apple_APFS>
```

即 **B5：没有 UFS 块设备/IOMedia，APFS 等不到 root**。
`NativeUfs` 目前是 sheng SM8550 版本，`CONFIG_XNU_NATIVE_UFS` 仍未打开；
下一步是 MSM8998 UFS host/PHY/GCC/SMMU 与 root publication 移植。
当前构建还带临时 `zzz-kext-debug.patch`，验证后删除。

### B5 只读 UFS 探针（2026-10-24）

独立 `drivers/NativeUfsProbe` + `native-ufs-probe=20`，只映射
`0x1da4000` 并读 HC 寄存器，不写 MMIO、不提交命令、不碰分区。
上机结果：

```text
NATIVE_UFS_PROBE MSM8998 read-only
hc=0xfffffff7a520f000 cap=1587001f ver=00000210
is=00000400 hcs=0000100f hce=00000001
utrlba=ffffe000 utrldbr=00000000
uecpa=00000001 uecdl=00000000 uecpr=fffff000 uecdme=00000000
```

结论：

- `VER=0x210`：UFS 2.1 HC。
- `HCS bit0=1`：UTRL ready；`HCE bit0=1`：controller enabled。
- UEFI 已把控制器/链路留在可 handoff 状态，**不必先重做 PHY/GCC 初始化**。
- 下一步：handoff 模式复用 firmware 请求列表/SMMU，先做只读 GPT/LUN 探测；
  完整 root 需要 APFS 镜像写入 userdata，属于破坏性操作，必须单独确认/备份。

相关提交：`drivers/NativeUfsProbe`、`d1a4eb6`；截图
`assets/b5-ufs-probe-readonly.jpg`。

### B5 UTRD 命令类型对照（2026-10-24）

上机日志已经把问题缩到 **UFS UTP 请求描述符（UTRD）的命令类型位**：

- UIC `DME_GET` 成功（`get=1 mode=0x11`），`AHIT=0`；
- GCC `0x82028=1`，UFS AGGRE1 AXI 时钟本来就在运行；
- firmware `0xffffe000` 低页写入后回读为 0，cache clean 生效；
- anoc1/anoc2 SMMU 没有启用的 SMR，DT 也没有 UFS `iommus`，暂不按 SMMU
  翻译处理；
- 但 UTP doorbell 后没有完成，且 CPU 读 `UTRLDBR/IS` 会挂住。

对照 OnePlus 5（MSM8998）vendor 内核 `drivers/scsi/ufs` 后确认了当前探针的
结构性错误：**之前沿用 P11 的 `0x10000000`（UTP native UFS，CT=1）发送
SCSI 命令和 NOP，和 Qualcomm Android 代码不一致。**

`ufshci.h`：

```c
UTP_CMD_TYPE_SCSI       = 0
UTP_CMD_TYPE_UFS        = 1
UTP_CMD_TYPE_DEV_MANAGE = 2
UTP_SCSI_COMMAND        = 0x00000000
UTP_NATIVE_UFS_COMMAND  = 0x10000000
UTP_REQ_DESC_INT_CMD    = 0x01000000
UTP_DEVICE_TO_HOST      = 0x04000000
```

`ufshcd_prepare_req_desc_hdr()` 的 DW0 组包：

```c
dword_0 = data_direction | (command_type << 28);
if (intr_cmd)
    dword_0 |= UTP_REQ_DESC_INT_CMD;
```

正确值：

| 命令 | DW0 |
| --- | --- |
| SCSI TEST UNIT READY | `0x01000000` |
| SCSI READ CAPACITY / READ(10) | `0x05000000` |
| NOP OUT | `0x21000000` |

直接证据：UEFI 留在 firmware 请求槽里的 UTRD 是 `05000000/00000000`，即
**SCSI + DEVICE_TO_HOST + INT**，而当前探针 CAP/READ 发的是
`0x10000000 | (2 << 25) = 0x14000000`（CT=1），NOP 发的是 `0x10000000`。
这解释了为什么 firmware 低页、cache clean 和 UIC 都正常时 UTP 仍不完成。

**2026-10-24 上机验证（CT 修正 + OCS-only 轮询 + 4 KiB data）**：
NOP、READ CAPACITY(16)、LBA1 GPT header 和 entries 全部完成。实机输出：

```text
NATIVE_UFS_GPT warm-link read-only
cap=1587001f ver=00000210 is=00000400 hcs=0000100f hce=00000001
queue=ffffe000 ucd=ffffe100 data=187b15000 cap=1000

L0 st=00 last=1c627ff bs=1000
L1 st=00 last=3ff     bs=1000
L2 st=00 last=3ff     bs=1000
L3 st=00 last=7fff    bs=1000
L4 st=00 last=fffff   bs=1000
L5 st=00 last=5ffff   bs=1000

GPT hdr=6bf93f53 ent=2b0de4e9 n=32 esz=128
cur=1 bak=1c627ff usable=6..1c627fa
persist  8-2007       32M
cache    2000-12007   256M
misc     12000-12107  1M
userdata 14f40-1c627fa 115928M
partitions=32 listed=4
```

结论：

- UTRD CT 修正是根因：SCSI 必须 CT=0（CAP/READ `0x05000000`），NOP 必须
  CT=2（`0x21000000`），不能用 P11 的 `0x10000000`。
- 修正后即使 GPT data 在高地址 `0x187b15000`，DMA 也能完成，说明此前
  own-DMA 失败不是高地址不可达，而是错误 UTRD 类型让 UTP 挂死。
- LUN0 为 4096 字节块、最后 LBA `0x1c627ff`；GPT 当前/备份 LBA 为
  `1`/`0x1c627ff`，可用区 `6..0x1c627fa`。
- `userdata` 为 `0x14f40..0x1c627fa`（约 115928 MiB），与主机侧备份
  几何一致。

主机侧 `gpt-primary-sda.bin` 逐项核对：

```text
header_crc=6bf93f53  entries_crc=2b0de4e9
cur=1  bak=0x1c627ff  first=6  last=0x1c627fa  n=32  esz=128
persist  0x8..0x2007
cache    0x2008..0x12007
misc     0x12008..0x12107
userdata 0x14f40..0x1c627fa
```

LUN 容量也与备份 `lun-sizes.txt` 全部一致：

| LUN | 实机 last LBA | 备份字节数 |
| --- | --- | --- |
| 0 | `0x1c627ff` | 121911640064 |
| 1 | `0x3ff` | 4194304 |
| 2 | `0x3ff` | 4194304 |
| 3 | `0x7fff` | 134217728 |
| 4 | `0xfffff` | 4294967296 |
| 5 | `0x5ffff` | 1610612736 |

**B5 只读 UFS/GPT 访问已打通并完成主机备份核对，未执行任何分区写入。**
完整 B5 root 仍需 APFS/userdata 写入（破坏性），必须单独批准并保留当前完整
备份；本阶段到此停止，不发送 WRITE/UNMAP/FORMAT，也不刷写、擦除或修改
任何分区。

### B5 只读 IOBlockStorageDevice / IOMedia（2026-10-24）

在 GPT 探测通过后，把探针扩展为只读 `NativeUfsLun : IOBlockStorageDevice`：

- `NativeUfsLun` 只实现读方向；`doAsyncReadWrite` 对非
  `kIODirectionIn` 返回 `kIOReturnNotWritable`，`reportWriteProtection=true`，
  没有任何 WRITE/UNMAP/FORMAT 命令。
- 使用 IoStorage 的独立 128 KiB DMA 区（实机回退到高地址
  `0x106c02000`），每个 READ10 最多 32 个 4096B 块。
- list/UCD 的 firmware 低页映射提升为 probe 成员，避免 `start()` 返回后
  `OSSharedPtr<IOMemoryMap>` 释放、后续 IOMedia 读盘访问已卸载地址。
- `NativeUfsLun` 的 190 个 vtable 槽位绑定到原始 IOStorageFamily ABI；
  binding 表在设备仓库
  `scripts/data/IOStorageFamily-bindings.json`，生成/校验脚本为
  `scripts/bind-native-ufs-storage.py`。

实机结果：

```text
NATIVE_UFS_GPT: read DMA low failed; high fallback
NATIVE_UFS_GPT: read DMA phys=106c02000 size=20000; publishing LUN0
NATIVE_UFS_GPT: published LUN0 last=1c627ff bs=1000 read-only
NATIVE_UFS_GPT: ring type=05000000 dir=2 bytes=4096
NATIVE_UFS_GPT: done type=05000000 rsp=21/00 tag=1
...（连续 4096B 读）
```

发布后 IOStorageDriver 已匹配并连续读取 4096 字节块，说明只读 block-device
路径已经成立；没有新的 data abort 或 panic。IOMedia 自动创建的日志没有直接
打印出来（快照线程会被同步读盘饿死；第 4 次请求快照为 `total=0`），因此
**IOMedia 存在性只算间接证据，不再继续为打印 IOMedia 反复构建**。

### B5 只读收尾（A，2026-10-24）

决定停止 IOMedia 日志验证循环，冻结 B5 只读里程碑：

- **已验证**：UFS warm-link、NOP/CAP16/READ10、GPT header/entries、6 个 LUN
  容量、LUN0 只读 `IOBlockStorageDevice` 发布、IOStorageDriver 连续 4 KiB
  读、与主机备份的 GPT/容量逐项一致。
- **未直接验证**：IOMedia 自动创建及其 `Content`；root matcher 仍等待
  `IOMedia Content=Apple_HFS`。
- **不视为失败**：当前 LUN0 的 `userdata` 是 Android 分区，不是 APFS/HFS
  根卷；即使 IOMedia 出现，也不会凭空匹配 Apple 根卷。
- **不再做**：为打印 `NATIVE_UFS_MEDIA` 继续构建/启动；继续调快照阈值或
  registry 枚举。该验证投入产出比低，不改变 root 结论。
- **边界**：本阶段全程只读，没有 WRITE/UNMAP/FORMAT，没有刷写、擦除或
  修改任何分区。

下一阶段 B5 root 必须写入 userdata/APFS 根卷，属于破坏性操作；在执行前需要
单独批准、当前完整备份，以及可验证的 APFS 根卷输入。

### B5 root（B：破坏性规划，未执行）

前置条件（当前只完成第 1 项）：

1. 当前系统完整备份存在且已校验：
   `/home/ls/UniXNU/.cache/firmware/backups/20261024-125857/`。
2. 明确批准写入 `userdata`；写入前不得有任何自动刷写/擦除。
3. 有可验证的 APFS 根卷输入（根 UUID、卷结构、与目标 iOS 版本匹配）；
   当前尚未确认这类输入存在。
4. UFS 驱动需要增加受控 WRITE10 路径；现有探针/只读 block device 没有写
   命令，不能直接复用。

计划草案：

1. 校验备份 SHA256，并记录 userdata 当前绝对范围
   `0x14f40..0x1c627fa`、块大小 4096。
2. 在离线环境构建/验证 APFS root 镜像与根 UUID；先做完整回读校验。
3. 增加隔离的写入工具/测试，只允许写 userdata 窗口，禁止其他 LUN/分区。
4. 执行前再次确认备份可恢复；写入后完整回读并比较哈希。
5. 启动 XNU，验证 IOMedia/APFS 根卷匹配和后续 AMFI/Trust Cache。
6. 失败时只使用已校验备份恢复；不重试盲写。

**当前不执行第 2 步以后的任何写操作。** 先确认 APFS 输入与用户批准。

### B5 root 可行性检查（B，2026-10-24）

用户确认：Android 系统是初始状态、没有个人数据，**不需要个人数据备份**；
B5 使用 **iPad7,1 / iPadOS 17.7.11（21H461）** 作为唯一路线。当前系统完整
备份仍保留，作为误写/回滚保护。

只读检查结果：

- 完整备份校验：`SHA256SUMS` 共 84 项，**84 成功、0 失败**。
- 实机 userdata 几何与 `board.toml` 一致：
  `first_lba=0x14f40`、`last_lba=0x1c627fa`、`lba_bytes=4096`。
- 仓库内没有现成的 `userdata-root.img`、APFS 根卷或 SHNGROOT 镜像。
- B5 使用 `board.toml` 的 iPad7,1 / 17.7.11 profile；IPSW/哈希/prelink/ADT
  都以这一份 fixture 为准。
- `NativeUfsProbe`/`NativeUfsLun` 只有 READ10；没有 WRITE10、UNMAP、
  FORMAT 或任何写工具。
- XNU 侧还缺 SHNGROOT 窗口发布/APFS root UUID 匹配；仅写入镜像不会让
  root matcher 自动完成。

第一轮阻塞点：

1. 构建并校验 iPad fixture 的 `userdata-root.img`；
2. 把镜像写入 userdata（需要明确的 fastboot/写入批准）；
3. 在 XNU 侧发布 SHNGROOT 的 OS/SystemOS 窗口，并配置 APFS 根 UUID。

**写入 userdata 是破坏性操作。** 用户已确认无个人数据、无需个人数据备份；
但执行前仍应保留当前系统完整备份，并只写 userdata 分区，不碰其他 LUN/
分区。

### B5 root 挂载成功（2026-10-25）

iPad7,1 / iPadOS 17.7.11 fixture 的 `userdata-root.img` 已写入并校验；XNU
现在能完成 **UFS 窗口发布、APFS 容器挂载、根卷 UUID 匹配**。

关键结果：

- userdata 写入/回读：SHNGROOT `magic=474e4853/544f4f52`，OS/SystemOS
  窗口偏移和 SHA256 全部匹配主机镜像。
- `NativeUfsWindow` 以 512B 逻辑块发布 OS/SystemOS；UFS 侧最多 128 KiB
  合并读。
- 根 UUID 来自 OS APFS 卷：
  `04769D26-2CA0-4825-B443-E2E5F88300F2`，通过 `/chosen/root-matching`
  匹配成功。
- APFS 已打印 `dev_init`/`nx_mount`；根卷挂载完成。
- 最后停点：
  ```text
  rootvp not authenticated after mounting @bsd_init.c:1100
  ```
  这是未封印 restore OS 的 `FSIOC_KERNEL_ROOTAUTH` 拒绝，不是 mount
  失败。

重要回归修复：

- `IODelay`/`IOSleep` 在此内核阶段会永久挂住；UFS NOP/心跳/超时已经全部
  改为 `mach_absolute_time` 忙等或纯轮询。
- 逐条 UFS 命令日志会严重拖慢 I/O；已关闭，仅保留进度/错误/窗口相位。

当前测试镜像：

- `out/cheeseburger/boot/boot-ufsH7-panic.img`
- CI run `40155569182`，commit `drivers: remove timer waits from UFS probe`

下一步（临时 bring-up，非最终安全形态）：

1. 加 boot arg `native-skip-root-auth=1`，仅在 root auth 拒绝时继续；
2. 目标进入 launchd/AMFI，记录下一批用户空间阻塞；
3. 之后用 `VerifiedRoot` 替换旁路，恢复真实 root 认证。

## 下一步

当前（2026-10-26）：

1. **H53 上机**：确认 16 KiB cache 预读 `resid=0`，文件/目录模式正常。
2. 若仍 `no dyld cache`：优先把 SystemOS union mount 到标准路径
   `/System/Library/Caches/com.apple.dyld`，或做 kernel-side bind；
   不要先换 root/cache。
3. 核对 cache mapping/slide 的 4 KiB 可表示性；必要时在 `shared_region`
   里拆分或重映射。
4. `launchd` 稳定后恢复 root auth/AMFI/trust cache，移除 bring-up 旁路。
5. 继续到 `configd`/SpringBoard，记录第一个用户态阻塞。
6. 更新 `STATUS.md` 并提交文档。

## 已完成

- [x] 选定 B 线（UniXNU / 4K），路线固定为 iPad7,1 / iPadOS 17.7.11。
- [x] 计划与状态文件落盘并提交（`c8a5eeb`）。
- [x] 主机工具安装：clang-20/lld-20、dtc、mtools、acpica-tools、nasm、bison、flex、uuid-dev、python3-pil、python3-git、gettext、abootimg。
- [x] 初始化 UniXNU 21 个子模块。
- [x] clone `edk2-msm8998` 并初始化其子模块。
- [x] 构建上游 `cheeseburger` baseline（GCC5，RELEASE）。
- [x] 建本地分支 `unixnu-cheeseburger`。
- [x] 修复 GCC 13 `-Werror=dangling-pointer`：`ArmPlatformLibMem.c` 的 `MemoryTable` 改为 `STATIC`。
- [x] 旧 edk2 兼容层：
  - `MdePkg/Include/Library/FdtLib.h`（映射到旧 `libfdt`）；
  - `MdePkg/Include/AsmMacroLib.h`（最小 `ASM_FUNC`）。
- [x] `UniXNULib.inf` 增加 `EmbeddedPkg/EmbeddedPkg.dec`（`gFdtTableGuid`）。
- [x] `build.sh` 支持 `UNIXNU_TREE`，把 UniXNU `package/` 和 `drivers/DarwinBoot/` 加入 `PACKAGES_PATH`。
- [x] cheeseburger DSC/FDF 加入 UniXNU、UniXNUDxe、DarwinBoot、CheeseburgerBootMemoryDxe。
- [x] 从 FDF 移除 `SimpleInitMain` 和 `LogoDxe`，为 UniXNU 腾空间。
- [x] 实现 `CheeseburgerBootMemoryDxe`：读 `0x83300000` 的 live DTB，发布 initrd 为只读 BlockIo。
- [x] 屏幕日志：RELEASE 也加入 `SerialDxe`，`UNIXNU:` 日志经 `FrameBufferSerialPortLib`（`0x9d400000`）输出到屏幕；`DEBUG` 与 UniXNU 共用同一控制台。
- [x] 删除内嵌 Linux 镜像 `LinuxSimpleMassStorage`（6.9 MiB），压缩 FV 从 2.60 MiB 降到 1.00 MiB，FD 空闲 4.97 MiB。
- [x] 重新构建并打包 b1b boot image（UEFI + UniXNU + 32 MiB payload）。
- [x] 按键输入沿用镜像里已有的 Qualcomm `ButtonsDxe`（depex 依赖 `SimpleTextIn`），不再新增驱动；b1e 实测通过。
- [x] 预检工具 `plans/oneplus5-cheeseburger/tools/check-cheeseburger.py`：b1b 全部通过（FD 空闲 4.97 MiB、模块齐全、boot image 无重叠、payload 可读），b1 也通过（3.35 MiB）。

## B3 调试断点（2026-10-22 晚，2026-10-23 已修正）

> **2026-10-23 更新：本节的“卡在 MMU 使能”结论已被对照实验推翻。**
> stage 3 的复位时刻与 20 s pre-MMU 对照一致，说明 MMU 已开；卡点移到
> `arm_init` / 第一次 C 初始化。以下保留为当时的推理记录。

当时的最后结论：stage 2（MMU 使能前）色带出现，stage 3（MMU 后）不出现 → 当时判为内核卡在 **MMU 使能**，不是 `arm_vm_init`。`VirtualBase` 已改为 `__TEXT`（`0xfffffff007004000`），但 `PhysicalBase` 仍指向窗口起点（`__PRELINK_TEXT` 对应的物理地址），两者相差 `VirtBase - Base ≈ 0x2D88000`，MMU 开启后第一条取指就会落到错误的物理页。

已编译但**尚未上机**的修正：

- 固件：`Address + (VirtBase - Base)` 作为 `boot_args->physBase`；FD `b59f2e49b1b0d6d5914310f5c9a9f9df038b51c0fa2d7d5ee124136c1834d69b`
- 镜像：`/home/ls/UniXNU-cheeseburger/out/cheeseburger/boot/boot-physbase.img`（`4847be5f393b1442980bb3e7331718f571f2557e83976f7d499f886c209b6b49`），payload `payload-stage3b.img`（`1d1b8fd4d055a2c6decc2775504abfaa43eb8cdb96799aee6571b29ca1017934`，stage 3 + 8 GiB ADT）
- DarwinBoot 补丁：`patches/darwinboot-virtbase-physbase.patch`（子模块已还原干净；重建固件前先 `git -C drivers/DarwinBoot apply`）

明天的第一步：

```sh
fastboot boot /home/ls/UniXNU-cheeseburger/out/cheeseburger/boot/boot-physbase.img
```

- 约 20 秒后由 stage 3 探针复位 → MMU 已过，换 panic 内核（`payload-ci.img`）继续。
- 没有在 20 秒左右重启 → 看 TTBR/TCR（stage 4 探针 `_set_mmu_ttb`）或 QEMU 隔离。

## B0 备份与存储映射（2026-10-24 重做）

当前系统已由用户重写刷写，2026-10-22 的旧备份无法对应/启动，已删除。
2026-10-24 用 TWRP `3.4.0-1`（`fastboot boot`，未 flash）重新 dump：

`/home/ls/UniXNU/.cache/firmware/backups/20261024-125857/`（gitignored；4.7 GB）

范围：除 `userdata`（约 113 GB）和可重建的 `cache` 外的**全部命名分区**，
包含 `system`、`vendor`、`boot`、`recovery`、`modem`、`persist`、`keymaster`
以及 `sda–sdf` 的主/备 GPT。每个文件都做了尺寸校验；关键分区另外与设备
原始分区做了远程 SHA-256 对比，全部一致。完整清单见目录内 `SHA256SUMS`
和 `partition-sizes.txt`。

| 关键分区 | 大小 | SHA-256 |
| --- | --- | --- |
| `boot` | 64 MiB | `b3f8c0b470c9c9b93a4a8e18ffcdacd104df66eb5e0cdc94fde40b4759325453` |
| `recovery` | 64 MiB | `f6625f1350d8b6655a49cf89edcd93d2a8884886f5fb0863ecaeee66cb4bd2a7` |
| `system` | 3 GiB | `acb4a6217aa4390eb6e2da2f514e93588b6dee83ab918e871d01924b3b4eb892` |
| `vendor` | 1 GiB | `dbe1e60b7de6de93e20fc287a40dfa313a1861151af60a6708a8f1e30f4a3660` |
| `persist` | 32 MiB | `69c67d371854280e6846dc69818cf9c84364a9cae93b8f70ca6f17461872a8ca` |
| `modem` | 110 MiB | `2e89843d24f9aa8c6c9287781482d982f435456b34e0049c93b3c895f0bb7729` |
| `modemst1` | 2 MiB | `e2bcc4531b24e20f1a23988440d3bd9772692ba8e2f7c488c3845123918e9eb2` |
| `modemst2` | 2 MiB | `1fc7abb2e03c498bff273e3ae5a465a1eaf06485b06d5ac8d46f0a35e1d4044d` |
| `keymaster` | 512 KiB | `72283e73c873ffb3421d7866bfa02622906df99730652e419cbfeb0c87a45ada` |
| `abl` / `xbl` / `tz` / `devcfg` / `devinfo` | 4–8 MiB | 见 `SHA256SUMS` |
| GPT 主/备（sda–sdf） | 32 KiB × 12 | 见 `SHA256SUMS` |

UFS LUN / GPT 映射（4096 B 逻辑块）：

| 设备 | 容量 | 内容 |
| --- | --- | --- |
| `sda` | 113.5 GiB | `ssd`、`persist`、`cache`、`misc`、`keystore`、`frp`、`oem_*`、`param`、`reserve1/2`、`config`、**`userdata` `0x14F40..0x1C627FA`** |
| `sdb` | 4 MiB | `xbl` |
| `sdc` | 4 MiB | `xblbak` |
| `sdd` | 128 MiB | `reserve`、`cdt`、`ddr` |
| `sde` | 4 GiB | `rpm`、`tz`、`hyp`、`pmic`、`modem`、`dsp`、`abl`、`boot`、`system`（3 GiB）、`recovery`、`devcfg`、`logdump`… |
| `sdf` | 1.5 GiB | `modemst1/2`、`fsg`、`fsc`、`vendor`（1 GiB）、`reserve3` |

## B3 产物（2026-10-22 首次）

macOS run `39769403554`（kernel + payload 全部通过）：

| 文件 | SHA-256 / 说明 |
| --- | --- |
| `b3/kernel/kernel.macho` | `8ee3ae2db93cf4925d81b3c0063baee4377b816257b7258bdfd727f7865b5b16`（51.6 MB） |
| `b3/payload/payload.img` | `b4755de7117eff6cc13e835e56437a5637c8fba5286702c1752f008cb82d52e7`（64 MiB） |
| ADT `kernel.macho.adt` | `644992b7966577b3ff7d232a9f7d35c75c35ee81a11904628934058a0e95a5dd` |
| `out/cheeseburger/b3/boot-op5-b3.img` | `9c120a0faf0631d806425063e23c929a0905f4f34b5668049bfd13912494061d`（65.4 MiB） |

boot image：kernel `0x10000000`、ramdisk `0x12000000` + 64 MiB、tags `0x18000000`。
内核参数：`-v debug=0x14e boot-progress=2 offline-legacy-gop=1 offline-legacy-unit-scale=1`。
prelink 自动带入了 `ApplePMGR`、`CoreAnalyticsFamily`、`IOReportFamily` 等依赖；B4 再裁。

## B3 首次上机结果（2026-10-22）

`fastboot boot` 接受 65.4 MiB 镜像；UniXNU 菜单自动选 `xnu`，DarwinBoot 日志：

```text
DARWINBOOT: ARM64 va=0xFFFFFFF00427C000 pa=0x1B007C000 args=0x1B4134000 entry=0x1831EC400 owned=0x00000000
DARWINBOOT: video base=0x9D400000 display=1 row=4320 width=1080 height=1920 depth=32
DARWINBOOT: inputs validated; exiting boot services
```

- 内核 Mach-O：`__TEXT` `0xfffffff007004000`，最低段 `__PRELINK_TEXT` `0xfffffff00427c000`；DarwinBoot 用最低段当 `Kernel.Base`。
- 录像里出现绿/蓝/品红三条早期探针色条 → 内核入口代码确实执行；随后设备进入 `05c6:900e` crashdump 并重启，**没到视频控制台，panic screen 没机会运行**。
- `owned=0` 与 ADT 里的 `darwinboot-memory-size=0x80000000` 不一致（ADT 本地解析确认值正确），需要给 DarwinBoot 加诊断打印确认它是没读到还是被清零。
- **已确认**：`boot_args->virtBase` 原先取最低段 `__PRELINK_TEXT`（`0xfffffff00427c000`），而 `__TEXT` 在 `0xfffffff007004000`。改为 `__TEXT` 后，内核不再硬崩进 crashdump，而是挂住——MMU 阶段很可能已通过，卡在更后面的早期初始化。
- 下一层：用 patcher 的 stage 3（MMU 后画条并延时复位）确认 MMU 是否真的开成功，再定位挂住的 symbol。
- stage 3 探针：只有绿/蓝/品红三段，随后挂几秒再重启 → 当时判为未到达 MMU 之后。注意：`payload-stage3b` 的 stage 3 cave 实际是 20 秒后 PSCI 复位（见“B3 探针可信度”）；需要按录像计时重新判读，不能直接沿用旧结论。
- 关键嫌疑：DarwinBoot 的 2 GiB 窗口被分配到物理 `0x18007C000`，而 ADT 的 `dram-size=0xF0000000` 只到 `0x170000000`，窗口在“内核认为不存在”的物理区间；已改为 `0x200000000`（8 GiB）重测。
- 8 GiB 结果：**仍是三段后挂住**，窗口高度不是唯一原因；MMU 仍未过。
- 下一批实验（按成本）：① 把窗口强制到 256 MiB，看能否落在 4 GiB 以下；② 给 `arm_vm_init`/pmap 加早图画点（stage 4 reset-after 符号）；③ QEMU 跑同一内核隔离 prelink/内核问题。
- 256 MiB 结果：低地址候选（`0x9007C000`…）分配失败，回退到 `0x17A...` 的洞，UEFI 抛 `Synchronous Exception at 0x17ABB13E8`；而 2 GiB 窗口落在 `0x18007C000` 时可访问（内核入口色条出现）。
- 结论：窗口物理位置不是 MMU 挂住的根因；`0x17A000000–0x180000000` 是 firmware 内存图的洞（记录给后续）。MMU 挂住需用 stage 4 在 `arm_vm_init` 里二分。

## 本地产物

### b1d（当前固件 + B0 数据采集，2026-10-22）

| 文件 | SHA-256 |
| --- | --- |
| `out/cheeseburger/b1d/boot-op5-b1d-info.img` | `3709f7d2e9c1c7fde4641afd85dcffa0088753d2a4120f17ec73fe6af78650d3` |
| `out/cheeseburger/b1d/MSM8998PKG_UEFI.fd` | `217c48fad447d92c80a10bec867e651f670c0fd102b1bd85c64771b7670a4b86` |
| `out/cheeseburger/b1d/op5-info-payload.img` | `d6442196cee68440282c30e3569f5cba5e2a4b863cdc893f9bb77c6586ad6176` |

固件变化：移除 `TerminalDxe`（它绑定 `SerialIo`，把每行 `ConOut` 在 framebuffer 上重复画一遍）。`CheeseburgerInfo` 只在 `[Components]` 里构建，不进 FV。

同一固件（FD `217c48fa…`）的 payload 变体：

| 镜像 | 用途 | SHA-256 |
| --- | --- | --- |
| `out/cheeseburger/b1e/boot-op5-b1e-keys.img` | 双条目菜单，按键测试 | `a4654ec5be146f34f8211acb852872fbed51e4150e6582f5e69cd0768dac1cd4` |
| `out/cheeseburger/b1f/boot-op5-b1f-check.img` | DarwinBoot `UniXNULoader` 检查 | `3b70f69d2a800a8039ffa1cedb8ac11f976829108dc2a2d4a2145ac55c8af1ce` |
| `out/cheeseburger/b1g/boot-op5-b1g-log.img` | `console = "none"`，验证屏上日志 | `2ca7d5cc019beee441dbe728e59d8bbaafd592060d6fcdc2ce1cbb6131578fe2` |

### b1c（上一版数据采集镜像，已被 b1d 取代）

| 文件 | SHA-256 |
| --- | --- |
| `out/cheeseburger/b1c/boot-op5-b1c-info.img` | `61875356ff4f4264d80c4c287d2c20979f9429293ccd03280ef57c9f240de35f` |
| `out/cheeseburger/b1c/MSM8998PKG_UEFI.fd` | `3b7a4c601506b036d46ed4baaba4ef5e99e9e4e4e90ee4a1fa2b721af17d4bff` |
| `out/cheeseburger/b1c/op5-info-payload.img` | `593f2928e4e0d0bbb73b3589837bbc3b7ab5e1c4349f0745551860ea17f8ac4a` |

### b1b（G1 测试镜像，2026-10-22 重建）

| 文件 | SHA-256 |
| --- | --- |
| `out/cheeseburger/b1b/boot-op5-b1b-safe.img` | `ea89a9c75d193b3216bc731ba60e3c303621f5f743c71c03763755f49eccabc3` |
| `out/cheeseburger/b1b/MSM8998PKG_UEFI.fd` | `3b7a4c601506b036d46ed4baaba4ef5e99e9e4e4e90ee4a1fa2b721af17d4bff` |
| `out/cheeseburger/b1b/op5-payload.img` | `3bc1ded555d0cd787d11c8531d2c2c60658bc4088d2842a68c042046e352b159` |

`boot-op5-b1b-safe.img`：kernel `0x10000000`（`0x1641df` B），ramdisk `0x12000000`（32 MiB），tags `0x14000000`，header v1，page size 2048。

### b1（首个构建，保留作回退）

| 文件 | SHA-256 |
| --- | --- |
| `out/cheeseburger/b1/boot-op5-b1-safe.img` | `d2bd409a29ffdf7e5dca31cd1d896ec330faab4b543bb544b35e7ed5ab72b41c` |
| `out/cheeseburger/b1/MSM8998PKG_UEFI.fd` | `06002711daadb5fac38697341c2a03e9565afaf455cea90f373fd108d53c5221` |
| `out/cheeseburger/b1/op5-payload.img` | `3bc1ded555d0cd787d11c8531d2c2c60658bc4088d2842a68c042046e352b159` |

## FD 空间

- FD 固定 `0x600000`（6.00 MiB），数据区在 `+0x8000`。
- `FVMAIN` 在 FDF 里是 `NumBlocks = 0`（按内容定长），所以构建日志里的 `EFI_FV_TAKEN == TOTAL` 是设计结果，不是“没有余量”。
- 真正的余量在 FD 层。2026-10-22 实测：`FVMAIN_COMPACT` 用 `1,050,320 B`，**空闲 `5,208,368 B`（4.97 MiB）**。
- 复测方法：解析 FD 的 FV 头，累加 FFS 文件大小，用 `0x5F8000 - 已用` 得到 FV 内空闲。

## 已知问题

- **非空 ramdisk 不能用 offset 0**：32 MiB payload 会覆盖内核；设备脚本必须显式设置 ramdisk/tags offset。
- `CheeseburgerBootMemoryDxe` 的 `PcdDeviceTreeStore=0x83300000` **已确认**：payload 卷能挂载说明 DTB/initrd 解析正确。
- UFS 的 6 个 LUN 都会出现在 UEFI 里；B5 必须按容量选主 LUN（116,264 MiB），不能按枚举顺序。
- 删除 `LinuxSimpleMassStorage` 后，`PlatformBootManagerLib` 仍会注册一个指向缺失 FV 文件的 “USB Attached SCSI” 启动项（`ENABLE_LINUX_SIMPLE_MASS_STORAGE` 在 family `.dsc.inc` 里）。`UniXNUDxe` 把 UniXNU 放在 `BootOrder` 首位，正常不会走到它。
- 按键映射已实测：音量上/下 = Up/Down，电源 = Enter。
- 还没有在 OnePlus 5 上实测。

## 待实测

- [x] RAM：**8 GB**（7039 MiB 可用）
- [x] userdata `first_lba` / `last_lba`：`0x14F40` / `0x1C627FA`
- [x] `lba_bytes`：**4096**
- [x] GOP framebuffer：`0x9D400000`，1080×1920，stride 1080，BGRX，`0x7E9000`
- [ ] 触摸 I2C 总线号和地址
- [x] live DTB 的 `PcdDeviceTreeStore` 和 `/chosen/linux,initrd-*`
- [x] `boot-op5-b1*-safe.img` 的 load address 被 ABL 接受
- [x] `ButtonsDxe` 的电源/音量键驱动 UniXNU 菜单（b1e 实测）
- [ ] `UNIXNU:` 日志是否出现在屏幕上

## 主机环境

Linux x86_64 / Ubuntu 24.04：

| 工具 | 状态 |
| --- | --- |
| `python3` / `git` / `make` / `fastboot` | 可用 |
| `aarch64-linux-gnu-gcc` 13.3.0 | 可用 |
| `clang` / `clang++` / `ld.lld` / `llvm-objcopy` | 可用（LLVM 20.1.2，已软链到 `/usr/local/bin`） |
| `dtc` / `mcopy` / `mformat` / `iasl` / `nasm` / `bison` / `flex` | 可用 |
| `xcodebuild` | 缺失（内核步骤需 macOS 或 GitHub Actions） |

## 阻塞

- **当前卡点**：`launchd` 已 exec，但 dyld 不使用 shared cache，报
  `Library not loaded: /usr/lib/libSystem.B.dylib (no such file, no dyld cache)`。
- 已排除：cache 文件缺失、嵌套路径错误、`/usr/lib/dyld` 架构/版本错误。
  H49 读到 `491520 B` 的主 cache（magic `dyld`）；H52 读到
  `/usr/lib/dyld` 为 arm64，`platform=2`，`minos/sdk=0x110700`。
- 仍待确认：dyld 是否忽略 `DYLD_SHARED_CACHE_DIR`、4K 内核能否映射
  16K 构建的 cache、以及 cache signature/AMFI 的影响。
- H53 已构建 16 KiB 预读诊断，尚未上机。
- trust cache runtime 仍关闭；root auth/AMFI 仍是 bring-up 旁路。
- DarwinBoot 的 `owned=0` 未解释，暂不影响 BSD/IOKit 阶段。

## 工作日志

| 日期 | 阶段 | 内容 | 结果 |
| --- | --- | --- | --- |
| 2026-10-22 | B0 | 建立 B 线计划与状态文件 | 完成 |
| 2026-10-22 | B0 | 主机环境检查 | 缺 clang/dtc/mtools/iasl/nasm/bison/flex |
| 2026-10-22 | B0 | 初始化 UniXNU 子模块 | 完成 |
| 2026-10-22 | B0 | clone edk2-msm8998 | 完成（shallow，后补子模块） |
| 2026-10-22 | B0 | 安装主机工具 | 完成（LLVM 20；clang-18 依赖冲突，改用 clang-20） |
| 2026-10-22 | B0 | 初始化 edk2-msm8998 子模块 | 完成 |
| 2026-10-22 | B1 | 构建 upstream cheeseburger baseline | 成功；GCC 13 dangling-pointer 需修 |
| 2026-10-22 | B1 | 修复 ArmPlatformLibMem + 建分支 | 成功，提交 `0471194` |
| 2026-10-22 | B1 | 加入 UniXNU/DarwinBoot/initrd 驱动并构建 | 成功；FVMAIN 100% 满 |
| 2026-10-22 | B1 | 打包 32 MiB 测试 payload + safe boot image | 完成，待设备实测 |
| 2026-10-22 | B1 | 提交 UniXNU/UEFI 改动 | `18f3020`、`270ea4b`；UEFI `708fa06` |
| 2026-10-22 | B1 | 复核 FD 空间（解析 FD 头） | 实际空闲 3.35 MiB；“FVMAIN 100% 满”是 `NumBlocks=0` 的假象 |
| 2026-10-22 | B1 | `SerialDxe` 进 RELEASE + 删除内嵌 Linux | 成功；压缩 FV 1.00 MiB，FD 空闲 4.97 MiB；UEFI `7f34479` |
| 2026-10-22 | B1 | 重建并打包 b1b | 完成；FD `3b7a4c60…`，boot `ea89a9c7…`；待设备实测 |
| 2026-10-22 | B1 | **G1 设备实测** | `fastboot boot` 成功；UEFI 启动，UniXNU 菜单、payload 卷、倒计时均正常 |
| 2026-10-22 | B1 | 诊断程序 `CheeseburgerInfo` 采集 B0 数据 | 8 GB RAM、4 KiB UFS、userdata `0x14F40..0x1C627FA`、GOP `0x9D400000` 1080×1920 |
| 2026-10-22 | B1 | 移除重复的 `TerminalDxe` 控制台 | 成功；新 FD `217c48fa…`，空闲 4.97 MiB |
| 2026-10-22 | B1 | 按键实测（b1e，双条目菜单） | 音量上/下移动高亮，电源键选中；不需要移植 KeyInput |
| 2026-10-22 | B1 | DarwinBoot 协议检查（b1f） | `UniXNULoader` 协议 `Success`；8 个 BlockIo；GOP 1080×1920 stride 1080 |
| 2026-10-22 | B1 | 屏上日志（b1h，`console=none`，600 s） | `UNIXNU:` 行出现在屏幕；`SerialDxe` 方案成立 |
| 2026-10-22 | B1 | **G1 全项通过** | 启动、菜单、payload、按键、DarwinBoot、BlockIo、日志、B0 数据全部确认 |
| 2026-10-22 | B2 | 建立 `UniXNU-cheeseburger` 骨架 | 本地 `a67972c`；board/defconfig/bootimg/check 完成 |
| 2026-10-22 | B2 | 取 iPad7,1 DeviceTree / StaticTrustCache | `unixnu ipsw fetch` range 请求成功；KernelCache 需 macOS 的 LZFSE |
| 2026-10-22 | B2 | MSM8998 ADT 生成器 + 校验 | 生成 13 KiB 树；`check-adt.py` 全项通过；设备仓库 `31f2bf7` |
| 2026-10-22 | B2 | macOS workflow + B3 配置裁剪 | `macos-cheeseburger.yml`；内核先不带 display/boot-progress kit |
| 2026-10-22 | B2 | 推送三个仓库 | UniXNU `main`、UEFI fork `unixnu-cheeseburger`、设备仓库 `main` |
| 2026-10-22 | B3 | 首次 macOS 构建失败 | `BINDM_FORCE_ROOT` 未定义：`c4af0f0` 删了定义和消费代码，漏删设置 |
| 2026-10-22 | B3 | 修复 XNU 补丁并重跑 | `7aa5b33`；workflow run `39765227392` |
| 2026-10-22 | B3 | 第二次失败：GitHub API 403 | `prepare-xnu-source.py` 加 token/重试；`91cf813`；run `39765521269` |
| 2026-10-22 | B3 | 第三次失败：`native_legacy_display_probe` 未定义 | 补丁缺 config 保护；`b741db8`；run `39766641560` |
| 2026-10-22 | B3 | **XNU 4K 内核构建成功** | run `39766641560` 的 Build kernel 8m18s 通过；payload 因缺 sheng 私有 panic 脚本失败 |
| 2026-10-22 | B3 | 关闭可选 panic screen | 设备仓库 `6e4b487`；run `39768255111` |
| 2026-10-22 | B3 | payload 49 MiB 超过 32 MiB 卷 | 卷改 64 MiB，tags 挪到 `0x18000000`；设备仓库 `79f5a27`；run `39769403554` |
| 2026-10-22 | B3 | **kernel + payload 构建通过** | run `39769403554`；本地组装 `boot-op5-b3.img` 并通过预检 |
| 2026-10-22 | B3 | 首次 B3 上机 | `fastboot boot` 成功；设备进入 Qualcomm crashdump 后重启，说明 XNU 执行并崩溃；panic 只显示几秒，没拍到 |
| 2026-10-22 | B3 | 移植 panic screen | 设备仓库 `233f283`；`patch-xnu-entry-probe.py` + `xnu-panic-screen.c` + `Font.h`；run `39771381162` |
| 2026-10-22 | B3 | panic 补丁找不到 `ld.lld` | 工具查找加 Homebrew keg/versioned 回退；本地已对 4K 内核验证补丁成功；`057a378`；run `39772620480` |
| 2026-10-22 | B3 | **首次 4K 内核上机** | 内核入口执行（早期色条），随后硬件级崩溃；未到控制台；DarwinBoot window 读到 0，待插桩 |
| 2026-10-22 | B0 | TWRP 备份 + 存储映射 | 291 MB，16 个分区 + GPT；`persist` 在 sda2，`userdata` sda13；`e83df1d` 修 adb recovery 状态；该旧备份已失效并删除 |
| 2026-10-24 | B0 | **当前系统重新备份（旧备份已删除）** | TWRP 3.4.0-1；4.7 GB，全部命名分区（除 `userdata`/`cache`）+ sda–sdf 主/备 GPT；关键分区远程 SHA-256 一致；`.cache/firmware/backups/20261024-125857/` |
| 2026-10-22 | B3 | CI 流水线首次全绿 | run `39776627855`：kernel + payload（含 panic screen）+ 上传全部成功；payload `551fa561…` |
| 2026-10-22 | B3 | DarwinBoot 窗口诊断 | 临时固件 `cb47dfe6…` 打印 ADT 属性和解析值，待上机读数 |
| 2026-10-22 | B3 | 诊断：ADT 值正确，`WindowSize` 仍 0 | 大字号确认 `darwinboot-memory-size=0x80000000`；强制 2 GiB 仍崩 → 窗口不是根因 |
| 2026-10-22 | B3 | **`VirtualBase` 修正** | DarwinBoot 原先传最低段（`__PRELINK_TEXT`），改为 `__TEXT`；行为从“硬崩进 crashdump”变为“挂住不重启”，MMU 阶段可能已过 |
| 2026-10-22 | B3 | 诊断补丁存档 | `patches/darwinboot-virtbase-physbase.patch`（诊断打印、强制 2 GiB、VirtBase、PhysicalBase 修正） |
| 2026-10-22 | B3 | stage 4 探针 / MMU 定位 | stage 2 出现、stage 3 不出现 → 卡在 MMU 使能；`PhysicalBase` 偏移假设，已编译待测（`boot-physbase.img`） |
| 2026-10-23 | - | 产物归档 + 状态同步 | B3 的 kernel/payload/CI 日志从 `/tmp/opencode` 归档到 `~/UniXNU/.cache/firmware/b3/`（含 `README.md` 哈希表）；下一步收敛为上机 `boot-physbase.img`；未上机 |
| 2026-10-23 | B3 | stage 3 计时复核 + PSCI 对照镜像 | `payload-stage3b` 的 stage 3 实际是 20 秒后复位，旧“几秒”记录需重判；给 `patch-xnu-entry-probe.py` 加 `--reset-before-mmu`，生成 `boot-psci-control.img`（蓝条 + 8 秒复位，不写 SCTLR）；预检通过，未上机 |
| 2026-10-23 | B3 | PSCI 复位路径上机验证 | 8 s 对照 Android 49.56 s、30 s 对照 71.61 s，差 22.05 s ≈ 延时差 22 s；复位由探针控制，不是 watchdog |
| 2026-10-23 | B3 | **MMU 已过** | 20 s pre-MMU 对照 kernel_start `+52.90 s`，`boot-physbase.img` stage 3 `+53.42 s`，差 0.52 s；stage 3 已执行，MMU 切换成功；下一步转 `arm_init` / C 初始化 |
| 2026-10-23 | B3 | stage 4 二分到 `_arm_vm_init` | `_arm_set_kernel_tbi`、`_PE_init_platform(FALSE)`、`_cpu_bootstrap` 均返回；`_arm_vm_init` 不返回；`_vm_set_page_size` 返回、`_arm_vm_prot_init` 不返回 |
| 2026-10-23 | B3 | **根因：prelink 段在 KVA 映射外** | 无条件调用 #4 `arm_vm_page_granular_RWNX(segPRELINKTEXTB, ...)` 返回点不复位；`size=0` 通过，4 KiB/4 MiB/2 MiB 对齐均挂；`start.s` 从 `__TEXT` 向上映射，`__PRELINK_TEXT` 在其下方 0x2d88000，首次 TTE 访问即未映射；需重建内核修 prelink/start.s |
| 2026-10-23 | B3 | 映射修复验证 | `mapfix2-stage3`（KVA 映射起点下移到 `__PRELINK_TEXT`）PASS，kernel_start `+53.24 s`；MMU 仍正常。`mapfix2-prot4` 仍挂，剩余阻塞是 helper 的 `assert(start >= gVirtBase)`：prelink 在 `__TEXT` 下方，断言恒假 |
| 2026-10-23 | B3 | **prelink 布局修复** | 设备仓库 `07503a9`：`slide = 0x4100000`，CI run `39951630215` success；新布局 `__PRELINK_TEXT` `0x937c000` 在 `__TEXT` 之上，无重叠；`slide-prot4` PASS `+42.93 s`，`_arm_vm_init` PASS `+45.23 s` |
| 2026-10-23 | B3 | 新 crash 窗口 | `_PE_init_platform(TRUE)` 返回/调用前均 crashdump `05c6:900e`；崩溃在 `arm_vm_init` 返回后、PE1 调用前；待测控制台初始化窗口 |
| 2026-10-23 | B3 | DT panic region | `CONFIG_XNU_DT_PANIC_REGION=y`；CI `39956257439`；`PE_create_console` 不再访问 BCM2837 `0x3c0fc000` |
| 2026-10-23 | B3 | source corecrypto + 重新封印 | `CONFIG_XNU_CRYPTO_BOOTSTRAP="source"`；CI `39960247193`；prelink 后用 `seal-source-crypto-kernel.py` 重新封印，FIPS POST 通过；`_kmem_crypto_init`/crypto ctx PASS |
| 2026-10-23 | B3 | trust cache runtime 延后 | `img4if`/`amfi` 未注册导致 `image4 interface not available @kern_trustcache.c:472`；关闭 `CONFIG_XNU_TRUST_CACHE_RUNTIME`；CI `39968985568` |
| 2026-10-23 | B3 | **XNU 进入 BSD/IOKit** | `_kernel_bootstrap` 早期、`_kernel_thread_create`、`_kernel_bootstrap_thread`、`_bsd_init`、`_IOKitBSDInit` 入口/返回全部 PASS；普通控制台无输出，panic screen 可用；下一阶段 B5 UFS/root |
| 2026-10-23 | B3 | DarwinBoot 大标题移除 | 删除 `AdtNode` 的 `UnicodeSPrint`/`DiagDraw`；保留小字 ADT 日志；新 FD `6da8ee30…`，无标题镜像 `1c25c29a…`；补丁已提交，子模块干净 |
| 2026-10-23 | B3 | **普通控制台输出恢复** | `initialize_screen(kPEAcquireScreen)` 会 `gc_enable(FALSE)`；在 `console_init` 后调用 `gc_enable(TRUE)`，XNU 日志/panic 均上屏；测试镜像 `boot-console-fix.img` `8d08d3b6…` |
| 2026-10-23 | B3 | **新阻塞：kmod start/stop 未 slide** | 日志显示 `Libm`/`corecrypto` start 指针在 `0xfffffff003…`，kext 在 `0xfffffff009…`；`emit_prelink.py` 只写 kmod address（+156），没有写 start/stop（+180/+188）；需修 prelink kit |
| 2026-10-25 | B5 | **root 挂载成功** | 写入/校验 SHNGROOT userdata；OS/SystemOS 窗口发布；APFS `nx_mount`；根 UUID `04769D26-2CA0-4825-B443-E2E5F88300F2` 匹配；停在 `rootvp not authenticated after mounting @bsd_init.c:1100` |
| 2026-10-25 | B5 | **修复 UFS 定时等待** | `IODelay`/`IOSleep` 会永久挂住；NOP/超时/轮询改为 `mach_absolute_time` 忙等；128 KiB 窗口合并读；镜像 `boot-ufsH7-panic.img`，CI `40155569182` |
| 2026-10-25 | B5 | **root auth bring-up** | 增加 boot arg `native-skip-root-auth=1` 的临时旁路；仅用于继续到 launchd/AMFI，最终替换为 VerifiedRoot |
| 2026-10-25 19:37 | B5 | **修正 boot 镜像打包** | `payload.img` 是 64 MiB FAT，不是 boot image；正确格式必须是 `make-bootimg.py` 生成的 68,564,992 B Android bootimg（UEFI kernel + payload + tags）。H23/H47 均为此格式；直接把 payload 当 bootimg 会停在 OnePlus logo/fastboot |
| 2026-10-25 | B5 | **SystemOS state / 主 cache** | H26：`stage=6 status=0 union=1 dev=/dev/disk4s1 err=0 dyld=8`；H27：主 cache `size=491520 lookup=0 stat=0`，magic `dyld_v1   arm64` |
| 2026-10-25 | B5 | **split cache 完整** | H29/H34：main 491520；`.01`–`.09` 分别 114720768/49152/114229248/229376/115458048/327680/75857920/245760/112934912；`.symbols` 68517856；主头 `cacheType=2 subCacheArrayCount=52 img=624/3354 subtype=1` |
| 2026-10-25 | B5 | **52 个 subcache 预检通过** | H37：使用 arm64 三空格 magic 和每文件 16 KiB pread，`ok=52 fail=0`；H35：主头 `cs=475136+16384==size=491520`、`prot=5/5`、`os=0x110700`、mapping 有效 |
| 2026-10-25 | B5 | **mount / env / ADT 诊断** | H39 `/System/Cryptexes/OS` 与 H40 `.../Caches/com.apple.dyld` 均 ENOENT；H41/H42 改挂 `/System/Library/Caches` 成功。ADT 增加 `/chosen/asmb`（`lp-sip0=0x6f` u64，`lp-sip1=1` u32）；H45 union + asmb 仍未改变 `no dyld cache`，且无 `shared_region`/trust-cache 报错 |
| 2026-10-26 00:18 | B5 | **env / 路径已证实** | H46：`envp=1500 err=0 env0=1400 env='DYLD_SHARED_CACHE_DIR=/System/Library/Caches/System/Library/Caches/com.apple.dyld'`；H47：`eperr=0 estat=0 esize=1792 etype=2`，c0–c4 全部 0 → 嵌套目录及父目录都存在，但尚未验证 `com.apple.dyld/dyld_shared_cache_arm64` 文件本身 |
| 2026-10-26 11:12 | B5 | **H48 launchd 实跑** | 移除 H47 `NATIVE_PATH` panic，保留 `DYLD_PRINT_SEGMENTS=1`；launchd 仍报 `Library not loaded: /usr/lib/libSystem.B.dylib (no dyld cache)`；CI run `40262171826` |
| 2026-10-26 12:09 | B5 | **H49 cache/dyld 文件确认** | `boot-ufsH49-file.img`：嵌套 cache 文件存在，`copen=0`、`csize=491520`、`ctype=1`、magic `dyld`；`/usr/lib/dyld` 存在，`size=937056`、arm64、`ncmds=18`。路径和文件层排除 |
| 2026-10-26 12:22 | B5 | **H50 private region 试验** | `boot-ufsH50-dyldlog.img`：加 `DYLD_SHARED_REGION=private`/额外打印 env；SystemOS mount 失败 `14 @kern_exec.c:8040`，没有拿到 dyld 日志 |
| 2026-10-26 12:42 | B5 | **H51 修 env 槽位** | `boot-ufsH51-private.img`：修正 private-region 的 env 向量槽位；launchd 仍 `no dyld cache` |
| 2026-10-26 13:27 | B5 | **H52 dyld 身份** | `boot-ufsH52-dyldver.img`：`/usr/lib/dyld` `magic=feedfacf`、`cputype=0x0100000c`（arm64）、`platform=2`、`minos/sdk=0x110700`、UUID `3b1e2a…`；架构/构建版本基本排除 |
| 2026-10-26 13:49 | B5 | **H53 16K 预读诊断** | `boot-ufsH53-16k.img`（SHA `fad6304c…`）：已上机，`cerr=0 cstat=0 copen=0 cread=0 resid=0 size=491520`；16 KiB pread 正常，数据读取层排除；`mode`/`dirmode` 因 panic 格式 `%o` 未显示 |
| 2026-10-26 | B5 | **H54 全 mapping 预检** | `boot-ufsH54-map.img`：`NATIVE_H54: ok=52 fail=0 main=512/1/491520 mc34=2 lmax34=1 mc52=2 lmax52=1`；52 个 subcache 的 mapping、连续性、重叠、TEXT/LINKEDIT 权限全部符合 dyld `preflightCacheFile`，mapping 候选排除 |
| 2026-10-26 | B5 | **H55 env/AMFI 首次验证** | `dyld_flags=0xA` + `DYLD_AMFI_FAKE=0xffffffff` + `DYLD_PRINT_TO_FILE=/dev/console`；仍无 dyld 日志、无 `NATIVE_CACHE_SIG`，final error 不变 |
| 2026-10-26 | B5 | **H58 env 栈证据** | `NATIVE_H58: argc=1 envc=8 cache=1 amfi=1 print=1 env=/System/Library/Caches/System/Library/Caches/com.apple.dyld`；env 确实进入新进程栈，排除“dyld 没拿到 env” |
| 2026-10-26 | B5 | **H59 devfirm/internalInstall** | dyld 的 iOS `internalInstall()` 读 commpage `_COMM_PAGE_DEV_FIRM`，不是 `os_variant`。H59 强制该位为 1，使 `DYLD_AMFI_FAKE`/testMode 真正生效；移除 H58 panic 和 os_variant 补丁 |

## H60–H79 用户态突破记录（2026-10-26/27）

| 阶段 | 结果 |
| --- | --- |
| **H60** | 用 launchd 进程的 `vfs_context` 直接 lookup/open nested cache：`dir=0/0 dmode=0x41ed file=0/0 fmode=0x81ed size=491520`。证明路径和权限层正常。 |
| **H61** | 用 bindfs 把 `SystemOS/System/Library/Caches` 叠到 `/System/Library/Caches`；仍未加载 cache。bindfs 不是最终方案。 |
| **H62** | 改挂 SystemOS 到 `/private/preboot`，启用 `XNU_OWNER_CACHE_CARRIER=1`；路径符合 `offline-owner-cache-carrier` 的 canonical 目录，但仍无 cache。 |
| **H63** | 加 `early_security_init=com.apple.driver.AppleMobileFileIntegrity=0xfffffff005ab4ae0`；prelink 接受，AMFI early init 运行，但仍无 cache。 |
| **H64** | 试 `amfi_get_out_of_my_way=0x1`、`amfi=0x80`、`DYLD_AMFI_FAKE=0x7f`；仍无。 |
| **H65** | `platform=2 min=110700 sdk=110700`，`argc=1 envc=8 cache=1 amfi=1 print=1 env=/private/preboot/System/Library/Caches/com.apple.dyld`；平台/env 排除。 |
| **H66/H67** | 主机确认实际 `/usr/lib/dyld` 含 `DYLD_SHARED_CACHE_DIR`/`DYLD_AMFI_FAKE`/`DYLD_PRINT_ENV`，937056 B；launchd 也是 platform 2。H67 open trace：`dir=-2 path=/private/preboot/... ->0`、`dir=1 path=dyld_shared_cache_arm64 ->0`、`F_ADDFILESIGS_RETURN cmd=0x61`；cache 已真正打开并进入 preflight。 |
| **H68/H69/H70** | 真实 fcntl 被 AMFI 拒绝：`cache ... is adhoc signed`、`unsuitable CT policy 0`。H69 恢复 `amfi=0x94` 无效；H70 的 `MAC_VNODE_CHECK_DYLD_SIM` 也无效。 |
| **H71/H72/H73** | H71 忽略 MACF 拒绝继续 attach blob，但报 `disallowing unsupported code signature page shift: 14`（4 KiB 内核 vs 16 KiB cache 签名）。H72 启用 `XNU_NATIVE_CS_HASH_CACHE=1` 未生效，因为 `ubc_subr.o` 的 CFLAGS 被重复覆盖；H73 合并 `-DXNU_NATIVE_CS_HASH_CACHE` + `-DXNU_NATIVE_CACHE_SIGNATURE_BYPASS` 后 page-shift 消失。 |
| **H74/H75** | H74 去掉 dyld_sim flag，仍 `first page not mmap()able`；H75 加 `CS_PLATFORM_BINARY`，mmap 仍被 AMFI 拒绝。 |
| **H76** | 在 `kern_mman.c` 忽略 `mac_file_check_mmap` 的 MACF 错误；**shared cache 首次成功映射**：dyld 日志出现 `Using mapping in dyld cache for /usr/lib/swift/libswiftDarwin.dylib` 等；随后 `Setting up kernel page-in linking for /sbin/launchd` 并出现 `userspace stack not initialized` panic。 |
| **H77** | `dyld_flags=0x8A`（加 0x80 disablePageInLinking）；page-in linking panic 消失，但 launchd 退出：`pid 1 exited -- exit reason namespace 1 subcode 0x7`。namespace 1 = JETSAM，subcode 7 = `JETSAM_REASON_MEMORY_PERPROCESSLIMIT`。 |
| **H78** | 把 `owned_memory_size` 从 2 GiB 提到 4 GiB；仍是同一个 per-process jetsam，不是物理内存总量。 |
| **H79（待确认）** | 把 `DYLD_SHARED_REGION=private` 改为 `system`，避免 1.5 GB cache 计入 launchd 的单进程内存上限；CI `40517795790` 成功，镜像 `boot-ufsH79-system.img` 已生成/启动，**结果尚未确认**。 |

### 当前生效的临时 bring-up 配置

- 主仓：UniXNU `949d1a0`；设备仓：UniXNU-cheeseburger `67dda64`。
- SystemOS 挂载点：`/private/preboot`。
- `XNU_OWNER_CACHE_CARRIER=1`、`XNU_NATIVE_CS_HASH_CACHE=1`、`XNU_OWNER_CACHE_STARTUP=1`。
- `DYLD_SHARED_CACHE_DIR=/private/preboot/System/Library/Caches/com.apple.dyld`。
- `dyld_flags=0x0000008A`（testMode + skipIgnition + disablePageInLinking）。
- `DYLD_SHARED_REGION=system`（H79 起；H77/H78 是 private）。
- `DYLD_AMFI_FAKE=0x7f`；`amfi_get_out_of_my_way=1`、`amfi=0x94`、`amfi_allow_any_signature=1`、`cs_enforcement_disable=1`、`native-skip-root-auth=1`、`native-skip-exec-signature=1`、`launchd_unsecure_cache=1`。
- 临时签名/mmap bypass：`zzz-cache-signature-bypass.patch` 忽略 MACF signature/mmap，设置 `CS_VALID|CS_SIGNED|CS_PLATFORM_BINARY`；最终必须移除。
- `panic_screen=false`：新增诊断代码占用了 `patch-xnu-entry-probe.py` 的固定 panic cave；H77–H79 用普通内核控制台输出。最终要恢复。
- CI 临时改用 `macos-15-intel` + Xcode 26.3 + `-Wno-unknown-warning-option`，因为 macOS 26 runner 容量长期排队；最终要恢复 `macos-latest`/Xcode 26.6。

### 主机侧只读确认

- OS APFS 窗口已只读挂载于 `/tmp/opencode/mnt-os`。
- `/usr/lib/dyld` 实际 937056 B，包含 `DYLD_SHARED_CACHE_DIR`、`DYLD_AMFI_FAKE`、`DYLD_PRINT_ENV`。
- `/sbin/launchd` `LC_BUILD_VERSION platform=2 minos/sdk=0x110700`。
- `/private/preboot` 和 `/System/Library/Caches` 在 OS root 存在；`/System/Library/Caches/com.apple.dyld` 在挂载前不存在。

### 下一步（历史 H79 条目，已被下方 H80–H82 记录取代）

- H79 的 per-process jetsam 已在 H81 通过 system-wide shared region 绕过；
  当前问题已变成 H81 的 `OS_REASON_LIBXPC` assertion 和 H82 的 Data skeleton 构建。
- 详细顺序见“H80–H82 记录”末尾。不要删除 `lock_rw.c` 的 panic assertion；修 caller。

## H80–H82 记录（2026-10-28）

### H80：private cache + PID1 footprint

- 回到 `DYLD_SHARED_REGION=private`，在 `native_owner_cache_prepare()` 里调用
  `task_set_phys_footprint_limit_internal(proc_task(p), -1, ...)`。
- `-1` 实际会回落到 `max_task_footprint`，不是无限；H80 仍出现
  `pid 1 exited -- exit reason namespace 1 subcode 0x7`，即
  `JETSAM_REASON_MEMORY_PERPROCESSLIMIT`。
- 结论：private cache 把约 1.5 GB cache 计入 launchd 的 per-process footprint，
  单靠 PID1 的 task limit 清理没有解决。
- H80 镜像：`boot-ufsH80-tasklimit.img`，CI `40526638185`，sha256
  `c6d32463b84d05392325b61c3bb92e1a2d5fd0a1ffca832710a74d96dd9920de`。

### H81：system-wide shared cache 成功

- 改回 `DYLD_SHARED_REGION=system`。
- `vm_shared_region_start_address()` 的 “failed” 只是共享区为空时的正常提示；
  真正失败点是 `shared_region_map_and_slide_np()` 的 AMFI/CS 检查。
- 新增 `zzz-shared-region-cache-bypass.patch`：
  - 在 `bsd/vm/vm_unix.c` 的 `mac_file_check_mmap()` 上临时忽略 AMFI；
  - 对共享区映射的 `ubc_cs_is_range_codesigned()` 检查临时放过；
  - 打印 `NATIVE_H81_SR: setup/root check/preboot/map_file` 诊断。
- `vm_unix.o` 增加 `-DXNU_NATIVE_CACHE_SIGNATURE_BYPASS=1`。
- **H81 上机结果：system-wide cache 映射成功，launchd 不再被 jetsam 杀掉。**
- H81 镜像：`boot-ufsH81-shared-region.img`，CI `40688172208`，sha256
  `4fdf7e8baf2986e61e2f64345172916d27313b6adda7e7d0609c7799cb85317d`。
- 但 launchd 很快以
  `pid 1 exited -- exit reason namespace 7 subcode 0x1`
  退出；namespace 7 = `OS_REASON_LIBXPC`，描述为
  `assertion failed: 992399: launchd + 338744 [48525932-8C69-315E-A51A-CC943E4E5051]: 0x1`。
- 结合 launchd 依赖 `/private/var/db/com.apple.xpc.launchd`、`/private/var/tmp`、
  `/private/var/run`，判断为 **没有可写 Data 卷 `/private/var`**，进入 H82。

### H82：Data skeleton + fork RamBlock（已构建，启动返回 fastboot）

- 新增 `prepare-owner-data-fixture.py --skeleton --size-mib 64`：
  - CI 上用 `hdiutil` 创建 64 MiB APFS Data skeleton；
  - 只创建 `/private/var` 所需目录（`db`、`db/com.apple.xpc.launchd`、`log`、
    `run`、`tmp`、`keybags`、`mobile/*`、`containers` 等），不复制 OS 的 400+ MB var；
  - 输出 `build/owner-data-fixture/seed.json` 和 `owner-data-32.cdr`；
  - workflow 从 `seed.json` 取 `volume_uuid`，写入 `XNU_OWNER_DATA_VOLUME_UUID`。
- board payload 当前为 76 MiB；payload 加 `--data-ramdisk`。
- 临时内核配置：
  - `XNU_OWNER_DATA_MOUNT=1`
  - `XNU_NATIVE_RAM_BLOCK=1`
  - `XNU_OWNER_BOOT_MANIFEST=0`（仅为当前 volatile Data bring-up）
  - `XNU_OWNER_CACHE_MOUNT=1`、`XNU_OWNER_CACHE_STARTUP=1`、
    `XNU_OWNER_CACHE_CARRIER=1`、`XNU_NATIVE_CS_HASH_CACHE=1`
- 构建失败与修复：
  1. `40693595648`：bootstrap pass 被
     `NativeRamBlock requires the qualified original storage/AES prelink closure`
     挡住。已在 bootstrap pass 关闭 `XNU_NATIVE_RAM_BLOCK`，并给 volatile Data
     加临时豁免。
  2. `40695589323`：最终链接缺 `native_ufs_store_*` 和
     `native_verified_block_manifest_sha384`。已临时关闭 boot manifest；store
     符号改为在 fork 的 `RamBlock` 子模块里提供 weak no-op fallback，不再放进
     XNU `kern_exec.c`。
  3. `40699122544`：CI fetch XNU 网络超时，与代码无关。
  4. `40701763118`：owner patch 插入 weak stubs 后 hunk 行数未更新，
     `corrupt patch at ...:345`。本地已重算 hunk 计数并提交，**尚未重新构建**。
- **H82 构建成功：** CI `40712047295`，payload 160 MiB，镜像
  `boot-ufsH82-data.img`，sha256
  `5069bad1f910d753e3b8c6bd8220ce29e5a7c445ddf58f475cdcd5f4320d00a1`。
- **H82 启动结果：** `fastboot boot` 接受并显示 `Booting OKAY`，但 30 秒后设备
  又回到 fastboot；尚未看到 UEFI/内核屏幕。可能是 160 MiB ramdisk 与 Android
  tags 的布局问题，或 UEFI/DarwinBoot 对 tags 地址的假设；Data/launchd 尚未验证。
- 为避开 160 MiB payload 与 `tags_offset=0x08000000` 重叠，本次镜像把 tags
  移到 `0x0c000000`（绝对地址 `0x1c000000`）；这本身也可能是启动失败原因。

### 当前临时配置（H81/H82）

- `DYLD_SHARED_REGION=system`。
- `XNU_OWNER_DATA_MOUNT=1`；当前 16 MiB APFS Data skeleton；`/private/var` 可写。
- `XNU_NATIVE_RAM_BLOCK=1`，无 NativeUfs 持久化；store fallback 为 fork
  `lss717/RamBlock`（submodule `drivers/NativeRamBlock`，提交 `bc079a5`）里的
  weak no-op。
- 主仓 `.gitmodules` 的 `drivers/NativeRamBlock` 已改指
  `https://github.com/lss717/RamBlock.git`；submodule 本地 `origin` 指 fork，
  `upstream` 指 `UniXNU/RamBlock`。
- `XNU_OWNER_BOOT_MANIFEST=0`：**当前 Data 未经过 authenticated owner boot manifest**。
- `max_task_pmem=0`：全局 task footprint 上限临时移除。
- `zzz-cache-signature-bypass.patch` + `zzz-shared-region-cache-bypass.patch`：
  AMFI/cache/CS/mmap 的临时 bring-up bypass；最终必须移除。
- `panic_screen=false`：固定 panic cave 被诊断代码占用；最终恢复。
- CI 仍为 `macos-15-intel` + Xcode 26.3；最终恢复 macOS 26 / 正常 runner。

### 下一步（按顺序）

1. 确认 H82 启动失败发生在哪一层：先看屏幕/UEFI 日志；若仍回 fastboot，
   优先怀疑 tags 地址或 160 MiB ramdisk 的装载布局。
2. 回退到 H81 镜像验证设备仍可进入内核；再试更小的 32/64 MiB Data seed，
   保持默认 `tags_offset=0x08000000`，或修正 UEFI/DarwinBoot 的 tags 处理。
3. 启动成功后观察 Data mount 是否通过、launchd 的 `namespace 7 subcode 0x1`
   是否消失；若 Data mount 失败，看
   `offline scratch Data mount failed: <errno>`、seed UUID、RAMDisk handoff。
4. 若 launchd 继续，进入 watchdog/tzinit、launchd job、`/private/var/db` 初始化。
5. 最终恢复 `XNU_OWNER_BOOT_MANIFEST=1`、VerifiedRoot/AMFI/TrustCache；
   用 qualified NativeUfs/AES store 替换 fork 里的 weak no-op fallback，并移除
   Data skeleton、AMFI/CS/mmap bypass、`panic_screen=false`。

### H82b–H82f 结果（2026-10-29）

| 镜像 | 配置 | 大小 | 结果 |
| --- | --- | --- | --- |
| `H82b` | 32 MiB seed / 96 MiB payload / 默认 tags | 102,119,424 B | fastboot，未进 UEFI |
| `H82c` | 同 payload / tags=`0x1c000000` | 102,119,424 B | 曾到 OnePlus 第一屏，UEFI 未起；重试不稳定 |
| `H82d` | 手工 90 MiB payload（32 MiB seed）/ 默认 tags | 95,827,968 B | fastboot |
| `H82-kernelonly-64` | H82 内核、无 Data seed、64 MiB payload | 68,564,992 B | **进入内核**，panic `offline scratch Data mount failed: 2` |
| `H82e` | 8 MiB seed / 64 MiB payload | 68,564,992 B | **进入内核**；launchd 仍 `namespace 7 subcode 0x1`；Data 状态未知 |
| `H82f` | 16 MiB seed / 76 MiB payload | 81,147,904 B | 已启动，未回 fastboot；等待屏幕 |

结论：

- H82 内核、fork RamBlock、UEFI/ADT 都能工作；`kernelonly-64` 已证明。
- 98 MiB / 95.8 MiB boot image 不能稳定启动；68.56 MiB 可以。77.4 MiB 正在验证，
  bootloader 上限大致在这两者之间。
- `H82e` 的 launchd assertion 与 H81 完全相同；8 MiB Data 可能未成功挂载，或
  容量/骨架不足，需要在 H82f 看新增的 `NATIVE_H82_DATA`。
- `H82f` 的状态行会打印：
  `NATIVE_H82_DATA: status=<errno> stage=<n> ro=<0/1> dev=<dev> size=<bytes>`。
  `status=0, stage=4, ro=0` 才表示 Data 挂载成功。

镜像：

- H82e：`boot-ufsH82e-8m.img`，sha256
  `5a83b98c565826912e9d92bf61256cbf06f89b07824608c72f5d6b97e100a8cc`
- H82f：`boot-ufsH82f-16m.img`，sha256
  `e636336d8e7cb9aec9ab6fbc5ce1ebfeaf757f0d5cff227e8ee143ecba300038`
- H82f CI：`40797926609`；Artifact `12864254042`。

### fork 接入

- 上游实际仓库：`UniXNU/RamBlock`（旧 `NativeRamBlock` URL 重定向）。
- fork：`lss717/RamBlock`，提交 `bc079a5`
  （`ram: provide weak volatile-store fallbacks`）。
- 主仓路径保持 `drivers/NativeRamBlock`；`.gitmodules` URL 改为
  `https://github.com/lss717/RamBlock.git`。
- 子模块 `origin` 指 fork，`upstream` 指 `UniXNU/RamBlock`。
- `NativeRamBlock` 的 weak no-op store fallback 已在 fork 中，不再放在 XNU
  `kern_exec.c`。
- device 仓库补了 base `IOBlockStorageDevice` 绑定：
  `27a6e70 storage: bind base IOBlockStorageDevice metadata`。

### H82f 之后

1. 等 H82f 屏幕，读取 `NATIVE_H82_DATA`。
2. 若 `status=0 stage=4`，launchd assertion 就不是 Data 挂载本身；继续查
   `/private/var/db`、bootstrap、libxpc。
3. 若 Data mount 失败，优先看 errno、seed UUID、16 MiB APFS 容量。
4. 若 77.4 MiB 启动不稳定，回退 68.56 MiB 尺寸，另找 Data 持久化/外置方案。
5. 最终恢复 `XNU_OWNER_BOOT_MANIFEST=1`、VerifiedRoot/AMFI/TrustCache，
   替换 weak UFS fallback，移除 bypass。


### H82g–H83c：launchd `namespace 7` 逐项排除（2026-10-29/30）

已用临时 `fastboot boot` 验证，均未写分区：

| 候选 | 配置 | 结果 |
| --- | --- | --- |
| H82j | 256 MiB packed Data | core dump ENOSPC，launchd 仍 `namespace 7 subcode 0x1` |
| H82k | sheng watchdog/job policy | 同 |
| H82l | `cpus=1` + 关闭 core + launchd 退出寄存器 | 同；`cpus=1` 无效 |
| H82m | 只加 StaticTrustCache | AMFI 仍报 adhoc，launchd/dyld 被 SIGKILL |
| H82n | `UNION_SYSTEMOS=1` | 同 |
| H82o | abort 入口打印描述/栈 | `assertion failed: 992999: launchd + 338744 [4B525992-...]: 0x1`，ns=7 code=0x1 |
| H82q | Static+System 完整 trust bundle | AMFI 仍 adhoc/CT policy 0 |
| H82r | 全线程 PC/帧 | abort 线程 tid=243，245 已 `pc=0`，主线程现场丢失 |
| H82s | 去 `launchd_unsecure_cache`/`dyld_flags`/lite | 同 |
| H82v | 1 GiB Data + 1 GiB window | 同 |
| H82w | 最小 AMFI 绕过 | 同 |
| H82x | `XNU_OWNER_DATA_MOUNT=0`、无 NativeRamBlock/Data | 同，Data 不是原因 |
| H83a | `XNU_TRUST_CACHE_RUNTIME=1` + 完整 trust | 未触发 AMFI/Image4 缺失 panic |
| H83b | `load_static_trust_cache` 缺 range 即 panic | 未触发，旧固件确实交付了 TrustCache range；但 AMFI 仍 adhoc |
| H83c | H83b + `amfi=0x94` | 同 |
| H83d | H83b + ADT `amfi-only-platform-code=0` | 同；AMFI 仍 adhoc/CT policy 0 |

关键文件/事实：

- 原始 `/sbin/launchd` 已从 21H461 OS DMG 提取，sha256
  `8cec9a3c44a443b08e977ccc5d577850316ad428c575dc01f09b018dbd5b6a25`，
  UUID `4B525992-8C69-315E-A51A-CC9433E45B51`，与 panic 描述一致。
- 完整 static+system trust bundle sha256
  `7416ca4f8377146626bb284e2133c76bee6cb4e3ee0af5d648b186235a7323a7`，
  与 DinnerABC 原始 bundle 一致；`audit-target-userspace.py` 可验证 launchd/dyld
  cdhash 在 StaticTrustCache 中。
- H82r 的 launchd 栈只含 libxpc/exit；主线程在 abort 前已退出。
  `launchd + 338744` 是 Swift error thunk，单独不能定位原始 XPC 调用。
- 新固件（当前 `drivers/DarwinBoot` 重新构建）不可用：RELEASE 报
  `Out of Resources`，随后 `Synchronous Exception at 0x...17A0B1258`；旧固件
  已恢复为测试基线。旧固件能给 kernel 提供 TrustCache range，但 AMFI 仍把
  launchd/dyld 判为 adhoc；不要重复把新固件当作已验证候选。
- 已排除：`cpus=1`、SystemOS union、Data mount、1 GiB Data/内存窗口、
  `launchd_unsecure_cache`、`dyld_flags`、AMFI 参数组合、lite policy；
  H83d 也排除了 `amfi-only-platform-code=0`。

下一步（仅剩高价值方向）：

1. 对照 DinnerABC 的 `patches/p10` AMFI/Image4/TrustCache 与 `amfi.c`/
   `interface.c` 注册路径，确认我们的 AMFI 是否真正把 loaded TrustCache 用于
   cdhash 查询；当前 `amfi`/`img4` 非空、range 存在，但 AMFI 仍拒绝。
2. 修复新固件 FV/UniXNU 的 `Out of Resources`/UEFI exception，才能重测
   TrustCache 侧车；旧固件只可作为绕过基线。
3. launchd Swift 主线程在 abort 前退出的现场仍缺失；在拿到真实 AMFI trust
   路径前，继续加 boot args 不会改变 `namespace 7`。


### H83g–H84b：launchd 突破与新的 UFS 读阻塞（2026-10-30）

关键突破：`namespace 7 subcode 0x1` 不是 Data/CPU/union/AMFI 参数导致的，而是
`startup.c` 里的 TrustCache 初始化被 `XNU_NATIVE_TRUST_CACHE` 条件编译掉了；
`load_static_trust_cache()` 从未执行。

- `patches/xnu/native-trust-cache-runtime.patch` 已改为无条件执行
  `trust_cache_runtime_init()` + `load_static_trust_cache()`。
- H83g 在入口 panic 证明函数执行：`amfi` 非空、`TrustCache.version=53`。
- H83h 放开入口后，launchd 真正运行并打印
  `com.apple.xpc.launchd ... Doing boot task`，`namespace 7` 消失。
- H83j 在 ADT `/chosen` 加入 48 字节 `boot-manifest-hash` 占位后，
  `mount-phase-1/2`、`data-protection`、`keybag`、`usermanagerd` 等 boot task 通过。
- H83j 新首错是 `tzinit` 找不到 `/private/var/db/timezone`（exit 22）。
- H83k 把原 OS 的 `/private/var/db/timezone` 注入 256 MiB packed seed
  （UUID 不变，encoded sha256 `e94a0683...`）；`tzinit` 不再 panic。
- H84a 启用 `XNU_NATIVE_MOBILE_DISPLAY=1`；H84b 仅把 payload 改成 `cpus=8`。
  两者都停在 `NativeUfsProbe` 的 UFS 读进度：最后一条 `OS req=...` 没有 `done`，
  `winBytes` 约 3.1 GiB，`winFlight=1`。这不是“写盘”，是 launchd/用户态读取
  SystemOS/OS 时的一个 UFS 请求没有返回；display 尚未真正执行。

当前可用镜像/哈希：

| 镜像 | sha256 | 说明 |
| --- | --- | --- |
| H83h | `de61bce881d355bcb0ed68b6363f20887b17726708c46e59e2f16d58b499f6d9` | launchd 首次越过 namespace7；无 Data |
| H83j | `5086999bb9dcf6489b65c6f863e5660097dad9568e39a1ef642ec7038ee47207` | mount/manifest 占位通过；tzinit exit22 |
| H83k | `54ba38bb291cb1637c8da702ade288696ea4c1afddea008f192be1f66a1771f3` | 补 timezone；tzinit 通过；UFS 读停住 |
| H84a | `d922af097dd71250adfe6522c4cfcc5edda3688ef69052084ba80a353e786254` | + NativeMobileDisplay；UFS 读停住 |
| H84b | `a811fec2fc9c6c56759c8eaae55803c0a4206269a38012cc96c69a6884c1ac20` | + cpus=8；UFS 读停住 |
| H85c | `6a7e83ac9554e5c486c85ac4f656f60ab974cbda8860af2bbb90ade6ffb55296` | 实际仍是 NativeUfsProbe；UFS 大读停住 |
| H85g | `c21a87c58fc9d1ed51643795d37023548a3593893702c58f23a005e3b2dbdfab` | personality 写成 NativeUfs，但 class 未链接；启动报 `Couldn't alloc class "NativeUfs"` |
| H85h | 无最终镜像 | 清 `build/xnu` 后 bootstrap 正常产出记录，但外层误带 `XNU_BOOTSTRAP=1`，最终链接仍按 bootstrap 模式失败 |
| H85i | 无最终镜像 | `.config` 修好后首次真正链接 `libNativeUfs.a`；暴露 VerifiedRoot/IOStorageFamily 依赖 |

### H85g–H85h 根因：`.config` 陈旧，配置从未生效

H85g 的 `KernelConfigTables` 已经显示 `IOClass = NativeUfs`，但 `nm kernel.macho`
只有 `NativeUfsProbe` 的 class 符号，没有 `NativeUfs`。根因不是补丁，也不是 archive：

- `unixnu` 构建时读取的是 `out/cheeseburger/.config`，不是
  `configs/cheeseburger_defconfig`；该 `.config` 停留在 10 月 29 日。
- 因此 `xnu_env()` 实际导出 `XNU_NATIVE_UFS=0`、`XNU_NATIVE_UFS_PROBE=1`，
  覆盖了 `run-h82-build.sh` 里的同名 export。
- 结果：personality 被临时补丁强制为 `NativeUfs`，但链接器只 force-load 了
  `libNativeUfsProbe.a`，`libNativeUfs.a` 从未进入内核，启动即
  `Couldn't alloc class "NativeUfs"`。
- H85h 另外把 `XNU_BOOTSTRAP=1` 带进了最终 pass，导致最终链接仍跳过 archive。

修复（H85i 起点）：先执行 `./unixnu defconfig cheeseburger_defconfig` 重新生成
`.config`，再清 `build/xnu` 与 `KernelConfigTables.cpo` 做干净构建。已删除临时
`zzz-force-native-ufs.patch`，personality 回到由配置驱动。

### H85i：完整 NativeUfs 不是 bring-up 捷径，而是 VerifiedRoot 终态

重新生成 `.config` 后，链接器第一次真正把 `libNativeUfs.a` 拉进来，随即暴露：

- `NativeUfsBlock : IOBlockStorageDevice` 需要原始 IOStorageFamily ABI 的 11 个
  alias；这些 alias 目前只由 `NativeUfsProbe` 的
  `NativeUfsLun-original-bindings.o` 生成。
- 驱动强制 `-DNATIVE_VERIFIED_BLOCK=1`，需要 `vr_open`/`vr_verify_block`/
  `vr_xnu_crypto` 与 `verified_root_policies`/`verified_root_key`，即完整的
  VerifiedRoot owner policy bundle；远端没有 `build/verified-root-*` 和
  `target-volumes` 原始 OS 镜像。
- 更关键：`NativeUfs::validate()` 在未启用 `NATIVE_VERIFIED_BLOCK` 时直接
  `return false`，注释明确写着 “Root publication requires owner verification in
  every build”。因此不能靠关掉校验让完整驱动先跑起来。
- 完整驱动还需要 `native_verified_block_root_authorized` /
  `native_verified_block_volume_device`，这两个 hook 在 probe 或
  NativeVirtioBlock 里，而不在 NativeUfs 里。

结论：完整 NativeUfs 只能和真实 owner manifest/VerifiedRoot 一起上线；它不能
替代 probe 做 bring-up。当前 UFS 大读阻塞仍只能在 `NativeUfsProbe` 的
`waitForCompletion`/SMMU fault 路径上查，或先补真实 owner policy bundle。

下一步二选一：

1. 回到 H83k/H84b 的 Probe 基线，在 probe 的读窗口完成等待处加超时、命令
   tag/中断状态和 SMMU fault 日志，定位 3.1 GiB 处 `winFlight=1` 不返回。
2. 先做真实 owner policy：用精确的 iPad7,1 17.7.11 OS/SystemOS DMG 在 macOS
   上跑 `build-owner-os-policy.py` 生成 `build/verified-root-*`，再启用完整
   NativeUfs + VerifiedRoot。

### H86a–H86i：Probe read-ahead，然后切到完整 NativeUfs + VerifiedRoot

Probe 路线：

- H86a/H86b 给 `NativeUfsProbe` 加 128 KiB read-ahead；顺序 4 KiB 读命中
  cache 时约 80–106 ticks（之前约 2100），并已进入用户态
  `exfild` / `distnoted` / `MobileGestaltHelper`。
- 但最终仍会停在一个窗口请求（例如 `req=a3d50/64`），且 H86c 的独立
  `NATIVE_UFS_HEARTBEAT` 也没有打印，说明 warm-link/MMIO 已经整体停住，
  不是单纯的慢。本轮不再继续这条路径。

完整 NativeUfs/VerifiedRoot 路线：

- 从原始 iPad7,1 17.7.11 IPSW 提取 OS/SystemOS：
  `044-76040-038.dmg` sha256 `3f99b859...`，`044-76034-038.dmg`
  sha256 `b4908cde...`。
- 用 `kits/VerifiedRoot/tools/manifest.py` + host C/RSA verifier 生成自签
  owner policies；两份都通过真实 C/RSA 全块校验（OS 1,483,785 块，
  SystemOS 806,400 块）。
- 新增 `build-native-ufs-runtime.py`，把 `NativeUfs`、`RootAuth.cpp`、
  `verified_root.c`、`xnu_crypto.c`、policy `inputs.o` 和原始
  IOStorageFamily bindings 打进 `libNativeUfs.a`。
- 32 字节 hash 时 policy 约 73 MB，kernel 120 MB，boot image 135 MB；
  H86f 停在 OnePlus logo，UEFI 根本没起来，确认是镜像过大。
- 把 per-block digest 临时截断到 8 字节（`VR_HASH_BYTES=8`，最终恢复 32 字节），
  policy 降到约 17 MB，kernel 67 MB，boot image 回到 81,147,904 字节。
- H86g（slide `0x8000000`）UEFI 菜单卡住；H86h（slide `0x5000000`）
  进入内核但 APFS 后 instruction fetch abort。
- H86i 把 `IOStorageFamily-bindings.json` 的所有绝对地址按
  `0x5000000 - 0x4100000 = 0xF00000` 修正后，完整 UFS 真正跑通：
  `NativeRamBlock` 注册、APFS 加载、`disk0` checkpoint 搜索成功。

H86i 镜像/内核：

| 项 | sha256 |
| --- | --- |
| boot | `07ac9dc46b8b7dea0310f111d4b00c66be88d584ee3e66cd03180d420201c350` |
| kernel | `2df14e024c4d9860f691cbdaa680b56a22ceed025da15dd98e5cb1c85afe3694` |
| payload | `f1db5c2a4b5b0fcc7eb4cab4985c2a390177205430af7d13616e53b6f47cac07` |

最后停在：

```text
apfs_extract_root_hash_arn: could not retrieve system-volume-auth-block from device tree
import_lboot_forwarded_roothash: apfs_extract_root_hash_and_manifest failed with error: No such file or directory (2)
```

这不是 UFS、Probe 或 Data seed 问题，而是 ADT 缺少真实的
`system-volume-auth-block`（owner/Apple root auth block）。最终必须提供真实的
owner boot manifest/VerifiedRoot；8 字节 digest 和自签 policy 都是临时 bring-up。

下一步二选一：

1. 先做临时 APFS root-auth bypass，跳过 `system-volume-auth-block` 到达
   launchd/UI；之后再补真实 Apple/owner root。
2. 直接补真实 `system-volume-auth-block`（需要 personalized IM4M/Apple root
   输入），同时把 policy digest 恢复为 32 字节。

### H86j–H87a：auth blob 放行，APFS root 挂载，进入 FileSet KC/driverkitd

- H86k：把 IPSW 的 `SystemVolume` root-hash IM4P（229 字节，
  `Firmware/044-76040-038.dmg.root_hash`）作为
  `/chosen/system-volume-auth-blob` 注入 ADT。APFS 不再报
  `could not retrieve system-volume-auth-blob`，改为停在
  `import_lboot_forwarded_roothash: importing root hash`。
- H86l/H86m：完整 NativeUfs 的 `start()` 只有 `native-ufs-root=1` 才调用
  `startRoot()`；之前没加这个 boot arg，OS/SystemOS 窗口根本没发布，APFS
  一直在等 `04769D26...`。加 `sheng-pmgr=2` 的空 stub 后 H86m 黑屏，
  说明完整 root 路径确实依赖真实 PMGR，stub 不够。
- H87a：回到 Probe 内核 + 真实 auth blob + `allow-root-hash-mismatch=1`，
  不带 `native-ufs-root`/PMGR。结果 APFS root 成功挂载，并进入用户态：
  `NATIVE_CACHE_SIG`/`NATIVE_H66_FCNTL`、`KextLog: Loading FileSet KC(s)`、
  `KextLog: driverkitd is active`。最后停在这里，但 UFS 仍在完成
  `SystemOS` 4 KiB 读（cache hit ticks 约 87），已不是 root auth 问题。

H87a 镜像/内核：

| 项 | sha256 |
| --- | --- |
| boot | `bd17e843e173e1c6188fca59eaa61d8dfe43820a4da5ba1ffb62af674365b0cd` |
| kernel | `dd275f7473783ec03a7e45dbe7585026be29950505755c6ca3c5ec0b72bb00a4` |
| payload | `cf7796371587f275a20f5a1721db76472057126ec6da36580e5dc25eaf53fc27` |

H87c：保留 board 默认的 permissive AMFI（`amfi_get_out_of_my_way=1`、
`amfi=0x94`、`amfi_allow_any_signature=1`、`cs_enforcement_disable=1`、
`native-skip-exec-signature=1`），只额外加 `allow-root-hash-mismatch=1`，
并回到 `cpus=1`。结果 `_check_cdhash_in_trustcache ... Only allowed
processes` 的循环消失，说明之前命令行追加的 `amfi=0 ...` 覆盖了 board 默认值。

H87c 之后仍停在：

```text
KextLog: Loading FileSet KC(s)
KextLog: driverkitd is active
NATIVE_UFS_WINDOW: SystemOS ... ok=1 ticks=114
```

而且没有 `NATIVE_UFS_ALIVE` heartbeat，说明不是空闲，而是 Probe 的 warm-link
路径在后续 SystemOS 读/kext 加载中停住。UFS root、auth blob、TrustCache
权限都已越过；剩下是 warm-link transport 的稳定性，或完整 NativeUfs 的真实
PMGR 依赖。

H87c：boot `3c0747cdd0ebe6d47c98679dc617157f87b560231276f22f55a9a0e5b38bcd42`，
payload `8cc67f1a0757c86d0cb80092b39180a97ff8958b707a22df44ce7a9b746ed15a`。

下一步二选一：

1. 移植 DinnerABC `CheeseburgerPlatformPower` + PMGR bindings，让完整
   NativeUfs root 路径可用（需要按当前 slide 调整 PMGR 绝对地址）。
2. 继续在 Probe warm-link 上做 transport watchdog/reset，尝试跨过这次
   SystemOS 读停住。

display/backboardd 仍未到达；当前只是内核控制台。

最终恢复真实 owner boot manifest、VerifiedRoot/AMFI/TrustCache，移除 ADT 占位。

### H88a–H89b：真实 PMGR、board-id、auth blob、mount-phase-1

- H88a：完整 NativeUfs root + 真实 PMGR，TrustCache 加载完成后
  `No POST boot-arg set.` 后黑屏。
- H88b：只保留 PMGR、不启用 `native-ufs-root`，出现
  `panic: P11 PMGR: device provider refused`。原因是 PMGR `start()` 要求
  platform provider 上有 17 字节（含 NUL）的
  `unixnu,board-id="oneplus,cheeseburger"`；ADT 没有，回退到 `model=iPad7,1`
  后拒绝。
- H88d：ADT 根节点补 `unixnu,board-id` 后，PMGR panic 消失，但完整
  `native-ufs-root=1` 仍然黑屏，说明黑屏来自 `startRoot()`，不是 PMGR。
- H88e：PMGR + board-id、不启用 root，PMGR 正常注册：
  `P11 PMGR: original ApplePMGRNub registered`、`AMFI: PMGRIDN linked`；
  NativeRamBlock/APFS/Data 都工作。该次 auth blob 因生成器被覆盖而丢失。
- H89a：Probe + 真实 PMGR + auth blob，launchd 走到
  `mount-phase-1`，`mount` 因缺少 `boot-manifest-hash` exit 66。
- H89b：ADT `/chosen` 补 48 字节 `boot-manifest-hash` 占位后，
  `mount-phase-1` 通过，继续到 `FileSet KC` / `driverkitd`，随后又停在一次
  SystemOS/OS UFS 读，且无 `NATIVE_UFS_ALIVE` heartbeat。

H89b：

| 项 | sha256 |
| --- | --- |
| boot | `d2a9d62bf464a1e6408e9c7a611b929fcd8fb1cc76d684fb69a6637c25cb619a` |
| kernel | `df4ff6e769f5b03e774b3a76ee25af5ee796606144efd08493da2f857303aa3f` |
| payload | `28409665c1fc1fb9ce4a9cc3e8edfabec4f0ae8565c3bdd0b655a77bceb4ebb0` |

结论：真实 PMGR 集成可用；完整 `startRoot()` 仍黑屏；Probe 路径能过
mount-phase-1 但在重读阶段停住。下一步要么给 Probe 加 transport
watchdog/reset，要么用串口/panic-screen 捕获停住原因。boot-manifest-hash
和 8 字节 policy digest 都是临时占位，最终恢复真实 owner manifest / 32 字节。

### H90a–H90d：参考 warm-link NativeUfs 移植

- 参考 `package/BoardRuntime/NativeUfs.cpp` 已移植为
  `scripts/NativeUfsWarm.cpp`，并随 runtime archive 编译。
- 必须使用 late object：
  `llvm-objcopy --remove-section=__DATA,__mod_init_func
  --globalize-symbol=__GLOBAL__sub_I_NativeUfs.cpp`，否则链接器报
  `__GLOBAL__sub_I_NativeUfs.cpp` 未定义。
- late 对象不会自动注册 class；H90b 出现
  `Couldn't alloc class "NativeUfs"`。H90c 在 `sheng_pmgr_run()` 里先调用
  `cheeseburger_ufs_register()`，class 注册成功，控制台出现
  `P11 UFS: original storage ABI matched; entering read-only warm-link driver`。
- H90d 放宽 compatible 为列表搜索后，仍反复出现
  `P8 UFS refused: missing verified MSM8998 profile`；同时出现
  `P8 UFS warm state CAP=... HCE=1 HCS=10`，说明部分 provider 已进入
  transport，但被 `(HCS&3)==3` 的 idle/live 检查拒绝（当前 HCS=0x10）。
  这是参考驱动对 firmware 控制器状态的假设不一致，不是 ABI/class 问题。

当前最佳可用镜像仍是 H89b：Probe + 真实 PMGR + auth blob + 48 字节
`boot-manifest-hash`，已过 mount-phase-1，到 FileSet KC/driverkitd，
随后在重读阶段停住。

H90e 放宽 `(HCS&3)==3` 后，结果没有实质变化：多数 provider 仍打印
`P8 UFS refused: missing verified MSM8998 profile`，进入 transport 的那个也在
`P8 UFS warm state ... HCS=10` 后停住，OS/SystemOS 没有发布，APFS 仍停在
`importing root hash`。参考 warm-link 驱动在当前 firmware/控制器状态下也无法
建立可用队列。

H91：Probe + PMGR + 10 秒 stall panic。实机没有触发
`NATIVE_UFS_STALL`，屏幕上 SystemOS 读持续完成（`ok=1 ticks≈96`），
`winBytes` 约 3.23 GB，最后停在 `KextLog: driverkitd is active`。
说明这次 UFS transport 没有硬卡死；剩余阻塞在 **FileSet KC / driverkitd
的 kext/SystemOS cryptex 加载循环**，不是 UFS 读停住。

H91：

| 项 | sha256 |
| --- | --- |
| boot | `cd81c5b8ec91f103f3e3c0fd7de7bfb26633f4fbabecd34490e5627dd6f3ff9b` |
| kernel | `7f02891c32825b75fcb76f2969ea2c1a541662d4c369d29fc85732b19f0e532d` |
| payload | `e7fa5cdff58217886ebf269b30683c78da83c409ae179c356650f69869f08942` |

下一步不再是 UFS。`cryptexd` 只认 **Cryptex1** 格式；当前 ADT 的
`boot-manifest-hash` 仍是 48 字节全零占位。`mount-phase-1` 只检查属性存在，
但 SystemOS 的 Cryptex1/FileSet KC 很可能需要真实 owner boot manifest /
SystemOS root hash 才能完成挂载，因此 `driverkitd` 循环。该占位已足以过
mount-phase-1，但不足以保证 cryptex 验证。

`cryptexd` 的字符串确认它走 AuthAPFS / Image4 `im4m` 认证：

```text
AuthAPFS disabled because quire lacks an im4m.
AuthAPFS enabled, skip up-front dmg authentication.
Running in debug mode, skip trust cache
```

没有现成的 `cryptex=0` boot arg。临时 bypass 需要改 `cryptexd`/libboot
跳过 AuthAPFS，但会改动 OS 根镜像，使当前 APFS root policy/root hash 失效，
必须重新生成 policy；或者提供真实 `Cryptex1,SystemVolume` owner manifest/im4m。
在此之前 display/UI 不会出现。

### H92：cryptexd stub + 完整 userdata 覆盖（破坏性）

按用户明确批准执行：

- OS DMG 转 UDRW，替换 `/usr/libexec/cryptexd` 为 keep-alive stub；
  修改后 OS whole-disk sha256 `10f92356d14ddbdb9b94d87ea291b54272fa3078eb4642e82ad10b57e11e7401`。
- 重建完整 userdata：OS `6,077,580,800` + SystemOS `3,303,014,400`，
  image `9,382,658,048` 字节，sha256
  `b0e75882e4de823c0b47fba909fe11f87a897e96a6f29f79a871e0b8425dc85a`。
- `fastboot flash userdata` 成功（12 个 sparse 块，350 秒）。
- 用 H91 启动：APFS root hash mismatch 被 `allow-root-hash-mismatch=1`
  放行，launchd/PMGR/用户态继续，但仍停在
  `KextLog: Loading FileSet KC(s)` / `driverkitd is active`。

结论：stub `cryptexd` 不是正确的 gate。SystemOS 的 Cryptex1 需要真实
AuthAPFS/owner manifest 才能完成挂载；只让 cryptexd 活着并不能提供
SystemOS/FileSet KC。当前设备 userdata 已被覆盖为上述镜像；原始 OS/SystemOS
可从 IPSW 恢复，Data 已丢失（用户已批准）。

真实 owner manifest/im4m 路径已核查：项目和 DinnerABC 都没有 Image4 IM4M
生成器。IPSW 的 `SystemVolume`/`Cryptex1,SystemVolume` 只是 229 字节 IM4P
root hash；`scripts/audit-target-root-hashes.py` 明确记录
`personalized_im4m_present=False`，这些 payload 不能提供 personalized IM4M。
我们的 VerifiedRoot manifest 是 `VROOT01` 自签格式，`cryptexd` 不识别。
真实 IM4M 需要 Apple TSS/personalization，当前输入无法生成。

### H93：cryptexd AuthAPFS stub patch（破坏性，未成功）

- 恢复原始 `cryptexd`，把 `_cryptex_core_image_authapfs_enabled` 的
  symbol stub（`0x3fce8`）改成 `mov w0,#0; ret`，强制 AuthAPFS disabled。
- 重建 userdata：OS 窗口 sha `5fae7a80...`，image sha `5b780d89...`，
  再次 `fastboot flash userdata` 成功（350 秒）。
- 启动后 APFS/launchd/PMGR 继续，但 `_check_cdhash_in_trustcache`
  “Only allowed processes can check the trustcache” 循环再次出现，随后仍是
  `FileSet KC` / `driverkitd`。修改后的 OS 二进制不再受 AMFI/trustcache 信任，
  这是 patch 系统 daemon 的直接副作用。
- `check_cdhash_in_trustcache` 在原始 Apple AMFI kext/预链接代码里，不在
  当前 XNU 源码中；继续 bypass 需要 patch AMFI/trustcache 或真实签名/权限。

结论：补真实 IM4M 不可得；patch `cryptexd` 又会触发 AMFI/trustcache 完整性
循环。当前设备 userdata 是 H93 修改版，UI 仍未到达。建议恢复原始 userdata
并停在 H91；若继续，只能进入预链接 AMFI/trustcache 的二进制 patch。

### H94：恢复原始 userdata，停在 H91

- 用原始 OS/SystemOS DMG 重建 userdata：
  OS 窗口 sha `e5634d04a8ac35f024845f583394e0a35deb26a04cb0c00286a8674e25a9a4f2`，
  SystemOS 窗口 sha `b4908cde41d23e6bc05e6af51a2c9eeb00cf0f863a6a18b1600c96a3fd70414f`，
  image sha `fd43ee7035ce185ce4215ac93fdc006fd784a54b3b7442e21457c226cbe03093`。
- `fastboot flash userdata` 成功（12 个 sparse 块，354 秒）。
- `fastboot boot` H91 成功，设备回到 Probe 基线（原 OS + PMGR + auth blob +
  48 字节 boot-manifest-hash）；H92/H93 修改版已从设备上移除。
- Data 已在 H92/H93 覆盖中丢失，用户已批准。

最终状态：H91 是当前最佳 Probe 镜像；剩余阻塞是 SystemOS Cryptex1 /
FileSet KC 的真实 owner IM4M/AMFI-trustcache，当前输入无法生成。完整
`startRoot()` 仍黑屏。

### H95：仓库收尾与当前产物

- 已删除项目根目录 183 个 `*.jpg`，以及
  `plans/oneplus5-cheeseburger/assets/`；后者的 Git 删除已提交。
- `drivers/NativeUfs` 子模块指针更新到 `43a4bc7`（已推送到
  `lss717/NativeUfs` fork）。
- VerifiedRoot 的 8 字节 digest bring-up 改动（`4c43a01`）因上游
  `UniXNU/VerifiedRoot` push 403，保存为
  `patches/verified-root-truncated-hash.patch`；应用方式：
  `git -C kits/VerifiedRoot am patches/verified-root-truncated-hash.patch`。
  父仓库不指向未推送的子模块 commit。
- 设备仓库新增 warm-link `NativeUfsWarm.cpp`、`CheeseburgerPlatformPower.cpp`、
  PMGR/AuthAPFS glue、ADT auth blob/boot-manifest-hash 属性、policy 生成脚本；
  这些是 bring-up 代码，默认 board 配置仍保持 Probe 基线。
- 远端测试 board 的差异（slide `0x5000000`、runtime/PMGR archive、payload
  `size_mib=76`、1 GiB 内存/AMFI placeholder）保留在远端 board.toml，未提交到
  干净基线。
- 当前设备：原始 userdata（OS `e5634d04...`、SystemOS `b4908cde...`）+
  H91 内核；停在 `FileSet KC` / `driverkitd` + `_check_cdhash_in_trustcache`
  循环。H91 boot `cd81c5b8...` 是当前最佳镜像。

最终恢复真实 owner boot manifest、VerifiedRoot/AMFI/TrustCache，移除 ADT 占位。

### H96：AMFI selector 0x5f 权限门定位与独立诊断补丁（只读调查）

- 新增 `plans/oneplus5-cheeseburger/BASELINE.md`：记录 H91 boot/payload/
  kernel/ADT/TrustCache 哈希、payload 原始启动参数、仓库/子模块状态和缺口。
- 新增 `plans/oneplus5-cheeseburger/diagnostics/H96-investigation.md`：
  启动链路、证据表、3 个根因假设、H96a/H96u 区分实验和缺失证据。
- 从 H91 payload 内 FAT 卷直接读出 `kernel.macho`
  `7f02891c...`、`kernel.macho.adt` `83d733d7...`、
  `kernel.macho.trustcache` `7416ca4f...` 和完整 boot args；H91 的
  kernel/ADT/TrustCache 哈希与 STATUS 原有记录一致。
- 对原始 iPad7,1 21H461 kernelcache 的 AMFI kext 做只读反汇编：
  `_check_cdhash_in_trustcache` 的 MAC syscall selector 为 `0x5f`；
  入口在 `0xfffffff005ab73a8` 检查
  `com.apple.private.amfi.can-check-trust-cache`，失败直接打印
  `%s: Only allowed processes can check the trustcache` 并返回 `13`
  (`EACCES`)，发生在 XNU `query_trust_cache()` 之前。当前循环因此是
  调用方权限/签名状态问题，不是 TrustCache 查询未命中。
- 原始 SystemOS 中该 entitlement 及 `Unable to find AMFI`、
  `cdhash ... is not in trust cache` 字符串位于
  `/System/Library/Frameworks/ExtensionFoundation.framework/ExtensionFoundation`
  （Mach-O `0x1785b000`，UUID `28968ac0-0ace-360e-83e3-c8e7ad996871`）；
  循环候选调用方是加载该框架的宿主/服务进程，具体 PID/comm 仍需 H96a
  运行日志确认。
- H90d `HCS=0x10`：按 UFSHCI，bit 4 是 `HOST_ERROR_INDICATOR`，
  `HCS & 3 == 0` 表示 `DEVICE_PRESENT` 与
  `UTP_TRANSFER_REQ_LIST_READY` 都未置位；不是 idle/live 编码差异。
  放宽 `(HCS & 3) == 3` 不解决问题，H96u 需先清 error/重建队列。
- 新增诊断补丁 `patches/xnu/zzzz-trustcache-amfi-trace.patch`：
  - `native-trace-amfi-syscall=1`：在 `__mac_syscall()` 记录 AMFI
    selector `0x5a..0x66` 的 pid/comm/csflags/blob/platform/entitled/
    cdhash；selector `0x5f` 限 64 次。
  - `native-trace-trust-cache=1`：在 `query_trust_cache()` 记录调用方
    pid/comm、query type、20 字节 cdhash、返回值和限频。
  - 默认 boot arg 缺失时两个函数立即返回；不改变 AMFI、TrustCache、
    root auth 或 VerifiedRoot 策略。
- H96 调查/补丁提交：`6102287`（父提交 `93c7fb6`）。
- 验证：补丁已对干净的 `xnu-10063.141.1` 源树通过
  `git apply --check`；本机无 macOS/Xcode 内核工具链，**尚未实际编译**，
  也**未上机**。H91 产物未覆盖。
- H96a 构建完成（macOS 15.8.2 / Xcode 26.3，H91 对象树 +
  `xnu-10063.141.1`）：raw kernel `92e55a70...`、kernel.macho
  `f5dbbe1c...`、payload `8c92f8c8...`、boot
  `790de811...`。payload/ADT/TrustCache/Data seed 沿用 H91，只替换
  kernel 与两个 trace boot arg。
- 第一次 H96a `fastboot boot` 返回 `Booting OKAY`，但停在 UEFI Shell：
  用 `mcopy` 覆盖 8.3 短名时丢失 VFAT 长名 `unixnu.toml` /
  `kernel.macho`，UniXNU 找不到配置/内核。已删除旧目录项并用长名重建
  payload；修正版 boot `790de811...` 等待再次上机。
- H96a 修正版已进入 XNU：屏幕可见 `NATIVE_UFS_PROGRESS`、
  `_check_cdhash_in_trustcache ... Only allowed processes` 循环、
  `KextLog: driverkitd is active`；未看到 `NATIVE_AMFI_SYSCALL`。
  可能是 64 次 trace 已滚屏，或 boot arg 未被解析。
- H96a2（强制 trace，不依赖 boot arg）已构建：kernel
  `918cdbe1...`、payload `da268d87...`、boot `8c58b138...`；
  仅构建树把 trace 默认打开并把限频提高到 100000/4096，仓库默认补丁
  仍保持 boot-arg 关闭。等待再次上机。
- H96a2 屏幕出现多次 `NATIVE_TC_QUERY ... ret=0`（UserEventAgent/
  launchd/spracy），说明 XNU TrustCache 查询和命中正常；仍无
  `NATIVE_AMFI_SYSCALL`。原因是 `__mac_syscall` trace 的 target 过滤用了
  bundle ID，而 AMFI 的 MAC policy 名很可能是
  `AppleMobileFileIntegrity`。旧 trace 还调用了
  `IOCurrentTaskHasEntitlement()`，可能提前初始化 entitlement，构成副作用。
- H96a3 取消 target 过滤、打印实际 target、删除 entitlement 查询调用：
  kernel `b36ec3a8...`、payload `d5030ee5...`、boot
  `98192bb0...`。等待再次上机。
- H96a3 仍无 `NATIVE_AMFI_SYSCALL`，但 `NATIVE_TC_QUERY ... ret=0`
  正常；说明 AMFI 检查不是从 XNU `__mac_syscall` 进入的，而不是 boot arg
  或滚屏问题。
- H96a4 一次性诊断镜像：把 AMFI 拒绝分支
  `VM 0xfffffff00aab73b8` 的 `tbz` 改成 `nop`，只跳过权限门，TrustCache
  查询仍执行；用于获取 `Process %d` 调用方和 query hit/miss。kernel
  `86a7e63a...`、payload `6d6f4935...`、boot
  `f745b54f...`。这是受控诊断绕过，不是默认配置或最终修复。
- H96a5（B 方案：拒绝即 panic 并打印调用方）已构建：在 AMFI 拒绝分支
  `VM 0xfffffff00aab75d8` 跳到代码洞，读取 `proc->p_pid`/`p_comm` 后
  `panic("H96_AMFI_REJECT: func=%s pid=%d comm=%s")`。kernel
  `2994ae9d...`、payload `2ed839d3...`、boot
  `c2a18350...`。这是诊断 panic，不改变 AMFI 策略/权限门。
- H96a5 panic 确认拒绝方：`pid=63 comm=driverkitd`，selector `0x5f`，
  `platform=1`、`blob` 非空、`csflags=0x2202331`。原版 iPad7,1 17.7.11
  `/usr/libexec/driverkitd` 是 adhoc 签名，entitlements 中**没有**
  `com.apple.private.amfi.can-check-trust-cache`；因此 AMFI 拒绝符合策略，
  真正问题是 driverkitd 为什么走了这个未授权的 MAC syscall 路径
  （可能缺少 AMFI IOKit user client/SystemOS 路径而回退）。
- H96a6（跳过权限门 + panic 查询结果）已构建：kernel `d06f6e69...`、
  payload `aec14df7...`、boot `3ac690a0...`；`mask=0` 表示命中，
  `mask=3` 表示两次查询都 miss。等待上机。
- H96a6 panic 结果：`pid=63 driverkitd target=AMFI call=95`，目标
  cdhash `7bb1e532...`；panic `mask=0x1`。按 AMFI 代码的失败位语义，
  这表示 type 1（static）miss、type 2（loadable）hit；目标 driver 已在
  loadable TrustCache 中。照片中紧随的 `NATIVE_TC_QUERY` type1/2 用的是
  另一个 cdhash `1fc19a...`，不能当作 AMFI 目标的查询结果。
- 结论：不是 TrustCache 完全缺失；driverkitd 自身没有
  `com.apple.private.amfi.can-check-trust-cache`，却走了未授权的
  `__mac_syscall("AMFI",0x5f,...)` 路径；目标 driver 已在 loadable
  cache 命中，下一步优先查 AMFI IOKit user client / amfid / XPC 路径
  为什么不可用或未被 driverkitd 使用，而不是继续补 blob。
- H96a7 已构建（AMFI IOKit service/user client 打点）：kernel
  `0befa003...`、payload `0313ac79...`、boot
  `413e2b50...`。driverkitd 调用 0x5f 时 panic 输出
  `H96_AMFI_PATH: ... service=<0/1> uc_calls=<N>`，用于判断 AMFI
  IOService/UserClient 是否可用。等待上机。
- H96a7 panic 结果：`H96_AMFI_PATH: pid=63 comm=driverkitd target=AMFI
  service=1 uc_calls=0`。AMFI IOService 已注册，但
  `AppleMobileFileIntegrityUserClient` 从未被调用；同屏 `amfid[47]`
  存在。driverkitd 直接走 `__mac_syscall("AMFI",0x5f,...)`，而原版
  driverkitd 没有该 entitlement，因此循环与 TrustCache 内容无关。
- 下一步用 H96a4（全局跳过 entitlement 门的诊断镜像）确认该 gate 是否
  是当前唯一阻塞，并观察 driverkitd 之后的下一个阶段。
- H96a4（全局跳过 AMFI gate）已上机：AMFI 拒绝循环消失，系统继续
  SystemOS 读取，最后停在 `NATIVE_UFS_PROGRESS ... winBytes=329618936`
  附近，无 `NATIVE_UFS_ALIVE` heartbeat、无 stall panic。说明 AMFI gate
  是 driverkitd 阶段阻塞；跳过后的新停点需要独立 UFS 诊断。
- H96a8（H96a4 + `cpus=2`，只改 CPU 数）已构建：payload
  `5c9a4005...`、boot `53f8cfcf...`；用于让 heartbeat/stall
  watchdog 运行并捕获停点快照。
- H96a8（H96a4 + `cpus=2`）已上机：越过 AMFI gate 和早期 UFS 停点，
  到 `KextLog: driverkitd is active`、SystemOS 读取约 3.22 GB 后日志
  停止；无 `NATIVE_UFS_STALL`，但未见后续服务。新阻塞点在 driverkitd/
  FileSet KC 之后，需定位最后 exec/等待任务。
- H96a9（H96a8 + `NATIVE_EXEC` trace + gate bypass）已构建：kernel
  `84312b76...`、payload `0e714fa8...`、boot
  `697536ff...`；用于显示最后执行的进程。等待上机。
- H96a9 已上机：最后 exec 为 `containermanagerd`/`MobileGestaltHelper`，
  之后 `driverkitd is active`，SystemOS 读取约 3.21 GB 后停止；无新
  exec/KextLog，无 UFS stall。停点在 driverkitd/FileSet KC 之后。
- H96a10（UFS 请求内超时 panic）已构建：kernel
  `fe684b31...`、payload `93d052bf...`、boot
  `bd3069c2...`；若 UFS 等待超时会直接 panic 并给出 LBA/长度/UTRD/
  寄存器；若仍无 panic，则停点在上层。等待上机。
- H96a10 已上机：系统启动到 configd/keybagd/amfid/mobileassetd/
  storageproxyd/fseventsd/IOMFB_FDR_Loader/xpcproxy 等大量用户态服务；
  未出现 `NATIVE_UFS_TIMEOUT`/`NATIVE_UFS_STALL`。最后停在 xpcproxy
  的 dyld cache 注册（`NATIVE_CACHE_SIG`）后，尚未出现 backboardd/
  SpringBoard。
- H96a11（exec trace 增加 argv0/argv1 + UFS timeout + gate bypass +
  cpus=2）已构建：kernel `b12e0480...`、payload `20751860...`、boot
  `d225ad4b...`；用于确认最后 xpcproxy 的目标服务。等待上机。
- H96a11 已上机：最后 `NATIVE_EXEC pid=90 comm=xpcproxy
  argv1=com.apple.nsurlsessiond`；目标服务未 exec，系统停在启动
  `com.apple.nsurlsessiond` 这一步；无 UFS timeout。
- H96a12 复用 H96a11 kernel，仅追加
  `sheng-disable-jobs=com.apple.nsurlsessiond`：payload
  `bd9a85b3...`、boot `90812e49...`。用于跳过该服务并观察是否继续到
  backboardd/SpringBoard。等待上机。
- H96a12 已上机：跳过 nsurlsessiond 后启动到 `backboardd`、`sharingd`、
  `imagent`、`lockdownd`，随后停在第二次 `driverkitd is active`，未到
  SpringBoard/WindowServer；无 UFS timeout。当前最远用户态进度。
- H96a13（同时跳过 nsurlsessiond 和 driverkitd job）已构建：payload
  `34573fe9...`、boot `3c5fe707...`；用于判断第二次停点是 launchd job
  还是内核 kext/驱动加载。等待上机。
- H96a13（跳过 nsurlsessiond+driverkitd job）已上机：停在
  `nehelper`，未到 backboardd；说明禁用 driverkitd job 不是正确方向，
  H96a12（只跳 nsurlsessiond，保留 driverkitd）仍是最远路径。
- H96a14（H96a12 参数 + 只记录 driverkitd AMFI 检查目标）已构建：
  kernel `ede3f2b8...`、payload `aa06e6c0...`、boot
  `06bc78c6...`；用于获取第二次 driverkitd 检查的 cdhash。等待上机。
- H96a14 本次运行停在 `routined`（未到第二次 driverkitd trace），与
  H96a12 到达 backboardd 存在 run-to-run 差异；`routined` 是新的同步
  等待候选。
- H96a15 复用 H96a14 kernel，payload 跳过
  `nsurlsessiond,routined,nehelper`：payload `4fed1175...`、boot
  `0b55644d...`。用于排除网络/位置服务同步等待并稳定到达
  backboardd/SpringBoard。等待上机。
- H96a15 已上机：越过第二次 driverkitd，启动到 `chronod`，UFS 约
  3.27 GB 后停住；无 UFS timeout，未到 SpringBoard/WindowServer。
  跳过 routined/nehelper 有效，chronod 是新的停在候选。
- H96a16 在 skip 列表加入 `com.apple.chronod`：payload
  `646bdae0...`、boot `49fc80be...`。用于观察下一服务。等待上机。
- H96a16 已上机：越过 chronod，启动到 `IOMFB_FDR_Loader`、
  `distnoted`、`log_helper` 后停住；无 UFS timeout。最后停点继续在
  routined/chronod/log_helper 之间漂移，说明是某类同步 XPC/launchd
  等待，不是单一 daemon。
- H96a17 扩大 skip 列表（nsurlsessiond,routined,nehelper,chronod,
  log_helper,accountsd,alsd,pfd）：payload `9fdc58c0...`、boot
  `6906c32e...`；用于一次性排除已观察 daemon。等待上机。
- H96a17 启动 panic：`sheng watchdog launchd policy failed: 22 @
  kern_exec.c:8294`。根因是 `sheng-disable-jobs` 字符白名单不允许
  `_`，列表中的 `com.apple.log_helper` 触发 EINVAL；不是用户态服务
  问题。H96a16 无下划线所以能启动到 log_helper。
- H96a18 移除 log_helper，skip 列表改为
  `nsurlsessiond,routined,nehelper,chronod,accountsd,alsd,pfd`：
  payload `c58640e1...`、boot `301d8edd...`。等待上机。
- H96a18 已上机：启动到 `cryptexd`/`eligibilityd`，fseventsd 线程数
  达高水位；之后 5 分钟无变化，无 UFS timeout，未到 SpringBoard。
  系统已进入 cryptexd/SystemOS 路径，可能再次撞上原始
  Cryptex1/AuthAPFS/owner manifest 依赖。
- H96a19 在 skip 列表加入 eligibilityd：payload `995d247c...`、boot
  `0bd34abf...`；用于观察 cryptexd 后下一跳。若仍停在
  cryptexd/SystemOS，则停止靠 skip 推进，转回 owner manifest/AuthAPFS
  或显示依赖。等待上机。
- H96a19 已上机：driverkitd 连续 4 次 AMFI 检查后进入
  `FileSet KC`，随后 cryptexd/eligibilityd 启动并停住。四个 cdhash
  前缀均不在 H91 `kernel.macho.trustcache`、OS DriverExtensions 或
  已检查的 .kext 中（trace 只打印 16 字节，需补完整 20 字节再定案）。
- 结论：继续加 skip 不能解决；问题回到 SystemOS/Cryptex1/FileSet KC
  的真实 trust cache/owner manifest。下一步应打印完整 cdhash 并确认
  来源 cache，或做一次性 AuthAPFS/cryptexd 受控诊断，不得当作最终修复。
- H96a20（完整 32 字节 AMFI 输入/返回 trace）已构建：kernel
  `e7538cb9...`、payload `bef11b85...`、boot
  `9a409302...`；用于确认 driverkitd 检查的完整 20 字节 cdhash 和
  AMFI 返回位图。等待上机。
- H96a20 已上机：多组 driverkitd `NATIVE_AMFI_SYSCALL`/`RESULT`，
  `ret=0`；`out` 前 20 字节与 `in` 相同，尾部为 `aaaa/0` 类值，用户
  缓冲区不能作为干净 mask 来源。H96a20 确认 FileSet KC 前有多次
  driverkitd 检查，但真实 mask 仍需在 AMFI 内部 `[sp+0x58]` 打点。
- 当前剩余阻塞仍是 SystemOS/Cryptex1/FileSet KC 信任链；不再扩大
  boot-arg/skip 列表。下一步只建议小型 AMFI 内部 panic 诊断（完整
  20 字节 cdhash + mask）。
- H96a21 已构建并启动：在 AMFI 成功路径通过二进制 cave 调用
  `native_amfi_tc_diag`，对 driverkitd 打印完整 20 字节 cdhash 与
  `[sp+0x58]` 真实 mask，第 4 次 panic 固定屏幕。kernel
  `b9cd4c9b...`、payload `bc3ea90e...`、boot
  `f7e9a2f0...`；等待 panic 照片。
- H96a21 已上机并 panic：四次 driverkitd 检查均为 `mask=0x1`，
  即静态 miss、loadable hit；目标 cdhash：
  `1fc191a7fbb1e532b54ca15befb83d7efe9ab3e1`、
  `0d6972a904236c9c4bea8297c2d8bdacbeb1167a`、
  `d4c8a0514d5952c89dc24039d9efd4a26e0c87ef`、
  `d599af53ba5c1d66a0a0c9135c5756f9d3713284`。
  因此 AMFI/这些 driver 的 trust-cache 检查已通过，不是当前阻塞；
  H96a19 “不在 H91 侧车”是 trace 截断/侧车范围造成的误判。
- 当前阻塞回到 FileSet KC 之后的 cryptexd/SystemOS/launchd 等待。
  下一步最小诊断：H96a22 只跳过 `com.apple.security.cryptexd` job
  （一次性诊断，不是最终修复），观察是否到 backboardd/SpringBoard；
  或在 cryptexd/launchd 路径加日志。
- H96a22 已构建：沿用 H96a20 无 panic 内核，只在 skip 列表加入
  `com.apple.security.cryptexd`；payload `7e2c37c9...`、boot
  `1b76aac0...`。用于一次性判断 cryptexd 自身等待还是更深层
  SystemOS/AuthAPFS；不是最终修复。等待 fastboot 上机。
- H96a22 已上机：跳过 cryptexd 后反而停在 `driverkitd is active`，
  没有 cryptexd/eligibilityd、没有 backboardd/SpringBoard，3 分钟无
  变化；说明 cryptexd/eligibilityd 不是可跳过的 daemon。结合 H96a19
  屏幕也停在 driverkitd active，确认 **H96a18（不跳过
  eligibilityd/cryptexd）才是目前最远路径**，可达 cryptexd/
  eligibilityd 后停住。skip 迭代到此为止。
- 当前剩余阻塞回到 SystemOS Cryptex1/AuthAPFS/owner manifest。仅剩：
  (1) 真实 `Cryptex1,SystemVolume` owner manifest/IM4M（不可得）；
  (2) 一次性 kernel-side AuthAPFS/Image4 诊断绕过（非最终修复）；
  (3) 停在 H96a18/H91。当前安全边界不允许重新 flash 修改后的 OS/
  userdata，H93 的直接 patch cryptexd 二进制方案不再采用。
- H96a22 的基线已跳过 eligibilityd，不能隔离“只跳 cryptexd”；因此
  另建 H96a23：H96a18 skip 列表（保留 eligibilityd）+ 只加
  `com.apple.security.cryptexd`，使用 H96a20 无 panic 内核。payload
  `23b29bda...`、boot `4d23038e...`。等待 fastboot 上机。
- H96a23 已上机：保留 eligibilityd、只跳过 cryptexd，仍停在
  `driverkitd is active`，无 cryptexd/eligibilityd、无 SpringBoard，
  2 分钟无变化。说明 cryptexd 是后续 launchd 阶段必需服务，不是可跳
  过的 gate；H96a18（不跳 eligibilityd/cryptexd）仍是唯一到达
  cryptexd/eligibilityd 的路径。skip 迭代结束。
- 当前阻塞回到 cryptexd/SystemOS Cryptex1/AuthAPFS/owner manifest。
  可选：真实 owner manifest/IM4M（不可得）、一次性 kernel-side
  AuthAPFS/Image4 诊断绕过（非最终修复）、或停在 H96a18/H91。
- H96a18/H96a21/H96a22/H96a23 的屏幕日志已采集；完整串口日志、SHNGROOT SystemOS offset 和 cryptexd/AuthAPFS syscall 级证据仍缺。
- 上述 H96a 任务已由 2026-10-31 只读源码审计取代；审计报告：
  `diagnostics/H96-readonly-source-audit-2026-10-31.md`。在完成真实
  owner/VerifiedRoot 路径或 cryptexd/AuthAPFS 只读跟踪前，不再叠加
  boot-args、skip 或 TrustCache/完整性验证修改。

## H96 只读源码审计（2026-10-31）

- 报告：`plans/oneplus5-cheeseburger/diagnostics/H96-readonly-source-audit-2026-10-31.md`
- 审计边界：只读源码/日志；未修改完整性验证逻辑，未新增 launchd skip，未刷写设备。
- 结论：H96a21 的四个 driverkitd 检查均为 loadable hit，AMFI/这些
  cdhash 的 TrustCache 命中不是当前阻塞；H96a18 是最远路径，H96a19/
  H96a22/H96a23 的 skip 更早停在 `driverkitd is active`。
- 关键源码发现：H96 使用 NativeUfsProbe 的
  `native_verified_block_root_authorized()` 桩（仅检查 `device != NODEV`），
  不链接完整 VerifiedRoot；H96 的 `XNU_OWNER_BOOT_MANIFEST=0`，
  ADT `boot-manifest-hash` 是 `make-cheeseburger-adt.py` 写入的 48 字节
  全零占位，内核不生成真实 hash。
- `winBytes` 是所有 NativeUfsWindow 共享的累计上层请求字节，不是
  磁盘位置/实际 UFS 字节；H96a23 的 `315523184` 约 300.9 MiB，最后
  成功 UFS 命令为 `lba=0x1b1bca, bytes=0x20000`，没有
  `NATIVE_UFS_TIMEOUT`/`NATIVE_UFS_STALL`。
- 材料区分：SHNGROOT 是 UniXNU 用户态容器；owner boot manifest 是
  VerifiedRoot/SHA-384 派生；Image4/IM4M 是 Apple personalization；
  TrustCache 是 cdhash allowlist。当前 `Cryptex1,SystemTrustCache`
  实际加载 module 和 cryptexd 的 IM4M/AuthAPFS 要求仍为“待确认”，
  不能按源码证明标记为缺失。历史 H92/H93 的“不可得”是当时输入结论，
  本次审计将其降级为“待确认（强二进制证据）”。
- 下一步只读检查：补 SHNGROOT `window[1]=SystemOS off=...` 日志、
  launchd job/exit 观测、cryptexd syscall/mach wait 跟踪；不要直接
  patch cryptexd、改 userdata 或继续 skip。

## H98b 用户态 / UFS / owner 诊断（2026-10-31）

- 报告：`plans/oneplus5-cheeseburger/diagnostics/H98b-process-ufs-diagnosis-2026-10-31.md`
- H98b 最终屏（`221.jpg`）：
  - `NATIVE_UFS_WINDOW: SystemOS ... abs=17f540000 lba=187bc5`；
  - `NATIVE_UFS_PROGRESS ... lastWin=SystemOS abs=17f540000 ... ufsWin=SystemOS base=17f540000`；
  - 最后完成 `SystemOS block=0x43420 count=8 done=4096 ok=1`；
  - 最后可见 `NATIVE_EXEC alsd pid=63 ppid=1`，随后 `driverkitd is active`；
  - **没有 cryptexd/eligibilityd，也没有 `NATIVE_EXIT`/`H97_SNAPSHOT`。**
- H96a18 曾创建 `cryptexd` PID 80、`eligibilityd` PID 87（xpcproxy 87/88），但旧日志没有 ppid/退出/快照；因此其运行/退出状态仍未知。
- H98b 与 H96a18 使用同一 skip 列表却未到 cryptexd/eligibilityd，说明存在 run-to-run 差异；不能把 H96a18 的进程集合当稳定结果。
- UFS 映射：H98b 确认 SystemOS 基址 `0x17f540000`；H96a23 `lba=0x1b1bca` 对应偏移 `0x3268a000`，但旧日志 `lastWin=0x93450`（偏移 `0x1268a000`）不一致，旧日志无法精确归因该 UFS 请求；H96a22 最后有标签读为 `block=0x13fcf0` / 偏移 `0x27f9e000` / `done=4096`。
- owner 信任链：H98b 仍是 `XNU_OWNER_BOOT_MANIFEST=0`、`XNU_OWNER_ROOT_TRUST=0`、`XNU_VERIFIED_ROOT_PROBE=0`、`XNU_NATIVE_VIRTIO_BLOCK=0`、`XNU_NATIVE_UFS_PROBE=1`；NativeUfsProbe root 授权桩只检查设备；ADT `boot-manifest-hash` 仍为 48 字节全零；不是真实VerifiedRoot/owner 认证。
- IM4M/AuthAPFS：H98b 未到 cryptexd，未观测 AuthAPFS/Image4 调用；IPSW 只有 229-byte IM4P root-hash，`personalized_im4m_present=False`；cryptexd 字符串只作线索，当前 **未知**，不能标记为已证明根因。
- 下一步建议 H99：修复/确认 H98b 快照触发，重复 H96a18 路径确认 cryptexd/eligibilityd 到达稳定性，并增加 launchd job/exit 与 cryptexd syscall/mach wait 观测；不再增加 skip，不修改完整性验证。

## H99 快照 / UFS 诊断（2026-10-31）

- 报告：`plans/oneplus5-cheeseburger/diagnostics/H99-snapshot-diagnosis-2026-10-31.md`
- 产物：H99 kernel `512b5f90...`、boot `eab1ff4a...`。
- H99 最终屏（`222-1.jpg`、`222-2.jpg`）：
  - `lastWin=OS abs=15040000 ... ufsWin=OS base=15040000`；
  - `lastWin=SystemOS abs=17f540000 ... ufsWin=SystemOS base=17f540000`；
  - 最后完成 `SystemOS block=0x4e200 count=8 done=4096 ok=1`，偏移 `0x9c40000`，绝对 LBA `0x190190`；
  - 最后可见 `NATIVE_EXEC alsd pid=94 ppid=1`，随后 `driverkitd is active`；
  - **没有 cryptexd/eligibilityd、没有 `NATIVE_EXIT`、没有 `H99_SNAPSHOT`/`H99_PROC`。**
- H99 的 UFS 基址确认了 H96a23 `lba=0x1b1bca` 属于 SystemOS 地址范围（相对 SystemOS 偏移 `0x3268a000`，806.5 MiB；相对 OS 超出 OS 大小），但旧 `lastWin=0x93450` 偏移 `0x1268a000` 与它不一致，旧日志无法精确归因到具体请求。
- H96a18 曾创建 cryptexd PID 80、eligibilityd PID 87；H99 同 skip 列表仍未创建，说明 run-to-run 差异；旧 H96a18 没有 ppid/退出/快照，目标进程运行状态仍未知。
- H99 没有观测到 AuthAPFS/Image4 调用；owner 链仍是 bring-up：`XNU_OWNER_BOOT_MANIFEST=0`、`XNU_OWNER_ROOT_TRUST=0`、`XNU_VERIFIED_ROOT_PROBE=0`、`XNU_NATIVE_VIRTIO_BLOCK=0`、`XNU_NATIVE_UFS_PROBE=1`，ADT `boot-manifest-hash` 仍为 48 字节全零。IM4M/AuthAPFS 是否是后续根因仍未知。
- 下一项 H100：先确认 `h97_start_snapshot` 是否被调用、`PE_parse_boot_argn("h97-snapshot")` 是否成功、线程是否启动；再用短周期/exec 触发快照，重复 H96a18 路径确认 cryptexd/eligibilityd 是否稳定出现。不得新增 skip，不修改完整性验证。

## H100 无 skip 对照（2026-10-31）

- 报告：`plans/oneplus5-cheeseburger/diagnostics/H100-noskip-control-2026-10-31.md`
- 产物：H100 payload `c325a545...`、boot `f831d259...`；kernel 沿用 H99 `512b5f90...`。
- 变量：只移除 H96a18 的 7 项 `sheng-disable-jobs`，保留 `sheng-lite=1`、`h97-snapshot=360` 和其他参数。
- 结果：最终停在 `pfd`/`accountsd`/`nsurlsessiond`/`routined` 一带，更早于 H99 的 `driverkitd is active`；没有 cryptexd/eligibilityd，没有 `NATIVE_EXIT`，没有 `H99_SNAPSHOT` 输出。
- 结论：
  - 跳过 eligibilityd/cryptexd 仍已证明会造成更早停止；
  - 7 项 skip 在掩盖 pfd/accountsd/nsurlsessiond/routined 的上游等待，不是 cryptexd 缺失的唯一原因；
  - H99 与 H96a18 的差异仍有 run-to-run/未观测成分，不能归因于单一 skip。
- 下一步 H101：不增加 skip；修复快照（无条件打印入口/参数解析/线程启动），在 cryptexd/eligibilityd exec 时触发短快照；逐项记录 pfd/accountsd/nsurlsessiond/routined 的等待对象。

## H101/H102 快照调试（2026-10-31）

- 报告：`plans/oneplus5-cheeseburger/diagnostics/H102-snapshot-debug-result-2026-10-31.md`
- H101（`h97-snapshot=120` + `H101_ARM_ENTER/PARSE/THREAD_START`）和 H102（无条件启动 180 秒快照）最终屏都没有 `H101_`/`H102_`、`H10x_PROC` 或 panic；都停在 `driverkitd active`。
- H102 无条件启动仍无结果，排除了“`h97-snapshot` boot-arg 解析失败”作为唯一解释；但不能从屏幕区分入口未执行、线程启动失败、线程未唤醒或输出被滚动覆盖。
- 因此 cryptexd/eligibilityd 是否运行/退出仍未确认；不能把“最终屏没有 cryptexd”当作完整进程集合。
- 产物：H101 kernel `9990f955...`、boot `39f21579...`；H102 kernel `80c30288...`、boot `fe08ad87...`。
- 下一步 H103：在 `h97_start_snapshot()` 入口直接 panic（或不可滚动标记）确认调用；若入口正常，改为在 `NATIVE_EXEC` 的 cryptexd/eligibilityd/alsd 分支直接 `panic` + `proc_iterate`，绕过线程/定时器。不得新增 skip，不修改完整性验证。

## 设备与构建主机（2026-11-01 起）

- 内核/镜像构建：`lss@192.168.10.103`（macOS 15.8.2 / Xcode 26.3）。
- 设备连接：手机已改接到 Windows 主机 `ls@192.168.10.225`，该机 PATH 中有 `fastboot.exe`。
- Windows 镜像目录：`D:\\Users\\ls\\Documents\\OnePlus5_ios`。
- 后续流程：macOS 构建 → 把 boot/payload/kernel 放到上述 Windows 目录 → 在 Windows 上 `fastboot boot`。
- 不在仓库记录主机密码。

## H103 Preboot/Cryptex 布局审计（2026-11-01）

- 报告：`plans/oneplus5-cheeseburger/diagnostics/H103-preboot-layout-audit-2026-11-01.md`
- 说明：H102 文档原建议的 H103 是快照入口 panic；本轮先做 Preboot/Cryptex 布局审计，快照入口实验顺延。
- 用本机 21H461 OS 窗口和 SystemOS 原始镜像只读核对：
  - `/System/Cryptexes/OS -> ../../private/preboot/Cryptexes/OS`；
  - `/System/Cryptexes/App -> ../../private/preboot/Cryptexes/App`；
  - `/private/preboot` 为空；
  - `/System/Library/Caches/com.apple.dyld` 不存在；
  - SystemOS 根下只有 `System`/`usr`，包含 `System/Library/Caches/com.apple.dyld`、378 个 Frameworks、1681 个 PrivateFrameworks。
- 结论：H62 挂 `/private/preboot`、H96 `XNU_OWNER_UNION_SYSTEMOS=1` 挂 `/` 都没有命中 launchd 的 `/private/preboot/Cryptexes/OS`；这比“cryptexd 等真实 IM4M”更符合现有证据。
- SystemOS trust cache module `d121ba89...` 已经在 H91 bundle 中，不是当前缺失项。
- 已准备默认关闭的 H103 实验：
  - `scripts/prepare-preboot-overlay.py`；
  - `scripts/prepare-owner-data-fixture.py --preboot-overlay`；
  - `patches/xnu/zzzz-owner-preboot-layout.patch`（`XNU_OWNER_PREBOOT_LAYOUT=1`）；
  - workflow input `preboot_layout`。
- H103 实验路径：Data mount → bindfs `/private/var/preboot` 到 `/private/preboot` → SystemOS 挂到 `/private/preboot/Cryptexes/OS` → `DYLD_SHARED_CACHE_DIR=/System/Cryptexes/OS/System/Library/Caches/com.apple.dyld`。
- 已在远程 macOS 构建 `XNU_OWNER_PREBOOT_LAYOUT=1` 镜像；尚未上机：
  - kernel `979260df...`
  - payload `5bb75401...`
  - boot `aed7e8d8...`
  - 本地产物 `out/cheeseburger/boot/boot-H103-preboot.img`
- 已执行 `fastboot boot out/cheeseburger/boot/boot-H103-preboot.img`，ABL 接受并离开 fastboot；最终屏幕待确认。下一步检查 `NATIVE_PREBOOT: status=0 stage=5`、`NATIVE_H82_DATA`、`NATIVE_ENV`，以及 launchd 是否越过 `driverkitd`/cryptexd 等待。
- 约束：`XNU_OWNER_PREBOOT_LAYOUT=1` 需要 `OWNER_CACHE_MOUNT=1`、`OWNER_CACHE_STARTUP=1`、`OWNER_DATA_MOUNT=1`、`NATIVE_RAM_BLOCK=1`、`OWNER_UNION_SYSTEMOS=0`。不新增 skip，不修改 AuthAPFS/Image4/TrustCache 验证。

## H103au–H103bg：启动前沿、显示参数与颜色面板（2026-11-03）

详细记录见
`plans/oneplus5-cheeseburger/diagnostics/H103-preboot-layout-audit-2026-11-01.md`
第 44 节。本段只保留结论和关键产物。

### 对照结果

- **H103au**：H103at + `offline-mobile-graphics-client=1`；
  `display calls=0`，backboardd 已 exec 但不打开显示。client probe 无效。
- **H103av**：加入 `NATIVE_ALIVE` 心跳；cpus=1/2 都停在
  `driverkitd is active`，屏幕没有 `NATIVE_ALIVE`。心跳线程启动/调度
  本身仍需独立诊断。
- **H103ax-1g**：Data 512 MiB → 1 GiB；3 次仍为 2 次 `calls=0`、
  1 次 FDR loader `calls=1`，无 ENOSPC。Data 容量不是唯一原因。
- **H103ay-4g**：`owned_memory_size` 从实际 1 GiB 改为 4 GiB；
  1 次出现 `backboardd/55` + `SpringBoard`，但 backboardd 仍不 open；
  另 2 次停在 driverkitd/eligibilityd 一带。内存也不是稳定修复。
- **H103az/H103be**：AMFI 权限门 `0x5f` 的 `tbz` NOP；H103be 上机
  1 次白色、2 次绿色，右上始终没有 backboardd 青色。AMFI 不是
  backboardd 不 open 的根因。
- **H103bf**：追加 DinnerABC 的
  `offline-mobile-graphics-client=1`、`offline-legacy-cpu-present=1`、
  `unit-scale=2`、`sheng-vsync=60/fixed=1`；仍无 backboardd client。
- **H103bg**：Data seed 写 BackBoard/CA 软件渲染偏好
  （`CAEnableDeepFramebuffer=false`、`CAEnableFramebufferCompression=false`、
  `CAEnableIOSurfaceCompression=false`、`CAForceSoftwareRendering=true`）；
  仍无右上青色。

### 颜色面板结论

H103ba–H103bd 在 `NativeLegacyDisplay::drawTestPattern()` 增加右上角
2×2 颜色面板，避免依赖滚屏日志：

- 左上：用户态前沿/退出（H103bd：绿=SpringBoard、白=SpringBoard+backboardd、
  紫=SpringBoard 退出、品红=backboardd 退出）；
- 右上：黑=无 client、黄=FDR loader、青=backboardd；
- 左下：client calls；
- 右下：swap/transfer；
- 右下角 16×16 心跳闪烁。

上机结果：

- H103bc：3 次都只有左上绿，backboardd 没有 exec；
- H103bd：2 次左上绿；1 次左上白、右上黄、左下蓝，等待 10 分钟不变；
- H103be/bf：最好也只有左上白 + 右上黄，没有右上青。

因此当前不是“backboardd 打开显示失败后立刻退出”，而是：

```text
backboardd 有时根本不启动
即使启动，也不调用 Legacy display 的 newUserClient
```

### 只读 backboardd 分析

`/usr/libexec/backboardd` 导入 `IOMobileFramebuffer`、`Metal`、
`IOSurface`，包含 `BKDisplayStartWindowServer`、
`BKDisplayIsHeadless`、`StartWindowServer: headless (display:%d/server:%d)`、
`IOServiceOpen failed for class ...`、`CADisplay`/`CAWindowServerDisplay`。
最符合证据的解释是 backboardd 没有拿到它认可的 CADisplay/display
provider，进入 headless，而不是 Legacy `newUserClient` 的问题。

### 当前阻塞与下一步

已排除：Data 512 MiB/1 GiB、4 GiB 内存、AMFI gate、client probe、
`offline-legacy-cpu-present`、`sheng-vsync`、unit-scale、Legacy
`newUserClient` 路径、BackBoard/CA 软件渲染偏好。

下一步按 A 线 provider 对齐计划执行（详见
`diagnostics/A-provider-parity-plan-2026-11-04.md`）：

1. **A1**：发布最小裸 `IOAcceleratorES`（不设置
   `MetalPluginName`/`MetalPluginClassName`），验证 backboardd 是否
   因为没有 accelerator 而 headless；
2. **A2**：A1 不足时，补最小 display 属性
   （`DisplayWidth/Height`、`RowBytes`、`PixelFormat`、
   `IOMFBDisplayRefresh`），不改 timing；
3. **A3**：仍不足时，才做 `IOFBDependentID=0` /
   去掉 `NativeSandboxGrantProcess` / `IOClass` override 的 identity parity；
4. **A4**：最后才补父节点 `compatible`/`canvas-width/height`。

不再增加 boot-arg、`sheng-disable-jobs`、Data 容量或 AMFI 诊断；
不再做 display timing/ordering/delay/pump 实验。

### A1 结果（2026-11-04）

`boot-H103cw-a1.img`（boot `43f831fb...`、kernel `4950bfa6...`、
payload `3ac218e4...`）上机 **panic**：

```text
Can't load kext com.apple.iokit.IOGPUFamily - requests to user space are disabled.
OSMetaClass::preModLoad() wasn't called for class NativeA540AcceleratorProbe
```

- `NativeA540AcceleratorProbe` 的 `OSKextLoadKextWithIdentifier()`
  返回值判断写反；
- pre-root late probe 阶段无法加载 `IOGPUFamily`，原始
  `IOAcceleratorES` 基类不可用。

结论：**A1 作为 pre-root 裸 accelerator stub 不可行**，源码已回退，
H103cg 仍是唯一稳定基线。下一步建议 **A2（最小 display 属性，不依赖
IOGPUFamily）**；A1b（prelink `IOGPUFamily` / post-root publication）
会改变 prelink 布局，暂不执行。详见
`diagnostics/A-provider-parity-plan-2026-11-04.md`。

### A2 结果（2026-11-04）

`boot-H103cx-a2.img`（boot `571d77b7...`、kernel `4fbd7673...`、
payload `9a6efa2e...`）：

```text
NATIVE_LEGACY: display properties 1080x1920 row=4320
NATIVE_LEGACY: phase=4 dt=1 probe=1 attach=1 start=1 reg=1
Waiting on <dict ... IOProviderClass=IOMedia ... UUID=04769D26-...>
```

- A2 属性已在显示服务上发布；
- 但 Legacy archive 变大再次触发 Data/APFS IOMedia race；
- 未到 backboardd，`calls` 无法评估；A2 镜像不可用，H103cg 保持。

**A2/A2c 已停止（2026-11-04，越界）**：用户指出 A2 改了 display
属性/refresh，A2c 又移除了 `drawTestPattern()` 的 accel 方块并改变
archive 成员，违反“不再做方块/灰条/display 时序/ordering 实验”的边界。
已恢复 H103cg 源码和 7 成员 builder；A1/A2/A2c 镜像均不使用。后续除用户
明确要求外，不再做 display archive、方块/灰条、display timing 或
ordering 实验。

### H103cg-1 结果（2026-11-04）

`boot-H103cg-1.img`（boot `e1fb98d4...`、kernel `7edeaf8e...`、
payload `105c0c08...`）按 H103cg 源码重打，仅把三个 16×16 指示块从
右下角移到左上角：

```text
fill(w-16,h-16) -> fill(8,8)
fill(w-40,h-16) -> fill(32,8)
fill(w-64,h-16) -> fill(56,8)
```

上机仍然停在：

```text
Waiting on <dict ... IOProviderClass=IOMedia ... UUID=04769D26-...>
```

结论：即使只改三个坐标常量，display archive 的二进制/布局变化仍会触发
Data/APFS race；H103cg exact 是唯一稳定点。源码已回退；H103cg-1 不使用。
指示块只能从 display archive 外部覆盖/绘制，或先修复 race，不能再通过
重编 display archive 移动。

### H103cg-2/H103cg-3 对照（2026-11-04）

| 包 | 内容 | boot | 结果 |
| --- | --- | --- | --- |
| H103cg-2 | H103cg 原镜像逐字节拷贝 | `4560ac5b...` | 可用 |
| H103cg-3 | H103cg 同源码 clean rebuild | `d9e3322b...` | 停在 `IOMedia UUID 04769D26` |

H103cg-3 源码与 H103cg 完全一致（submodule `5f29dde`、7 成员 builder），
但 kernel/ADT/payload 构建输出不同：

```text
H103cg:   kernel 7b5d4ae1...  ADT d98d9522...  payload a12f5fc9...
H103cg-3: kernel fe155269...  ADT 158ebdb6...  payload 9d8fb9b7...
```

结论：**H103cg 源码可重复，二进制不可重复**；精确布局/时序变化会触发
Data/APFS race。H103cg 原二进制是唯一稳定点，H103cg-2 是可用拷贝；
H103cg-3 不使用，不再把“同源码重编译”当作无改动包。后续只能从
display archive 外部覆盖/注入，或先修 race。

### H103cg-6 成功（2026-11-04）

`boot-H103cg-6.img`：

```text
boot:    1201d68379dedd8f08a3a01cdadccde9caf6009b9304e2dae5f4a8f410a4ac7f
kernel:  76634aa5e4104d97b53526dfa0d33b212846a959951525fc31935dfc52982e6e
ADT:     d98d9522451a66bd380a3e45e0f8d5b7df417b8d5a8fd2c4a847b6a21d695fef
payload: c9f841e6f7612bd58dc3e96eb1227a5a165f540f8eca8aea6251dcf62911fdb6
```

H103cg-6 通过 root，可用。根因是 macOS 构建树的
`NativeUfsProbe.cpp` 仍是旧 Android 逻辑：GPT 选 `"userdata"`、root
manifest/window 绝对地址硬编码 `0x14f40*4096`。H103cg-6 恢复为
H103cg 的 `userdata_ios` first LBA，并修正：

```text
rootFirst = GPT 中 userdata_ios 的 first LBA
manifest  = rootFirst
window    = rootFirst * 4096 + rootOffset
```

H103cg-5 只修了 partition，window 仍用 `0x14f40`，所以仍失败；H103cg-6
修正 window 后通过。

结论：

- 重编译内容可以稳定过 root；H103cg-6 是可用重编译基线；
- H103cg-2 是原镜像逐字节拷贝，仍可用作原版对照；
- 之前 H103cg-3/4/5 的 `04769D26` 是 UFS probe 分区/window 错误，
  不是 display archive 或 kernel 时序；
- display provider 仍 `calls=0`，无 UI；本次未移动心跳指示块。

### H103da 候选（2026-11-04）

`boot-H103da.img`：

```text
boot:    997f5cd88d42fbbe4c8f0f8bd8d4abf42b3b6aa74c4f4ab044c446a9316c7a53
kernel:  f8a238140fdaf0ee0082e4c77ed7389c6b50efa536230a7e16762a468ee1d6c3
ADT:     d98d9522451a66bd380a3e45e0f8d5b7df417b8d5a8fd2c4a847b6a21d695fef
payload: 9ebfe2ec3dbc7ae578d7683d376a3b4e9b583268690e8566dfed6b053ec1944e
```

基线 H103cg-6（`userdata_ios` probe + window 修复）；display 只把三个
16×16 指示块移到左上角 `(8,8)/(32,8)/(56,8)`，其他不变。

上机：**root 通过，`calls=0`，左上角三个块没有出现**。当前
`NativeUfsProbe` heartbeat 没有调用 `native_legacy_test_pattern()`，
clean rebuild 丢失了 H103cg 的周期重绘调用，`drawTestPattern()` 从未
执行；坐标本身已编译进 archive。

### H103db 候选（2026-11-04）

```text
boot:    efc4bc2fbb5bc6721982046cc9b837b8182af55f4ee409b5035c8110e0849f21
kernel:  d559aef221fbf554db6994e75d609dffb49d9e5d730596feabfec99bed0f8713
ADT:     d98d9522451a66bd380a3e45e0f8d5b7df417b8d5a8fd2c4a847b6a21d695fef
payload: 3d0a464f0b082d5d413f2193aa89c95fa809f499febb5fd450d31c2e3a239d51
```

`NativeUfsProbe` heartbeat 每 10 秒调用 `native_legacy_test_pattern()`
（weak import），恢复 H103cg probe 的周期重绘。display 仍是 H103da 左上角
三个块；root 修复不变。

上机：右上和左上都没有方块。H103cg 原 kernel 反汇编确认 heartbeat 顺序是
`IOLog -> native_legacy_test_pattern`；H103db 在 `IOLog` 前调用，日志把
图案盖掉。H103dc 把调用移到 `IOLog` 后。

### H103dc 候选（2026-11-04）

```text
boot:    909bb8998f5f9c06e3a1ab5e43fb404673560e82ced026c072e45c70cd1ab5f5
kernel:  1c1032a4dfad07a3cc0331c5cb4b5bb561923e4378f06a374b79f0952910462f
ADT:     d98d9522451a66bd380a3e45e0f8d5b7df417b8d5a8fd2c4a847b6a21d695fef
payload: 5caa2c9ceb4461fbec201d337eddaca1394d88629cadd440bb2e5327ced060a9
```

按 H103cg 原 heartbeat 顺序，在 `IOLog` 后调用
`native_legacy_test_pattern()`；display 仍是 H103da 左上角三个块。

上机：仍然没有方块。H103cg 原 heartbeat 是直接
`bl _native_legacy_test_pattern`；H103dc 的 `weak_import + if (symbol)`
在 kernel link 里绑定为空，调用被跳过。H103dd 改为直接调用。

### H103dd 候选（2026-11-04）

```text
boot:    571b00917dceb81f478e59ce070bf278311775af739f174f858a8ae563b5f4ff
kernel:  971f37ee5330129b116e315c01ff678aeed00f0d9647fbe42160376019462c9c
ADT:     d98d9522451a66bd380a3e45e0f8d5b7df417b8d5a8fd2c4a847b6a21d695fef
payload: 310053dbc060c32d5c20ac13ce9edbe24a616ab888e71d0037b19f4aaeb02335
```

反汇编已确认 `IOLog -> native_legacy_test_pattern` 直接调用，和 H103cg
原 kernel 一致。结果待上机。

### H103de 候选（2026-11-04）

```text
boot:    c21879d16a8351ebfcd0dd0f11dcace35241af9c88a2c0d32007fa914b80be0c
kernel:  1476ba31c9c0ecb6ceb9eb5145e3291a577821328b9a50f5d040745c967b3846
ADT:     d98d9522451a66bd380a3e45e0f8d5b7df417b8d5a8fd2c4a847b6a21d695fef
payload: e777c4e1fc6f0aaf5d0d353ac309683781398ccc3d7ccc73e8a1b4f01eafd30c
```

`NativeUfsProbe` 增加 `sheng-log=1` 持久化快照：写 `userdata_ios` 末尾
32 MiB 的 `SHLOG001` v3 头 + 1 MiB kernel msgbuf；probe start 后等待 20 秒
（root 完成）写一次；WRITE(10)+SYNCHRONIZE CACHE；不碰 Android
`userdata`/display archive。新增 `scripts/h103-log-pull.py` 从 TWRP 只读
拉取并解析。结果待上机。

### H103df 候选（2026-11-04）

H103de 的 32 MiB 窗口全 0；H103df 在 body/header 前先写一个
`H103TEST` 块并读回，失败时停屏 30 秒。puller 识别 `H103TEST`。

```text
boot:    2a998098c4c2c147d0d52ad71997e98d67eb3cbc75616a76c1ebd43daaafe4f3
kernel:  87aff456682aaa40b8aaff49d6606268fd578c9310fdbab9bb3ecb8cae5a2eaa
ADT:     d98d9522451a66bd380a3e45e0f8d5b7df417b8d5a8fd2c4a847b6a21d695fef
payload: faed19554139fd3457b566715963a5b752175b1041a38700ce83569557afb70b
```

结果待上机。

### H103dg 候选（2026-11-04）

H103df 仍无 `H103TEST`；H103dg 增加：

- `NATIVE_UFS_LOG: setup ...` 后停屏 30 秒；
- `H103TEST` 同时写窗口 block 0 和最后一块；
- puller 扫描整个 32 MiB 窗口找 `H103TEST`。

```text
boot:    94040dc74ea2b20f2ba9b8655b92b1dc9d3bf81502a95884f94145c1d5aa9685
kernel:  f1bd6e7cba37a50b73df7b57fdf18bd5ec032ba1962a29af9c3e8ae7d4f21d2e
ADT:     d98d9522451a66bd380a3e45e0f8d5b7df417b8d5a8fd2c4a847b6a21d695fef
payload: f637a087a016b84c505a1b4edffbd52e56b54b0b1dc9d9b1b2cdb75df5b480a9
```

日志仍在 `userdata_ios` 未使用尾部；Android `userdata` 是 64 GiB ext4，
不能盲写末尾 32 MiB，若必须用需先校验 ext4 文件系统边界。

### H103dh 候选（2026-11-04）

H103dg 的 30 秒停屏在 probe start 末尾、UI 之前，上机反馈“启动后没有
停留”。H103dh 把停屏移到 heartbeat 启动后 45 秒，并打印 setup 参数：

```text
NATIVE_UFS_LOG: late mode=%u first=%llx root=%llx..%llx
IOSleep(30000)
```

log 线程仍在 20 秒写 `H103TEST`（首块/末块）和 `SHLOG001`。

```text
boot:    d35d87a2a744f749078a06c5d3d41ec0e78ffbae8cd7491a8b31332bcc05dc46
kernel:  3bfb18374e43aeda3ce94d44f7a0a20d89112c64c71091ebd629e10a6ca7f896
ADT:     d98d9522451a66bd380a3e45e0f8d5b7df417b8d5a8fd2c4a847b6a21d695fef
payload: 5a123ca1658860d6a315595b0524d6096ae3b8726268cd1bab30d4cf3ea6883c
```

### H103di 候选（2026-11-04）

H103dh 确认 boot 哈希正确，但启动后仍无 45 秒停屏，说明 heartbeat 不可靠。
H103di 把日志写入从 heartbeat 解耦：

- `NativeUfsProbe::start` 同步写一次 `H103TEST` + `SHLOG001`；
- `NativeUfsWindow::doAsyncReadWrite` 累计 64 MiB 后触发第二次快照并停屏 30 秒；
- `logThread` 先写测试，不再因首块非空提前返回；
- 打印所有 `kernel_thread_start` 返回值；
- heartbeat 改用 50 秒计数停屏；
- payload 加 `h103-skip-pattern=1`，本轮不画 2×2。

```text
boot:       e025ea4f401e1fca990ae160c300058775e2c8a5b1b3b852de1b2694297e9f0a
kernel pre: a59dd74d8ca950d2dc0e462e78182d56999384c8219d570fa79fef2593b889b7
kernel:     171185264ec4c0eae82722164b45094045860d657f490bd982d6179390e66cc0
ADT:        d98d9522451a66bd380a3e45e0f8d5b7df417b8d5a8fd2c4a847b6a21d695fef
payload:    d046fc95e76e648a707f131ea6e15c77634079e8ef5a35ad4cb969b45d3e8763
```

H103di 上机在 `start snapshot begin` 后 panic。原因是
`start` 栈帧 0x2360 加 `h103Snapshot` 栈帧 0x20e0（`test[4096]` +
`check[4096]`）超过 16 KiB 内核栈。H103dj 把 test/check 移到
`IOMallocAligned`，`h103Snapshot` 栈帧降到 0x120。

```text
H103dj boot:       4a9de88e2c884506b75bcfeb14ea82a2e2ee09c8afa2711fe14a622d267d2047
H103dj kernel pre: 77134eed24d453e1653666613b397a7d591e08755726eb34a69f72dbf1527d06
H103dj kernel:     276f9927225058e5d7a0fab702a0af6150af0edac16614159fd4c778642a38b2
H103dj payload:    4a8321c68e55f9bd373f0ada49d8396a99331c6e6400efc9827b4b5f5d600897
```

H103dj 栈修复生效，但仍在 `start snapshot begin` 后 panic，PC/LR 在读路径
附近。H103dl 把所有快照的读回关掉，只写 `H103TEST`/body/header，由
puller 验证。

```text
H103dl boot:       491301d0755278baf94c6253c85d2f00306321a96cd39e0e269871094829889b
H103dl kernel pre: 6c0c1f3c55cf9fc5c7590c8f164c4efe2cdaf3c13b492e99f1688d96ab941e16
H103dl kernel:     79646a830126c3aa88afa44ffc90475737e67da6d0de53e8c6de4c210b695ed6
H103dl payload:    9db662466cf4e3ac3dedea4094ab6e704713fd55780595b16d2b86f43b6ad797
```

H103dl 仍 panic 在 start snapshot begin，PC/LR 在 readBlocks/readWindowBulk
附近。H103dm 删除 start 快照和 logThread，只保留 root 之后 64 MiB 窗口读
触发的一次快照，写 H103TEST/body/header，不读回，停屏 30 秒。

```text
H103dm boot:       6de47089a78519b1f239850ec11c9476397bbfc0f2b4df7f578d7e2b93e730cb
H103dm kernel pre: 8676eae81e20f71ddb3e42776e26604e59498e6b658e4b034bed98291fad8d49
H103dm kernel:     5e88804355076fa0231afc2cd8ab4b3906736c637933733053e20c0d6bf41014
H103dm payload:    dc2ae7b63c084cc68762bc4a42ac1b0316c93bb24f8a22a0495faf3bf5323350
```

H103dm 进入用户态，但 `window trigger bytes=67695616` 后仍 panic。H103dn
增加独立的可写 log DMA 缓冲，`logWrite()` 临时替换 `fQueue.data`，避免
CPU 写 live readData；快照仍只在 root 后 64 MiB 窗口读触发，不读回。

```text
H103dn boot:       f6d4e0dcc574a6c1a37f6281caa30d99b40007c819817cfa9772ce907c14aefb
H103dn kernel pre: 0abc16afdfdf987ecfd69f684194cba5c7a0a279ab610516eb4e594d90e03b19
H103dn kernel:     2a86871c25939305e32bcfb72dd2c75d25baf8914a57f262ddf29a2fed20b650
H103dn payload:    8700cc6deb106c3697fcab7bfb12e0074c51955be060c247ce0f66f27d5e4b6e
```

H103dn 仍在 window trigger 后 panic，无 `write test`。H103do 只做单块
WRITE(10)，不写 body/header，不读回，用于隔离写命令本身。

```text
H103do boot:       7c9c92d6fcbc0ee4ada709dbe991974dd632bb1496e0786de71ee5b3f3190235
H103do kernel pre: d5a08ec5023c0e3b3a0b8cf51392400497137e40c1bf2dcaf1a049135c18ae3a
H103do kernel:     7d4d9969d65d9d478a02f6b03be69fa3ff920b3089bb39149d477f37c3f7d097
H103do payload:    8ee9f42772bfa247c9f6cbf2138d503533c7b6e994806061fe450d4f3fc5fd92
```

H103do 单块写开始后 panic，确认单块 WRITE(10) 就崩。H103dp 用
`fLogDma->writeBytes()` 填 DMA，并让 `ufsWriteCommand()` 的 `src` 等于
`q->data`，跳过内部 memcpy；仍只写单块。

```text
H103dp boot:       89e1ea912460ce161b2d58297f767e5a0f8bcd8ba1522f5d9754d02540c085a3
H103dp kernel pre: c0a274cb2e0c4f5c230c0817187a9545a532e09ecbba35e2820f6f59b455695d
H103dp kernel:     4b5ca54dddef09acad19e03a3b7965f6edf5f1cf0cfb25eb85502c8bca3b7f60
H103dp payload:    fe6de668a05e4b4b83dd0a7f5ca91170a0adef4119d963bd5f88c048ef40542f
```

H103dp 在 `fLogDma->writeBytes()` 触发 `kIOMemoryPreparedReadOnly` 断言，
因为 log DMA 用了 `kIODirectionOut`。H103dq 改成 `kIODirectionIn`，仍只写
`userdata_ios` 窗口首块。

```text
H103dq boot:       6f6ffb9148f947a7097e25cdba0b29d9a53764c9dce91688aae7d314fc5879e0
H103dq kernel pre: 664888302c4e39d9fc3418fef47f50d03c179d034d016cc50b3f89b318bceba7
H103dq kernel:     f100195bb7fa93a7f8ec2ed51ce29cf63a971f2ccb60c6c5451c5f93016be97e
H103dq payload:    9fb582e3d250d6ec5de36e38ced8b0a77e62f1ea1d3345be4aa5d76f2bc03d52
```

H103dq 单块写成功；TWRP 拉到 `H103TEST`。H103dr 恢复窗口触发的完整
`SHLOG001` 快照，仍只写 `userdata_ios`，不读回。

```text
H103dr boot:       9d566a4ceea7243ecde98f7442265deafc5f96e1a93c0011bedde4122d99beb3
H103dr kernel pre: 09733c6e1b0d7baf4497454d5194c32f8acfd34268054015a2447dc3f99c5c0a
H103dr kernel:     a5c0a9ead8ea24ba3ce49a8a8e1976226211abf9879f47fff6f8ee196490e8ee
H103dr payload:    c12852ab4ac428385f91594774f611d44176326b5bd98689598d43f3c67943d5
```

H103dr 的 SHLOG001 成功，但 64 MiB 快照停在 launchd/dyld cache。H103ds 在
heartbeat 第 12 tick 增加 late 快照，并去掉 `h103-skip-pattern=1` 恢复 2×2。

```text
H103ds boot:       5f6bfd17c9ad87a3070de1da27a56f907b58bdbef12d838e030ed261531388ec
H103ds kernel pre: e0f57cb61db08be3685de2d8b8cb50f2eeed58006836b3082ca5223c6b4c3080
H103ds kernel:     22e1aa5980149bef455326e3c5d83def80fff98db18b6652294d807822be814c
H103ds payload:    39194bd3cdfb27df875b076ae1a5e28aa1bcfe83c9e9749f34da43d680edae8b
```

H103ds 屏幕停在 `write test`；日志已有有效 SHLOG001，卡点是 header 后的
`logSync()`/SYNCHRONIZE CACHE。H103dt 删除 logSync，WRITE10 用 FUA；
早期 window 快照 pause=0，late heartbeat 快照 pause=1，保留 2×2。

```text
H103dt boot:       d4c770df3ade2f3a7990daa673e58aa3cc77d9f974710ac20f17a70fe1977486
H103dt kernel pre: 50958392dfcd669560aae3ce3ccde2bae1fbc7cc9795f4049b3eb86f34099a7e
H103dt kernel:     1cfdae53833c10286e1f202f46a9a6ad48e24df679648c7a727c92d70e334935
H103dt payload:    f7e1792c50851f052e87f1dbe867c247ad478099266511a5a8d72e1cf29f8fa6
```

H103dt 屏幕继续但 TWRP 文件仍是 64 MiB 早期快照。H103du 增加 1 GiB
`late-window` 快照，两个窗口快照都不停屏；puller 增加
`blockdev --flushbufs` 和 `dd iflag=direct` 回退。

```text
H103du boot:       ba2c1f0a0c75e3e380d794d4794455853b86e6dfa70fcb5833d88857aec4f76c
H103du kernel pre: 495beb9382fbec52b0eefe65dba8b6a3232cd2b407f55b667fc7aaafe6c51178
H103du kernel:     2e660304ece04bfff8638583841be9629ce4517fd950f95b69557aa17db3f854
H103du payload:    c1e7d4a2a77cea2e088da84f8951b739fc40d8435314076c33578208e80d3c59
```

H103du 仍在 late-window 的 1 MiB body/header 处停住。H103dv 改为
heartbeat 每 2 秒写 128 KiB slot，关闭窗口大快照；puller 读最新 slot。
显示侧增加 `NATIVE_LEGACY_PATTERN` 打点。

```text
H103dv boot:       4ed5ce91831621d2b4868f3d88b290d3421c50e61dcb6647b69162a8c1589676
H103dv kernel pre: 441f56c0d14157736532afeace2b2a892ab79acac229f14fdc58d62060fb0b69
H103dv kernel:     593be1065ebfc7f3e0633c66ed0e826178c4b66a2b8ee8062eec71e568962c32
H103dv payload:    ad3b2e80ddd06480261db84ec1698a3c39d39ad64eb1af52fd85044bf76db364
display:           03fa715 (GopFramebuffer display-op5)
```

H103dv 的 display archive 没链入/console 刷屏。H103dw 回到 H103bd 的
`5f29dde` 2×2 颜色面板，保留 2 秒 slot，`gH103Quiet=1` 关闭周期 UFS 日志，
payload 去掉 `-v`/debug/boot-progress。

```text
H103dw boot:       99307b5f99a7154a7212b5e9ff3e0ef562b5fc89e98d4bf334c42c28cde7fcc4
H103dw kernel pre: 57122244f4a55f1e37d02753b094c8f812f163b18478b4a81e4aa38eff235bc8
H103dw kernel:     61603873ad954ef895b3ffefebf722bb3bde3e9ef4fca3f06827b20997628374
H103dw payload:    ff3a13661b3fbccbe38a42633865b84b51cfd57dd8b7f2ba4db218a096462217
display:           5f29dde
```

H103dx 只改顺序：先 `native_legacy_test_pattern()`，后 `h103SlotWrite()`；
仍静默、2 秒 slot、`5f29dde` 2×2。

```text
H103dx boot:       7c4f4e6dde4f55a971dd8abb92e7715d8fdcc9a457670977692e9aed76b21211
H103dx kernel pre: cb65784262dbdc70c6a868664cf58a5f2f7885c85e9ce926fc9c5ae604cc058c
H103dx kernel:     5addf7cd16d56c698344a93af59b33ddb504b6043e29b5ee1721cbcf094fafd5
H103dx payload:    a339391de2d8ba325efd693fa7e5c5a96bd2e8a352f50540d8bec5eae6f435de
```

H103dx 上机 2×2 和 slot 都没有更新。H103dy 完全移除 slot，只保留
heartbeat 2×2，用于判断 heartbeat 是否运行。

```text
H103dy boot:       5bc2f835a73f93c5b0c7717f6d8987fc2ea457bd6c5ef9799bafaa012bf35cef
H103dy kernel pre: a324abf0ea174d95b272ee2cf72400d3adefcab9909bdf71b24169cd57b98335
H103dy kernel:     9cda2b9a65a211ea1b84835b42db7111d08eb13068186cecc95167e4b637b1e1
H103dy payload:    116b6885483c2139e0eff3fa0739c0e511471cb981c9d3f33119d30ee1ca9dd5
display:           5f29dde
```

H103dz 用 H103bd 的 payload 参数（-v/debug=0x14e/boot-progress=2）配
H103dy 的“只画 2×2、不写 slot”内核。

```text
H103dz boot:    4fecce628a60ae0ce0bbf39923931b92f3f4152d5f07b25205caca661f9803c8
H103dz kernel:  9cda2b9a65a211ea1b84835b42db7111d08eb13068186cecc95167e4b637b1e1
H103dz payload: 06c4e71bac2bc1f727de45dedd566c888d99564de11268b9a922b91eaa304749
```

H103ea 回到 H103bd 的心跳节奏：`IOSleep(10000)`、先打印
`NATIVE_UFS_ALIVE`、再调用 `native_legacy_test_pattern()`；`gH103Quiet=0`
恢复周期日志。2 秒 slot 移到独立线程，只在测试图案之外写
`userdata_ios`，不改变心跳顺序。payload 沿用 H103dz 的 H103bd 参数
（含 `sheng-log=1`），display 仍是 `5f29dde`。

```text
H103ea XNU:     32d9bf03ef5a76b64ea4d1f76d38879b150beea8b98a69904ff916b7e9a56166
H103ea kernel:  560571229630b8edd2e235d7c8104ca57cf9fd320e1efe2ec041f7a5cda62e61
H103ea ADT:     d98d9522451a66bd380a3e45e0f8d5b7df417b8d5a8fd2c4a847b6a21d695fef
H103ea payload: 4da9f3c04fc867713badd6ca243c20797ad5f7def0ca39b23fd3219d669c6665
H103ea boot:    a07fe2567b43edcf7cbd58a86a8d4474cae49ac84e79b667e074205e1b4e2f5e
display:         5f29dde
```

上机判读：

- 右上 2×2 出现：H103bd 的心跳/日志顺序 + slot 线程可以共存；
- 2×2 仍不出现：display archive 与 H103bd 的差异（其内核含
  `NATIVE_LEGACY_STATE/CLIENTS`，当前 `5f29dde` 不含）或独立 slot 线程
  是最后两个变量，下一步回退到 H103bd 精确镜像；
- TWRP 应能读到每 2 秒更新一次的 `SHLOG001` 最新 slot。

H103ea 上机后右上仍没有 2×2；换成 H103bd 原始 payload 参数
（无 `sheng-log`、无 `native-cpufreq-early`）的 `boot-H103ea-bdargs.img`
也没有。直接对比 H103bd 内核后发现：H103bd 的 **UFS 完成路径**在
`(gUfsCommands & 63) == 0` 时先调用 `native_legacy_test_pattern()`，再打印
PROGRESS/STATE；当前源码只在 10 秒心跳里调用。H103eb 恢复这条 UFS
完成路径重绘，仍用 H103bd 原始参数：

```text
H103eb XNU:     7c4af02f609ae748b4f2e71fcb3b0efb40d660e7bde8bec90412a03dcf61d14d
H103eb kernel:  91959feaa38bf249f91cfe6b534968b00498817cc5d704bffe377169d00ba691
H103eb payload: 32963f1f9e9f4e90af00fa1cbb3cef829cd143fbf0e7c986902253f53959cfc1
H103eb boot:    614c83a5d912ca4c5ab815190e95122a30a2f972518d89fcee1fc73763ea1f59
```

H103eb-bdargs 上机确认右上 2×2 出现。H103ec 保持同一个 H103eb 内核，
只把 H103bd payload 参数加一项 `sheng-log=1`，让独立的 2 秒 slot 线程
真正写入 `userdata_ios`：

```text
H103ec kernel:  91959feaa38bf249f91cfe6b534968b00498817cc5d704bffe377169d00ba691
H103ec payload: 6160640a420c170ab779af16af94e41bea58fe531e2d8f67b763c021b0330424
H103ec boot:    a03f380ded8ff9a3519f217abf0e4f966ca782bf027665e86ce04ae22a20ae1a
```

上机判读：2×2 仍在且 TWRP 能读到持续更新的 `SHLOG001` 最新 slot，就说明
H103bd 面板 + 2 秒持久日志可以共存。

H103ed 把右下角三个 16×16 状态块移到左上角（CPU 频率 x=0、A540 x=20、
心跳 x=40），并在 payload 里补上 `native-cpufreq-early=2`。当前内核
（H103eb）其实已经链入了 `NATIVE_CPUFREQ_EARLY` 代码，只是 H103bd 参数
没有这个 boot arg；H103cz 当时传了，所以频率块是绿色。

```text
H103ed XNU:     ed750931b794ebe206e1fd5aada71d5459fba0268b77e597b33c010de5e4fdb9
H103ed kernel:  fec7abd797ce920211cae12f9135ae5e05b3ac9cca70e91392123cdb3f4fbc40
H103ed payload: 0d0a2176c87ee724b9a3c17c546e9d0708fddfbd4c3b17331b927235c5622b4d
H103ed boot:    59908da2f8804aa2723b1d10ad57254001356129e38effca9bcdeb0850c4c059
display:        adf38de
```

注意：`NATIVE_CPUFREQ_EARLY` 目前只存在于 Mac 构建缓存里的
`build/sheng-platform-power/ShengPlatformPowerGlue.o` /
`libShengPlatformPower.a`（11 月 4 日 15:57），当前设备仓库的
`ShengPlatformPowerGlue.cpp` 已被覆盖成短版本。重建该 archive 前必须先把
早期 CPUFREQ 源码恢复，否则会丢失 `native-cpufreq-early=2` 的实际编程。

H103ed 上机后 TWRP 读到的 `SHLOG001` 仍是 snapshot 写的
（`state=2`、`rounds=1`、`slots_written=0`），slot 线程没有更新 header。
H103ee 加诊断：slot 线程启动前后各打一条 `NATIVE_UFS_HB: slot ...`；
`h103SlotWrite` 前 8 次尝试打印 ready/lock/dma、body 写结果和 header 写结果；
`h103Snapshot` 不再把 `slots_written` 清零。

```text
H103ee kernel:  96321f31312d788dca3410eea773b9a4bad638351256061767df3f6d6124443f
H103ee payload: efa7b4ff54af63d3c7392dbf68e02e5a929dd398078cb7442aee3094b5dbaedb
H103ee boot:    b742d928fea5eb7dbc7e4f92245dcacbd0969584462fabf192957783c708e34a
```

H103ff 在左上角增加第 4 个 16×16 slot 状态块，不再依赖 TWRP 判断线程：
黑=未就绪，黄=窗口就绪但写失败，绿=slot+header 写成功。UFS 侧导出
`native_h103_slot_state`，display 侧读取。

```text
H103ff kernel:  4bd10ad1b1d3d205e4edb5661102e581bf4c8c551615104985bd3743e1b852f8
H103ff payload: 4692224ecbe160d584fab0bcb8b3d18f6e6d1076c53c7b42df202610fc077057
H103ff boot:    cccf4fad7c75a430ac15bf59538bcfc17432bebefeb5cb3388dcc24dc5902489
```

H103fg 把第 4 块改成始终可见：红=线程未进入，蓝=线程已进入但窗口未就绪，
黄=窗口就绪但写失败，绿=写成功；第 3 块心跳改为白/橙交替，避免黑块看不见。

```text
H103fg kernel:  79a653767ab035a9c69068a810d96e0e4bdf781e3a7200734168262ace4556df
H103fg payload: 61b46a19fe95f018b6214f16c6d19f076a7c73c23e4b8ba5b1a973d3429771f3
H103fg boot:    e456efd8a12681b40131affe0d026c09bc8506cabc76fd13745b9c1c91c33b2a
```

H103fg 第 4 块为蓝色，证明 slot 线程已进入但一直停在 `IOSleep`；日志里也
没有心跳的周期性 `NATIVE_UFS_ALIVE` 和 20 秒 `logThread` snapshot。H103fh
把 2 秒 slot 计时移到 UFS 完成路径（2×2 重绘本来就是这条路径），不再依赖
线程唤醒。

```text
H103fh kernel:  f21ee606da951562994a75c0c243990ba096c14894ec3af33ac66838b5579feb
H103fh payload: 9953381de226ca1b1ceb5fb61c2a8aa12895d63f1557b89d062c96110a8629e3
H103fh boot:    36f6938ee1de01756706c7adbdb312e8fcb5de8702994b712c56bcd2a3410dfa
```

H103fh 上机停在 UEFI：slot tick 放在 `ufsCommand` 完成路径，而 `readBlocks`
仍持有 `fLock`，`h103SlotWrite()` 再拿 `fLock` 造成自死锁。H103fi 把 tick
移到 `readBlocks` / `readWindowBulk` 的 `IOLockUnlock` 之后。

```text
H103fi kernel:  b868e9007b5b93e7a3e287c00a828bbe5640bda2ae33153a931b1b13757146a5
H103fi payload: 90e6c1fc2b9926128dcc69bb6e292d3d31e05628e9ec8d0c76a2af9fd04b4f3c
H103fi boot:    cf87300170090acb019b5cdfc5f76157724824befef3e5dd1bf985f8eabbb393
```

H103fi 上机成功：`slots_written=109`、`state=3`、`write_errors=0`，slot body
是最新的 65s–264s 日志。H103fj 做静默：`gH103Quiet=1`，payload 去掉
`-v`、`debug=0x14e`、`boot-progress=2`，保留 2×2（已与 quiet 解耦）和
UFS 读路径上的 2 秒 slot。

```text
H103fj kernel:  d0b0089b5f53f8e720ce575a3f00aa3b637b959dac5468b8d636e10277a9516e
H103fj payload: c1da6c3ffc7f9fb2b7db57f59ac927db4c0725c864fb2c240612520ac65de444
H103fj boot:    85c34f005809eada4d6eb02284cc26bd9d540d232024e26388333120158fcf35
```

H103fk 把 slot 节流从 2 秒降到 500 ms，并只在 `msgbufp->msg_bufx` 变化时
写；slot 历史约 2 分钟（247 × 0.5 s）。

```text
H103fk kernel:  90ed23031b45f195fca40f631cd7e9e7bfd45985e322fe7c88def7e8fce1d6cc
H103fk payload: 17b7894192bfaffdd91e40b21eb02b1b160a0cb51c12daeeb83dc918a508a8ed
H103fk boot:    f3c461aeb8510db31bb6d5e9c22dedb99c84e7e550dad100bdc1008cfb749626
```

H103fk 上机日志停在 `KextLog: driverkitd is active`，左上第 3 块（UFS 活动
心跳）同时停止。H103fj（2 秒 slot）在同一位置停下，说明 500 ms 不是根因。
第 3 块由每 64 条 UFS 命令的 `drawTestPattern()` 驱动，10 秒心跳线程从不
醒来，所以 I/O 空闲时它本来就会停，不能证明内核死机。

H103fl 在 UFS 驱动里加 1 Hz `IOTimerEventSource`（不改 display archive），
用 `h103-hb-timer=1` 开启；`h103-slot=0/1/2` 选择无 slot / 500 ms / 2 秒；
`logWrite` 改为 2 秒锁超时；header 的 `uptime_s`/`write_errors` 现在有值。

```text
H103fl kernel:  44cea2a033539590f0e357cba95908471122d2f278f5d0f38011c0d35a83cf44
H103fl payload: cfa7f7fb916e17f0882a5515b9cc019235282a386721727bfb0059de1565b6a7
H103fl boot:    625be88506e2b4b8a06eeec2f4ac92a08a7852815b0bdc885171bc122c4d7b4e

H103fl-noslot payload: c5985776b6c248aa2fdf27e4f1b759f8237c1a240fe36aa2c0d6002f14776465
H103fl-noslot boot:    e9fdecefef16fe6020da360b2f960e31ca38197f00380888014fa73415512d94
```

H103fl 上机：`IOTimerEventSource` 的 `start` 成功，但日志里没有一条
`fire=`；1 Hz 定时器从未触发，所以第 3 块静止不能说明内核死机。
header `uptime_s=167` 与最后一条日志 `1667.5`（UFS 起点 1500.2）一致，
slot 写入一直正常到停点，`write_errors=0`。

H103fl-noslot 的串口最后停在 `logd_helper`，但 H103fl 的 msgbuf 继续到
`driverkitd active`，两者不是同一停点；noslot 只是控制台输出先停。

H103fm 做两个 UFS-only 诊断（不改 display archive）：

- `h103-hb-timer=2`：用 `timer_call`（定时器中断上下文）而不是 workloop
  线程，尝试真正的 1 Hz 心跳；
- 消息不变时每 2 秒只重写 header，`uptime_s` 继续前进就证明停点之后
  UFS 读路径还活着；
- header 的 `export_*` 改名 `diag_*`：`diag_cmds`、`diag_last_lba`、
  `diag_last_bytes`、`diag_flags`（bit0/1=in-flight，bit8+=timer fires）、
  `diag_loops`。

```text
H103fm kernel:  8b6da448afb00e64868bcee1c0f7bd49026a72f860663e0dfaf0c77fd9b52698
H103fm payload: 8bb388f503fe8ff97ca5a27db30eff6ff5bc2877ed009f02893b58f04c47fd58
H103fm boot:    fd9c21497e22d95381ee661c7cc5a1067014d673f100de0e0839910cf46d2e92
```

H103fm 上机：`timer_call` 也没有触发（前 8 次 slot 都是 `fires=0`），
说明 kernel thread/timer 唤醒在这套 bring-up 里整体不可用；第 3 块不能
作为死机指示。日志实际继续到 `eligibilityd`（1077.4，UFS 起点 907.7），
header `uptime_s=169`、`diag_cmds=31170`，说明停点之后 UFS 读路径还活着。
`diag_flags=2` 表示最后一次 header 写入时有一个 window read 在飞，但日志
继续，不能据此判 stall。

H103fn 做 post-root `IOGPUFamily` 加载测试（不改 display archive）：
`h103-load-iogpu=1`，在 `native_boot_flags & 8`（SpringBoard exec）后从
UFS 读路径调用一次 `OSKextLoadKextWithIdentifier`，只记录返回码。

```text
H103fn kernel:  98117070e625c96591991b0c61198192a15cbe89686f08619b1ba5fb17fa8190
H103fn payload: 3edc860a3a7008efce00d4601bcdc49738ac8d47a766e020c3fdc68701d023e1
H103fn boot:    7ba4b9a64256f7ed7825d82ffedbcd0e229a16ae0218765ebd8abefd37d163ac
```

H103fn 证明 post-root 也无法用 `OSKextLoadKextWithIdentifier` 加载
`IOGPUFamily`：`rc=dc00800a`（`kOSKextReturnDisabled`），因为内核按
`NO_KEXTD` 构建，`sKernelRequestsEnabled=false`。

P1 只改 prelink roots（Mac 设备仓库 `board.toml` 加
`com.apple.iokit.IOGPUFamily`），不改 display archive。prelink report：

```text
components 29 -> 30
new component: com.apple.iokit.IOGPUFamily
IOGPUFamily __TEXT/__TEXT_EXEC/__DATA_CONST/__DATA 合计增加 223,097 B
现有组件 VA 不变
```

```text
H103fo kernel:  69885c070e883b7b1f8a156afe649b71fc6230c250277a632bf28e3cfb1f03d1
H103fo payload: 04f2fcee81bb27bdf05bd948050c83556764f5a05fec54aab51b4d8932e97077
H103fo boot:    bc34cb8e49fe81ee82f4c8ba7aefe6e25d6626f4a14a65eebf27db079171cfff
```

H103fo 上机：`NATIVE_IOGPU_LOAD: rc=0 flags=8`。prelinked `IOGPUFamily`
已注册，`OSKextLoadKextWithIdentifier` 命中已加载路径，`NO_KEXTD` 不再
阻挡。

H103fp 开始 P2：A540 类在 display probe 的同一个
`preModLoad`/`postModLoad` 窗口里注册，随后发布裸
`IOAcceleratorES`；移除了旧的 `OSKextLoadKextWithIdentifier` 调用和
`MetalPluginName`/`MetalPluginClassName`。

```text
H103fp kernel:  12d7513a347bb4315ba46f6c1f663bc6db35e4fd61c83989c567df704d2375c4
H103fp payload: 8d6bf887c1af74c9f36dddc57d377d866c8c32ce720e4bc8b4eb4dde239b94e8
H103fp boot:    24bc548352aa0dfc991cfbcad14643c87aa382bcfb1dd4647c4522007cc1b8e6
```

H103fp 上机 panic：`far=0xfffffff006aeb698`，正好是旧
`ioaccelerator-base-abi.json` 的 `metaclass+0x10`。旧 JSON 来自 candidate
`da673101...`，当前 prelink 是 `12d7513a...`；旧地址不在当前 IOGPUFamily
区间，简单加 slide 落在 `__PLK_TEXT_EXEC` 的未映射空洞，仍然 abort。

H103fq 用新脚本 `scripts/audit-target-accelerator-base-current.py` 从**当前**
prelinked kernel 静态推导 ABI：

```text
metaclass    0xfffffff00caeb688
vptr         0xfffffff00bd439a0
C2 aliases   0xfffffff00abd3b20 / 0xfffffff00abd3b40
D2           0xfffffff00abd3b64
getMetaClass 0xfffffff00abd3bac
```

重建 display archive 后 accelerator bindings 指向这些地址，再重建 kernel/prelink。

```text
H103fq kernel:  0cc2ec0454595b44bbc2f6541a345f8e9de8d567f749c1a7dab94e99c7a902fc
H103fq payload: 75ff3251ca43b457a2c7e8fde36ae7f273b6def2dbf75474dbf0057430158a4e
H103fq boot:    a7a240200c7fdc96a9ca3e369e3f1be073d61f40ff72c53bccf6e91bc41b484a
```

H103fq 在 UFS probe 阶段就 panic：`OSMetaClass::addInstance`，
`x0=0xfffffff00caeb600`，`far=0`。原因是新的 accelerator 绝对符号
`IOAcceleratorES::gMetaClass=0xfffffff00caeb688` 落在 prelinked
IOGPUFamily 数据里，作为 `.globl` 绝对符号被 prelink/符号表当成 kernel
target，早期 UFS 对象的 metaclass 指针被错误绑定。

H103fr 把 accelerator bindings 改成 `.private_extern`，让它们只参与本
次链接、不进入 kernel 导出/prelink 绑定表（`nm` 中已从 `A` 变成 `a`），
地址仍是新 ABI 的 `0xfffffff00caeb688` 等。

```text
H103fr kernel:  0922e9d182674e9bc014c79611efd170667910a3b5c32518bfe82b4873b79268
H103fr payload: 455fd32c07acc61619824fb32e9b5a769bd99624ac1111cde3b1cb53cbd22d99
H103fr boot:    ec0d60362c3cd17d5d776fbe2c5a05be60530f43dc3607f5f96eb03a81eddeb1
```

H103fs 改为 property-only stub：`NativeA540AcceleratorProbe` 直接继承
`IOService`，只设 `IOClass=IOAcceleratorES`。不再引用 `IOAcceleratorES`
基类；final kernel 的 `nm` 里已经没有 `IOAcceleratorES` 绝对符号，
accelerator bindings 对象不会被链接。

```text
H103fs kernel:  ae7b40a74143a86712d45221a76d6609a70a22c1c0ef886edaf024fb5892144c
H103fs payload: 90e6802e44c124b898f5dff3a4bdc65f700ca8da6ebf4d32bef67f8050842b6e
H103fs boot:    09c31cc705e32a2c37a5c7a2bf29735d3a7e3d8edfdd11e4a3482bcdfb7fcb39
```

H103fs 上机：A540 服务在 16.9s 发布，A540 块变绿；右上 2×2 左上角绿、
其余三块灰。日志里 SpringBoard/27 已 exec，但只有
`xpcproxy ... backboardd`，没有 `comm=backboardd` 的实际 exec；最后一行是
`KextLog: driverkitd is not active`。说明提前发布假
`IOClass=IOAcceleratorES` 服务把 driverkitd/backboardd 的交接拖住了。

H104a 保留 A540 类注册在 display probe 的 pre/post 窗口，但把服务发布移到
UFS 读路径，只在 `native_boot_flags & 4`（backboardd 实际 exec）之后调用
`native_a540_accelerator_probe()`。

```text
H104a kernel:  c5023e1e481488852a1f83ed9bd632976c146a45cbdf47a34adede6b95f082cb
H104a payload: 58d7463c2dcc0bbebc5e37a3e3aa89a4059b5da115df31adc6cfef4c5364c3a2
H104a boot:    72a06e3965a012139e50f61546f1729afbd9e49c605ae5f07764b0af34fed78b
```

H104a 上机：没有 `NATIVE_A540_PUBLISH`，也没有 `comm=backboardd` 的实际
exec；日志仍停在 `KextLog: driverkitd is not active`（199.3s），2×2 左上
仍是绿色。延迟服务发布没有解决问题，说明差异来自 **A540 类在 display
probe 的 pre/post 窗口注册**，而不是服务发布。

H104b 完全移除 A540 static init/类注册，回到 H103fo 的 display archive
行为；A540 对象仍在 archive 中但不再注册。

```text
H104b kernel:  5ec6ddd29fe941883fb3777e15d5a27040c910ae4569bdc24610f943c3fa0e40
H104b payload: 4dda07372741ebc7b65a0f6cf873c974fc9c670b8f167f4ca55eeebb68ee84dd
H104b boot:    8cc632f2f1fc12714fd7a05fae6a76face35ac73714962db5f2b19171688dcec
```

H104b 上机恢复到已知显示关口：

```text
519.691 backboardd/55 实际 exec
526.480 NATIVE_DISPLAY_OPEN service=NativeLegacyDisplay
        process=IOMFB_FDR_Loader grant=backboardd match=0
526.505 NATIVE_LEGACY: client enter pid=78 comm=IOMFB_FDR_Loader
531.238 KextLog: driverkitd is active
```

2×2：左上白（SpringBoard+backboardd）、右上黄（IOMFB_FDR client）、
左下蓝（calls<10）、右下灰（无 transfer/swap/IKit match）。A540 块黑是
预期。结论：backboardd 已起来，但先打开显示的是 IOMFB_FDR_Loader；
backboardd 仍未打开显示。

H104c 在 display client 的 externalMethod wrapper 里打印前 16 次
`selector/status`，用来定位 FDR loader 停在哪个 selector。

```text
H104c kernel:  365f6f6eb22bb2331a386d7b179a113bb57bb55bf219a917fa7475ef2206a22b
H104c payload: fc455727471d8c5be0e006029741fed79b6e59a1d792ffac91a14b7dca7f6d22
H104c boot:    e3ba65d79b7c8f62c24e8d0ef2a62c983517df979e478a550b0b5bb3e3fd80ef
```

H104c 第二次上机（16:33 日志）：

```text
189.199 NATIVE_DISPLAY_OPEN service=NativeLegacyDisplay
        process=IOMFB_FDR_Loader grant=backboardd match=0
189.226 NATIVE_LEGACY: client enter pid=78 comm=IOMFB_FDR_Loader type=0
189.251 NATIVE_LEGACY: client observed status=0
189.276 / 189.303 另外两次 IOServiceOpen
199.026 KextLog: driverkitd is not active
199.466 KextLog: driverkitd is active
```

没有 `NATIVE_LEGACY: client sel`：FDR loader 只 open 了显示服务，**没有
调用任何 externalMethod selector**。所以 selector 日志不是当前阻塞点。

2×2：左上白、右上黄、左下蓝；右下上半灰（无 transfer/swap），下半**橙**
（`native_boot_flags & 128`）：backboardd 已经开始做 IOKit matching，但
匹配的不是 IOMobile* 类。

H104d 增强 IOKit trace，打印 backboardd 前 32 个 matching dictionary 的
`IOClass` / `IOProviderClass` / `IONameMatch`。

```text
H104d kernel:  009631ae5ed1e6c72ce7d4855374a2a1f74b960c2f42d1e72921ea807559af12
H104d payload: 88de84fdf0b90bde8e6440d5568a9e6cdae673f1d173fe1fa79ff8f94861ce14
H104d boot:    9a7afb45b214a1db31e3099aba6615219a9504bf55c593a383b7e859d7a5cbbb
```

H104d 多次上机都到不了左上白：左上变绿（只有 SpringBoard）后停住，
`NATIVE_IOKIT_MATCH` 从未打印。H104d 的 kernel text/layout 改动把
backboardd 之前的停点带回来了，停止使用 H104d；恢复 H104c 的旧 IOKit
trace（只置 bit 128）。

离线检查 21H461 的 OS cryptex 字符串：

```text
Metal:    MTLRegisterDevices ... IOAcceleratorES
QuartzCore: CAEnableMetalWhippet ... IOAcceleratorES
```

确认 backboardd/CoreAnimation 匹配的是 **IOAcceleratorES**，不是
IOMobile*。之前只设 `IOClass=IOAcceleratorES` 属性的 stub 被忽略，因为它
不是真正的 `IOAcceleratorES` 实例。

H104e 不再注册本地子类，也不在 kernel matching 路径打印日志；它用
prelinked 的真实类：

```text
native_iogpu_accel_probe()
  OSMetaClass::allocClassWithName("IOGPU")
    fallback "IOAcceleratorES"
  attach(IOService::getResourceService())
  registerService()
  h103-iogpu-start=1 时才调用 start()
```

只在 backboardd 实际 exec（bit 4）后调用一次。A540 状态块：
绿=已注册，红=alloc/attach/start 失败，黑=未运行。

```text
H104e kernel:  bdb070e20c8cd55b6e4d7577d14c625464354fd06325c79316103bf96ddce50a
H104e payload: e15f7962d99069be89f35a73cf99554c8d62fc8b5025765272d90de429d4a67c
H104e boot:    108a5181ec655665fc9bead733c6674fe328ee538c7feb84db2674c215c21130
```

H104e 上机仍是左上绿，A540 黑（backboardd 未 exec，探针未运行）。检查
H104b/H104c/H104d 的日志后确认这是 **launchd 启动顺序 race**：

```text
成功:
  driverkitd 实际 exec -> IOMFB_FDR_Loader exec -> backboardd 实际 exec
  -> loader 在 backboardd 之后 open 显示

失败:
  IOMFB_FDR_Loader exec -> driverkitd 实际 exec -> backboardd 永不 exec
```

H104d-green 的 loader 比 driverkitd 早 0.26s；成功时 driverkitd 早约
1.0–1.1s。H104b 也多数停在绿，说明不是某张镜像的独立回归，而是 loader
抢跑后占住显示、backboardd 无法 exec。

先做两个只改 boot args 的 H104b 对照：

```text
boot-H104b-slot0.img    h103-slot=0        关闭持久化日志/slot 写入
boot-H104b-noiogpu.img  h103-load-iogpu=0  不加载 IOGPUFamily
```

```text
H104b-slot0 boot:   1aa9829522cf7f79713dc8ce577c084f83c564485c5f41aa35025ee2556e7fc0
H104b-noiogpu boot: 90b8d14229944b5308d0995b5371472a4abaab2fc233b39e6685ab141f455aae
```

H104b-slot0 上机：**能稳定左上白**。确认早期 slot/UFS 写入让 driverkitd
输掉 launchd race；noiogpu 不必再测。

H104f 把 slot/header 写入推迟到 `native_boot_flags & 4`（backboardd 实际
exec）之后；IOGPUFamily load 和真实 accelerator 探针保持原样。消息缓冲区
在等待期间继续累积，第一次 post-backboardd 写入仍会把早期启动日志落盘。
心跳线程/2×2 不受影响。

```text
H104f kernel:  fbbc84592a7b099e27c68627917433d471da53a5c74acb86d04f451245209fde
H104f payload: bf806aa279e88b3ed0dcc872826218515be8f71cae39caa5b0bc449da5accd48
H104f boot:    7a76334f2ff0d43d7f6ee087d7132c88926716d87f1b151ce081a67c9f543232
```

H104f 上机：左上稳定白，slot gate 成功；backboardd 之后日志恢复。关键行：

```text
204.481 backboardd 实际 exec
204.521 NATIVE_IOGPU_ACCEL: class=IOAcceleratorES object=0 service=0
204.543 NATIVE_IOGPU_PUBLISH: rc=0 flags=f
```

`IOGPU` 和 fallback `IOAcceleratorES` 的 `alloc` 都返回 0，说明它们是抽象
类或不可分配。H104g 逐个探测 `IOGPU`、`IOGPUDevice`、`AGXAccelerator`、
`IOAcceleratorES`、`IOGPUFamily`，分别打印 metaclass/obj/service，寻找
真正可分配的 concrete 类。

```text
H104g kernel:  26956e603bb39ad001738b3b79a373409737bb00367d90ef8f30ba8c2306bb90
H104g payload: 4946a49a82ba03ed53f554b09cd62004f3ecd89cc00e0356dc1bfcffcb4ebf6f
H104g boot:    c2574fe9f71574839cd24a630eb725573b22067e12f093efab73f557f2ef459a
```

判读：看 `NATIVE_IOGPU_ACCEL: name=... mc=... obj=... service=...`。
- 某个 `obj/service` 非 0：真实 concrete 类可分配，attach/register 后看
  A540 绿和 CA 反应。
- 全部为 0：IOGPUFamily 在当前 prelink/设备上没有可分配 concrete 类，
  下一步要么做安全的本地 `IOAcceleratorES` 子类，要么提供 provider。

判读：左上应稳定白；backboardd 之后持久化日志开始写入，应包含
`NATIVE_IOGPU_ACCEL` 和 A540 状态。若第一次大块 dump 影响 loader 打开
显示，下一步把 dump 拆成小块/限速。

判读：如果 `slot0` 能稳定白，说明 slot 写入/UFS 争用让 driverkitd 输掉
race；如果 `noiogpu` 能稳定白，说明 IOGPUFamily load 影响时序。两者都无效
则做 H104f：在 `newUserClient` 里把 FDR loader 的 open 延迟到 backboardd
实际 exec 之后。

判读：先看 H104e 是否恢复到左上白（backboardd 实际 exec）。然后看
`NATIVE_IOGPU_ACCEL: class=... object=... service=...` 和 A540 块：
- alloc 返回 0：真实类不可用/抽象；
- attach/register 成功：真实服务已发布，观察 CA 是否接受；
- 如果仍然只有橙/灰、显示器不打开，下一步补真实 accelerator 属性/ABI，
  或尝试其它真实类。

判读：看 `NATIVE_IOKIT_MATCH: class=... provider=... name=...`。如果
class 是 `IOGPU`/`IOAcceleratorES`，下一步提供对应服务；如果是
`IOMobileFramebuffer`/`IOFramebuffer`，调整匹配/属性；如果全是 `-`，
下一版打印完整 matching dictionary XML。

判读：看 `NATIVE_LEGACY: client sel pid=78 selector=... status=...`。
若某个 selector 返回非 0 或之后没有新 selector，下一步实现/补该 selector
或检查 FDR loader 等待的 kext/数据。若 selector 正常但 backboardd 仍不
打开显示，转向 backboardd/CoreAnimation 的 provider 等待。

判读：应恢复到 `driverkitd is active`、`comm=backboardd` 实际 exec、2×2
左上变白；A540 块会变黑（没有 probe）。若仍停 `driverkitd not active`，
说明 A540 类注册也不是原因，需要查 H103fo→H103fs 之间的其他差异。

判读：应看到 `NATIVE_EXEC ... comm=backboardd`、2×2 左上角变白，然后
`NATIVE_A540_PUBLISH: rc=... flags=4`；再看 client 块是否变化。若仍停在
`driverkitd is not active`，说明提前发布不是唯一原因，需要查 display
archive/IOKit 匹配。

判读：先确认不再在 UFS L5 后 panic；若出现
`NATIVE_A540_ACCEL: published IOAcceleratorES (probe only)`，看 A540 块和
2×2 client 是否变化。property stub 可能被 CA 忽略；若被忽略，下一步只能做
运行时真实基类解析。

H103fr 仍然在 UFS probe 的 L5 后 panic，`OSMetaClass::addInstance`，
`x0=0xfffffff00caeb680`、`x21=0xfffffff00caeb600`。`.private_extern`
没有解决问题，说明不是导出符号/prelink 绑定表的问题；A540 的绝对
`IOAcceleratorES` 基类绑定本身在早期 IOGPUFamily 类元数据/实例登记里
造成了破坏。

结论：本地 `IOAcceleratorES` 子类 + 绝对基类地址的方案在当前 prelink
下不可行。下一步只能改成不绑定基类的 property-only `IOService` stub，
或在运行时解析真实基类；H103fo 仍是可用回退。

判读：应出现 `NATIVE_A540_ACCEL: published IOAcceleratorES (probe only)`，
A540 块变绿；若仍 data abort，核对 `far` 是否等于 `0xcaeb698`。

判读：日志应出现 `NATIVE_A540_ACCEL: published IOAcceleratorES (probe only)`，
A540 块变绿；然后看 backboardd/2×2 client 是否变化。若停在
`Waiting on IOMedia UUID 04769D26`，是 display archive 改动触发 race，回退
H103fo。

判读：若 `IOMobileGraphicsFamily` 的先例成立，prelinked 的
`IOGPUFamily` 应让 `NATIVE_IOGPU_LOAD: rc=0`；这验证 prelink 注册，不改
display archive。若 rc 仍非 0，先查 prelink 注册，不要动 A540。

设备仓库已整理提交：`2400c54`（config）、`19d46d6`（ufs）、
`6fef5db`（power）、`9ad41b0`（adt）、`2bf3c61`（patch tool）、
`097d00c`（lock + bring-up inputs）。origin/main 原本已在 `5ad54e2`
跟踪 11 个 DinnerABC 文件、`SystemVolume.im4p` 和 `cpu-tables.h`；本地
H103 线已 merge origin/main（`1ca9258`），这些文件不再被忽略。设备仓库
`main` 已 push 到 GitHub（`c1e91be..1ca9258`）；`GopFramebuffer` 的
`53f5d20` 也已 push 到分支 `h103-a540`，供 H103fp 的 submodule 指针使用。

### 关键产物

```text
H103au boot      2a263cce4b0ed4fdb7c35ef0e0ae35d39d36ebeef9143b9061865adac1f56288
H103ax-1g boot   73cf3927771e3191f38ae8cf89daaf10a883e3d35ecdd9ef4780699c0297f84e
H103ay-4g boot   f148919d96db01302a53d72d6061445a89cfe652a9ca33fdc14f61a1ba41cd3e
H103az boot      3bb5b2bebe792d0b45ba9823d1e7bbc7adb78ca7adb040d47f63723e327990c5
H103ba boot      3195072d616d9be0411d73903c6468b9d6aac40423c3d92c342ad807cd8190f2
H103bb boot      d9b91fc6a29ecb90822da84c31923539b5160b57927ccd1cc60cd24e0b1b2130
H103bc boot      e5349527c74df987e5e21bcd670febe7be8eac41260a9556e719c34a458a42c6
H103bd boot      8284f2616412cea7e98cdb9d929802b4b2cdd6bb7b8af536210a38c08c9910f3
H103be boot      6ce2a1e2584055903edcb383432da9cc6eb478e7f44b21ec3e5059db7a4694ae
H103bf boot      768f8eb8c01995df59d6cd8eb24d7080d4dd29d0e41400bd2857193f6262aced
H103bg boot      ae9c39268b5cd4e49ee1dcf4cdbb0e933b673ea63c4016995ab23fd50677e0a6
H103ea boot      a07fe2567b43edcf7cbd58a86a8d4474cae49ac84e79b667e074205e1b4e2f5e
```

## 更新规则

每次工作结束追加一行日志，并更新“当前阶段/下一步/待实测/阻塞”。构建产物哈希、日志路径和失败现象记录在日志里；私有文件只放 `.cache/firmware/`，不提交。
