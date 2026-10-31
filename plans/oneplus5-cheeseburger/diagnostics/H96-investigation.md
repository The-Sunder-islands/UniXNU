# H96 只读调查：AMFI TrustCache 拒绝路径与 UFS 诊断分离

- 日期：2026-10-31
- 范围：H91/H95 基线之后的只读调查；未刷写设备、未改 userdata、未改正式签名策略。
- 基线：`plans/oneplus5-cheeseburger/BASELINE.md`。
- 目标：回答 `_check_cdhash_in_trustcache` 循环究竟卡在“可信数据缺失”、
  “注册/加载失败”、“查询未命中”还是“调用方权限拒绝”。

## 1. 结论摘要

1. **当前循环的直接拒绝点是 AMFI 的调用方权限门，不是 TrustCache 查询未命中。**
   原始 iPad7,1 21H461 kernelcache 中，AMFI 的
   `_check_cdhash_in_trustcache()` 对应 MAC policy syscall selector `0x5f`。
   它在读取 cdhash、调用 XNU `query_trust_cache()` 之前，先检查调用进程是否
   具有 `com.apple.private.amfi.can-check-trust-cache`。检查失败时打印
   `%s: Only allowed processes can check the trustcache` 并返回 `13`
   （`EACCES`）。这和 STATUS 中观察到的循环文本完全一致。
2. 因此，**继续改 TrustCache blob、继续加 boot-args、或直接把查询改成
   “永不拒绝”都不能解释当前循环**。下一步必须先确认调用方是谁，以及它的
   code signing/entitlement 状态为什么不可用。
3. 原始 SystemOS 镜像中，`com.apple.private.amfi.can-check-trust-cache` 及
   `Unable to find AMFI`、`Unable to connect to AMFI`、
   `cdhash: %@ is not in trust cache` 等字符串位于
   `/System/Library/Frameworks/ExtensionFoundation.framework/ExtensionFoundation`
   （Mach-O 起始偏移 `0x1785b000`，UUID
   `28968ac0-0ace-360e-83e3-c8e7ad996871`）。循环的候选调用方是加载
   ExtensionFoundation 的宿主/服务进程（例如 `pkd`/`extensiond` 一类），
   但在没有 H96a 运行日志前不能把具体进程名当成已证实事实。
4. H91 的 payload 确实携带 `kernel.macho.trustcache`（61,356 字节），
   DarwinBoot 会把它填入 `/chosen/memory-map/TrustCache`；H91 也没有触发
   “missing external trust caches” panic。因此“完全没有交付 TrustCache”
   不是当前循环的首要解释，但静态 cache 是否覆盖目标进程仍需单独验证。
5. H90d 的 `HCS=0x10` 不是“另一种 idle/live 编码”。按 UFSHCI 的 HCS 定义，
   bit 4 是 `HOST_ERROR_INDICATOR`；`HCS & 3 == 0` 表示
   `DEVICE_PRESENT` 和 `UTP_TRANSFER_REQ_LIST_READY` 都未置位。把
   `(HCS & 3) == 3` 放宽并不会让控制器进入可运行状态；应先解释并清除
   host error/控制器未 ready 的原因。UFS 与 TrustCache 必须继续分开。

## 2. 本轮读取与核验

### 2.1 仓库

- 主仓 `UniXNU`：`main` / `93c7fb6`，工作区干净。
- 设备仓 `UniXNU-cheeseburger`：`main` / `5ad54e2`，工作区干净。
- 关键子模块：`NativeUfs 43a4bc7`、`NativeRamBlock f3cffc9`、
  `DarwinBoot d099354`、`VerifiedRoot 2854c61`、`NativePrelink 2b6ffb5`。
- 没有执行 `git reset --hard`、`git clean -fdx`，没有覆盖任何未提交文件。

### 2.2 H91 产物

- boot：`cd81c5b8ec91f103f3e3c0fd7de7bfb26633f4fbabecd34490e5627dd6f3ff9b`
- payload：`e7fa5cdff58217886ebf269b30683c78da83c409ae179c356650f69869f08942`
- payload 内 `kernel.macho`：
  `7f02891c32825b75fcb76f2969ea2c1a541662d4c369d29fc85732b19f0e532d`
- payload 内 `kernel.macho.adt`：
  `83d733d770a9ec6961f60c1af13f4eeeb8552c776098241c0c536d13f810e54a`
- payload 内 `kernel.macho.trustcache`：
  `7416ca4f8377146626bb284e2133c76bee6cb4e3ee0af5d648b186235a7323a7`

H91 的 payload 启动参数已从 FAT 卷内 `unixnu.toml` 原样读出，见
`BASELINE.md` 第 3 节。当前 H91 是 Probe/窗口 UFS + 真实 PMGR
（`sheng-pmgr=2`），没有 `native-ufs-root=1`。

### 2.3 原始 iPad7,1 21H461 kernelcache

从 IPSW 的 `kernelcache.release.ipad7` 解出 46,415,900 字节 Mach-O；
其中的 AMFI kext 是原始未修改代码，用来对照当前 H91 内核里的同一段
AMFI。分析只读取二进制，不运行、不修改。

## 3. 实际启动链路与证据

| 阶段 | 现有证据 | 缺失证据 |
| --- | --- | --- |
| UEFI / DarwinBoot | G1 已实测菜单、按键、payload、DarwinBoot、屏幕日志；H91 payload 含 `kernel.macho`、ADT、TrustCache、Data seed | H91 本次启动的完整串口/屏幕日志 |
| XNU 进入 BSD/IOKit | H91 `kernel.macho` 可启动到用户态；STATUS H91 记录 | 精确构建提交、CFLAGS、kernel.json |
| NativeRamBlock / UFS | H91 为 Probe；STATUS 记录 SystemOS 读持续完成、无 `NATIVE_UFS_STALL` | 请求级 ID/OCB/OCS/中断日志 |
| APFS root | H87a 成功挂载；H89b 过 `mount-phase-1`；H91 沿用 | 本次挂载的 UUID/manifest 校验明细 |
| launchd / boot tasks | H87c/H89b/H91 已到 launchd 和 `mount-phase-1` | 任务退出码、等待点、失败任务名 |
| FileSet KC / driverkitd | `KextLog: Loading FileSet KC(s)`、`KextLog: driverkitd is active` | driverkitd 之后的首个失败点、Cryptex1 挂载结果 |
| AMFI / TrustCache | 循环文本 `_check_cdhash_in_trustcache ... Only allowed processes`；H91 payload 有静态 TrustCache | 调用方 PID/进程名、entitlement 状态、cdhash、返回码 |
| display / WindowServer | 未到达 | 任何显示初始化证据 |

关键判断：H91 的 UFS 读没有硬卡死；`FileSet KC`/`driverkitd` 是当前最后的
可见阶段。不要把 H89b/H90d 的 UFS 停住当成 H91 当前根因，也不要用 UFS
问题解释 AMFI 循环。

## 4. 关键发现：AMFI selector `0x5f` 的权限门

### 4.1 原始 AMFI 二进制中的位置

原始 21H461 kernelcache 中 AMFI kext：

- `_PrelinkExecutableLoadAddr`：`0xfffffff0052db6c0`
- `_PrelinkExecutableSize`：`0xb153`
- AMFI `__TEXT_EXEC,__text`：`0xfffffff005aaa000`，大小 `0x1fa90`
- AMFI `__TEXT,__cstring`：`0xfffffff0052dbd68`，大小 `0x9247`

`_check_cdhash_in_trustcache()` 的 MAC policy syscall dispatcher 在
`0xfffffff005ab72a8`。它以 `w1 - 0x5a` 查 13 项跳表
（`0xfffffff0052e5e40`），selector `0x5f` 跳到
`0xfffffff005ab73a8`。

### 4.2 selector `0x5f` 的执行顺序

从反汇编可直接读出：

```text
0xfffffff005ab73a8  adrp/add  x1, "com.apple.private.amfi.can-check-trust-cache"
0xfffffff005ab73b4  bl        0xfffffff005ab138c        ; 查询进程 entitlement
0xfffffff005ab73b8  tbz       w0, #0, 0xfffffff005ab75cc ; 无权限 -> 拒绝
0xfffffff005ab73bc  ...       copyin 32 bytes from user arg
0xfffffff005ab73f0  bl        0xfffffff005aad100        ; query type 1
0xfffffff005ab7410  bl        0xfffffff005aad100        ; query type 2
0xfffffff005ab7424  ...       copyout 32 bytes
0xfffffff005ab75cc  ...       print rejection message
0xfffffff005ab75e4  mov       w19, #0xd                 ; EACCES
```

拒绝路径使用的字符串：

```text
0xfffffff0052e18ad  "%s: Only allowed processes can check the trustcache\n"
0xfffffff0052e18e2  "int _check_cdhash_in_trustcache(struct proc *, user_addr_t)"
```

权限字符串：

```text
0xfffffff0052dc597  "com.apple.private.amfi.can-check-trust-cache"
```

这条路径在 XNU `query_trust_cache()` 之前结束。也就是说：

- 如果日志只出现 `Only allowed processes...`，**XNU 的 TrustCache
  查询根本没有被调用**；
- 这时看到的是调用方身份/entitlement 问题，不是 cdhash 不在静态/加载
  trust cache 中的问题；
- 只有权限检查通过后，selector `0x5f` 才会 copyin 32 字节参数并向
  XNU 查询 type 1/2，随后把结果位图 copyout。

这解释了为什么 H93 修改 `cryptexd` 后循环重新出现：被修改的二进制不再
具有可信签名/entitlement，调用 AMFI 时在权限门就被拒绝，而不是因为
SystemOS trust cache 内容不对。

### 4.3 调用方候选

在原始 SystemOS 镜像中搜索
`com.apple.private.amfi.can-check-trust-cache`，命中
`/System/Library/Frameworks/ExtensionFoundation.framework/ExtensionFoundation`
（Mach-O 起始 `0x1785b000`，UUID
`28968ac0-0ace-360e-83e3-c8e7ad996871`；entitlement 字符串约在
`0x178e3fxx`）。同一二进制包含：

```text
Unable to find AMFI
Unable to connect to AMFI [%ld]
cdhash: %@ is not in trust cache
Unknown error (%ld) checking cdhash %@
com.apple.private.amfi.can-check-trust-cache
```

因此循环的调用方很可能是使用 ExtensionKit 的宿主/服务进程（`pkd`、
`extensiond` 一类），而不是 XNU 或 APFS 本身。**具体进程名尚未证实**；
必须用 H96a 的 `pid/comm` 日志确定。

### 4.4 H91 的 TrustCache 状态

- `kernel.macho.trustcache` 存在且为 61,356 字节，DarwinBoot 的
  `ValidTrustCaches` 会校验并填入 ADT 的 16 字节 `TrustCache` 属性。
- H91 内核字符串包含 `NATIVE_TRUSTCACHE_LOAD_START`、
  `NATIVE_TRUSTCACHE_PROP`、`NATIVE_TRUSTCACHE: missing external trust
  caches`。反汇编 H91 的 `_load_static_trust_cache` 可见：range 长度为 0
  会走 panic cold path，静态 module `loadModule` 失败也会走 panic cold
  path。H91 能启动到 `driverkitd`，因此这次启动中静态 TrustCache range
  非零且静态 module 至少成功加载。
- 但 H91 的完整日志没有保存，静态 module 的 cdhash 列表是否覆盖目标
  进程仍未验证。这仍是 H3 假设，不是当前循环的直接原因。

## 5. 根因假设（按置信度排序）

### H1（高）：调用方权限门拒绝，不是 TrustCache 查询未命中

`_check_cdhash_in_trustcache` 在 selector `0x5f` 的入口检查
`com.apple.private.amfi.can-check-trust-cache`，调用方没有该 entitlement
（或 AMFI 无法从它的 code signature 取得 entitlement 对象）时直接返回
`EACCES`，用户态循环重试。

**区分方法**：H96a 同时记录
`NATIVE_AMFI_SYSCALL ... call=0x5f` 和 `NATIVE_TC_QUERY`。若循环期间只有
前者、没有后者，且 `entitled=0`，H1 成立。

### H2（中）：调用方本身处于“能运行但签名状态不可信”的 bring-up 状态

当前 H91 使用 permissive AMFI（`amfi_get_out_of_my_way=1`、
`amfi=0x94`、`amfi_allow_any_signature=1`、
`native-skip-exec-signature=1`）。这些开关可能让进程越过 exec 拒绝，
但进程的 `CS_VALID`/`CS_SIGNED`/entitlements 并未按正常路径建立；或者
调用方来自 SystemOS cryptex，而对应的 `Cryptex1,SystemTrustCache` 尚未
加载。结果是进程能跑，但 AMFI 不承认它的 entitlement。

**区分方法**：H96a 日志中的 `csflags`、`blob`、`platform`、`entitled`
四个字段：

- `blob=NULL` 或 `csflags` 缺 `CS_VALID`：签名/entitlement 没有被保留；
- `blob!=NULL`、`platform=0`、`entitled=0`：签名在，但未被当作可信
  platform binary；
- `entitled=1` 但 AMFI 仍拒绝：AMFI 内部的 entitlement 查询与 XNU
  查询不一致，需继续追 AMFI 的 proc/cred 路径。

### H3（低，但对最终 boot 仍重要）：静态 TrustCache 不覆盖 SystemOS/Cryptex 调用方

H91 的静态 cache 来自 `kernel.macho.trustcache`，主要覆盖 OS root；
SystemOS/Cryptex1 有独立的 `Cryptex1,SystemTrustCache`。即使 H1/H2 的
权限门被正确满足，目标 cdhash 仍可能在 XNU 查询中 miss，导致
`driverkitd`/FileSet KC 无法继续。

**区分方法**：权限门通过后，看 H96a 的 `NATIVE_TC_QUERY` 是否出现、
`ret` 是否为 `KERN_NOT_FOUND`，以及静态/SystemOS cache 的 module 数与
目标 cdhash 是否匹配。不要在权限门未通过时用 H3 解释循环。

## 6. H96a 最小诊断补丁

### 6.1 补丁

`patches/xnu/zzzz-trustcache-amfi-trace.patch`

修改两个文件，全部由 boot arg 控制，默认关闭：

1. `security/mac_base.c`
   - 在 `__mac_syscall()` 入口，对
     `com.apple.driver.AppleMobileFileIntegrity` 的 selector `0x5a..0x66`
     打点。
   - selector `0x5f` 最多记录 64 次；其他 selector 每个只记录第一次。
   - 记录：`pid`、`comm`、`call`、`arg`、`proc_getcsflags(p)`、
     `csproc_get_blob(p)`、`csblob_get_platform_binary(blob)`、
     `IOCurrentTaskHasEntitlement("com.apple.private.amfi.can-check-trust-cache")`、
     以及 copyin 的 20 字节 cdhash。
   - boot arg：`native-trace-amfi-syscall=1`。
2. `bsd/kern/kern_trustcache.c`
   - 在 `query_trust_cache()` 返回前记录调用方 pid/comm、query type、
     完整 20 字节 cdhash 和返回值；最多 64 次。
   - boot arg：`native-trace-trust-cache=1`。

没有修改 AMFI 二进制，没有改变 trust cache 注册/查询逻辑，没有改变
AMFI 策略、签名校验、root auth 或 VerifiedRoot。默认 boot arg 缺失时
两个函数都立即返回。

### 6.2 构建集成

`scripts/build-xnu-4k-baseline.sh` 会自动应用
`patches/xnu/*.patch`，新补丁无需额外 CFLAGS。已用干净的
`xnu-10063.141.1` 源树执行：

```text
git apply --check patches/xnu/zzzz-trustcache-amfi-trace.patch  -> PASS
```

本机没有 macOS/Xcode 内核工具链，**尚未完成实际 XNU 编译**；这属于
本轮未完成项，不能把“补丁 apply 成功”当成“已能启动”。

构建后必须检查产物确实包含两处诊断字符串（不是只看补丁文件）：

```sh
strings build/.../kernel.macho | grep -F 'NATIVE_AMFI_SYSCALL'
strings build/.../kernel.macho | grep -F 'NATIVE_TC_QUERY'
strings build/.../kernel.macho | grep -F 'native-trace-amfi-syscall'
strings build/.../kernel.macho | grep -F 'native-trace-trust-cache'
```

四条都存在，才说明 `security/mac_base.c` 和 `bsd/kern/kern_trustcache.c`
的改动进入了最终链接产物；任何一条缺失都先查 object 是否被缓存跳过或
被后续 CFLAGS 覆盖，不要上机。

### 6.3 H96a 启动参数

在 H91 的 payload 参数基础上，只追加：

```text
native-trace-amfi-syscall=1 native-trace-trust-cache=1
```

其余 H91 参数、ADT、TrustCache、Probe/PMGR 配置保持不变。不要同时改
UFS、AMFI 策略或 root auth。

### 6.4 预期日志与判定

权限门拒绝（H1）：

```text
NATIVE_AMFI_SYSCALL: pid=<N> comm=<name> call=95 arg=0x... csflags=0x... blob=0x... platform=... entitled=0 cdhash=... cdhash_valid=1
_check_cdhash_in_trustcache: Only allowed processes can check the trustcache
```

随后循环重复，但**不应出现** `NATIVE_TC_QUERY`。

权限门通过、查询 miss（H3）：

```text
NATIVE_AMFI_SYSCALL: ... call=95 ... entitled=1 ...
NATIVE_TC_QUERY: pid=<N> comm=<name> type=... cdhash=... ret=<...>
```

两条日志的 `pid` 必须一致，才能把“同一调用方的 0x5f 调用”和
“XNU 查询”对应起来；不要只看全局是否出现过 `NATIVE_TC_QUERY`。

若 `ret` 为 `KERN_NOT_FOUND`，再比较静态 cache module 与 cdhash；
若 `ret=0`，问题在 AMFI 之后的策略/调用方逻辑。

判定：

- 出现 `call=95` 且 `entitled=0`：H1 成立，停止改 TrustCache blob，
  转向“调用方为什么没有 entitlement / 签名状态为什么不可信”。
- 出现 `call=95` 且 `entitled=1`，但 AMFI 仍拒绝：需要继续反汇编/对照
  AMFI 的 entitlement 查询路径；不要通过删除检查来推进。
- 完全没有 `call=95`：当前循环不是这条 AMFI 路径，重新核对日志来源，
  不要继续按 H1 处理。

### 6.6 H96a 构建记录（2026-10-31）

构建机：`192.168.10.103`（macOS 15.8.2 / Xcode 26.3），使用 H91 的
`out/cheeseburger/tree` 对象树，源版本 `xnu-10063.141.1`（`d8b8029`）。
构建前已把 H91 的 `kernel.macho`、`kernel.json`、`payload.img`、
`payload.json` 和 raw kernel 备份到 `~/h96-backup/`。

使用的关键环境（从 H91 对象树的 stamp 反推并匹配）：

```text
XNU_TRUST_CACHE_RUNTIME=0
XNU_OWNER_CACHE_MOUNT=1
XNU_OWNER_CACHE_STARTUP=1
XNU_OWNER_CACHE_CARRIER=1
XNU_NATIVE_CS_HASH_CACHE=1
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
XNU_NATIVE_VIRTIO_BLOCK=0
XNU_SOURCE_CORECRYPTO14=1
XNU_NATIVE_RAM_BLOCK=1
XNU_NATIVE_MOBILE_DISPLAY=0
XNU_NATIVE_AES=0
XNU_NATIVE_ADRENO=0
XNU_ARM_DT_PLATFORM=1
XNU_ARMCPU=1
XNU_SOURCE_PTHREAD=1
XNU_NATIVE_ARCH_TIMER=1
XNU_DT_PANIC_REGION=1
XNU_BOOT_PROGRESS=0
XNU_SOURCE_MOCKFS=1
XNU_CRYPTO_BOOTSTRAP=source
XNU_OWNER_DATA_VOLUME_UUID=D312EFB3-1335-4E03-87C3-D78A298BB0B1
XNU_SHENG_PMGR=1
```

预链接参数沿用 H91：

```text
--slide=0x5000000
--prelinked out/ipsw/21H461/kernelcache.decompressed
--root ...（与 H91 的 15 个 root 完全相同）
--early-security-init com.apple.driver.AppleMobileFileIntegrity=0xfffffff005ab4ae0
```

产物：

| 文件 | 大小 | SHA-256 |
| --- | ---: | --- |
| H96a raw kernel | 19,445,640 | `92e55a7076cac782429d7ca3c60e88fabbf44d8d55e3d1b5ccb9647683160045` |
| `H96a-kernel.macho` | 52,088,832 | `f5dbbe1c6a15a8f6fbfbccbb7cbf562edc385769a30845c68112bc9e324e466c` |
| `H96a-payload.img` | 79,691,776 | `8c92f8c85ccaed52ced0693242c5acd98285ae857cfc15f7d8859c6d4f7be14e` |
| `boot-H96a-amfitrace.img` | 81,147,904 | `790de81156545c2fa64c4271f92207ade01d0019ca8af35ca538f5d6fbeb98b5` |

`H96a-kernel.macho` 已用 `strings` 确认包含
`NATIVE_AMFI_SYSCALL`、`NATIVE_TC_QUERY` 和两个 trace boot arg 字符串。
payload 从 H91 payload 复制，只替换 `Payloads/kernel.macho` 和
`unixnu.toml` 的 args；ADT、TrustCache、Data seed 均沿用 H91。
boot image 从 H91 boot image 复制，只在 ramdisk 偏移 `1456128` 处
替换为同尺寸的 H96a payload；UEFI kernel 和 header 未改。

第一次 `fastboot boot` 返回 `Booting OKAY`，但设备停在 UEFI Shell，未进入
UniXNU/XNU。原因：用 `mcopy` 覆盖 `UNIXNU~1.TOM` / `KERNEL~1.MAC` 时
指定了 8.3 短名，导致 VFAT 长名 `unixnu.toml` / `kernel.macho` 丢失；
UniXNU 按长名查找配置/内核失败，回到 shell。已删除旧目录项并用长名重新
写入，修正后 payload/boot 哈希为上表值。raw kernel 和 kernel.macho 未变。

注意：构建脚本在本次重编译了 12 个 object（trace 两处，加上它每次都会
重新应用的 late patches 所触及的对象）。环境变量按 H91 stamp 匹配，但
尚未做“去掉 trace 补丁后重建并比对 H91 哈希”的严格 control。H96a 的
结论只应归因于 trace 两处，除非后续 control 证明其他 object 未改变。

### 6.7 H96a 首次 XNU 结果与 H96a2 强制 trace

H96a（修正长名后）成功进入 XNU，屏幕可见：

```text
NATIVE_UFS_PROGRESS: ...
NATIVE_UFS_WINDOW: SystemOS ...
_check_cdhash_in_trustcache(struct proc *, user_addr_t): Only allowed processes can check the trustcache
KextLog: Loading FileSet KC(s)
KextLog: driverkitd is active
```

这确认了三点：

1. H96a 的 XNU/payload 链正常，长名修复有效；
2. UFS 仍在持续完成 SystemOS 读，没有 `NATIVE_UFS_STALL`；
3. AMFI 的 `_check_cdhash_in_trustcache` 拒绝循环在 `driverkitd` 阶段真实存在。

但该帧没有看到 `NATIVE_AMFI_SYSCALL`。两种解释：

- 64 次 trace 预算在截图前已经滚出屏幕；
- `native-trace-amfi-syscall=1` 没有被 `PE_parse_boot_argn` 解析到。

因此构建 H96a2：**强制打开 trace**，不依赖 boot arg，并把 selector
`0x5f` 预算提高到 100000、query 预算提高到 4096，使屏幕任意时刻都应能
看到 trace 与 AMFI 拒绝行交替出现。H96a2 只用于诊断；仓库默认补丁仍
保持 boot-arg 关闭。

H96a2 产物：

| 文件 | 大小 | SHA-256 |
| --- | ---: | --- |
| `H96a2-kernel.macho` | 52,088,832 | `918cdbe11a638e7a603b902850b4aa3372543a941e5596971eef654629303a18` |
| `H96a2-payload.img` | 79,691,776 | `da268d87a7c0bd267a4b09d92f7c27bbb03afb5cb910bdbf71fda85bf05c019e` |
| `boot-H96a2-amfitrace.img` | 81,147,904 | `8c58b13832c14d6bf2a8f27fcc6a98aad4323ef1aebb03ae00db427883bc062d` |

H96a2 源码差异（仅构建树）：

```diff
-static int native_amfi_syscall_trace = -1;
+static int native_amfi_syscall_trace = 1;
-static int native_trace_tc_query = -1;
+static int native_trace_tc_query = 1;
```

并分别把 `0x5f`/query 的限频从 64 提高到 100000/4096。除 trace 外，
H96a2 的配置、ADT、TrustCache、Data seed 与 H96a/H91 相同。

### 6.8 H96a2 结果与 H96a3 修正

H96a2 屏幕可见 `NATIVE_TC_QUERY` 多次成功返回：

```text
NATIVE_TC_QUERY: pid=24 comm=UserEventAgent type=1 cdhash=... ret=0
NATIVE_TC_QUERY: pid=1 comm=launchd type=0 cdhash=34f3216e... ret=0
NATIVE_TC_QUERY: pid=80 comm=spracy type=0/1 cdhash=... ret=0
KextLog: driverkitd is active
```

说明 XNU 的 TrustCache 查询本身工作，且能命中；但这帧仍没有
`NATIVE_AMFI_SYSCALL`。原因已定位：`__mac_syscall` 的 target 过滤写成了
bundle ID `com.apple.driver.AppleMobileFileIntegrity`，而 AMFI 注册的
MAC policy 名更可能是 `AppleMobileFileIntegrity`，导致 trace 直接 return。

另外，旧 trace 在 `printf` 参数里调用 `IOCurrentTaskHasEntitlement()`。
这不仅是只读观测，可能提前初始化/缓存调用方的 entitlement，从而改变
AMFI 后续判断。H96a2 中没有再看到 `Only allowed` 循环，不能排除这个
副作用。

因此 H96a3：

- 取消 target 过滤，`0x5a..0x66` 的调用都会记录，并打印实际 `target`；
- 删除 `IOCurrentTaskHasEntitlement()` 调用，只保留 pid/comm/target/
  csflags/blob/platform/cdhash，避免改变策略状态；
- `0x5f` 仍强制、限频 100000；query trace 强制、限频 4096。

H96a3 产物：

| 文件 | 大小 | SHA-256 |
| --- | ---: | --- |
| `H96a3-kernel.macho` | 52,088,832 | `b36ec3a89e05f4fa9e364b7f7a3191951c9bb0c60bd21dffd6eed99fed0e648e` |
| `H96a3-payload.img` | 79,691,776 | `d5030ee552dc9627a000d6ddc8d2eaa9ccd6310ac3c4b6dfc062f5a152d9887d` |
| `boot-H96a3-amfitrace.img` | 81,147,904 | `98192bb07988871506eaa49a1a43c98644ef562243853923b0c277be84fa22e1` |

### 6.9 H96a3 结果与 H96a4 受控绕过

H96a3 屏幕可见多次 `NATIVE_TC_QUERY ... ret=0`（UserEventAgent、
launchd、xpcproxy），但仍完全没有 `NATIVE_AMFI_SYSCALL`。H96a3 已强制
trace 并取消 target 过滤，因此结论是：**AMFI 的
`_check_cdhash_in_trustcache` 检查不是从 XNU `__mac_syscall` 进入的**
（至少 H96a3 的可见窗口内没有 `0x5a..0x66` 调用），而不是 boot arg 或
滚屏问题。

为直接拿到拒绝点的调用方和查询结果，制作 H96a4 一次性诊断镜像：

- 在 H96a3 prelinked kernel 的 AMFI 拒绝分支，把
  `VM 0xfffffff00aab73b8` 的 `tbz w0, #0, reject` 改成 `nop`；
- 只跳过 `com.apple.private.amfi.can-check-trust-cache` 的权限门，
  TrustCache 查询本身仍然执行；
- 预期 AMFI 自己的 `Process %d is checking...` 日志会打印调用方 PID，
  同时 `NATIVE_TC_QUERY` 会给出 cdhash 和 hit/miss。

这是**受控诊断绕过**，只存在于独立镜像 H96a4，不是仓库默认配置，也不是
最终修复；它没有把任意 hash 无条件加入 TrustCache，也没有永久关闭 AMFI。

H96a4 产物：

| 文件 | 大小 | SHA-256 |
| --- | ---: | --- |
| `H96a4-kernel.macho` | 52,088,832 | `86a7e63abf3e32725e5665c3c538bc21c541845a542d2a9fbcfb9194a4e0feb6` |
| `H96a4-payload.img` | 79,691,776 | `6d6f49355ca1b1f09e70f39ded35fdeda7ba0064d5ceac29db89501f45a22ec0` |
| `boot-H96a4-tcbypass.img` | 81,147,904 | `f745b54f551ab6473fe8e6aeee68951d592e8d128ed5eb7facce5308bf3f6be0` |

### 6.10 H96a5：首个 AMFI 拒绝即打印调用方并 panic（B 方案）

按用户选择 B，H96a5 不再依赖滚屏，而是在 AMFI 的拒绝分支做一次
**只读诊断 panic**：

- 在 H96a3 prelinked kernel 的 AMFI 拒绝分支
  `VM 0xfffffff00aab75d8` 改成跳到代码洞
  `VM 0xfffffff00a96b318`；
- 代码洞读取 `proc->p_pid`（`+0x60`）和 `proc->p_comm`（`+0x5bc`），
  拼接原函数名后调用 `panic()`：
  ```text
  H96_AMFI_REJECT: func=%s pid=%d comm=%s
  ```
- `panic` 会停住系统并保留屏幕，不再继续刷屏；这是一次性诊断，
  不改变 AMFI 策略、不跳过权限门、不修改 TrustCache。

H96a5 产物：

| 文件 | 大小 | SHA-256 |
| --- | ---: | --- |
| `H96a5-kernel.macho` | 52,088,832 | `2994ae9d7f7dae99d84f597ce3edf4a3a05a962de7fbbf922ddb2893022150cb` |
| `H96a5-payload.img` | 79,691,776 | `2ed839d356e313a520733963e25965af3c9d30bf2b982e0fe9e83dc15690d9a2` |
| `boot-H96a5-paniclog.img` | 81,147,904 | `c2a183500c2cff77c3331751e862c5b5487b937d256e49608add075ab81361cc` |

预期屏幕（只出现一次，然后停在 panic）：

```text
H96_AMFI_REJECT: func=int _check_cdhash_in_trustcache(struct proc *, user_addr_t) pid=<PID> comm=<NAME>
```

拿到 PID/comm 后即可判断拒绝方是 launchd、amfid、pkd、xpcproxy 还是其他
进程；随后沿该进程的签名/entitlement 来源继续，而不是继续改 TrustCache。

### 6.11 H96a5 结果：拒绝方是 driverkitd，且原版 driverkitd 本就没有该 entitlement

H96a5 panic 屏幕给出：

```text
NATIVE_AMFI_SYSCALL: pid=63 comm=driverkitd target=AppleF... call=95
  csflags=0x2202331 blob=0xfffffff6dfe23050 platform=1 cdhash=... cdhash_valid=1

H96_AMFI_REJECT: func=int _check_cdhash_in_trustcache(struct proc *, user_addr_t)
  pid=63 comm=driverkitd
```

因此调用方确定为 **driverkitd（PID 63）**，selector 为 95（0x5f），
`platform=1`、`blob` 非空、`csflags=0x2202331`，说明它的 code signing
blob 是被 AMFI 识别为 platform binary 的；但 entitlement 门仍然拒绝。

在 macOS 构建机上挂载原版 iPad7,1 17.7.11 OS DMG，
`/usr/libexec/driverkitd` 的签名是 **adhoc**，CDHash
`2811f83446d213b1b14a8f7b0e81b79063fadf77`，entitlements 只有：

```text
com.apple.private.kernel.get-kext-info
com.apple.private.sandbox.profile:embedded = com.apple.driverkitd
com.apple.private.security.iocatalog-management
com.apple.private.security.kext-collection-management
com.apple.private.security.storage.driverkitd
com.apple.private.spawn-driver
com.apple.private.xpc.launchd.job-manager = com.apple.driverkitd
com.apple.runningboard.launch_dexts
```

**没有** `com.apple.private.amfi.can-check-trust-cache`。所以在当前
签名状态下，AMFI 拒绝 driverkitd 是符合策略的；问题变成：
**driverkitd 为什么在本次 bring-up 中会走到这个未授权的 AMFI MAC
syscall 路径？**

最可能的解释不再是“TrustCache blob 缺失”，而是：

1. driverkitd 正常应该通过 DriverKit/AMFI 的 IOKit user client 或
   SystemOS/Cryptex 信任链处理 driver approval；当前环境中该路径不可用，
   于是回退到 `__mac_syscall`，而 driverkitd 没有该 entitlement；
2. driverkitd 被错误的 launchd/SystemOS 状态或被注入的
   ExtensionFoundation/Security 路径带入了 trust-cache 查询；
3. 当前 AMFI 的 IOKit service/user client 未启动或未匹配，导致回退。

这些都需要在 H96a6 中进一步区分。

### 6.12 H96a6：跳过权限门并打印 TrustCache 查询结果

H96a6 在 H96a3 基础上做两个二进制诊断改动：

- 把 AMFI 权限门的 `tbz`（`VM 0xfffffff00aab73b8`）改成 `nop`，让
  driverkitd 的请求进入实际查询；
- 在 AMFI 查询完成、copyout 之前（`VM 0xfffffff00aab7424`）跳到代码洞，
  读取 `p_pid`、`p_comm`、查询结果位图 `[sp+0x58]` 和 cdhash
  `[sp+0x40..0x50]`，然后 `panic()`：
  ```text
  H96_TC_RESULT: pid=%d comm=%s mask=0x%llx cdhash=%016llx%016llx
  ```

`mask=0` 表示 AMFI 的两次查询都命中；`mask=3` 表示两次都 miss。
这能区分“driverkitd 只是没有 entitlement，但它要查的 cdhash 已在
TrustCache 中”与“cdhash 本身也不在 TrustCache 中”。

H96a6 产物：

| 文件 | 大小 | SHA-256 |
| --- | ---: | --- |
| `H96a6-kernel.macho` | 52,088,832 | `d06f6e695cc21093c8ffb0acc4dec0d3458738bf69ed2359c7f430d9d139026e` |
| `H96a6-payload.img` | 79,691,776 | `aec14df75054b80832399301ef2289a9902fe2c0f07e5732d492163975db7881` |
| `boot-H96a6-tcresult.img` | 81,147,904 | `3ac690a0e083202668173215e68b942eb94310118f098169c1646af616dbd46b` |

### 6.13 H96a6 结果：目标 cdhash 在静态 cache 命中，loadable 查询 miss

H96a6 panic 屏幕给出：

```text
NATIVE_AMFI_SYSCALL: pid=63 comm=driverkitd target=AMFI call=95
  arg=0x16f23b710 csflags=0x22002301 blob=0xfffffff... platform=1
  cdhash=7bb1e5321fc19a7efbb83d7eb54ca15b cdhash_valid=1

NATIVE_TC_QUERY: pid=63 comm=driverkitd type=1
  cdhash=1fc19a7... ret=0
NATIVE_TC_QUERY: pid=63 comm=driverkitd type=2
  cdhash=1fc19a7... ret=56

H96_TC_RESULT: pid=63 comm=driverkitd mask=0x3
  cdhash=7bb1e5321fc19a7efbb83d7eb54ca15b
```

判读：

- `target=AMFI`：实际 MAC policy 名是 `AMFI`，不是 bundle ID；这解释了
  H96a2/H96a3 旧 target 过滤为什么没有日志。
- type 1 查询 `ret=0`：目标 cdhash 已在**静态 TrustCache** 命中。
- type 2 查询 `ret=56`：`KERN_NOT_FOUND`，loadable/SystemOS 侧没有
  该 cdhash。
- `mask=0x3`（照片略糊，需用更清晰图确认）：AMFI 把两次查询都当作
  miss 位；但 type 1 的 XNU 查询本身是命中的，说明 AMFI 内部的
  type 1 wrapper 可能还有额外的 cache capability/type 检查，不能只看
  XNU query 返回值。
- `cdhash=7bb1...` 是 driverkitd 请求检查的**目标**（driver/kext），
  不是 driverkitd 自身 cdhash；原版 driverkitd 自身是 adhoc
  `2811f834...` 且没有 trust-cache entitlement。

这进一步确认：**当前循环不是“TrustCache 完全缺失”**。目标 cdhash 在
静态 cache 命中，但：
1. driverkitd 自身没有 `com.apple.private.amfi.can-check-trust-cache`，
   却被带进了 `__mac_syscall("AMFI", 0x5f, ...)` 路径；
2. loadable/SystemOS 查询 miss；
3. AMFI 的 type 1 wrapper 结果与 XNU 查询返回值不完全一致，需要看
   AMFI 的 cache type/capability 条件。

下一步应优先回答：**为什么 driverkitd 会走这个未授权的 MAC syscall，
而不是正常的 AMFI IOKit user client/DriverKit 路径？** 这比继续补
TrustCache blob 更能解释循环。

### 6.5 回滚

- 不覆盖 H91 boot/payload/kernel。
- H96a 使用独立文件名，例如 `boot-H96a-amfitrace.img`。
- 回退：重新 `fastboot boot boot-H91-stallpanic.img`。
- 补丁默认关闭；即使留在树里，不带两个 trace boot arg 时行为与 H91
  相同（仅多出未执行的静态函数）。

## 7. UFS 独立诊断设计（H96u，不与 H96a 同镜像）

H91 没有 UFS 硬 stall，因此 UFS 不是当前第一优先；但 P2 要求可证伪的
请求级诊断，且不能把 H89b/H90d 的停住与 AMFI 混为一谈。

### 7.1 先解释 `HCS=0x10`

按 UFSHCI 的 HCS（Host Controller Status，0x30）定义：

| bit | 名称 |
| ---: | --- |
| 0 | `DEVICE_PRESENT` |
| 1 | `UTP_TRANSFER_REQ_LIST_READY` |
| 2 | `UTP_TASK_REQ_LIST_READY` |
| 3 | `UIC_COMMAND_READY` |
| 4 | `HOST_ERROR_INDICATOR` |
| 5 | `DEVICE_ERROR_INDICATOR` |
| 8..10 | `UIC_POWER_MODE_CHANGE_REQ_STATUS` |

因此 H90d 的 `HCS=0x10` 表示 **host error indicator 置位**，同时
`DEVICE_PRESENT`、`UTP_TRANSFER_REQ_LIST_READY` 都未置位；
`(HCS & 3) == 3` 为假不是“驱动假设错了”，而是控制器没有进入可运行
状态。H90e 放宽检查后仍无法建立队列，与这个解释一致。

正确方向不是删除 profile 校验或把任意 HCS 当成 ready，而是：

1. 读 `IS`/`IE`/`HCE`/`HCS`/`UIC` 错误位；
2. 确认是初始化顺序、时钟/电源、复位、还是此前 H89b/H90 的控制器
   残留状态导致 host error；
3. 在确认 DMA 停止后，按 UFSHCI 顺序清 error/重新 enable，并验证
   `UTP_TRANSFER_REQ_LIST_READY` 与 `UIC_COMMAND_READY`。

### 7.2 请求生命周期字段

在 `NativeUfsProbe`（当前 Probe 路径，不是 warm-link）中为每个请求记录：

- request ID、LUN、LBA、block count、byte count；
- UTRD slot/tag、UTRD/command descriptor 地址；
- PRDT 条目数、DMA buffer 地址/长度；
- submit 时间戳；
- doorbell 前后 `HCS`/`IS`/`IE`/`UTRLDBR` 快照；
- 中断或轮询是否观察到完成；
- OCS/transfer response/status；
- completion callback 是否执行；
- outstanding 数量变化；
- 超时后一次性限频快照，并进入可恢复 timeout/error 路径。

### 7.3 测试顺序

1. 单请求、同一 LBA 重复读；
2. 连续小块读；
3. 不同长度读；
4. 多 LUN 交替读；
5. 长时间重复读；
6. 最后才测并发/read-ahead。

每步用已知原始窗口 hash 校验读回数据；只统计“读到字节”不算正确性
证明。H96u 必须先独立构建、独立镜像，不要与 H96a 的 AMFI trace 混在
同一个 boot image 里。

## 8. 安全边界与风险

- H96a 只增加默认关闭的观测日志；不改变 AMFI 决策、不改变 TrustCache
  注册、不改变 root auth、不改变 VerifiedRoot digest。
- `IOCurrentTaskHasEntitlement` 是只读查询；不会授予权限。
- 不把 `entitled=0` 的检查删除或改成 `entitled=1`；不把 48 字节占位
  manifest 当成真实 owner manifest；不把 8 字节 digest 当成最终方案。
- 不自动刷写 userdata、GPT、ABL/XBL/modem/persist。
- 若 H96a 证明调用方是修改过的二进制，必须回到原始签名二进制或真实
  owner manifest/trust cache，而不是永久关闭 AMFI。

## 9. 缺失证据

1. H91 的完整启动日志和循环首发 PID/进程名。
2. H91 的精确构建提交、CI run、kernel.json、CFLAGS。
3. H91 静态 TrustCache 的 module 数、cdhash 列表，以及目标进程 cdhash
   是否在其中。
4. `Cryptex1,SystemTrustCache` 是否已加载、加载到哪一层。
5. AMFI 内部 entitlement 查询失败的具体原因（需要 H96a 的
   `csflags`/`blob`/`entitled` 组合）。
6. UFS 请求级日志；当前只能确认 H91 没有硬 stall。

## 10. 下一步唯一优先事项

**H96a：用 `native-trace-amfi-syscall=1` 和 `native-trace-trust-cache=1`
启动 H91 配置的独立诊断镜像，确认 `_check_cdhash_in_trustcache` 的调用方
PID/comm、`entitled`、`csflags`、`blob`、`platform` 和 cdhash，并确认
循环期间是否出现 `NATIVE_TC_QUERY`。**

在拿到这组日志前，不要：

- 继续叠加 amfi/boot-args；
- 修改 TrustCache blob；
- 修改 AMFI/trustcache 二进制；
- 把 UFS 问题与 AMFI 循环合并处理。

## 11. 需要用户执行的设备测试

先构建（用户/CI，不在本次自动执行）：

1. 使用与 H91 相同的构建配置，确认补丁已应用：
   ```sh
   git -C .cache/xnu-work apply --check patches/xnu/zzzz-trustcache-amfi-trace.patch
   ```
2. 构建 H96a kernel/payload，payload 参数只追加：
   ```text
   native-trace-amfi-syscall=1 native-trace-trust-cache=1
   ```
3. 生成独立 boot image，例如
   `out/cheeseburger/boot/boot-H96a-amfitrace.img`，记录 boot/kernel/payload
   SHA-256。

上机（用户明确授权后）：

```sh
fastboot boot out/cheeseburger/boot/boot-H96a-amfitrace.img
```

只做 `fastboot boot`，不要 `fastboot flash userdata`，不要刷写固件分区。
采集：

- 屏幕/串口完整日志；
- 所有 `NATIVE_AMFI_SYSCALL` 行；
- 所有 `NATIVE_TC_QUERY` 行；
- AMFI `_check_cdhash_in_trustcache` 拒绝行；
- 最后到达的 `KextLog` 阶段。

若设备未授权或构建未完成，停在本文的只读结论，不要自行上机。
