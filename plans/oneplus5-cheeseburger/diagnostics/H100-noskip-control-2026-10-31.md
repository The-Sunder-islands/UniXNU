# H100 无 skip 对照结果

- 日期：2026-10-31
- 输入：H100 最终屏幕（`223.jpg`）
- 目的：验证 `sheng-disable-jobs` 的 7 项 skip 是否在掩盖更早的停点，或是否直接导致 cryptexd/eligibilityd 不可达。
- 边界：只读日志分析；没有新增 skip；没有刷写设备；没有覆盖 H91/H96/H98b/H99 产物。

## 1. H100 变量

H100 使用 H99 kernel（含 H99 的 exec/exit/UFS 映射/快照插桩），payload 从 H96a18 参数出发，只移除：

```text
sheng-disable-jobs=com.apple.nsurlsessiond,com.apple.routined,
com.apple.nehelper,com.apple.chronod,com.apple.accountsd,
com.apple.alsd,com.apple.pfd
```

保留：

```text
sheng-lite=1
h97-snapshot=360
native-trace-amfi-syscall=0
native-trace-trust-cache=0
```

因此 H100 是“移除 7 项 job skip、保留 lite 策略”的对照，不是完全裸启动。

| 文件 | SHA-256 |
| --- | --- |
| H100 payload | `c325a5450ee532a932352faa0cd5c0aca73b063416fabe26c936c39d55afb9ea` |
| H100 boot | `f831d2593ff68d22c4f5b84278b92642294f5853441f1ae8dc52e5c3e362ad84` |

H100 kernel 沿用 H99：

```text
512b5f90a4170c076b56f3224e0aa61d2f96cb43a758d72fda33ca4deae3e75e
```

## 2. H100 最终屏幕事实

最终屏幕可见：

```text
NATIVE_EXEC: pid=88 ppid=1 comm=xpcproxy argv1=com.apple.pfd
NATIVE_UFS_PROGRESS: ... lastWin=SystemOS abs=17f540000 ...
NATIVE_EXEC: pid=32 ppid=1 comm=routined argv1=LAUNCHED_BY_LAUNCHD
NATIVE_EXEC: pid=89 ppid=1 comm=xpcproxy argv1=com.apple.accountsd
NATIVE_EXEC: pid=80 ppid=1 comm=pfd argv0=/usr/libexec/pfd argv1=
NATIVE_UFS_PROGRESS: ... lastWin=OS abs=15040000 ...
NATIVE_EXEC: pid=90 ppid=1 comm=xpcproxy argv1=com.apple.nsurlsessiond
NATIVE_EXEC: pid=89? ppid=1 comm=accountsd argv0=/System/Library/Frameworks/Accounts.framework/accountsd argv1=
NATIVE_UFS_PROGRESS: ... lastWin=OS/... abs=15040000 ... ufsWin=OS base=15040000 ...
```

之后停在：

```text
NATIVE_EXEC ... nsurlsessiond / accountsd / pfd / routined
```

没有出现：

```text
cryptexd
eligibilityd
KextLog: driverkitd is active
H99_SNAPSHOT_ARMED
H99_SNAPSHOT tick
H99_PROC
NATIVE_EXIT
H99 process snapshot
```

## 3. 与 H96a18/H99 的对比

| 实验 | `sheng-disable-jobs` | 最终可见停点 | cryptexd/eligibilityd |
| --- | --- | --- | --- |
| H96a18 | 7 项 skip | 曾到 cryptexd/eligibilityd | 曾出现 |
| H99 | 7 项 skip | `driverkitd is active` | 最终帧未见 |
| H100 | 无 skip（保留 lite） | `pfd`/`accountsd`/`nsurlsessiond`/`routined` | 未见 |

H100 的结果说明：

- 移除 7 项 skip 后，系统**更早**停在 `pfd`/`accountsd`/`nsurlsessiond`/`routined` 一带；
- 7 项 skip 确实在掩盖这些上游同步等待；
- 但 H99 在保留这 7 项 skip 时仍只到 `driverkitd active`，所以这 7 项 skip **不是 cryptexd 缺失的唯一原因，也不是当前停点的充分解释**；
- 已证明有害的是额外的 `eligibilityd`/`cryptexd` skip（H96a19/H96a22/H96a23 更早停止）。

## 4. 对“是不是 skip 导致”的结论

结论分三层：

1. **跳过 `eligibilityd`/`cryptexd`：是，直接导致更早停止。**
   这已在 H96a19/H96a22/H96a23 中重复看到。

2. **当前 7 项 skip：不是单纯“制造了 cryptexd 缺失”。**
   H100 去掉它们后并没有出现 cryptexd，反而更早停在上游服务；说明它们是绕过上游等待的工作区，不是 cryptexd 路径的唯一开关。

3. **H99 与 H96a18 的差异仍不能归因于单一 skip。**
   H99 保留 7 项 skip 时最终帧未看到 cryptexd/eligibilityd，但旧 H96a18 曾看到；可能是 launchd 时序、服务依赖或未捕获的早期/滚动日志。H99/H100 的 `H99_SNAPSHOT` 没有输出，无法确认进程状态。

## 5. UFS 映射补充

H100 再次确认：

```text
OS:       base 0x15040000
SystemOS: base 0x17f540000
```

H100 最后有标签的 SystemOS/OS 读仍显示在窗口内，`done=4096 ok=1`；没有 `NATIVE_UFS_TIMEOUT`/`NATIVE_UFS_STALL`。`winBytes` 仍是累计上层请求量，不是位置。

## 6. 下一步

H101 不应继续增加 skip，也不应重复“无 skip”实验；应聚焦：

1. 修复 H99/H100 快照未输出的观测问题：在 `h97_start_snapshot()` 入口无条件打印，确认 `PE_parse_boot_argn("h97-snapshot")`、线程启动和首次 `tsleep`。
2. 在 `cryptexd`/`eligibilityd` exec 时触发短快照，而不是只依赖 360 秒定时。
3. 若需要越过 pfd/accountsd/nsurlsessiond/routined，必须逐项记录这些服务的等待对象；不要再用未验证的 skip 组合。
4. owner/IM4M 路径仍未被 H100 触发；继续标记为未知，不要伪造 manifest 或修改 AuthAPFS/Image4 验证。

## 7. 安全

- 没有刷写设备/GPT/userdata。
- 没有新增 skip。
- 没有修改 AuthAPFS/Image4/TrustCache 验证行为。
- H91/H96/H98b/H99/H100 产物均保留。
