# H101/H102 快照调试结果

- 日期：2026-10-31
- 输入：H101 最终屏、H102 最终屏
- 目的：确认 `h97_start_snapshot()` / 快照线程为何没有输出，并获得 cryptexd/eligibilityd 的进程状态。
- 结论：**H101（boot-arg 定时）和 H102（无条件启动）都没有在最终屏幕产生 `H10x_SNAPSHOT`/`H10x_PROC`/panic，快照路径仍未工作。进程状态仍未知。**

## 1. 实验

| 实验 | 变化 | 结果 |
| --- | --- | --- |
| H101 | 保留 H96a18 skip；`h97-snapshot=120`；打印 `H101_ARM_ENTER/PARSE/THREAD_START`；60/120 秒快照 | 最终屏无 H101 行、无 panic；停在 `driverkitd active` |
| H102 | 保留 H96a18 skip；**不再依赖 boot arg**，无条件启动 180 秒快照；60/120/180 秒快照 | 最终屏无 H102 行、无 panic；停在 `driverkitd active` |

H102 产物：

```text
H102 kernel: 80c3028835f902e1607e15c25252a83e6ecd082e9e2d3a68fbc162b59b2934e3
H102 boot:   fe08ad872bdaf41b3d4086f3d3541f702700b8843cbf6dae9ba88eec29c8f73f
```

H102 的源码插桩与 H101 相同，但 `h97_snapshot_thread()` 改为无条件启动，不再读取 `h97-snapshot`。

## 2. 能确定什么

- H101 和 H102 的最终屏幕都没有出现快照输出或 panic。
- H102 无条件启动仍然没有结果，因此**不能把问题归因于 `PE_parse_boot_argn("h97-snapshot")` 解析失败**。
- 由于 `H102_ARM_ENTER`/`H102_THREAD_START` 可能出现在更早的滚动区域，最终屏无法证明函数没有被调用；也不能证明线程没有启动。
- 可以直接确定的是：**在当前可见窗口内，快照路径没有产生结果，因此不能据此判断 cryptexd/eligibilityd 是否运行/退出。**
- H102 最终仍是：
  ```text
  NATIVE_EXEC ... alsd
  KextLog: driverkitd is active
  ```
  没有 cryptexd/eligibilityd 的可见 `NATIVE_EXEC`；但没有完整日志/快照，不能把最终帧当作完整进程集合。

## 3. 可能原因（未证实）

1. `h97_start_snapshot()` 在 `load_init_program()` 中未被执行到（入口被别的路径绕过）。
2. `kernel_thread_start()` 失败；`H102_THREAD_START` 行已滚出屏幕。
3. 线程启动但 `tsleep()` 没有按预期超时/唤醒，或线程被阻塞。
4. 快照线程运行了，但 `proc_iterate()`/`printf` 在当前控制台没有输出，或 `panic` 没有生效。
5. 180 秒时系统已经处于不可调度/控制台不再刷新的状态（但最终屏仍有 UFS 日志，暂时不支持该解释）。

这些都需要下一轮**入口级/串口级证据**，不能靠猜测。

## 4. 对上一轮结论的影响

H101/H102 没有改变以下事实：

- H100 证明移除 7 项 skip 会停在更早的 pfd/accountsd/nsurlsessiond/routined；
- 跳过 eligibilityd/cryptexd 仍已证明会造成更早停止；
- H99/H100 的 OS/SystemOS UFS 映射仍成立：
  - OS base `0x15040000`
  - SystemOS base `0x17f540000`
- owner 链仍是 bring-up，没有真实 VerifiedRoot/owner manifest；
- IM4M/AuthAPFS 仍未被 H99/H100/H101/H102 触发到可验证点。

## 5. H103 建议

只做观测定位，不改完整性逻辑、不增加 skip：

1. 在 `h97_start_snapshot()` 的第一行执行 `panic("H103 arm entered")`（或写一个不可滚动的早期标记），先确认该函数是否被调用。
2. 如果入口被调用但线程不工作，改为在 `NATIVE_EXEC` 的 `cryptexd`/`eligibilityd`/`alsd` 分支里直接调用一个 `panic` + `proc_iterate`，绕过线程和定时器。
3. 如果无法从屏幕获得早期日志，使用串口/持久化日志能力；当前只有屏幕，无法证明被滚动覆盖的内容。
4. 不要继续添加 skip，也不要把全零 `boot-manifest-hash` 当作真实 owner 认证。

## 6. 安全

- 本轮没有刷写设备，没有覆盖 H91/H96/H99/H100/H101/H102 产物。
- 没有修改 AuthAPFS/Image4/TrustCache 验证行为。
- 没有伪造 owner manifest/IM4M/签名材料。
