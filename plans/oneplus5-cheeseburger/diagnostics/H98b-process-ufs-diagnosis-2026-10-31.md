# H98b 用户态停滞 / UFS 映射 / owner 信任链诊断

- 日期：2026-10-31
- 范围：只读日志分析 + 一次最小诊断内核（H98b）；未刷写设备，未覆盖 H91/H96 基线。
- 最终证据：`221.jpg`（H98b 最终屏幕），以及此前的 H96a18/H96a21/H96a22/H96a23 照片。
- 结论状态：**H98b 的进程快照没有出现在最终屏幕；cryptexd/eligibilityd 的当前运行状态仍未知。** UFS SystemOS 映射已由 H98b 直接确认。

## 0. 结论摘要

1. **H98b 直接确认 SystemOS 窗口基址为 `0x17f540000`。** 新日志同时打印了窗口标签、窗口内偏移、绝对 LBA 和 UFS 请求所属窗口：
   ```text
   NATIVE_UFS_WINDOW: SystemOS req=... block=43420 count=8
     abs=17f540000 lba=187bc5
   NATIVE_UFS_PROGRESS: ... lastWin=SystemOS abs=17f540000 off=...
     ufsWin=SystemOS base=17f540000 ufsOff=...
   ```
   这回答了 H96 旧日志中缺失的窗口归属问题。

2. **H98b 最终停在 `driverkitd is active`，没有创建 `cryptexd`/`eligibilityd`。**
   H96a18 曾出现这两个进程；H98b 使用同样的“不跳过 eligibilityd/cryptexd”列表，却没有出现。因此 H96a18 的 cryptexd/eligibilityd 到达结果存在 **run-to-run 差异**，不能把 H96a18 的进程集合当作稳定基线。

3. **H98b 的进程快照诊断没有产生 `H97_SNAPSHOT_BEGIN` / `H97_PROC`。**
   因此本轮的 cryptexd/eligibilityd 是否运行、是否退出、退出状态、launchd 等待对象，仍然**没有观测证据**。不能把“没有看到退出”写成“进程仍在运行”。

4. **H96a18 的可见进程事实仅到 exec 层：**
   - `xpcproxy` PID 87，argv1 为 `com.apple.eligibilityd`
   - `xpcproxy` PID 88，argv1 为 `com.apple.security.cryptexd`
   - `cryptexd` PID 80，argv0 为 `/usr/libexec/cryptexd`
   - `eligibilityd` PID 87，argv0 为 `/usr/libexec/eligibilityd`
   
   H96a18 未打印父 PID；H98b 的 `NATIVE_EXEC ... ppid=1` 只适用于 H98b 自己的进程，不能反推 H96a18 的这些 PID 的父进程。

5. **H98b 的最后一条有标签的成功 SystemOS 读**为：
   ```text
   SystemOS req=43000 block=43420 count=8
   abs=17f540000 lba=187bc5
   done=4096 ok=1 ticks=84
   ```
   即窗口内偏移 `0x8684000`（约 134.5 MiB），完成 4 KiB；绝对 UFS LBA 为 `0x187bc5`。

6. **H96a23 的 `lba=0x1b1bca` 不能仅凭旧日志关联到 `lastWin=0x93450`。**
   用 H98b 确认的 `0x17f540000` 基址，`0x1b1bca` 落在 SystemOS 内（偏移 `0x3268a000`，约 842.6 MiB），但它与旧日志的 `lastWin=0x93450`（偏移 `0x1268a000`）不一致。它可能是另一个 read-ahead/请求；没有旧日志的 `ufsWin` 字段，不能精确归因到 `lastWin` 那次请求。

7. **owner 信任链在 H98b 中仍是 bring-up 模式。**
   H98b 沿用 H96 构建配方：`XNU_OWNER_BOOT_MANIFEST=0`、`XNU_OWNER_ROOT_TRUST=0`、`XNU_OWNER_ROOT_AUDIT=0`、`XNU_VERIFIED_ROOT_PROBE=0`、`XNU_NATIVE_VIRTIO_BLOCK=0`、`XNU_NATIVE_UFS_PROBE=1`。ADT `boot-manifest-hash` 仍是 48 字节全零占位，真实 owner manifest 路径没有执行。

8. **IM4M/AuthAPFS 不是 H98b 停点的已证实原因。**
   H98b 没有到达 cryptexd，因此没有到达 AuthAPFS/Image4 调用点；cryptexd 二进制字符串只是线索。当前结论：**未知，不能标记为“已证明缺失/根因”**。

## 1. 任务 A：基线冻结

### 1.1 仓库与构建状态

审计/诊断阶段开始时：

| 项目 | 值 |
| --- | --- |
| 本地 UniXNU HEAD | `59fecce2b2523989a0b55c1772dec65dd24d3960`（上一轮审计记录） |
| 本地后续文档提交 | `f357cb4`（只读审计报告） |
| 设备仓 | `5ad54e21a067ee747d67009122c87510dfdd3803` |
| 原 H96 构建树 | `9f3daad0c350c21fda3269c8da7e7d2780fc6db8` |
| XNU 基础 | `d8b80295118ef25ac3a784134bcf95cd8e88109f` |
| 本地工作区 | 31 个未跟踪 `196.jpg` … `221.jpg`；无已跟踪源码修改 |
| H96 脏源码备份 | `.cache/xnu-work.h96dirty-20261031`（远程构建树，保留 H96 trace） |

H98b 为诊断单独建立干净 XNU 源码树：`refer/xnu` 的 `d8b8029` + 原始 patches，然后加入 H98b 插桩；没有覆盖 `.cache/xnu-work.h96dirty-20261031`。

### 1.2 镜像哈希

| 镜像 | SHA-256 |
| --- | --- |
| H91 boot | `cd81c5b8ec91f103f3e3c0fd7de7bfb26633f4fbabecd34490e5627dd6f3ff9b` |
| H91 payload | `e7fa5cdff58217886ebf269b30683c78da83c409ae179c356650f69869f08942` |
| H91 kernel | `7f02891c32825b75fcb76f2969ea2c1a541662d4c369d29fc85732b19f0e532d` |
| H91 ADT | `83d733d770a9ec6961f60c1af13f4eeeb8552c776098241c0c536d13f810e54a` |
| H91 trustcache | `7416ca4f8377146626bb284e2133c76bee6cb4e3ee0af5d648b186235a7323a7` |
| H96a18 payload / boot | `c58640e146c4f7d2a97ceaa511bc88e2382256a44f3bd03a0469fe6b60bb74c7` / `301d8edd7f2c13b144f8c980fa4364ab1ed89141bac2e52325ea65826ad805b6` |
| H96a18 kernel | `ede3f2b803113ec9921a3faae052bced060ee32f50e7656436889e2d6355ef3a` |
| H96a21 kernel / boot | `b9cd4c9b44a0d5c977b50d1de8f861483797d4a362062e025d3fd8bce47b95d3` / `f7e9a2f051f6cc446d2a775d799b489bd2297f92ef043b516f40d7b9ee88eb67` |
| H96a22 payload / boot | `7e2c37c9c50203e7fa92a20b95a65c1bab4fbea2aa2415c2a4ee239323317801` / `1b76aac07fc3e68830a91069e18d98008b177c745345bab289e51acc16f728f9` |
| H96a23 payload / boot | `23b29bdaa78e9069a5a78c6d446472b8a68abe8f56796bcceefd79258f0af66b` / `4d23038ecc56823c391b04d31f913d7afb566cf34bcba2cc21df5ebbd451ce22` |
| H97 kernel / boot | `8c9b1744f0526a0ba8a67b6ff6e1850c86c9e539fcff021c65989985fa04049d` / `56ace425980a3e511d96fb17022a54018503443a387b751c953b2b5707278af6` |
| H98b kernel（未打 gate 补丁） | `267c4021c0545edd5d5d97933f316611e99783fac70af9af51d8f1fdee656e67` |
| H98b kernel gatebypass | `fa6836fb34813e617fa33eb613d91f9f07cfb0f769c774b952b3c82c5ee3fb93` |
| H98b payload / boot | `88e1027c5fa192b87e8d6ba6a9ed49c46c80dc251ffb6faefc723992843ec555` / `c3f25f448eb26653b620f168ccd3bfcd42ac01fe2250412e43c8ff9219437044` |

H98b 的 ADT 仍来自 H91 payload，因此 ADT 哈希仍为 `83d733d7...`。H98b 使用 H91 ADT 的 48 字节全零 `boot-manifest-hash`。

### 1.3 H98b 构建参数

H98b 使用 H96a18 的 skip 列表（不跳过 eligibilityd/cryptexd），并追加：

```text
h97-snapshot=360
native-trace-amfi-syscall=0
native-trace-trust-cache=0
```

H98b 的内核来自干净源码 + H98b 插桩；UFS 驱动 archive 用：

```sh
SDKROOT=iphoneos python3 scripts/build-arm-driver.py NativeUfsProbe
```

H98b 的源码插桩只在构建树，不在本地 git；H96 脏源码备份保留。

## 2. 任务 B：H96a18 / H98b 的用户态停滞

### 2.1 H96a18 可见事实

H96a18 的照片（`214.jpg`）明确记录：

```text
NATIVE_EXEC: pid=87 comm=xpcproxy argv1=com.apple.eligibilityd
NATIVE_EXEC: pid=88 comm=xpcproxy argv1=com.apple.security.cryptexd
NATIVE_EXEC: pid=80 comm=cryptexd argv0=/usr/libexec/cryptexd
NATIVE_EXEC: pid=87 comm=eligibilityd argv0=/usr/libexec/eligibilityd
```

能回答：

- **是否创建进程**：是，至少 xpcproxy/launchd 路径创建了 cryptexd、eligibilityd 的 exec 记录。
- **PID/启动参数**：如上；PID 80（cryptexd）、87（eligibilityd），xpcproxy 87/88。
- **父进程**：H96a18 的旧 trace **没有 ppid 字段**，无法从该日志确认父 PID；不能把 H98b 的 `ppid=1` 直接套到 H96a18。
- **是否退出/状态**：H96a18 内核没有 `NATIVE_EXIT`/进程快照；照片也没有退出行，**未知**。
- **退出状态/信号**：无观测，**未知**。
- **launchd 等待对象**：无 job-level/Mach 服务观测，**未知**。
- **AuthAPFS/Image4 调用**：无观测，**未知**。

因此，H96a18 最后可确认的成功操作是 `eligibilityd`/`cryptexd` 的 exec；之后哪项操作未完成，现有日志不足以区分“服务内部阻塞”“launchd 等待 job”“driverkitd/FileSet KC 未完成”。

### 2.2 H98b 可见事实

H98b 的 `221.jpg` 中：

- `NATIVE_EXEC` 记录到 `xpcproxy`、`MobileGestaltHelper`、`cfprefsd`、`backboardd`、`sharingd`、`imagent`、`lockdownd`、`alsd` 等；
- 所有可见 `NATIVE_EXEC` 行均为 `ppid=1`（例如 backboardd、sharingd、alsd）；
- **没有 cryptexd/eligibilityd 的 `NATIVE_EXEC`**；
- 没有 `NATIVE_EXIT`；
- 没有 `H97_SNAPSHOT_BEGIN` / `H97_PROC`；
- 最后停在 `KextLog: driverkitd is active`，随后是 SystemOS 读和字体/cache 日志。

能回答：

- H98b 本次运行没有创建 cryptexd/eligibilityd；所以“H96a18 的 cryptexd/eligibilityd”不是这个镜像的稳定结果。
- H98b 中可见的 launchd 子进程父 PID 为 1，说明这些 job 是由 PID 1 直接/经 xpcproxy 启动的；这不能证明 H96a18 的 cryptexd/eligibilityd 的父进程。
- H98b 最后成功 exec 是 `alsd`（可见 PID 63，`ppid=1`）；之后没有新 exec。
- H98b 最后完成的 SystemOS 读是 `block=0x43420, count=8, done=4096, ok=1`；没有 UFS timeout/stall。

### 2.3 任务 B 的明确答案

| 问题 | H96a18 | H98b | 证据状态 |
| --- | --- | --- | --- |
| cryptexd/eligibilityd 是否创建 | 是，有 exec 记录 | 否，未出现 | H96a18 照片；H98b 照片 |
| PID | 80/87（另有 xpcproxy 87/88） | 无 | H96a18 照片 |
| 父 PID | 未记录 | 可见进程为 ppid=1；目标进程无 | 缺少 H96a18 ppid |
| 启动参数 | `/usr/libexec/cryptexd`、`/usr/libexec/eligibilityd` | — | H96a18 照片 |
| 是否仍运行 | 未知 | 未知（目标未出现） | 无退出/快照 |
| 是否退出/信号/状态 | 未知 | 目标未出现 | 无 `NATIVE_EXIT` |
| launchd 等待依赖/Mach/文件/IPC | 未知 | 未知；最后无新 exec | 无 job/syscall 观测 |
| AuthAPFS/Image4 调用 | 未知 | 未到达 | 无 trace |

**不能把“没有 `NATIVE_EXIT`”写成“进程仍在运行”。** H98b 的快照线程没有产生可见输出；H96a18 的旧内核根本没有退出 trace。缺失的观测能力必须保留为“未知”。

## 3. 任务 C：SystemOS 窗口与 UFS 映射

### 3.1 H98b 直接证据

H98b 的 UFS archive 已用修改后的 `NativeUfsProbe` 重建，最终日志：

```text
NATIVE_UFS_WINDOW: SystemOS req=40960 block=1884d count=8
  abs=17f540000 lba=1884d5
NATIVE_UFS_WINDOW: SystemOS req=40960 done=4096 ok=1 ticks=100
  read=0 write=50 abs=17f540000 lba=1884d5

NATIVE_UFS_WINDOW: SystemOS req=43000 block=43420 count=8
  abs=17f540000 lba=187bc5
NATIVE_UFS_WINDOW: SystemOS req=43000 done=4096 ok=1 ticks=84
  read=0 write=74 abs=17f540000 lba=187bc5
```

以及：

```text
NATIVE_UFS_PROGRESS: ... lastWin=SystemOS abs=17f540000 off=...
  ufsWin=SystemOS base=17f540000 ufsOff=...
```

这证明：

- SystemOS 窗口基址是 `0x17f540000`；
- 这些请求的窗口标签确实是 `SystemOS`；
- 窗口内偏移和绝对 LBA 是分别记录的，不能把 `winBytes` 当作偏移。

### 3.2 关键请求映射

SystemOS 大小按原始窗口为 **3,303,014,400 bytes = `0xc4e00000`**，基址 `0x17f540000`。

| 来源 | 窗口标签 | 窗口内 block（512B） | 窗口内 offset | 绝对 LBA（4K） | 绝对字节 | 结果 |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| H98b 最后完成读 | SystemOS | `0x43420` | `0x8684000` | `0x187bc5`（日志） | `0x187bc5000` | `done=4096 ok=1` |
| H98b 另一条 | SystemOS | `0x1884d` | `0x3109a00` | `0x1884d5` | `0x1884d5000` | `done=4096 ok=1` |
| H96a22 最后有标签读 | SystemOS | `0x13fcf0` | `0x27f9e000` | `0x1a74de`（由基址计算） | `0x1a74de000` | `done=4096 ok=1` |
| H96a23 旧日志最后 UFS | 旧日志无 `ufsWin` | 旧 `lastWin=0x93450` | `0x1268a000` | `0x1b1bca`（日志） | `0x1b1bca000` | 旧日志不能精确归因 |

H96a23 的说明：

- `0x1b1bca * 4096 = 0x1b1bca000`；
- 减去 H98b 确认的 SystemOS 基址 `0x17f540000`，得到偏移 `0x3268a000`（842.6 MiB），仍在 `0xc4e00000` 的 SystemOS 大小内；
- 但旧日志的 `lastWin=0x93450` 对应偏移 `0x1268a000`（294.5 MiB），与 `0x3268a000` 不同。旧日志没有 `ufsWin` 字段，无法判断 `lba=0x1b1bca` 是哪个 window 请求的 read-ahead，还是另一个未显示的请求。
- 只有 H98b 的新字段 `ufsWin=SystemOS`/`base=17f540000` 才能完成该归因。

### 3.3 `winBytes` 语义（再次确认）

`NativeUfsProbe.cpp:40-51` 与 `:958-1002`：

- `gWindowBytes` 是所有 `NativeUfsWindow` 共享的累计值；
- 每个上层请求入口执行 `gWindowBytes += count * 512`；
- 不是实际 UFS 传输字节，不是磁盘偏移，也不是单个窗口的进度；
- `lastWin` 在请求开始时写入，不代表最后完成；
- `NATIVE_UFS_PROGRESS` 只是 UFS 命令完成时的快照。

因此 H96a23 的 `winBytes=315523184`（300.9 MiB）和 H96a22 的 `3222299648`（3.001 GiB）只能说明累计上层请求量，不能拿来比较启动位置。

## 4. 任务 D：owner 信任链审计

### 4.1 SHNGROOT

- 生产者：`scripts/unixnu/userdata.py:10-12,104-110`。格式为 `"SHNGROOT"`, version/count/image_bytes，每窗口 128 字节 entry（name/offset/size/block_size/flags/sha256），byte 4064 为 header SHA-256。
- 消费者：`drivers/NativeUfsProbe/NativeUfsProbe.cpp:1573-1595` 读取 header，发布 OS/SystemOS 窗口。
- 当前 H98b 的 NativeUfsProbe **没有校验 header SHA-256 或窗口 sha256**；只检查 magic/version/windows 和字段边界。完整 `NativeUfs/RootLayout.h:157-209` 才会校验，但 H98b 链接的是 Probe。
- 结论：SHNGROOT 是 UniXNU 用户态容器，不是 Apple owner manifest，不是 IM4M，也不是 TrustCache。

### 4.2 owner boot manifest / `boot-manifest-hash`

- 当前 H91/H98b ADT 的 `boot-manifest-hash` 由 `make-cheeseburger-adt.py:146-148` 写入 `bytes(48)` 全零；H91 ADT 中属性长度 `0x30`，48 字节全零。
- 内核真实发布函数 `IOKitBSDInit.cpp:581-629` 只在 `XNU_OWNER_BOOT_MANIFEST=1` 时编译；它从 VerifiedRoot root/cache manifest 计算 SHA-384，并拒绝覆盖已有属性。
- H98b 的 `XNU_OWNER_BOOT_MANIFEST=0`，因此没有执行真实发布；`native_owner_publish_boot_manifest()` 不在 H98b 路径中。
- 消费者：`/sbin/mount` 二进制含 `IODeviceTree:/chosen`、`boot-manifest-hash`、`failed to get boot-manifest-hash`；`scripts/check-mount-phase-checkpoints.py:16-29` 记录缺少属性时 `mount -P 1` exit 66。
- `IOGetBootManifestHash()`（`IOKitBSDInit.cpp:539-567`）只按长度读取并零填充，不校验内容。
- 结论：**48 字节全零占位只满足属性存在性检查**；现有证据没有显示 `mount` 对值做密码学比较，源码不可得，故值验证仍为“待确认”。

### 4.3 VerifiedRoot / root binding

- H98b 构建配方仍为 `XNU_VERIFIED_ROOT_PROBE=0`、`XNU_OWNER_ROOT_AUDIT=0`、`XNU_OWNER_ROOT_TRUST=0`、`XNU_NATIVE_VIRTIO_BLOCK=0`、`XNU_BRINGUP_CACHE_MOUNT=1`、`XNU_NATIVE_UFS_PROBE=1`。
- `NativeUfsProbe.cpp:578-585` 的 `native_verified_block_root_authorized()` 仍只返回 `device != NODEV`；它是 bring-up 桩。
- `bsd_init.c:1072-1108` 的 root auth 失败可由 `native-skip-root-auth=1` 放行；H98b 仍带该 boot arg。
- 结论：H98b 的 root/SystemOS 挂载不是真实 VerifiedRoot 认证；不能把无 panic 当作认证成功。

### 4.4 Image4 / IM4M

- 目标 IPSW 实测（本机 `iPad_Pro_HFR_17.7.11_21H461_Restore.ipsw`）：
  - `BuildManifest` 为 `21H461`，`j120ap` 两个 identity；
  - `SystemVolume` = `Firmware/044-76040-038.dmg.root_hash`，229 bytes，`Personalize=True`；
  - `Cryptex1,SystemVolume` = `Firmware/044-76034-038.dmg.root_hash`，229 bytes，`Personalize=True`；
  - `Cryptex1,SystemTrustCache` = `Firmware/044-76034-038.dmg.trustcache`，1871 bytes，`Personalize=True`；
  - `StaticTrustCache` = `Firmware/044-76040-038.dmg.trustcache`，59519 bytes，`Personalize=True`。
- `scripts/audit-target-root-hashes.py:33-60` 明确记录：这些 229-byte 输入是 IM4P root-hash，`personalized_im4m_present=False`、`signature_authentication_qualified=False`、`root_authentication_solved=False`。
- `cryptexd` 二进制字符串含 `AuthAPFS disabled because quire lacks an im4m.`、`AuthAPFS enabled...`、`im4m identifier mismatches cryptex...`；但没有 cryptexd/AppleImage4 源码，故按任务要求标记为**待确认**，不是“源码证明必需且缺失”。
- H98b 没有到达 cryptexd，因此 AuthAPFS/IM4M 不是 H98b 停点的已证实直接原因。

### 4.5 TrustCache

- H96a21 四个 driverkitd 检查均为 `mask=0x1`（静态 miss、loadable hit），目标 cdhash 已记录；这说明这些目标在运行时 loadable 侧命中。
- 不证明 `Cryptex1,SystemTrustCache` 的具体 module 已全部加载，也不证明 H98b 的 cryptexd 路径通过。
- H98b 没有 cryptexd，因此本轮没有新的 AuthAPFS/TrustCache 失败证据。

## 5. 最后成功操作与下一项未完成操作

### 5.1 H96a18

- 最后可见成功：`cryptexd`/`eligibilityd` 的 exec（PIDs 80/87）。
- 下一项未完成：无法从现有日志区分 cryptexd 服务完成、launchd 下一 job、driverkitd/FileSet KC 或 AuthAPFS/Image4；没有退出/快照/服务内部日志。
- 结论：**未知**，不能用推测填充。

### 5.2 H98b

- 最后可见成功：`alsd` exec（PID 63, `ppid=1`）以及最后的 SystemOS 4 KiB 读（`block=0x43420`, `done=4096`, `ok=1`）。
- 下一项未完成：launchd 没有启动 cryptexd/eligibilityd；随后停在 `driverkitd is active`。可能是 launchd job 依赖、driverkitd/FileSet KC 或另一个无日志等待；**没有证据确认**。
- 没有 `NATIVE_EXIT`/`H97_SNAPSHOT`，所以目标进程状态仍未知。

## 6. 已证实、推断、未知

### 6.1 已证实

- H98b 的 SystemOS 窗口基址为 `0x17f540000`，最后有标签读为 SystemOS。
- UFS `winBytes` 是累计上层请求字节，不是位置/实际 UFS 字节。
- H98b 最后成功 SystemOS 读为 `block=0x43420`, `done=4096`, `ok=1`。
- H96a18 曾创建 cryptexd/eligibilityd；H98b 同参数未创建。
- H98b 使用 Probe 的 root 授权桩；`XNU_OWNER_BOOT_MANIFEST=0`；ADT hash 为 48 字节全零。
- 目标 IPSW 只有 229-byte IM4P root hash，不是 personalized IM4M。

### 6.2 推断

- H96a18/H98b 的差异可能是 launchd 调度/服务依赖的 run-to-run 差异，不是单一 daemon 跳过造成；但这只是推断，没有 job 级日志。
- H98b 的停点更像 launchd/服务等待，而不是 UFS 硬超时；但没有 `NATIVE_EXIT`/快照，不能排除未观测的进程退出。

### 6.3 未知

- H96a18 的 cryptexd/eligibilityd 是否仍在运行、是否退出、退出状态。
- H98b 的 `H97_SNAPSHOT` 为什么没有出现在最终屏幕（快照线程未触发、被延迟、或时间未到）；因此 process snapshot 不可用。
- launchd 具体等待的 job/Mach 服务/文件/IPC。
- AuthAPFS/Image4/IM4M 是否实际进入并失败。
- `Cryptex1,SystemTrustCache` 的实际加载 module 集合。
- `/sbin/mount` 是否对 `boot-manifest-hash` 进行密码学验证。

## 7. 下一项最有价值的诊断实验

建议 **H99**，不修改验证逻辑、不刷写：

1. 先修复 H98b 快照未出现的观测问题：把快照改成 boot 后固定延时、并在 `load_init_program` 之后的明确路径中打印 `H97_SNAPSHOT_ARMED`，确认线程启动；快照触发时先打印 `H97_SNAPSHOT_BEGIN`，再 panic。
2. 在 H96a18 的同一 skip 列表下重复 2–3 次，确认 cryptexd/eligibilityd 的到达是否稳定；H98b 本次未到达，不能把 H96a18 作为稳定基线。
3. 若需要进程依赖证据，增加 **launchd job/exit 事件**和 cryptexd 的 syscall/mach wait 观测；不要用更多 skip。
4. owner 链方面，只有在准备启用真实 `VerifiedRoot`/`XNU_OWNER_BOOT_MANIFEST` 时，才验证真实 48-byte hash；当前全零占位不是认证材料。

## 8. 本轮源码插桩与回滚

H98b 的插桩只在远程构建树，未改本地 git：

| 文件 | 修改 | 理由 |
| --- | --- | --- |
| `bsd/kern/kern_exec.c` | `NATIVE_EXEC` 加 `ppid`；`h97_snapshot_proc/thread/start`（约 `:7609-7649`）；`h97_start_snapshot()` 在 `:7696` | 记录父进程与延迟快照 |
| `bsd/kern/kern_exit.c` | `native_trace_process_exit()`（`:1515-1535`），在 `exit_with_reason()` `:1550` 调用 | 记录选定进程退出/原因 |
| `drivers/NativeUfsProbe/NativeUfsProbe.cpp` | 窗口标签/基址/偏移/绝对 LBA 全局（`:48-51`）；`readWindowBulk` 参数（`:232`, `:902`）；progress/window 日志（`:757-780`, `:997-1002`, `:1088-1095`） | 建立 SystemOS 请求映射 |

回滚方式（不改设备）：

```sh
# 恢复 H97/H98 插桩前的三份文件
cp ~/h96/h97-before/kern_exec.c .cache/xnu-work/bsd/kern/kern_exec.c
cp ~/h96/h97-before/kern_exit.c .cache/xnu-work/bsd/kern/kern_exit.c
cp ~/h96/h97-before/NativeUfsProbe.cpp drivers/NativeUfsProbe/NativeUfsProbe.cpp

# 如需恢复 H96 脏源码，保留/使用：
# .cache/xnu-work.h96dirty-20261031
```

H98b 的干净源码构建使用临时脚本 `scripts/build-xnu-4k-baseline-h98.sh` 和 `~/run-h98-build.sh`（不在本地 git）；删除这两个临时脚本即可移除构建入口，H91/H96/H97/H98b 镜像不删除。

## 9. 安全与结论

- 未刷写设备，未改 GPT/userdata，未覆盖 H91/H96 基线产物。
- 未伪造 owner manifest/IM4M/签名材料。
- 未修改 AuthAPFS/Image4/TrustCache 验证决策。
- H98b 证明 UFS 映射和 SystemOS 读取完成，但**没有证明**真实 owner 认证、IM4M 存在或 cryptexd 停顿原因。
- 当前最准确的结论是：**UFS 读取链路在最后一条 SystemOS 4 KiB 读处成功；停点在 launchd/driverkitd/服务层，cryptexd/eligibilityd 在 H98b 本次运行中未出现；H96a18 的 cryptexd/eligibilityd 进程状态仍未知。**
