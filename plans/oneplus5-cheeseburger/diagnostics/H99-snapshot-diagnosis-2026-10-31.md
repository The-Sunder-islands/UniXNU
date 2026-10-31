# H99 快照诊断结果

- 日期：2026-10-31
- 输入：`222-1.jpg`、`222-2.jpg`（H99 最终屏幕）
- 边界：只读日志/源码分析；没有新的 fastboot 启动/刷写。H99 镜像、H98b/H96/H91 产物均保留。
- 结论状态：**H99 的 `H99_SNAPSHOT` 行仍未出现，进程快照没有获得有效输出；cryptexd/eligibilityd 的运行/退出状态仍未知。**

## 0. 结论摘要

1. H99 最终停在 `driverkitd is active`，没有创建 `cryptexd`/`eligibilityd`；最后可见 exec 是 `alsd`。
2. H99 没有出现 `H99_SNAPSHOT_ARMED`、`H99_SNAPSHOT tick`、`H99_PROC`、`NATIVE_EXIT` 或 `H99 process snapshot` panic。快照路径**没有提供可读结果**，不能据此判断目标进程是否运行/退出。
3. H99 的 UFS 日志直接确认了两个窗口基址：
   - OS：`0x15040000`
   - SystemOS：`0x17f540000`
4. H99 最后完成的有标签 SystemOS 读是：

   ```text
   block=0x4e200 count=8
   offset=0x9c40000
   done=4096 ok=1
   ```

5. H96a23 的 `lba=0x1b1bca`：
   - 绝对字节 `0x1b1bca000`；
   - 相对 OS 基址 `0x15040000` 为 `0x19cb8a000`，超出 OS 窗口大小 `0x16a408600`，不可能属于 OS；
   - 相对 SystemOS 基址 `0x17f540000` 为 `0x3268a000`（806.5 MiB），在 SystemOS 大小 `0xc4e00000` 内；
   - 因此该 LBA 属于 **SystemOS**。但旧 H96a23 日志的 `lastWin=0x93450`（偏移 `0x1268a000`，294.5 MiB）与它不一致，说明该 UFS 命令不是那次 `lastWin` 的同一请求；旧日志无法精确归因到具体请求。
6. owner 信任链仍是 bring-up：H99 继续使用 NativeUfsProbe 桩、`XNU_OWNER_BOOT_MANIFEST=0`、全零 ADT hash；没有真实 owner 认证。
7. IM4M/AuthAPFS 不是 H99 停点的已证实原因：H99 没到 cryptexd，未观测 AuthAPFS/Image4 调用。

## 1. 基线、源码与镜像

### 1.1 仓库

- 本地 UniXNU：`f357cb4`（上一轮诊断文档提交）。
- 设备仓：`5ad54e2`。
- H99 干净构建树：XNU `d8b8029` + 原始 patches + 远程 H99 插桩。
- H96 脏源码保留在 `.cache/xnu-work.h96dirty-20261031`。
- 本地工作区只有未跟踪照片；没有修改本地源码。

### 1.2 H99 产物

| 文件 | SHA-256 |
| --- | --- |
| H99 kernel（未 gate） | `512b5f90a4170c076b56f3224e0aa61d2f96cb43a758d72fda33ca4deae3e75e` |
| H99 kernel gatebypass | `de511644b90d080a201e2778ff621e14afb0104d9b63aee4941a887ab1e72c4c` |
| H99 payload | `89aac04cef6084116f89df29f913d79b6b5c52d007375061bdbbbcfff6e68641` |
| H99 boot | `eab1ff4a1e8f10e183ee55ceda060140405b8323557b27c4c3f7b6d3ebcd986f` |

H99 使用 H91 ADT（`83d733d7...`），因此 `boot-manifest-hash` 仍是 48 字节全零。

### 1.3 H99 构建参数

H99 从 H96a18 的 skip 列表出发，不跳过 `eligibilityd`/`cryptexd`，并设置：

```text
h97-snapshot=360
native-trace-amfi-syscall=0
native-trace-trust-cache=0
```

H99 的快照线程被改为每 60 秒打印一次，到 360 秒 panic：

```text
H99_SNAPSHOT_ARMED delay=360
H99_SNAPSHOT tick=60
H99_PROC: ...
...
H99 process snapshot
```

代码位置：`bsd/kern/kern_exec.c` 的 `h97_snapshot_thread()`，当前构建树约 `:7624-7645`；启动入口 `h97_start_snapshot()` 约 `:7636-7646`，在 `load_init_program()` 约 `:7696` 调用。UFS 映射日志位于 `drivers/NativeUfsProbe/NativeUfsProbe.cpp`（`gWindowLastLabel`/`gUfsLastWindowLabel` 约 `:48-51`，`readWindowBulk` 约 `:902`，progress 约 `:757-780`）。

## 2. H99 最终屏幕事实

### 2.1 进程 exec

H99 最终屏幕出现：

```text
NATIVE_EXEC: pid=45 ppid=1 comm=xpcproxy argv0=xpcproxy argv1=com.apple.containermanagerd.system
NATIVE_EXEC: pid=95 ppid=1 comm=xpcproxy argv0=xpcproxy argv1=com.apple.mobilegestalt.xpc
NATIVE_EXEC: pid=85 ppid=1 comm=containermanagerd argv0=/usr/libexec/containermanagerd.system argv1=--runmode=privileged
NATIVE_EXEC: pid=86 ppid=1 comm=MobileGestaltHelper argv0=/usr/libexec/MobileGestaltHelper argv1=
NATIVE_EXEC: pid=94 ppid=1 comm=alsd argv0=/usr/libexec/alsd argv1=
```

- 这些 job 的父 PID 为 1（launchd）。
- **没有 cryptexd/eligibilityd 的 `NATIVE_EXEC`**。
- 最后可见的新 exec 是 `alsd`（PID 94）。
- 屏幕出现 `MobileGestaltHelper[86] triggered uncaught NSException ...` 的日志；这是该次用户态异常，不能证明它是当前停点的根因。

### 2.2 快照

最终屏幕没有：

```text
H99_SNAPSHOT_ARMED
H99_SNAPSHOT tick
H99_PROC
NATIVE_EXIT
H99 process snapshot
```

因此：

- 不能断言 `h97-snapshot` 线程是否启动；
- 不能断言 cryptexd/eligibilityd 是否运行或退出；
- 不能断言 launchd 等待的具体 job/Mach 服务；
- 不能把“没有 NATIVE_EXIT”当作“进程仍在运行”。

这是观测能力缺失，不是阴性证据。

## 3. H99 UFS 映射

### 3.1 两个窗口

H99 日志同时显示了 OS 和 SystemOS 的标签/基址：

```text
NATIVE_UFS_PROGRESS: ... lastWin=OS ... abs=15040000 ...
  ufsWin=OS base=15040000 ...
NATIVE_UFS_PROGRESS: ... lastWin=SystemOS ... abs=17f540000 ...
  ufsWin=SystemOS base=17f540000 ...
```

因此：

- OS 窗口基址：`0x15040000`
- SystemOS 窗口基址：`0x17f540000`

OS 窗口大小来自 SHNGROOT/OS 镜像：`6,077,580,800 = 0x16a408600`；SystemOS：`3,303,014,400 = 0xc4e00000`。

### 3.2 最后完成读

H99 最后有标签的 SystemOS 读：

```text
NATIVE_UFS_WINDOW: SystemOS req=51200 block=4e200 count=8
  abs=17f540000 lba=190190
NATIVE_UFS_WINDOW: SystemOS req=51200 done=4096 ok=1 ticks=93
  read=0 write=70 abs=17f540000 lba=190190
```

- 窗口内偏移：`0x4e200 * 512 = 0x9c40000`（156.25 MiB）
- 完成：4 KiB，`ok=1`
- 绝对 LBA：`0x190190`（4 KiB）
- 绝对字节：`0x190190000`

### 3.3 历史请求映射

| 来源 | 窗口 | 窗口内偏移 | 绝对 LBA | 结果 |
| --- | --- | ---: | ---: | --- |
| H99 最后 SystemOS | SystemOS | `0x9c40000`（block 0x4e200） | `0x190190` | done=4096 ok=1 |
| H98b 最后 SystemOS | SystemOS | `0x8684000`（block 0x43420） | `0x187bc5` | done=4096 ok=1 |
| H96a22 最后有标签 | SystemOS | `0x27f9e000`（block 0x13fcf0） | `0x1a74de`（由基址计算） | done=4096 ok=1 |
| H96a23 旧 UFS | SystemOS（按地址范围判定） | `0x3268a000` | `0x1b1bca` | 旧日志无法精确归因到 `lastWin` |

H96a23 的判定依据：

```text
0x1b1bca000 - 0x15040000 = 0x19cb8a000 > OS size 0x16a408600  -> not OS
0x1b1bca000 - 0x17f540000 = 0x3268a000 < SystemOS size 0xc4e00000 -> SystemOS
```

但旧 H96a23 的 `lastWin=0x93450` 对应 SystemOS 偏移 `0x1268a000`，与 `0x3268a000` 不同；因此该 UFS 命令属于 SystemOS，但不能说属于 `lastWin` 那次 window 请求。H99/H98b 的新 `ufsWin` 字段才支持精确归因。

### 3.4 `winBytes`

`NativeUfsProbe.cpp:40-51`、`:958-1002` 仍是累计上层请求字节的全局变量；H99 的 `winBytes=320…` 不是磁盘位置、不是实际 UFS 物理读取量、不是单窗口进度。`lastWin` 是请求开始时的快照。

## 4. 进程状态：仍然缺少观测

### 4.1 H96a18 旧证据

H96a18 曾出现：

```text
xpcproxy PID 87 argv1=com.apple.eligibilityd
xpcproxy PID 88 argv1=com.apple.security.cryptexd
cryptexd PID 80 argv0=/usr/libexec/cryptexd
eligibilityd PID 87 argv0=/usr/libexec/eligibilityd
```

但旧日志没有 ppid、退出 trace、线程栈或快照。因此：

- 创建：是；
- PID/argv：是；
- 父进程：未记录（不能从 H98b/H99 的 `ppid=1` 反推）；
- 运行/退出/信号/退出状态：未知；
- launchd 等待对象：未知；
- AuthAPFS/Image4 调用：未知。

### 4.2 H99 事实

- 没有 cryptexd/eligibilityd exec；
- 没有 `NATIVE_EXIT`；
- 没有 `H99_PROC`/快照。

因此 H99 不能补充 H96a18 的进程状态；它只说明 H99 这次运行更早停在 `driverkitd is active`。

## 5. owner 信任链状态

H99 仍使用 H96 的 bring-up 配方：

```text
XNU_OWNER_BOOT_MANIFEST=0
XNU_OWNER_ROOT_TRUST=0
XNU_OWNER_ROOT_AUDIT=0
XNU_VERIFIED_ROOT_PROBE=0
XNU_NATIVE_VIRTIO_BLOCK=0
XNU_NATIVE_UFS_PROBE=1
XNU_BRINGUP_CACHE_MOUNT=1
```

- NativeUfsProbe 的 `native_verified_block_root_authorized()` 仍是 `device != NODEV` 桩（`NativeUfsProbe.cpp:578-585`）。
- H91 ADT `boot-manifest-hash` 仍是 48 字节全零（`make-cheeseburger-adt.py:146-148`）。
- `IOKitBSDInit.cpp:581-629` 的真实 SHA-384 owner manifest 发布在 H99 中未启用；`/sbin/mount` 只被观察到消费该属性（字符串/checkpoint），没有证据显示它对全零值做密码学验证。
- IPSW 的 `SystemVolume`/`Cryptex1,SystemVolume` 是 229-byte IM4P root hash，`personalized_im4m_present=False`；cryptexd 的 `im4m` 字符串仅是线索，**IM4M/AuthAPFS 是否直接导致停滞仍未知**。
- H99 没有到达 cryptexd，因此本轮没有 AuthAPFS/Image4 失败证据。

## 6. 最后成功操作 / 下一项未完成操作

### H99

- 最后成功可见 exec：`alsd` PID 94，`ppid=1`。
- 最后成功 UFS 读：SystemOS `block=0x4e200`, `done=4096`, `ok=1`。
- 下一项未完成：launchd 没有启动 cryptexd/eligibilityd；随后 `driverkitd is active`。是 launchd job 依赖、driverkitd/FileSet KC、还是其他服务等待，**没有证据**。
- 进程状态：未知。

### H96a18

- 最后成功可见 exec：cryptexd/eligibilityd；
- 下一项未完成：同样无法从现有日志区分；
- 运行/退出：未知。

## 7. 结论分级

### 已证实

- H99 有 OS 基址 `0x15040000`、SystemOS 基址 `0x17f540000`。
- H99 最后完成 SystemOS 4 KiB 读 `block=0x4e200`, `ok=1`。
- H99 最终没有 cryptexd/eligibilityd exec，没有 `NATIVE_EXIT`，没有 H99 快照。
- H96a23 的 `lba=0x1b1bca` 属于 SystemOS 地址范围，但不能精确归因到旧 `lastWin`。
- H99 仍是 bring-up owner 授权，不是真实 VerifiedRoot。

### 推断

- H99 的停点更像 launchd/服务等待而非 UFS 硬超时；但没有目标进程状态，不能确认。
- H96a18/H98b/H99 的差异可能是 launchd 调度时序差异；未证实。

### 未知

- cryptexd/eligibilityd 的实际运行/退出状态。
- H99 快照为何没有输出（解析 boot arg、线程启动、`tsleep`/时间点或屏幕滚动均未证实）。
- launchd 具体等待的 job/Mach 服务/文件/IPC。
- AuthAPFS/Image4/IM4M 是否是后续根因。
- `Cryptex1,SystemTrustCache` 的实际加载 module。
- `/sbin/mount` 是否对 hash 内容做密码学验证。

## 8. 下一项实验建议

**H100**（仍不刷写、不改验证逻辑）：

1. 在 `h97_start_snapshot()` 入口无条件 `printf("H100_ARM_ENTER ...")`，确认该函数被调用；再打印 `PE_parse_boot_argn` 的返回值和 seconds。
2. 若线程启动成功但 `tsleep` 不醒，改用更短的 10 秒一次、总 60 秒的周期，或使用可验证的 timer。
3. 在 `NATIVE_EXEC` 对 `cryptexd`/`eligibilityd` 打印 `H100_TARGET_EXEC`，并在其 exec 后启动一次性短快照，避免依赖长延时。
4. 若仍无目标进程，重复 H96a18 的 skip 列表 2–3 次，确认 cryptexd/eligibilityd 是否稳定出现；不要新增 skip。
5. 只有准备验证真实 owner 时，才启用 `VerifiedRoot`/`XNU_OWNER_BOOT_MANIFEST`；当前全零 hash 不能当认证材料。

## 9. 安全

- 没有新的 fastboot 启动/刷写；H99 是最后一次用户要求的上机诊断。
- 没有覆盖 H91/H96/H98b/H99 产物。
- 没有修改 AuthAPFS/Image4/TrustCache 验证行为。
- 没有伪造 owner manifest/IM4M/签名材料。
