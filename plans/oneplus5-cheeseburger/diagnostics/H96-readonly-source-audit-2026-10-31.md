# OnePlus 5 / UniXNU H96 只读源码审计

- 日期：2026-10-31
- 范围：只读源码、只读镜像/日志；未修改完整性验证逻辑，未跳过新的 launchd job，未刷写设备。
- 目标：解释 H96a18/H96a21/H96a22/H96a23 的停点，核对 `winBytes=315523184` 的语义，区分 SHNGROOT、owner boot manifest、Image4/IM4M、TrustCache 和 VerifiedRoot。
- 结论状态：**根因仍未由源码确认**；本文把所有未满足“源码明确证明”的项标成“待确认”。

## 0. 结论摘要

1. **H96a21 已排除当前 AMFI/TrustCache 命中问题。** 四次 driverkitd 检查均为 `mask=0x1`（静态 miss、loadable hit），说明这四个目标在运行时 loadable TrustCache 中；不能再用“TrustCache 缺失”解释停点。
2. **H96a18 仍是唯一到达 cryptexd/eligibilityd 的路径。** H96a19 跳过 eligibilityd、H96a22 同时跳过 eligibilityd+cryptexd、H96a23 只跳 cryptexd，三者都更早停在 `driverkitd is active`。因此这些 skip 是回归，不是修复。
3. **H96 实际使用的是 bring-up 授权，不是真实 owner 信任链。** H96 构建把 `XNU_OWNER_BOOT_MANIFEST` 设为 0、`XNU_OWNER_ROOT_TRUST`/`XNU_OWNER_ROOT_AUDIT` 设为 0，并依赖 NativeUfsProbe 的 `native_verified_block_root_authorized()` 桩（只检查 `device != NODEV`）。H91 ADT 的 48 字节 `boot-manifest-hash` 仍是全零占位，H96 也没有发布真实 hash。
4. **`winBytes` 不是磁盘进度，也不是 SystemOS 的绝对偏移。** 它是 NativeUfsProbe 所有 window 共享的累计“上层请求字节数”，在 `doAsyncReadWrite()` 入口按 `count * 512` 增加；不是实际 UFS 读字节，不区分 OS/SystemOS，也不是“最后读到哪里”。H96a23 的 `315523184` 约为 300.9 MiB；H96a22 的 `3222299648` 约为 3.001 GiB。
5. **H96a23 的最后一次 UFS 命令成功位置是 `lba=0x1b1bca`、`bytes=0x20000`（128 KiB 读前量）**；最后一次有窗口标签的 SystemOS 读是 H96a22 的 `block=0x13fcf0`、`done=4096`。没有 `NATIVE_UFS_TIMEOUT`/`NATIVE_UFS_STALL` 证据，因此停点更像存储之上的等待，而不是 UFS 硬超时。
6. **48 字节 boot-manifest-hash 的生产者目前是 ADT 生成器**（`bytes(48)` 全零），不是内核；内核的真实 SHA-384 发布函数在 H96 中被编译掉，而且它会拒绝覆盖已有属性。消费者是用户态 `/sbin/mount`（`IODeviceTree:/chosen` + `boot-manifest-hash`）；其值是否被密码学比较，源码不可得，**待确认**。
7. **IM4M/AuthAPFS 不能按“源码已证明缺失”处理。** 项目没有 cryptexd/AuthAPFS 源码；只有 cryptexd 二进制字符串（`AuthAPFS disabled because quire lacks an im4m`）和 H92/H93 的运行证据，所以标为“待确认（强二进制证据）”。

## 1. 审计方法与只读边界

- 读取了 `plans/oneplus5-cheeseburger/PLAN.md`、`STATUS.md`、`BASELINE.md`、`AGENTS.md`（本仓库唯一 `AGENTS.md` 位于 `drivers/NativeRamBlock/`，是组件级说明；根目录没有 AGENTS.md）。
- 读取了本地 XNU 补丁、NativeUfsProbe/UFS、VerifiedRoot、构建脚本和用户态检查脚本。
- 远程只读检查了构建主机 `/Users/lss/UniXNU/out/cheeseburger/tree/.cache/xnu-work` 的实际源码和 `/Users/lss/h96` 的 H96 产物；没有在远程修改源码或镜像。
- 未执行 `fastboot boot`、`fastboot flash`、`git reset`、`git clean`；未删除任何 H91/H96 产物或 `*.jpg` 设备照片。

## 2. 当前源码/仓库状态

### 2.1 本地主导航仓

| 项目 | 值 |
| --- | --- |
| `git rev-parse HEAD` | `59fecce2b2523989a0b55c1772dec65dd24d3960` |
| HEAD 说明 | `diag: record H96a23 cryptexd-only skip regression and close skip iteration` |
| 审计开始时工作区 | 31 个未跟踪 `196.jpg` … `219.jpg`；无已跟踪源码修改 |
| 设备仓 `UniXNU-cheeseburger` | `5ad54e21a067ee747d67009122c87510dfdd3803`，工作区干净 |

本地主仓关键子模块指针：

```text
1951cc84 drivers/ArmCpu
0d544705 drivers/ArmDTPlatform
532de8c0 drivers/ArmVirtPlatform
d099354d drivers/DarwinBoot
a34964b8 drivers/NativeAES
5cf11357 drivers/NativeAdreno
b5399b56 drivers/NativeGeniSpi
588c96fe drivers/NativeHidInput
e59baca4 drivers/NativeKeyStoreEvents
ff0c70d7 drivers/NativeMetal
c031d882 drivers/NativeMobileDisplay
f3cffc99 drivers/NativeRamBlock
43a4bc71 drivers/NativeUfs
98909eb9 drivers/NativeVirtioBlock
066f9bde drivers/NativeVirtioConsole
2b6ffb50 kits/NativePrelink
2854c61f kits/VerifiedRoot
```

### 2.2 实际 H96 构建树（远程）

| 项目 | 值 |
| --- | --- |
| 构建树 HEAD | `9f3daad0c350c21fda3269c8da7e7d2780fc6db8` |
| XNU 基础提交 | `d8b80295118ef25ac3a784134bcf95cd8e88109f`（`xnu-10063.141.1`） |
| XNU 工作区 | 94 个修改/未跟踪文件（含本项目补丁和 H96 trace） |
| 构建树状态 | `drivers/NativeUfs` 子模块脏、`NativeUfsProbe.cpp` 脏、`VerifiedRoot` 脏、`scripts/seed-data-grow-1g.py` 未跟踪 |

这次审计确认了三个与 H96 停点直接相关的未提交差异：

1. `NativeUfsProbe.cpp` 的 OCS timeout 从“打印错误并返回 false”改为 `panic("NATIVE_UFS_TIMEOUT: ...")`。远程行号 `drivers/NativeUfsProbe/NativeUfsProbe.cpp:723-735`。因此 H96a23 没有出现该 panic，说明至少没有触发这条 UFS 命令超时路径。
2. `NativeUfs.cpp` 把 VerifiedRoot envelope 的每块摘要从固定 `* 32` 改成 `* VR_HASH_BYTES`；`VerifiedRoot` 允许 `VR_HASH_BYTES` 为 8/16/32 的 bring-up 截断。这是 VerifiedRoot 的临时缩减，不是 H96 实际 owner 认证成功的证据。
3. `security/mac_base.c` 含 H96a20 的完整输入/输出 trace 和 H96a21 的 `native_amfi_tc_diag()`；这些修改只在远程构建树，不在本地 git 的 HEAD 中，因此 **H96a20/H96a21 不能只靠本地仓库复现**。

### 2.3 H91/H96 产物哈希

H91（设备仓保留，未覆盖）：

| 文件 | 大小 | SHA-256 |
| --- | ---: | --- |
| `out/cheeseburger/boot/boot-H91-stallpanic.img` | 81,147,904 | `cd81c5b8ec91f103f3e3c0fd7de7bfb26633f4fbabecd34490e5627dd6f3ff9b` |
| `payload-H91-stallpanic.img` | 79,691,776 | `e7fa5cdff58217886ebf269b30683c78da83c409ae179c356650f69869f08942` |
| `Payloads/kernel.macho` | 52,088,832 | `7f02891c32825b75fcb76f2969ea2c1a541662d4c369d29fc85732b19f0e532d` |
| `Payloads/kernel.macho.adt` | 14,604 | `83d733d770a9ec6961f60c1af13f4eeeb8552c776098241c0c536d13f810e54a` |
| `Payloads/kernel.macho.trustcache` | 61,356 | `7416ca4f8377146626bb284e2133c76bee6cb4e3ee0af5d648b186235a7323a7` |

H96 关键镜像：

| 名称 | SHA-256 |
| --- | --- |
| H96a18 payload / boot | `c58640e146c4f7d2a97ceaa511bc88e2382256a44f3bd03a0469fe6b60bb74c7` / `301d8edd7f2c13b144f8c980fa4364ab1ed89141bac2e52325ea65826ad805b6` |
| H96a18 kernel（payload 内） | `ede3f2b803113ec9921a3faae052bced060ee32f50e7656436889e2d6355ef3a` |
| H96a19 payload / boot | `995d247ce5dfe436bb25211cdf102f6b7256530072af8b042c8be9577694021a` / `0bd34abf97c50979c0f24e58c1f1c1b2c816b74429b6e12d4e3940737ad2b7e9` |
| H96a20 kernel gatebypass | `e7538cb9e022fcc5bad5241fb9e7610cd352dec0214c6fe7cdfc07afa398d90a` |
| H96a20 payload / boot | `bef11b854700493c89dd51792700d00f0b46ce9c802fe044c415baae880c8792` / `9a409302774277cf97c0209694104c7f08c2d16ed1df3193cb617f93ced59b88` |
| H96a21 kernel hook | `b9cd4c9b44a0d5c977b50d1de8f861483797d4a362062e025d3fd8bce47b95d3` |
| H96a21 payload / boot | `bc3ea90e8eb733e1658a4f5c6e170036b264550a65ca12e8b43a605ef70819f9` / `f7e9a2f051f6cc446d2a775d799b489bd2297f92ef043b516f40d7b9ee88eb67` |
| H96a22 payload / boot | `7e2c37c9c50203e7fa92a20b95a65c1bab4fbea2aa2415c2a4ee239323317801` / `1b76aac07fc3e68830a91069e18d98008b177c745345bab289e51acc16f728f9` |
| H96a23 payload / boot | `23b29bdaa78e9069a5a78c6d446472b8a68abe8f56796bcceefd79258f0af66b` / `4d23038ecc56823c391b04d31f913d7afb566cf34bcba2cc21df5ebbd451ce22` |

## 3. H96 的实际构建配方

远程 `/Users/lss/run-h96a-build.sh`（不在 git 中）设置了决定本次行为的关键变量：

```text
XNU_TRUST_CACHE_RUNTIME=0
XNU_OWNER_CACHE_MOUNT=1
XNU_OWNER_CACHE_STARTUP=1
XNU_OWNER_CACHE_CARRIER=1
XNU_OWNER_UNION_SYSTEMOS=1
XNU_BRINGUP_CACHE_MOUNT=1
XNU_OWNER_DATA_MOUNT=1
XNU_OWNER_BOOT_MANIFEST=0
XNU_OWNER_DATA_VOLATILE=1
XNU_OWNER_CRYPTEX_FILES=0
XNU_NATIVE_UFS=0
XNU_NATIVE_UFS_PROBE=1
XNU_OWNER_ROOT_AUDIT=0
XNU_VERIFIED_ROOT_PROBE=0
XNU_OWNER_ROOT_TRUST=0
XNU_NATIVE_VIRTIO_BLOCK=0
XNU_NATIVE_MOBILE_DISPLAY=0
XNU_NATIVE_AES=0
XNU_NATIVE_ADRENO=0
XNU_SHENG_PMGR=1
```

源码中对应的允许/拒绝条件：

- `XNU_OWNER_CACHE_MOUNT=1` 只要求 `XNU_OWNER_ROOT_TRUST=1` **或** `XNU_BRINGUP_CACHE_MOUNT=1`；H96 正是后者（`scripts/build-xnu-4k-baseline.sh:372-381`）。
- `XNU_OWNER_BOOT_MANIFEST=1` 才要求 `XNU_OWNER_CACHE_STARTUP=1`；H96 设 0，故真实 owner manifest 发布被编译掉（同文件 `:391-400`）。
- `XNU_OWNER_ROOT_TRUST=1` 需要 `XNU_OWNER_ROOT_AUDIT=1` 和 `XNU_NATIVE_VIRTIO_BLOCK=1`；H96 三者均为 0/0/0。
- H96 的 `NativeUfsProbe` 被强制链接；`NativeUfs` 完整驱动不链接（同文件 `:624-635`）。

## 4. launchd → SystemOS 的真实调用链

### 4.1 UFS/SHNGROOT 发布

`drivers/NativeUfsProbe/NativeUfsProbe.cpp`：

- `:1564-1600`：读取 userdata 首块（`0x14f40`），检查 `SHNGROOT` magic、version、windows，再读取每个 entry 的 `name/offset/size`。
- `:1626-1643`：`absolute = 0x14f40 * 4096 + rootOffset[index]`，为每个窗口创建 `NativeUfsWindow` 并 `registerService()`。
- `:1657-1660`：启动 `heartbeatThread()`。

关键缺口（源码明确）：

- `:1573-1595` 只验证 magic/version/windows 和字段边界，**没有校验** header 的 SHA-256（offset 4064）或窗口 entry 的 `sha256[32]`。
- 同文件 `:578-585` 的 `native_verified_block_root_authorized()` 实现是：
  ```c
  return device != NODEV;
  ```
  注释自己说明这是 “Temporary B5 bring-up authority ... Replace with VerifiedRoot before any final configuration”。
- 完整的 `drivers/NativeUfs/RootLayout.h:157-209` 才会验证 header SHA-256、窗口 SHA-256、重叠/边界；但 H96 链接的是 Probe，不是完整 `NativeUfs`。

因此，H96 的 OS/SystemOS 窗口来源是 **SHNGROOT 容器 + 设备存在性/窗口边界**，不是 VerifiedRoot 的签名链。

### 4.2 root 挂载与 root auth

`bsd/kern/bsd_init.c`（远程 `.cache/xnu-work`）：

- `:1008-1056`：`vfs_mountroot()` 挂载 OS root，`VFS_ROOT()` 取得 rootvnode。
- `:1072-1108`：非 macOS 平台要求 `FSIOC_KERNEL_ROOTAUTH`；若失败，在 `XNU_OWNER_ROOT_AUDIT` 分支尝试 owner 认证，但 H96 的 audit/trust 均为 0，实际走 `native-skip-root-auth=1` 的打印/放行路径：
  ```c
  if (autherr && !owner_authorized) {
      PE_parse_boot_argn("native-skip-root-auth", ...);
      if (native_skip_root_auth) {
          printf("NATIVE_ROOT_AUTH_BYPASS: autherr=%d\n", autherr);
      } else {
          panic("rootvp not authenticated after mounting");
      }
  }
  ```
- H96 的 H91 参数包含 `native-skip-root-auth=1`，因此 root auth 失败不会 panic；**不能从“没有 root auth panic”推断 root 已被真实认证**。

### 4.3 launchd 之前的 SystemOS 挂载

`bsd/kern/kern_exec.c`：

- `:7010-7031` 说明 `load_init_program_at_path()` 是 init/launchd 装载入口。
- `:7066-7201` 是 `native_owner_cache_prepare()`：
  - `:7068` 固定 SystemOS UUID `8846FE78-2910-4297-AF97-D59BB6F8FC16`。
  - `:7073` `XNU_OWNER_UNION_SYSTEMOS=1` 时 mountpoint 为 `/`，`request.flags |= MNT_UNION`。
  - `:7117-7119` 检查 root 只读、设备非 NODEV，并调用 `native_verified_block_root_authorized()`；在 H96 Probe 中该函数恒为真（只要 NODEV 以外的设备）。
  - `:7121-7125` 按 UUID 调 `native_verified_block_volume_device()` 选择 SystemOS 设备。
  - `:7136-7139` 调 `mount(p, &request, &result)`，只读 APFS；失败返回错误。
  - `:7140-7160` 查 `/`，验证实际设备等于 selected、只读、`f_fstypename == "apfs"`、`native_verified_block_root_authorized(actual)` 和 native union。该授权同样落在 Probe 桩上。
  - `:7183-7201` 再做 root recheck；`:7186-7190` 只有 `XNU_OWNER_BOOT_MANIFEST` 打开时才发布 owner hash；H96 关闭，故直接跳过。`:7191-7199` 设置 dyld flags bit 3。
- `:8267-8298` 是 `load_init_program()`：
  - `:8280` 调 `native_owner_cache_prepare()`；失败会 `panic("offline owner SystemOS mount failed: %d")`。
  - `:8286` 调 `native_owner_data_prepare()`；失败会 panic。
  - `:8292-8294` 执行 `sheng_boot_watchdog_policy()`；参数非法会 panic（H96a17 的 `_` 白名单问题）。
  - `:8324-8348` 依次尝试 `/sbin/launchd`/development launchd 并 `execve`。
- 由于 H96a18/H96a22/H96a23 都没有上述 panic，说明这些内核准备路径在本次启动中返回了成功；但 root/SystemOS 的“授权”是 Probe 桩，不是真实 VerifiedRoot 认证。

### 4.4 launchd、mount-phase-1 与 job

- `/sbin/mount`（远程 `~/h96/osmnt/sbin/mount`）的字符串包含：
  ```text
  IODeviceTree:/chosen
  boot-manifest-hash
  failed to get boot-manifest-hash
  failed to get boot manifest hash - %s
  ```
  证明它是 `/chosen/boot-manifest-hash` 的消费者。
- `scripts/check-mount-phase-checkpoints.py:16-29` 记录：PID 1 `launchd` spawn `/sbin/mount -P 1` 为 PID 3；缺少 `boot-manifest-hash` 时 `mount` exit 66；H91/H96 的 ADT 有该属性，所以能过这一存在性检查。
- H96a18 的屏幕最后可见 `NATIVE_EXEC ... com.apple.security.cryptexd`、`cryptexd`、`com.apple.eligibilityd`；之后 5 分钟没有新 exec/错误。
- H96a19 屏幕实际停在 `KextLog: driverkitd is active`，没有 cryptexd；H96a22/H96a23 同样更早停在 `driverkitd is active`。这说明“跳过 eligibilityd/cryptexd”会让 launchd 的更后续服务无法继续，而不是解决 cryptexd 的等待。

### 4.5 TrustCache / AMFI

- `osfmk/kern/startup.c:758-766` 无条件调用 `trust_cache_runtime_init()` 和 `load_static_trust_cache()`（`native-trust-cache-runtime.patch` 把 BCM2837 条件改为 `#if 1`）。
- `bsd/kern/kern_trustcache.c:455-495`：若 `img4if == NULL || amfi == NULL` 直接 panic；H91/H96 未出现此 panic，说明两者存在，`trust_cache_ready` 被置 1。
- `:617-655` 的 `xnu_query_trust_cache()` 调用 AMFI 的 `TrustCache.query`；`:969-998` 的 `query_trust_cache()` 是 XNU 入口。
- H96a21 在 AMFI 成功路径读到四次 driverkitd 检查均为 `mask=0x1`；按既有 AMFI 位语义为 type 1（静态）miss、type 2（loadable）hit。H96a21 的完整 cdhash 为：
  ```text
  1fc191a7fbb1e532b54ca15befb83d7efe9ab3e1
  0d6972a904236c9c4bea8297c2d8bdacbeb1167a
  d4c8a0514d5952c89dc24039d9efd4a26e0c87ef
  d599af53ba5c1d66a0a0c9135c5756f9d3713284
  ```
  这证明这些目标在 loadable 侧命中；不证明具体是哪一个 cache module（静态/System/Cryptex1）加载，也不证明 `Cryptex1,SystemTrustCache` 的完整内容。

## 5. `winBytes=315523184` 的准确语义

### 5.1 源码

`drivers/NativeUfsProbe/NativeUfsProbe.cpp`（本地行号；远程仅 timeout 段有差异）：

- `:40-47`：`gWindowRequests/gWindowBytes/gWindowInFlight/gWindowLastBlock/...` 都是 **static global**，不是每个窗口的字段。
- `:958-982`：`NativeUfsWindow::doAsyncReadWrite()` 每个请求：
  ```c
  uint64_t request = gWindowRequests;
  gWindowRequests = request + 1;
  gWindowBytes = gWindowBytes + count * 512;
  gWindowLastBlock = block;
  gWindowLastCount = count;
  gWindowLastReqBlock = block;
  gWindowLastReqCount = count;
  gWindowStartTicks = mach_absolute_time();
  gWindowInFlight = 1;
  ```
  这里 `count * 512` 是 **上层请求的字节数**，不是实际 UFS 传输量；读前量（read-ahead）会额外读取最多 128 KiB。
- `:1002-1047`：实际 UFS 读取按 `page` 做 128 KiB read-ahead；一次上层 4 KiB 请求可以触发一次 128 KiB UFS 命令。
- `:1069-1085`：完成日志打印 `done`（本请求实际复制的字节数）；`gWindowInFlight` 在返回前清零。
- `:749-764`：`NATIVE_UFS_PROGRESS` 每 64 个 UFS 命令打印一次；`winReq/winBytes/winFlight/lastWin/req` 都来自上述全局计数。
- `:49-80`：heartbeat 每 10 秒检查；若 `gWindowInFlight` 持续且 `gWindowStartTicks` 超过 10 秒，panic `NATIVE_UFS_STALL`。
- 远程 `:723-735`：OCS timeout 改为 panic `NATIVE_UFS_TIMEOUT`。

### 5.2 数值判读

| 观测 | 值 | 含义 |
| --- | --- | --- |
| H96a23 `winBytes` | `315523184` = 300.9 MiB | 所有窗口累计的上层请求字节；不是 3.0 GiB，也不是偏移 |
| H96a23 `winReq` | `37482` | 累计上层 window 请求数 |
| H96a23 `winFlight` | `1` | 打印该 progress 时有一个 window 请求在处理；不代表永久卡住 |
| H96a23 `lastWin` / `req` | `0x93450 / 8` | 最近一次 window 请求的 512-byte block 和 count（8×512=4 KiB）；**没有窗口标签** |
| H96a23 `last` | `0x1b1bca / 0x20000` | 最近一次成功 UFS 命令的 4K LBA 和字节数（128 KiB read-ahead） |
| H96a22 `winBytes` | `3222299648` = 3.001 GiB | 另一次运行累计请求量；不是绝对读位置 |
| H96a22 最后有标签的 SystemOS 窗口 | `req=52224 block=0x13fcf0`，`done=4096 ok=1` | 最后一次可见的 SystemOS 4 KiB 完成读；block 为窗口内 512-byte 单位 |

H96a23 的最后一次 UFS 命令成功位置是 **LUN0 绝对 4K LBA `0x1b1bca`（字节 `0x1b1bca000`，约 6.94 GiB）**，读取 `0x20000` 字节。它是否落在 SystemOS 窗口内，需要 SHNGROOT 的 SystemOS offset；该 offset 在本轮日志中没有出现（`NativeUfsProbe.cpp:1593` 会打印 `window[...] off=...`，但本屏未捕获）。因此不能把 `0x1b1bca` 说成“SystemOS 内偏移”。

H96a22 的最后一条有标签 SystemOS 读是 `block=0x13fcf0`，以 512 字节单位计算为 `0x27f9e000`（639.6 MiB）的窗口内偏移，`done=4096` 证明 4 KiB 完成。照片中该行的 `count` 看起来是 `0`，但 `doAsyncReadWrite()` 在 `count == 0` 时会返回 `kIOReturnBadArgument`，所以这里不能把照片的 `count=0` 当作真实值；`done=4096` 才是可靠完成证据。

**没有 UFS 硬超时证据**：

- H96a23 没有 `NATIVE_UFS_TIMEOUT`（远程 panic 版本）。
- 没有 `NATIVE_UFS_STALL`（heartbeat 版本）。
- 因而“2 分钟无进展”不是 UFS 请求超时；更可能是上层 launchd/cryptexd/驱动的无日志等待。`winFlight=1` 是 progress 打印时序，不是永久 in-flight 的充分证据。

## 6. 材料区分

| 材料 | 生产者 | 消费者/用途 | 当前状态 |
| --- | --- | --- | --- |
| **SHNGROOT** | `scripts/unixnu/userdata.py:10-12,104-110` | `NativeUfsProbe.cpp:1573-1595` 读取并发布 OS/SystemOS 窗口 | 存在；H96 Probe 只验证 magic/边界，不验证 header/window SHA-256 |
| **owner boot manifest** | `make-cheeseburger-adt.py:146-148` 生成 48 字节全零；内核 `IOKitBSDInit.cpp:581-629` 条件性生成 SHA-384 | `/sbin/mount` 读取 `/chosen/boot-manifest-hash`；`IOGetBootManifestHash` 读回 | H96 的 ADT 仍是全零；`XNU_OWNER_BOOT_MANIFEST=0`，内核不生成 |
| **Image4 / IM4M** | Apple personalization / Image4，仓库无生成器 | cryptexd/AuthAPFS 二进制字符串要求 im4m | 229 字节 `system-volume-auth-blob` 只是 IM4P root hash；`personalized_im4m_present=False`；**待确认** |
| **TrustCache** | DarwinBoot 侧车 `kernel.macho.trustcache`；`Cryptex1,SystemTrustCache` 原模块 | `load_static_trust_cache()`/AMFI 查询 | H96a21 四次 driverkitd 检查均 loadable hit；具体加载 module 待确认 |
| **VerifiedRoot** | `kits/VerifiedRoot` 自定义签名 manifest | `NativeVirtioBlock` 的 `native_verified_block_*` | H96 使用 Probe 桩；VerifiedRoot 未启用（`VERIFIED_ROOT_PROBE=0`、`NATIVE_VIRTIO_BLOCK=0`） |

### 6.1 48-byte `boot-manifest-hash` 的生产者/消费者/验证

**生产者（H91/H96 实际）**：

- `UniXNU-cheeseburger/scripts/make-cheeseburger-adt.py:146-148`：
  ```python
  # Bring-up placeholder for the owner boot manifest hash; mount-phase-1
  # requires the property to exist. Replace with the real owner manifest later.
  ('boot-manifest-hash', bytes(48)),
  ```
- 直接解析 H91 `kernel.macho.adt` 可见属性名在文件偏移 `0x228`，长度 `0x30`（48），后续 48 字节全零。

**生产者（条件性、H96 未启用）**：

- `IOKitBSDInit.cpp:581-629` 的 `native_owner_publish_boot_manifest()`：
  - `:585-601` 用 domain `VerifiedRoot.OwnerBootSet.v1`、role 1（root）、role 2（cache）和两个 48 字节 VerifiedRoot manifest digest 计算 SHA-384；
  - `:605-608` 若 `/chosen` 已有 `boot-manifest-hash` 或 `owner-boot-manifest-domain`，返回 status 3，**不覆盖**；
  - `:613-617` 写入并用 `IOGetBootManifestHash()` 读回比较。
- H96 的 `XNU_OWNER_BOOT_MANIFEST=0` 使 `kern_exec.c:7186-7190` 的调用被编译掉；同时 Probe 不提供 `native_verified_block_manifest_sha384()`。因此 H96 的 48 字节 hash **不是**这个内核函数生产的。

**消费者/验证**：

- `IOGetBootManifestHash()`（`IOKitBSDInit.cpp:539-567`）从 `/chosen` 取属性，允许长度 ≤48，复制并零填充；**没有检查内容是否为非零或是否为某个签名 digest**。
- `/sbin/mount` 二进制字符串包含 `IODeviceTree:/chosen`、`boot-manifest-hash`、`failed to get boot-manifest-hash`；`scripts/check-mount-phase-checkpoints.py:16-29` 记录缺少属性时 `mount -P 1` exit 66。
- H91/H96 的全零占位能通过 H91 的 mount-phase-1（历史记录）；这说明观察到的 gate 至少是**属性存在性**，不是密码学相等检查。`/sbin/mount` 的完整源码不可得，是否存在更深的 hash 比较**待确认**。

### 6.2 Image4/IM4M 与 TrustCache 的边界

- `scripts/audit-target-root-hashes.py:33-60` 解析 `SystemVolume`/`Cryptex1,SystemVolume`/`Cryptex1,AppVolume`，确认它们是 229 字节 IM4P root-hash 输入，`personalize_required=True`、`personalized_im4m_present=False`、`root_authentication_solved=False`；脚本明确说“这些 payload 不能提供 personalized IM4M”。
- H91 ADT 的 `/chosen/system-volume-auth-blob` 是 229 字节 IM4P（`BASELINE.md:88-90`），不是 owner IM4M。
- `cryptexd` 二进制字符串（远程 `~/h96/osmnt/usr/libexec/cryptexd`）含：
  ```text
  AuthAPFS enabled, skip up-front dmg authentication.
  AuthAPFS disabled because quire lacks an im4m.
  im4m identifier mismatches cryptex...
  ```
  这是 AuthAPFS/IM4M 依赖的强二进制证据，但 cryptexd 源码不在仓库；按本次“只有源码明确证明才标记缺失”的规则，**IM4M 标记为待确认，而不是已证明缺失**。
- TrustCache 与 IM4M 不是同一材料：H96a21 的 loadable hit 证明 cdhash 查询命中；不证明 `Cryptex1,SystemTrustCache` 已加载，也不证明 AuthAPFS 的 IM4M 验证已满足。
- 历史 `STATUS.md` 的 H92/H93 条目把 IM4M/owner manifest 写成“不可得”；这是当时的输入/运行结论，不是本次审计所要求的源码证明。本文将其状态降级为“待确认（二进制字符串强证据）”，不把“项目里没有现成生成器”等同于“系统源码证明该材料必需且缺失”。
- `trust_cache_runtime_init()` 在 `img4if/amfi` 缺失时 panic；H91/H96 没有该 panic，至少说明 Image4/AMFI 接口对象存在。但接口存在不等于 personalized IM4M 已通过。

## 7. 最后成功调用点、未完成操作与无日志等待

### 7.1 分实验

| 实验 | 最后可见成功操作 | 之后 | 证据 |
| --- | --- | --- | --- |
| H96a18 | `NATIVE_EXEC` 到 `cryptexd` 和 `eligibilityd`；UFS 持续完成 | 5 分钟无新 exec/KextLog/错误 | 214.jpg、STATUS H96a18 |
| H96a19 | 四次 driverkitd 检查后 `FileSet KC`、`driverkitd is active` | 无 cryptexd/eligibilityd；停在 driverkitd active | 215.jpg、H96-investigation 6.30 |
| H96a21 | 第 4 次 driverkitd AMFI 检查（loadable hit） | 诊断 panic 固定屏幕；不进入后续 | 217.jpg、H96-investigation 6.34 |
| H96a22 | `alsd` exec、`driverkitd active`、最后的 SystemOS 4 KiB 完成读 | 无 cryptexd/eligibilityd；3 分钟无变化 | 218.jpg、H96-investigation 6.36 |
| H96a23 | `driverkitd active`；最后一次 UFS 命令 `0x1b1bca/0x20000` | 无 cryptexd/eligibilityd；2 分钟无变化 | 219.jpg、H96-investigation 6.38 |

### 7.2 内核错误处理

- `native_owner_cache_prepare()` 失败会 panic（`kern_exec.c:8282`）；H96a18/22/23 没有这个 panic，说明它返回 0（或路径被跳过）。
- Data mount 失败会 panic（`:8288`）；没有发生。
- `sheng_boot_watchdog_policy()` 参数非法会 panic（`:8294`）；H96a17 曾因 `com.apple.log_helper` 的 `_` 触发；H96a18/22/23 的列表没有该下划线。
- TrustCache 初始化缺 `img4if/amfi` 会 panic；没有发生。
- UFS OCS timeout 在远程被改成 `NATIVE_UFS_TIMEOUT` panic；没有发生。
- UFS window 心跳超过 10 秒会 panic `NATIVE_UFS_STALL`；没有发生。

这些“无 panic”只能说明对应错误路径未触发，**不能**证明 root/SystemOS 已通过真实密码学认证，因为 H96 明确启用了 `native-skip-root-auth=1`、`BRINGUP_CACHE_MOUNT=1`，并链接了 Probe 的 `native_verified_block_root_authorized()` 桩。

### 7.3 可能的无日志等待（均待确认）

1. **cryptexd/AuthAPFS/IM4M**：H96a18 最后到 cryptexd/eligibilityd；cryptexd 可能在等待 AuthAPFS/Image4 im4m 服务或 cryptex mount。强二进制字符串证据，但没有源码。
2. **launchd job 依赖**：H96a22/H96a23 在跳过 cryptexd 后反而停在 `driverkitd is active`；这更像 launchd 在等待被跳过的必需 job，而不是新的内核错误。
3. **driverkitd/FileSet KC/DriverKit user client**：H96a21 已证明四次 AMFI 检查为 loadable hit，但 driverkitd 可能仍在等待 FileSet KC 完成、DriverKit 服务或 kernel/user client；没有源码/日志证明。
4. **显示/WindowServer**：H96 构建 `XNU_NATIVE_MOBILE_DISPLAY=0`、`XNU_NATIVE_ADRENO=0`，legacy display 也未启用；即使到用户态，当前构建也没有完整显示路径。这不是当前 cryptexd 停点的证据，但是最终 UI 的前置缺口。

## 8. 源码明确证明 vs 待确认

### 8.1 源码/二进制直接证明

- H96 使用 NativeUfsProbe，不链接完整 NativeUfs（构建脚本 `:624-635` + 远程 wrapper）。
- Probe 的 root 授权是 `device != NODEV` 桩（`NativeUfsProbe.cpp:578-585`）。
- Probe 发布窗口前不校验 SHNGROOT header/window SHA-256（`:1573-1595`；对比 `RootLayout.h:162-209`）。
- H96 的 `XNU_OWNER_BOOT_MANIFEST=0`，故 `native_owner_publish_boot_manifest()` 未参与本次启动；ADT 生成器写入 48 字节全零（`make-cheeseburger-adt.py:146-148`）。
- `IOGetBootManifestHash()` 允许零值且不做内容验证（`IOKitBSDInit.cpp:539-567`）。
- `winBytes` 是全局累计请求字节，不是窗口位置/实际 UFS 字节（`NativeUfsProbe.cpp:40-47,958-982`）。
- H96a22/H96a23 的 skip 实验与 H96a18 的对比（日志/照片/历史记录）。

### 8.2 仍需确认

- SHNGROOT 的 SystemOS 窗口 offset（`NATIVE_UFS_ROOT: window[1]=SystemOS off=...`）未记录，因此 `0x1b1bca` 的窗口归属无法精确判定。
- `/sbin/mount` 是否只检查 `boot-manifest-hash` 存在，还是还比较内容；二进制字符串/历史 checkpoint 只能证明存在性 gate，源码不可得。
- `Cryptex1,SystemTrustCache` 是否实际由 AMFI 加载，以及 H96a21 的 loadable hit 来自哪个 module。
- cryptexd 的 AuthAPFS/IM4M 要求需要 cryptexd/AppleImage4 源码或 syscall 级跟踪；当前仅有二进制字符串/H92/H93 运行证据。
- `driverkitd` 在 `driverkitd active` 之后等待的具体对象（FileSet KC / DriverKit service / user client）需要专门 trace。

## 9. 建议的后续只读检查（不修改完整性逻辑）

1. 在 H96a18 路径上只增加 **UFS root 窗口 offset 日志**和 **launchd job/exit 观测**，不改变启动/授权逻辑，取得 `window[1]=SystemOS off=...` 和 cryptexd 等待点。
2. 对 `cryptexd` 做只读 syscall/mach wait 跟踪，确认是否在 AuthAPFS/Image4 im4m；不要直接 patch cryptexd 或改 userdata。
3. 读取 AMFI/trust cache 的实际 module 加载日志，确认 H96a21 的 loadable hit 来自静态还是 `Cryptex1,SystemTrustCache`；不要用“命中”推断 owner manifest。
4. 若最终要验证 owner boot manifest，需要先启用真实 `VerifiedRoot`/`NATIVE_VIRTIO_BLOCK` 和 `XNU_OWNER_BOOT_MANIFEST`，并确认 Probe 桩被替换；在此之前，48 字节全零 hash 只是 bring-up 占位。

## 10. 审计期间的状态变更

本次只读审计只新增本文档并更新 `STATUS.md`；没有修改 XNU/驱动/验证逻辑，没有刷写设备，没有启动新的 H96 镜像。H91/H96 镜像和本地 `*.jpg` 证据保持不变。
