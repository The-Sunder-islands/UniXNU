# H103 Preboot/Cryptex 布局审计（2026-11-01）

 - 说明：H102 文档原建议的 H103 是“快照入口 panic”。本轮先做更直接的 Preboot/Cryptex 布局审计；快照入口实验顺延到后续编号。
- 目的：用本机已有的 21H461 iPad7,1 OS/SystemOS 原始镜像，核对 `/System/Cryptexes`、`/private/preboot` 和 SystemOS 内容，确认 H96a18/`driverkitd` 停点是否只是布局问题。
- 边界：只读分析；没有刷写设备，没有修改 OS/SystemOS/AuthAPFS/Image4 验证。
- 参考：`/tmp/opencode/sagit-ios-port`（小米 6、同 SDM835、已进 SpringBoard）。

## 1. 输入

OS 窗口来自本项目已记录的 userdata：

```text
/mnt/data_2048/project/ios_data/userdata/userdata-root.img
OS offset 0x104400（APFS 对象起点；userdata-root.json 的 0x100000 少了 0x4400 头部）
OS size   6,077,580,800
OS SHA-256 e5634d04a8ac35f024845f583394e0a35deb26a04cb0c00286a8674e25a9a4f2
```

SystemOS：

```text
/mnt/data_2048/tmp/opencode/systemos.img
SHA-256 b4908cde41d23e6bc05e6af51a2c9eeb00cf0f863a6a18b1600c96a3fd70414f
UUID    8846FE78-2910-4297-AF97-D59BB6F8FC16
```

## 2. OS 根卷的实际布局

从 OS 窗口挂载/解析得到：

```text
/System/Cryptexes/OS
  -> ../../private/preboot/Cryptexes/OS

/System/Cryptexes/App
  -> ../../private/preboot/Cryptexes/App

/private/preboot         空目录

/System/Library/Caches  存在
/System/Library/Caches/com.apple.dyld  不存在

/usr/standalone/firmware  存在
/usr/standalone/firmware/Cryptex1  不存在
```

这解释了 H39/H40 的 `ENOENT`：

- `/System/Cryptexes/OS` 不是普通目录，而是指向 `/private/preboot/Cryptexes/OS` 的符号链接；
- `/private/preboot` 没有 Preboot 卷内容，链接目标不存在；
- `/System/Library/Caches/com.apple.dyld` 也没有建立。

## 3. SystemOS 的实际内容

SystemOS 是单卷 sealed APFS，根下只有：

```text
/System
/usr
```

其中有：

```text
/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64
/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64.01 ... .52
/System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64.symbols
/System/Library/Frameworks/...             378 项
/System/Library/PrivateFrameworks/...      1681 项
/usr/lib/...                               9 项
```

**SystemOS 根下没有 `OS` 子目录。** 因此不能把 SystemOS 挂到 `/private/preboot/Cryptexes` 后还指望 `/System/Cryptexes/OS` 解析到它；挂载点必须是：

```text
/private/preboot/Cryptexes/OS
```

AppOS 同理，挂载点应是：

```text
/private/preboot/Cryptexes/App
```

## 4. launchd 的硬编码路径

从 OS 的 `sbin/launchd` 和 `System/Library/xpc/launchd.plist` 读到：

```text
mount-phase-1
mount-phase-2
/System/Cryptexes/OS/System/Library
/System/Cryptexes/App/System/Library
/private/preboot/Cryptexes/OS/System/Library
/private/preboot/Cryptexes/App/System/Library
cryptex1.boot.os
cryptex1.boot.app
cryptex1.safari-downlevel0*
```

这证实：

1. launchd 同时使用 `/System/Cryptexes/...` 和 `/private/preboot/Cryptexes/...`。
2. `/private/preboot/Cryptexes/OS` 和 `/private/preboot/Cryptexes/App` 是实际挂载点。
3. `cryptex1.boot.os` / `cryptex1.boot.app` 是 Preboot 元数据中的 cryptex 名称。

## 5. Cryptex 元数据

从 IPSW `iPad_Pro_HFR_17.7.11_21H461_Restore.ipsw` 的 j120ap Erase 身份提取：

| 组件 | 路径 | 大小 | SHA-256 |
| --- | --- | ---: | --- |
| `Cryptex1,SystemOS` | `044-76034-038.dmg` | 3,303,014,400 | `b4908cde...` |
| `Cryptex1,SystemVolume` | `Firmware/044-76034-038.dmg.root_hash` | 229 | `adb214bf...` |
| `Cryptex1,SystemTrustCache` | `Firmware/044-76034-038.dmg.trustcache` | 1,871 | `9742f3d5...` |
| `Cryptex1,AppOS` | `044-75188-038.dmg` | 10,485,760 | `52e568e1...` |
| `Cryptex1,AppVolume` | `Firmware/044-75188-038.dmg.root_hash` | 229 | `bd60eb41...` |
| `Cryptex1,AppTrustCache` | `Firmware/044-75188-038.dmg.trustcache` | 767 | `5d9bf23c...` |

SystemTrustCache 的原始 module：

```text
version 2
uuid    fd99dff6-55d8-4340-9c99-803d70fdeeeb
entries 76
bytes   1848
sha256  d121ba89167e03aafd00fa687c30cd8ebc4ccafd10c1408adb2ba3de12403651
```

该 module 已经是 H91 原始 TrustCache bundle 的第二段（`d121ba89...`），所以 **SystemOS trust cache 不是当前缺失项**。

## 6. 结论

H96a18 的 `cryptexd`/`driverkitd` 停点很可能不是“等真实 IM4M”，而是：

```text
SystemOS 已启动
  + /System/Cryptexes/OS 符号链接存在
  + /private/preboot/Cryptexes/OS 没有内容
  + /private/preboot/cryptex1/current 没有内容
  -> launchd/cryptexd 找不到 cryptex 挂载/元数据
  -> driverkitd / launchd 后续服务停住
```

H62 的 `/private/preboot` 挂载和 H96 的 `/` union 都没有命中 `/private/preboot/Cryptexes/OS`，所以都没有解决这条路径。

## 7. 下一步

1. 新增一个 **Preboot overlay**（可来自 Data 卷 `/private/var/preboot`）：
   ```text
   cryptex1/current/os.dmg
   cryptex1/current/os.dmg.root_hash
   cryptex1/current/os.dmg.trustcache
   cryptex1/current/app.dmg...
   Cryptexes/OS/
   Cryptexes/App/
   ```
2. 用 bindfs 把 Data 的 `/private/var/preboot` union 到 `/private/preboot`。
3. SystemOS 改挂到 `/private/preboot/Cryptexes/OS`。
4. `DYLD_SHARED_CACHE_DIR` 改为 `/System/Cryptexes/OS/System/Library/Caches/com.apple.dyld`。
5. 先不依赖 `imageboot-attach`：systemos 已由现有 UFS/APFS 路径按 UUID 挂载，`os.dmg` 只作为 Preboot 元数据占位；如果 launchd 坚持打开 DMG，再补 AppOS/SystemOS 文件后端。
6. 不在这一轮新增 skip，不改 AuthAPFS/Image4/TrustCache 验证。

## 8. 工具

新增 `scripts/prepare-preboot-overlay.py`：

```sh
python3 scripts/prepare-preboot-overlay.py \
  --ipsw /path/to/iPad_Pro_HFR_17.7.11_21H461_Restore.ipsw \
  --output build/preboot-overlay
```

它只提取 root hash / trust cache 和创建 `cryptex1/current`、`Cryptexes/{OS,App}`，不复制 3.3 GiB SystemOS，不改 APFS。

## 9. 已落地的实验代码

本轮已经准备好一个默认关闭的 H103 实验，不改变现有 H91/H96 基线：

- `scripts/prepare-preboot-overlay.py`
  - 从本地或远程 IPSW 只读取 `BuildManifest.plist`、SystemOS/AppOS 的 root hash 与 trust cache；
  - 生成 `cryptex1/current/{os,app}.dmg.{root_hash,trustcache}` 和 `Cryptexes/{OS,App}`；
  - 校验 BuildManifest 摘要；
  - 不复制 3.3 GiB SystemOS。
- `scripts/prepare-owner-data-fixture.py --preboot-overlay DIR`
  - 把 overlay 放到 Data seed 的 `/private/preboot` 对应位置（Data 根即 `/private/var`，目录名为 `preboot`）。
- `patches/xnu/zzzz-owner-preboot-layout.patch`
  - 新增 `XNU_OWNER_PREBOOT_LAYOUT=1`；
  - 在 Data mount 之后、SystemOS mount 之前，用 bindfs 把 `/private/var/preboot` union 到 `/private/preboot`；
  - SystemOS 挂到 `/private/preboot/Cryptexes/OS`；
  - `DYLD_SHARED_CACHE_DIR=/System/Cryptexes/OS/System/Library/Caches/com.apple.dyld`；
  - 不启用 `XNU_OWNER_UNION_SYSTEMOS`，避免继续把 SystemOS 挂到 `/`。
- `.github/workflows/macos-cheeseburger.yml`
  - 新增 `preboot_layout` workflow_dispatch 布尔输入；
  - 为 true 时自动准备 Preboot overlay 和 Data seed。

构建约束由 `scripts/build-xnu-4k-baseline.sh` 检查：

```text
XNU_OWNER_PREBOOT_LAYOUT=1
  requires XNU_OWNER_CACHE_MOUNT=1
  requires XNU_OWNER_CACHE_STARTUP=1
  requires XNU_OWNER_DATA_MOUNT=1
  requires XNU_NATIVE_RAM_BLOCK=1
  requires XNU_OWNER_UNION_SYSTEMOS=0
```

对应 boot 图像仍然是 `fastboot boot`，不写 userdata/GPT。

## 10. 仍需验证

1. `/private/preboot` bindfs union 是否成功，`NATIVE_PREBOOT: status=0 stage=5` 是否出现。
2. SystemOS 是否成功挂到 `/private/preboot/Cryptexes/OS`，`NATIVE_H82_DATA`/`NATIVE_ENV` 是否正常。
3. `cryptexd` 是否仍启动但不再阻塞，launchd 是否能继续到 `backboardd`/`SpringBoard`。
4. `os.dmg` 当前是指向 OS 卷 `/usr/standalone/firmware/Cryptex1/SystemOS.dmg` 的符号链接，而本项目的 21H461 OS 根没有 `Cryptex1` 目录。若 launchd/cryptexd 坚持打开这个文件，需要再加 SystemOS 文件后端或占位方案。
5. AppOS 只准备了 `Cryptexes/App` 目录和元数据，没有挂载 AppOS；Safari/WebKit 相关 job 可能仍失败，但不应阻塞主屏。

## 11. 远程 macOS 构建结果（2026-11-01）

构建机：`lss@192.168.10.103`（macOS 15.8.2 / Xcode 26.3 / Python 3.12）。

关键配置：

```text
XNU_OWNER_PREBOOT_LAYOUT=1
XNU_OWNER_UNION_SYSTEMOS=0
XNU_OWNER_CACHE_MOUNT=1
XNU_OWNER_CACHE_STARTUP=1
XNU_OWNER_CACHE_CARRIER=1
XNU_OWNER_DATA_MOUNT=1
XNU_NATIVE_RAM_BLOCK=1
XNU_OWNER_CRYPTEX_FILES=0
XNU_OWNER_BOOT_MANIFEST=0
XNU_OWNER_DATA_VOLATILE=1
```

Data seed：

```text
UUID       95BE5FCF-BCA7-49C2-80C5-A7BE97DEF40A
packed     ~/h103/owner-data-fixture/owner-data.dbr
sha256     911932c26f4195058d22dc196e0bc928c46b0afb750ade4d8f60f12714be0615
```

引导参数沿用 H96a18 基线，只增加本实验的挂载布局；没有新增 skip：

```text
cpus=2 allow-root-hash-mismatch=1 sheng-pmgr=2
native-trace-amfi-syscall=1 native-trace-trust-cache=1
sheng-disable-jobs=com.apple.nsurlsessiond,com.apple.routined,
com.apple.nehelper,com.apple.chronod,com.apple.accountsd,
com.apple.alsd,com.apple.pfd
```

产物：

| 文件 | 大小 | SHA-256 |
| --- | ---: | --- |
| `kernel.macho` | 52,088,832 | `979260df84f1922726c4e9ce6ac540d023543a16fddee33d50d2f32d86b378fe` |
| `payload-H103-preboot.img` | 79,691,776 | `5bb75401ea53a2144798f15a917b85b15d74ad33f443088ee195acc0552a74da` |
| `boot-H103-preboot.img` | 81,147,904 | `aed7e8d8cf2d7af1402a88dfa46183607325a843f8455961293c6dd8fa2ae287` |
| payload ADT | — | `4b6d2cc33de8af2cc945a3472474ce34d0423bbd48b86e42c00aa1e53775f4d3` |

本地产物：

```text
/home/ls/UniXNU-cheeseburger/out/cheeseburger/boot/boot-H103-preboot.img
/home/ls/UniXNU-cheeseburger/out/cheeseburger/boot/payload-H103-preboot.img
```

内核中已确认包含：

```text
NATIVE_PREBOOT: status=%d stage=%d
/private/preboot/Cryptexes/OS
DYLD_SHARED_CACHE_DIR=/System/Cryptexes/OS/System/Library/Caches/com.apple.dyld
```

已上机：

```text
fastboot boot out/cheeseburger/boot/boot-H103-preboot.img
Sending 'boot.img' (79246 KB)  OKAY
Booting                        OKAY
Finished. Total time: 11.848s
```

设备随后离开 fastboot/USB，没有自动回到 fastboot。最终屏幕内容尚未采集，需要现场确认。

观察目标：`NATIVE_PREBOOT: status=0 stage=5`、`NATIVE_H82_DATA: status=0 stage=4`、`NATIVE_ENV`，以及 launchd 是否越过 `driverkitd is active`。

## 12. H103 首次上机结果（2026-11-01）

最终屏关键日志：

```text
NATIVE_H81_SR: setup pid=1 files=54 mappings=62 ...
NATIVE_H81_SR: cache mount differs from root mount, checking preboot
NATIVE_H81_SR: preboot lookup error=0 cache_mount=... preboot_mount=...
shared_region: ... [1(launchd)] map(... 'dyld_shared_cache_arm64'):
  not on process' root volume nor preboot volume
NATIVE_H81_SR: setup error=-1
shared_region: ... vm_shared_region_start_address() failed
dyld[1]: dyld cache '(null)' not loaded: syscall to map cache into shared region failed
dyld[1]: DYLD_SHARED_CACHE_DIR=/System/Cryptexes/OS/System/Library/Caches/com.apple.dyld
dyld[1]: Library not loaded: /usr/lib/libSystem.B.dylib
```

结论：

1. **Preboot overlay 和 SystemOS 挂载已经生效**：`preboot lookup error=0`，`preboot_mount` 非空；`DYLD_SHARED_CACHE_DIR` 已是新的 `/System/Cryptexes/OS/...`。
2. 失败点不是挂载，而是 `native_owner_shared_cache_carrier()`：
   - 它硬编码 `directory = "/private/preboot/System/Library/Caches/com.apple.dyld"`；
   - 当前 cache 实际在 `/private/preboot/Cryptexes/OS/System/Library/Caches/com.apple.dyld`；
   - 因此 `owner_carrier=false`，随后 `preboot_vp` 的 mount 与 cache mount 不同，shared region 拒绝映射。
3. 其余 launchd 退出是 dyld cache 失败的直接后果，不是新的独立错误。

## 13. H103b 修复与构建

修复：

```c
#if defined(XNU_OWNER_PREBOOT_LAYOUT) && XNU_OWNER_PREBOOT_LAYOUT
	const char *directory = "/private/preboot/Cryptexes/OS/System/Library/Caches/com.apple.dyld";
#else
	const char *directory = "/private/preboot/System/Library/Caches/com.apple.dyld";
#endif
```

同时：

- `vm_unix.o` 增加 `-DXNU_OWNER_PREBOOT_LAYOUT`；
- `vm_unix.o` 的 stamp 包含 preboot layout，切换时强制重编；
- `zzzz-owner-preboot-layout.patch` 加入 `LATE_PATCH` 反应用列表，避免先反 `zzz-shared-region-cache-bypass` 后破坏本补丁上下文。

H103b 产物：

| 文件 | SHA-256 |
| --- | --- |
| kernel | `399ecaa2fec9ecfab5021df2d7e85cff1017b6d309f8e036e1849881b99ba816` |
| payload | `d3d7df410cde3134e4bc97469c9a8d2449cf631f6bbca7358443b2f6929596d0` |
| boot | `3a0f6f196482c382bcb9c82dbf162a046a4dc2d8261682adde8a1fb7329c7566` |

本地路径：

```text
/home/ls/UniXNU-cheeseburger/out/cheeseburger/boot/boot-H103b-preboot.img
/home/ls/UniXNU-cheeseburger/out/cheeseburger/boot/payload-H103b-preboot.img
```

下一次上机重点：确认 `owner_carrier` 放行后 shared region 映射成功，dyld 能加载 `/usr/lib/libSystem.B.dylib`。

## 14. H103b 上机结果（2026-11-01）

H103b 成功越过 dyld cache：

```text
msuearlyboottask: doing boot task
com.apple.xpc.launchd: Doing boot task
FIPSPOST_USER ... all tests PASSED
launchd boot tasks: fsck, mount-phase-1, data-protection,
  finish-obliteration, detect-installed-roots, commit-boot-mode,
  mount-phase-2, init-with-data-volume, fixup-mobile-tap,
  MSEarlyBootTask, fips, keybag, useragentd, dirs_cleaner,
  xpcroleaccountd, init_featureflags, auerlyboot
```

说明：

- shared region / dyld cache 不再报 `not on process' root volume nor preboot volume`；
- `/usr/lib/libSystem.B.dylib` 的 `no dyld cache` 错误消失；
- launchd 已经进入标准 boot task 序列。

新停点：

```text
tzinit: Unable to locate data; resetting to system partition
tzinit: Last resort force_system_dataset() failed: No such file or directory
pid 17 exited -- no exit reason available -- (signal 0, exit 2)
Please go to https://panic.apple.com
```

原因是 Data skeleton 缺少：

```text
/private/var/db/timezone/zoneinfo
```

OS root 的 `/usr/share/zoneinfo` 是指向 `/var/db/timezone/zoneinfo` 的符号链接；Data 挂到 `/private/var` 后，OS root 的 `/private/var/db/timezone` 被隐藏，而 Data seed 没有补这个目录，所以 `tzinit` 找不到时区数据。

## 15. H103c 修复与构建

`scripts/prepare-owner-data-fixture.py` 的 skeleton 模式新增：

- `db/timezone` 目录；
- 从 OS mount 的 `usr/share/zoneinfo.default` 复制 602 个文件到 `db/timezone/zoneinfo`（约 364 KiB）；
- `db/timezone/localtime -> /var/db/timezone/zoneinfo/US/Pacific`。

新 Data seed：

```text
UUID    9E4EDD86-20C5-44A5-A964-35D291D7D956
packed  bc0f10d4650fc322ce69d0eb7b7190428f93305292bd0bd03f21588da8c79253
```

H103c 产物（内核沿用 H103b）：

| 文件 | SHA-256 |
| --- | --- |
| kernel | `399ecaa2fec9ecfab5021df2d7e85cff1017b6d309f8e036e1849881b99ba816` |
| payload | `1318aa1b723f909946440f52c0a3ad6c7620ff725f1063d18985f1edc57a4216` |
| boot | `4c86ac177500f21b72fc14d41ebf4a2c253922addd69c1142e1f2f2fd85a5333` |

本地产物：

```text
/home/ls/UniXNU-cheeseburger/out/cheeseburger/boot/boot-H103c-preboot.img
```

下一步上机确认 `tzinit` 不再退出，并记录下一个 boot task。


## 16. 设备连接环境变更（2026-11-01）

- 构建机不变：`lss@192.168.10.103`（macOS）。
- 设备改接到 Windows：`ls@192.168.10.225`，使用该机 PATH 中的 `fastboot.exe`。
- 镜像存放目录：`D:\Users\ls\Documents\OnePlus5_ios`。
- 后续每轮：macOS 构建 → HTTP/文件传输到 Windows 目录 → Windows 上 `fastboot boot`。
- 不在仓库记录 Windows 密码。

## 17. H103c 上机结果与 H103d 修复（2026-11-01）

H103c 已经越过 root/launchd 前置阶段，但 Data mount 失败：

```text
NATIVE_ROOT_BYPASS: auth=80
NATIVE_H82_DATA: status=2 stage=0 ro=-1 dev=-1 size=0
panic: offline scratch Data mount failed: 2
```

`status=2` 是 `ENOENT`，`stage=0` 表示还没进入设备查找。原因是 H103c 只重新打了 payload/Data seed，内核仍编译旧 UUID：

```text
kernel: 95BE5FCF-BCA7-49C2-80C5-A7BE97DEF40A
Data:   9E4EDD86-20C5-44A5-A964-35D291D7D956
```

H103d 修复：

- 内核 `XNU_OWNER_DATA_VOLUME_UUID` 改为 `9E4EDD86-20C5-44A5-A964-35D291D7D956`；
- 强制重编 `kern_exec.o`；
- 继续使用 H103c 的 timezone Data seed。

H103d 产物：

| 文件 | SHA-256 |
| --- | --- |
| kernel | `2fcb8b5f2173e8b39403ed3f80075e984b0baaaeb4eadf728782d646e8514224` |
| payload | `79240916039008f433b631e1b37575a46a6dae99103a61f89afc966fd71c5692` |
| boot | `219236da0e7b7336b691e831fd71e844126221b857bbc0d4e37bb998cd130e75` |

Windows 目录：

```text
D:\Users\ls\Documents\OnePlus5_ios\boot-H103d-preboot.img
```

已在 Windows 上执行：

```text
fastboot boot "D:\Users\ls\Documents\OnePlus5_ios\boot-H103d-preboot.img"
Sending ... OKAY
Booting   OKAY
```

等待最终屏幕确认 Data mount 和 tzinit。

## 18. H103d 上机结果与 H103e 诊断（2026-11-01）

H103d 已经不再报 Data UUID mismatch，launchd 继续执行 boot task；新 `tzinit` 错误变为：

```text
tzinit: Unable to locate data; resetting to system partition
tzinit: Unable to create directory with attributes: Read-only file system
tzinit: Last resort force_system_dataset() failed: Read-only file system
pid 17 exited -- no exit reason available -- (signal 0, exit 2)
```

Data seed 中已确认存在：

```text
db/timezone/zoneinfo      （602 个文件）
db/timezone/localtime -> /var/db/timezone/zoneinfo/US/Pacific
```

因此问题收窄为两种可能：

1. `/private/var` 在 tzinit 运行时不是 Data 的 RW 挂载，而是 OS root 的只读目录；
2. Data 是 RW，但 tzinit 期望的 `/var/db/timezone` 数据集布局（`tz/`、`tz_latest`、`icutz`）不存在，它回退写系统目录时撞上只读。

H103e 只增加诊断，不改变行为：

- `zzzzz-h103-data-diag.patch`；
- 在 `tzinit` 退出时打印：
  ```text
  NATIVE_H103_DATA: status=... ro=... dev=...
  NATIVE_H103_VAR: flags=... fstype=...
  ```

H103e 产物：

| 文件 | SHA-256 |
| --- | --- |
| kernel | `0ef659ae57587396e24af61699e2d84c5f3854752a689d1500ce35247cf80b73` |
| payload | `24601447421f6577023506dea68698512bcc04959461a48ac17e99d9ff7d36f2` |
| boot | `45e5a3ca42db952e30d3291119e6ee7e37860cdccfdbb46aacb251c52fc25bc9` |

Windows 路径：

```text
D:\Users\ls\Documents\OnePlus5_ios\boot-H103e-preboot.img
```

## 19. H103f：tzinit 数据集布局 + Data 诊断（2026-11-01）

H103d 的 `tzinit` 报：

```text
tzinit: Unable to create directory with attributes: Read-only file system
tzinit: Last resort force_system_dataset() failed: Read-only file system
```

为一次拿到两件事的证据，H103f 在 H103e 诊断内核基础上，把 Data seed 的时区布局改成 tzinit 期望的版本化数据集：

```text
/private/var/db/timezone/
  tz/44l/zoneinfo/...
  tz_latest -> tz/44l
  zoneinfo -> tz_latest/zoneinfo
  icutz -> /usr/share/icu
  localtime -> /var/db/timezone/zoneinfo/US/Pacific
```

数据来自 OS root 的 `/usr/share/zoneinfo.default`（602 个文件，`+VERSION` = `44l`）。

H103f Data seed：

```text
UUID    1A4CE9DF-34B6-4B52-9744-5052A9D20B23
packed  699c39b26147386d7cd8adcf7d549d3db318ff4d52512dca00cffa8adb70ca54
```

H103f 产物：

| 文件 | SHA-256 |
| --- | --- |
| kernel | `509800409818abf4ae9ed9c19e694b5c6170bb99d826e668c6da8967c2263275` |
| payload | `ede0771652453e29fcef0d13c5b86bd68fb7c3a0e0003169ef2bbbe6ed70adfc` |
| boot | `5b4ed54041d13e27dfa3f69c637087ba0414c1a23ec4c2f918489354337cd613` |

已从 Windows boot：

```text
fastboot boot "D:\Users\ls\Documents\OnePlus5_ios\boot-H103f-preboot.img"
Sending ... OKAY
Booting   OKAY
```

若 `tzinit` 仍失败，屏幕会出现：

```text
NATIVE_H103_DATA: status=... ro=... dev=...
NATIVE_H103_VAR: flags=... fstype=...
```

据此可直接判断是 Data 挂载问题还是 tzinit 路径问题。

## 20. H103f 上机结果：tzinit 通过，回到 driverkitd（2026-11-01）

H103f 最终屏不再出现 `tzinit` 错误，launchd boot task 序列继续；最后可见：

```text
KextLog: Loading FileSet KC(s)
KextLog: driverkitd is active
NATIVE_UFS_PROGRESS: ... lastWin=OS/SystemOS ...
```

没有 panic，没有自动回到 fastboot。说明：

- Preboot/Cryptex 布局 + owner carrier + dyld cache + tzinit 都已通过；
- 新停点回到旧的 `FileSet KC` / `driverkitd is active`；
- 当前内核没有 `NATIVE_EXEC` 日志，无法确认 `cryptexd`/`eligibilityd` 是否启动、是否退出。

H103f 产物：

| 文件 | SHA-256 |
| --- | --- |
| kernel | `509800409818abf4ae9ed9c19e694b5c6170bb99d826e668c6da8967c2263275` |
| payload | `ede0771652453e29fcef0d13c5b86bd68fb7c3a0e0003169ef2bbbe6ed70adfc` |
| boot | `5b4ed54041d13e27dfa3f69c637087ba0414c1a23ec4c2f918489354337cd613` |

## 21. H103g：进程/退出观测（2026-11-01）

新增 `zzzzzz-h103-proc-diag.patch`：

- `NATIVE_EXEC: pid=... ppid=... comm=... argv0=... argv1=...`
- 对 `cryptexd`/`eligibilityd`/`launchd`/`xpcproxy`/`driverkitd`/`backboardd` 记录：
  ```text
  NATIVE_EXIT: pid=... ppid=... comm=... status=... ns=... code=...
  ```

H103g 产物：

| 文件 | SHA-256 |
| --- | --- |
| kernel | `397637af7df6841a50f67e841b471d632f56b6e5c67db06980ddf5b11b60d910` |
| payload | `eef3bea8407ed55d9c8722947cbb3f10f513edb91c19c658b784117ef74710b9` |
| boot | `7b60d3fdd4d876e69cfd324f562bf87db614fb2d46a6ac8c2de96ee6370e1aa5` |

Windows 路径：

```text
D:\Users\ls\Documents\OnePlus5_ios\boot-H103g-preboot.img
```

下一步 boot H103g，确认：

1. `cryptexd`、`eligibilityd` 是否出现 `NATIVE_EXEC`；
2. 是否出现 `NATIVE_EXIT`；
3. `driverkitd` 是否在等某个 `FileSet KC` kext。

## 22. H103g 上机结果与 H103h 进程快照（2026-11-01）

H103g 已经能看到完整 exec 序列：

```text
NATIVE_EXEC: pid=65 ppid=1 comm=driverkitd
NATIVE_EXEC: pid=70 ppid=1 comm=AppleCredentialManager...
NATIVE_EXEC: pid=73 ppid=1 comm=rapportd
NATIVE_EXEC: pid=74 ppid=1 comm=useractivityd
NATIVE_EXEC: pid=76 ppid=1 comm=fseventsd
NATIVE_EXEC: pid=77 ppid=1 comm=IOHFB_FDR_Loader
NATIVE_EXEC: pid=82 ppid=1 comm=xpcproxy argv1=com.apple.logd_helper
NATIVE_EXEC: pid=84 ppid=1 comm=logd_helper
```

之后屏幕停止刷新，停在：

```text
KextLog: Loading FileSet KC(s)
KextLog: driverkitd is active
NATIVE_UFS_PROGRESS: ... winFlight=1 lastWin=SystemOS/OS
```

可见窗口内没有 `cryptexd`/`eligibilityd`，也没有 `NATIVE_EXIT`/panic。判断：

- `driverkitd` 已启动，launchd 继续拉起一批 daemon；
- 但 `cryptexd`/`eligibilityd` 之前就停住；
- 停点仍是 `driverkitd` / `FileSet KC`，需要确认是 launchd 等 driverkitd，还是 UFS 读停住。

H103h 增加一个 **thread_call 进程快照**：

- 在 `launchd`/`driverkitd` exec 时调度；
- 90 秒后打印：
  ```text
  NATIVE_PROC_BEGIN
  NATIVE_PROC: pid=... ppid=... comm=...
  ...
  NATIVE_PROC_END
  ```

H103h 产物：

| 文件 | SHA-256 |
| --- | --- |
| kernel | `f9b73b9c4d268df4224dfd38aa13b50ae048f32ff216b91b76c12f64da7f60c2` |
| payload | `61198f9d00366b4d1c21156984a255f5cf152934040f8fabdb4bd9ac1cba0953` |
| boot | `a86c5a6a360cbb3055539141f2ca6165c81e6c2ffff041766dd1c073092aa143` |

Windows 路径：

```text
D:\Users\ls\Documents\OnePlus5_ios\boot-H103h-preboot.img
```

已在 Windows boot。下一步等待 90 秒后的 `NATIVE_PROC` 输出，确认 `cryptexd`/`eligibilityd`/`driverkitd` 的实际 PID 和父进程。

## 23. H103h 进程快照结果（2026-11-01）

H103h 的 `thread_call` 快照没有在最终屏出现；屏幕在 3 分钟内只持续输出：

```text
NATIVE_UFS_WINDOW: SystemOS ...
NATIVE_UFS_PROGRESS: ... lastWin=SystemOS ... winBytes≈3.2 GiB ... winFlight=1
```

没有 `NATIVE_PROC_BEGIN`/`NATIVE_PROC_END`/panic。可能原因：

- `thread_call` 没有在这个停点执行（或输出被刷掉）；
- 或系统停在 UFS 读，快照线程也无法输出。

H103g 的 exec 日志已经确认：

```text
driverkitd
AppleCredentialManager
rapportd
useractivityd
fseventsd
IOHFB_FDR_Loader
xpcproxy/logd_helper
logd_helper
```

之后没有 `cryptexd`/`eligibilityd`，也没有 `NATIVE_EXIT`。

## 24. H103i：一次性跳过 driverkitd 的诊断（2026-11-01）

为了判断 launchd 是否卡在 `driverkitd`，H103i 只改 payload，不重建内核：

```text
sheng-disable-jobs=...,com.apple.driverkitd
```

内核仍是 H103h：

```text
kernel f9b73b9c4d268df4224dfd38aa13b50ae048f32ff216b91b76c12f64da7f60c2
```

H103i 产物：

| 文件 | SHA-256 |
| --- | --- |
| payload | `2aefb03b484878d159ed3cb097818c9f1357401f9102b18483d65c2208b1b721` |
| boot | `7129e8b85a340a0f3ee2eb353486b1041f54f089009e2fb46994e7b54a1a6646` |

Windows 路径：

```text
D:\Users\ls\Documents\OnePlus5_ios\boot-H103i-skipdk.img
```

这是**一次性诊断**，不是修复。判读：

- 若跳过 driverkitd 后出现 `cryptexd`/`eligibilityd`，说明 launchd 在等 driverkitd；
- 若更早 panic/停止，说明 driverkitd 是必需服务，回到 UFS/FileSet KC 路径；
- 无论结果，后续都不把该 skip 当作最终方案。

## 25. H103i 结果：跳过 driverkitd 会继续，但仍无 cryptexd（2026-11-01）

H103i（唯一变量：`sheng-disable-jobs=...,com.apple.driverkitd`）的最终屏：

```text
NATIVE_EXEC: pid=93 ppid=1 comm=livedriven
NATIVE_EXEC: pid=94 ppid=1 comm=xpcproxy argv1=com.apple.coresymbolicationd
NATIVE_EXEC: pid=95 ppid=1 comm=xpcproxy argv1=com.apple.donotdisturbd
NATIVE_EXEC: pid=97 ppid=1 comm=donotdisturbd
NATIVE_EXEC: pid=99 ppid=1 comm=... datanator / dataaccess
NATIVE_H66_FCNTL ...
NATIVE_CACHE_SIG: ...
NATIVE_UFS_PROGRESS ...
```

结论：

- 跳过 `driverkitd` 后 launchd 确实继续启动了一批服务（`coresymbolicationd`、`donotdisturbd`、`datanator` 等）；
- 但仍没有 `cryptexd`/`eligibilityd`；
- 这更像 **cryptexd 依赖 driverkitd**，而不是跳过 driverkitd 能解决问题；
- H103i 只作为对照，不作为最终方案。

## 26. H103j：Preboot 挂载 + SystemOS root union（2026-11-01）

H96a18 曾用 `XNU_OWNER_UNION_SYSTEMOS=1` 把 SystemOS union 到 `/`，到达 cryptexd；H103 只挂到 `/private/preboot/Cryptexes/OS`，launchd/dyld 路径正确，但 driverkitd 可能仍需要 root 上的 `/System/DriverKit` 视图。

H103j 保留 H103 的 Preboot/Cryptex 布局，同时增加一次 **SystemOS root union**：

```c
if (!error) {
    struct mount_args root_request = request;
    root_request.flags = MNT_RDONLY | MNT_UNION;
    copyout("/", root_request.path, 2);
    root_error = mount(p, &root_request, &root_result);
    printf("NATIVE_H103_ROOT_UNION: %d\n", root_error);
}
```

构建开关：

```text
XNU_OWNER_PREBOOT_LAYOUT=1
XNU_OWNER_PREBOOT_ROOT_UNION=1
```

boot args 不使用 `com.apple.driverkitd` skip，恢复 driverkitd。

H103j 产物：

| 文件 | SHA-256 |
| --- | --- |
| kernel | `e71fe93c3bb882f1d64868b4630046612da7b0294487d05de48fb300961eb7c5` |
| payload | `b44703e035cd86a912efb871b6150a2b7242080966426c99bf243d8a40d26953` |
| boot | `59c5ec427b300081e310f98d266c5b73f500b27a12425c122f042fd31de906eb` |

Windows 路径：

```text
D:\Users\ls\Documents\OnePlus5_ios\boot-H103j-rootunion.img
```

已 boot。判读：

- `NATIVE_H103_ROOT_UNION: 0` 且 driverkitd 不再 stall、cryptexd 出现 → root union 是缺失项；
- root union 失败或 driverkitd 仍 stall → 需要转向 FileSet KC/UFS 读路径。

## 27. H103j 失败原因与 H103k 修复（2026-11-02）

H103j 上机停在：

```text
NATIVE_H82_DATA: status=2 stage=0 ro=-1 dev=-1 size=0
panic: offline scratch Data mount failed: 2
```

原因仍是 UUID：远程 `run-h103-build.sh` 中 `XNU_OWNER_DATA_VOLUME_UUID` 被恢复成旧值：

```text
95BE5FCF-BCA7-49C2-80C5-A7BE97DEF40A
```

而 Data seed 是：

```text
1A4CE9DF-34B6-4B52-9744-5052A9D20B23
```

H103k 修复：

- 内核 UUID 改为 `1A4CE9DF-34B6-4B52-9744-5052A9D20B23`；
- 强制重编 `kern_exec.o`；
- 保留 `XNU_OWNER_PREBOOT_ROOT_UNION=1`。

H103k 产物：

| 文件 | SHA-256 |
| --- | --- |
| kernel | `ca2f40bf8d5e96c0d2619098fd11eb62461e15d4e860f1adab148fedae1c809f` |
| payload | `0fdf0f4d630d6ff3c13c9c41851cbcb8d99cc90f894fb7024a6ddc59ae6ea760` |
| boot | `235bdd25e322c3f6f086f741d4dda429ee06fab02a6c8db0e4215fe038044882` |

Windows 路径：

```text
D:\Users\ls\Documents\OnePlus5_ios\boot-H103k-rootunion.img
```

已 boot，等待：

```text
NATIVE_H103_ROOT_UNION: 0
```

## 28. H103k/H103l：root union 改为 bindfs（2026-11-02）

H103k（APFS 二次 mount 到 `/` + MNT_UNION）仍然停在：

```text
KextLog: Building FileSet KC(s)
KextLog: Loading FileSet KC(s)
KextLog: driverkitd is active
```

`NATIVE_H103_ROOT_UNION` 没有出现在最终可见窗口，无法区分是 mount 失败还是成功但 driverkitd 仍卡。因此 H103l 改为 **bindfs root union**，不再次挂 APFS：

```c
source = "/private/preboot/Cryptexes/OS";
mount("bindfs", "/", MNT_RDONLY | MNT_UNION, source);
```

这样 `/System/DriverKit`、`/System/Library/...` 等路径从已挂载的 SystemOS 视图叠加到 root，同时保留 Preboot/Cryptex 布局。

H103l 产物：

| 文件 | SHA-256 |
| --- | --- |
| kernel | `eaa1dbd6060a597af4c910b22475a687a37323a7eb9498333ac1bb86d0fae787` |
| payload | `fb84ee1102735a1cb64735393cca522ab3d7dd78a5adf71509e1b6a358d5dbd2` |
| boot | `b9163e77b30bd473ace944225663ed3467da38387508836944e1b20bda92f2fa` |

Windows 路径：

```text
D:\Users\ls\Documents\OnePlus5_ios\boot-H103l-rootbindfs.img
```

已 boot。判读：

- `NATIVE_H103_ROOT_UNION: 0` 且 driverkitd 不再 stall → root bindfs 是缺失项；
- 非 0 → bindfs mount 失败，查 entitlement/源路径；
- 0 但仍 stall → driverkitd 的 FileSet KC 不是 root 视图问题，转 UFS/FileSet KC 调用链。

## 29. H103l 失败：bindfs root union 破坏 dyld/launchd（2026-11-02）

H103l 没有出现 `NATIVE_H103_ROOT_UNION`，而是 launchd 直接退出：

```text
NATIVE_LAUNCHD_ABORT_DESC: Library not loaded: /usr/lib/libSystem.B.dylib
  Referenced from: <...>/sbin/launchd
  Reason: tried: '/usr/lib/libSystem.B.dylib' (no such file, no dyld cache)
NATIVE_EXIT: pid=1 ppid=0 comm=launchd status=0x6 ns=6 code=0x1
pid 1 exited -- exit reason namespace 6 subcode 0x1
```

结论：

- **bindfs root union 不可用**：它破坏了 root `/usr/lib` / dyld cache 的可见性或 root 卷判断；
- H103l 是回归，不能作为后续基线；
- 回退到 H103f/H103g（Preboot/Cryptex 挂载，不做 root union）。

H103l 产物仅保留作失败记录：

```text
kernel eaa1dbd6060a597af4c910b22475a687a37323a7eb9498333ac1bb86d0fae787
boot   b9163e77b30bd473ace944225663ed3467da38387508836944e1b20bda92f2fa
```

## 30. 后续方向

root union 两条路都失败：

- APFS 二次 mount：driverkitd 仍停在 FileSet KC；
- bindfs root union：launchd 找不到 libSystem/dyld cache。

当前应回到 **无 root union 的 H103g 基线**，集中解决：

1. `driverkitd` 的 FileSet KC 到底在等哪个文件/服务；
2. 为什么它持续读 SystemOS/OS UFS 而没有新的 exec；
3. `cryptexd` 是否依赖 driverkitd 的 Mach service。

下一步只做读/追踪，不改变挂载布局：

- 在 `NativeUfsProbe::readWindowBulk`/`NATIVE_UFS_WINDOW` 增加调用者 PID/comm；
- 或在 driverkitd exec 后调度一个 **panic 进程快照**，确保输出不被滚屏；
- 不再尝试 root union、不再新增 cryptexd/eligibilityd skip。

## 31. H103m2：UFS 调用者 PID/comm 追踪（2026-11-02）

回到 H103g 无 root union 基线，给 `NativeUfsProbe` 的窗口日志加调用者：

```text
NATIVE_UFS_WINDOW: ... caller=<comm>/<pid>
```

首次构建时 archive 是旧的（`build/native-ufs-probe` 未重建），预链接内核里仍是旧格式；H103m2 手工执行：

```sh
python3 scripts/build-arm-driver.py NativeUfsProbe
```

后重新链接，预链接内核已包含 `caller=%s/%d`。

H103m2 产物：

| 文件 | SHA-256 |
| --- | --- |
| kernel | `b0fb38e9116c7c941b41ee77bac89f5ed704e58f4acb65a75063f5c35baf77d1` |
| payload | `3276752c39055a14013bf1e5ca59f00714349a60f9a1a8cc62b1184739cae153` |
| boot | `45163f34c59447802c97826eba48f53ec49e73d2adda114f1842ad72732ffa36` |

Windows 路径：

```text
D:\Users\ls\Documents\OnePlus5_ios\boot-H103m2-ufscaller.img
```

已 boot。判读：

- 若持续 SystemOS 读的 `caller=driverkitd/<pid>`，说明 driverkitd 在扫描/加载 FileSet KC；
- 若 `caller=launchd/<pid>`，说明 launchd 在等文件；
- 若 `caller=kernel_task/0`，说明是 pagein/内核预读，不是用户态主动读。

## 32. H103m2 结果：UFS 调用者确认是 driverkitd（2026-11-02）

H103m2 最终屏出现了 caller 字段：

```text
NATIVE_UFS_WINDOW: SystemOS req=53248 block=c4ea8 count=8 abs=17f540000 lba=19f715 caller=driverkitd/62
NATIVE_UFS_WINDOW: SystemOS req=53248 done=4096 ok=1 ticks=86
  read=0 write=73 abs=17f540000 lba=19f715 caller=driverkitd/62

KextLog: driverkitd is not active
KextLog: Loading FileSet KC(s)
KextLog: driverkitd is active
NATIVE_UFS_PROGRESS: ... winBytes=3233879040 winFlight=1 lastWin=SystemOS
```

同时还能看到：

```text
caller=MobileGestaltHelper/86
```

结论：

- **持续读 SystemOS 的是 `driverkitd`（PID 62）**，不是 launchd/kernel_task；
- `driverkitd` 正在加载 FileSet KC，读的是 SystemOS 窗口；
- `driverkitd is not active -> Loading FileSet KC(s) -> driverkitd is active` 说明它在 FileSet KC 流程中；
- 之后没有 `cryptexd`/`eligibilityd`，launchd 很可能在等 FileSet KC/ driverkitd 完成。

H103m2 产物：

```text
kernel b0fb38e9116c7c941b41ee77bac89f5ed704e58f4acb65a75063f5c35baf77d1
boot   45163f34c59447802c97826eba48f53ec49e73d2adda114f1842ad72732ffa36
```

下一步：

- 若 UFS 读仍在推进，等待 FileSet KC 完成；
- 若停住，需要 UFS 请求级 stall/panic 或定位 FileSet KC 的具体路径/文件；
- 不再尝试 root union。

## 33. H103m2 的关键结论与下一步（2026-11-02）

H103m2 直接证明了：

```text
NATIVE_UFS_WINDOW: SystemOS ... caller=driverkitd/62
```

并出现：

```text
KextLog: driverkitd is not active
KextLog: Loading FileSet KC(s)
KextLog: driverkitd is active
```

同时没有 `NATIVE_UFS_STALL`/panic；心跳线程没有触发 UFS stall。说明：

- 持续读 SystemOS 的是 **driverkitd**；
- 不是 launchd 随机等待，也不是单纯内核 pagein；
- driverkitd 正在处理 FileSet KC；
- launchd 在 cryptexd 之前停住。

`driverkitd` 二进制字符串显示它会加载：

```text
BootKernelCollection.kc
AuxiliaryKernelCollection.kc
SystemKernelCollection
```

而当前 OS root 没有：

```text
/System/Library/KernelCollections
```

SystemOS 也没有 KernelCollections。因此下一步的最高优先级是：

1. 只读确认 driverkitd 的 `KernelPath`/`BootKernelCollection` 配置和它实际打开的文件；
2. 如果确实缺 `BootKernelCollection.kc`/`AuxiliaryKernelCollection.kc`，考虑从原始 kernelcache 派生一个窄 overlay，只覆盖 `/System/Library/KernelCollections`，不要再次 root union；
3. 不跳过 driverkitd、不新增 cryptexd/eligibilityd skip。

H103m2 是当前最好的诊断基线：

```text
kernel b0fb38e9...
boot   45163f34...
```

## 34. H103m3：补齐 driverkitd Data 目录（2026-11-02）

`driverkitd` 二进制配置要求：

```text
ThirdPartyDriverKitStagingDirectory = /private/var/db/driverkitd/SystemExtensions
ApprovalsDatabasePath               = /private/var/db/driverkitd/driver-approvals.plist
ApplicationCachePath                = /private/var/db/driverkitd/applications.plist
KernelExtensionManagement Staging   = /private/var/db/KernelExtensionManagement/Staging
```

原 Data skeleton 没有 `db/driverkitd` 和 `db/KernelExtensionManagement`。H103m3 在不改变 Data UUID 的前提下，直接把下面目录加进 `owner-data-fixture3` 的 APFS：

```text
/private/var/db/driverkitd/SystemExtensions
/private/var/db/KernelExtensionManagement/Staging
/private/var/tmp/com.apple.driverkitd/DriverExtensions
```

新 packed Data seed：

```text
sha256 af96cfdf13e41bd74c71810da8fe4a1338393d3b766ab39faf0e1bc2402cd475
```

H103m3 产物（内核沿用 H103m2）：

| 文件 | SHA-256 |
| --- | --- |
| kernel | `b0fb38e9116c7c941b41ee77bac89f5ed704e58f4acb65a75063f5c35baf77d1` |
| payload | `29fde9c74340df404f6d2e65c2cd6bed475cb3e947e9cf02a3f0edb065d297c5` |
| boot | `62ce9ab467427a8f99ee2f67aeca9b9a689fe972878b8f8a787dfff32edcc435` |

Windows 路径：

```text
D:\Users\ls\Documents\OnePlus5_ios\boot-H103m3-dkdirs.img
```

已 boot。判读：driverkitd 是否不再反复 FileSet KC，或是否出现新的 approvals/state 错误。

## 35. H103m3 结果：driverkitd Data 目录后用户态明显前进（2026-11-02）

H103m3 最终屏出现大量新的 `NATIVE_EXEC`，包括一批此前没有的服务（截图可辨识的有 `pkd`、`securityd`、`authd`、`ctkd`、`cloudpaird`、`mobileassetd`、`distnoted` 等），同时仍有：

```text
NATIVE_UFS_WINDOW: SystemOS ... caller=driverkitd/62
```

说明：

- 补 `/private/var/db/driverkitd`、`KernelExtensionManagement/Staging`、`tmp/com.apple.driverkitd/DriverExtensions` 后，launchd 明显继续启动更多用户态服务；
- `driverkitd/62` 仍在读 SystemOS，FileSet KC 可能仍在后台完成；
- 最终是否停住、cryptexd 是否出现，需要下一张更清晰的屏幕确认。

H103m3 产物：

```text
kernel b0fb38e9116c7c941b41ee77bac89f5ed704e58f4acb65a75063f5c35baf77d1
payload 29fde9c74340df404f6d2e65c2cd6bed475cb3e947e9cf02a3f0edb065d297c5
boot   62ce9ab467427a8f99ee2f67aeca9b9a689fe972878b8f8a787dfff32edcc435
```

## 36. H103m3 最终结果：SpringBoard 已启动（2026-11-02）

最终屏关键行：

```text
NATIVE_UFS_WINDOW: SystemOS req=33792 block=f8320 count=8
  abs=17f540000 lba=19d5a4 caller=SpringBoard/27
NATIVE_UFS_WINDOW: SystemOS req=33792 done=4096 ok=1
  ticks=93 read=0 write=82 abs=17f540000 lba=19d5a4 caller=SpringBoard/27

NATIVE_UFS_WINDOW: SystemOS req=34816 ...
  caller=driverkitd/62
```

同时可见大量服务已经 exec，包括：

```text
SpringBoard/27
logd_helper
distnoted
...
```

结论：

- **H103m3 首次到达 SpringBoard**，已经越过 H96a18 的 cryptexd/eligibilityd 停点；
- `driverkitd` 仍在后台读 SystemOS，但不再是唯一停点；
- 当前没有 UI/桌面，因为构建仍是：
  ```text
  XNU_NATIVE_MOBILE_DISPLAY=0
  XNU_NATIVE_ADRENO=0
  ```
  且 legacy display 未启用；
- 下一步是显示/WindowServer/backboardd 链路，而不是继续 cryptexd。

H103m3 是当前最远基线：

```text
kernel b0fb38e9...
payload 29fde9c7...
boot   62ce9ab4...
```

## 37. H103m3 最终结果：SpringBoard 已启动（2026-11-02）

最终屏明确出现：

```text
NATIVE_UFS_WINDOW: SystemOS req=33792 block=f8320 count=8
  abs=17f540000 lba=19d5a4 caller=SpringBoard/27

NATIVE_UFS_WINDOW: SystemOS req=33792 done=4096 ok=1
  ticks=93 read=0 write=82 abs=17f540000 lba=19d5a4 caller=SpringBoard/27
```

同时还有：

```text
NATIVE_UFS_WINDOW: SystemOS ... caller=driverkitd/62
```

结论：

- **H103m3 首次到达 SpringBoard**，已经越过 H96a18 的 cryptexd/eligibilityd 停点；
- `driverkitd` 仍在后台读 SystemOS，但不再是唯一停点；
- 最终没有桌面/UI，是因为当前构建仍关闭显示：
  ```text
  XNU_NATIVE_MOBILE_DISPLAY=0
  XNU_NATIVE_ADRENO=0
  XNU_NATIVE_LEGACY_DISPLAY=0
  ```
- 下一步从 cryptexd/FileSet KC 转向 **显示/WindowServer/backboardd**。

H103m3 是当前最远基线：

```text
kernel b0fb38e9...
payload 29fde9c7...
boot   62ce9ab4...
```

## 38. H103n：OP5 原始 Legacy Framebuffer 首次编入内核（2026-11-02）

### ABI 审计结果

OP5 21H461 原始 kernelcache 中的 `com.apple.iokit.IOMobileGraphicsFamily`：

```text
__TEXT_EXEC 0xfffffff00676c000 size 0x36f74
SHA-256    2e044ee68c1650397e1435234ca6459206262e4c7ed709cb7131c6f664f9bd9b
```

该 executable 与 sheng 参考中的 family 完全相同；OP5 的 prelink slide 为
`0x5000000`，因此 native kernel 中的地址为原始地址 + `0x5000000`：

```text
Legacy constructor 0xfffffff0067865d8 -> 0xfffffff00b7865d8
Legacy vtable      0xfffffff006f38560 -> 0xfffffff00bf38560
gMetaClass         0xfffffff007b35940 -> 0xfffffff00cb35940
abstract slots     211,213,245,246,306,307,337,338,345
instance bytes     0x18a0
```

### 新增构建

- `board.toml` roots 增加 `com.apple.iokit.IOMobileGraphicsFamily`；prelink 自动拉入
  `IOSurface`、`IOAVFamily`、`AppleM2ScalerCSCDriver`，共 29 个 component。
- `scripts/audit-mobile-graphics-legacy-op5.py`：从 21H461 kernelcache 定位
  constructor/vtable/abstract slots。
- `scripts/build-op5-legacy-display.py`：生成 `OriginalLegacyFramebufferABI.h`，
  编译 `NativeLegacyDisplay`/`NativeGOPPresenter`/`LegacyDisplayProbe`，绑定原始
  family/IOSurface 符号，产出 `libNativeLegacyDisplay.late.a`。
- `XNU_NATIVE_LEGACY_DISPLAY=1` 链接该 archive。
- H103n 启动参数新增：
  ```text
  offline-mobile-graphics-load=1
  offline-legacy-gop=1
  offline-legacy-phase=4
  offline-legacy-register=1
  offline-legacy-validate=1
  offline-legacy-transfer=1
  offline-legacy-retire=1
  offline-legacy-pm=1
  ```

### H103n 产物

```text
kernel  e3b3c356ff18c98a0365d2fd0b0c225496c181595a77ad87858e9bf8890a395c
payload f66dcdec71bf1d34ba6b5a22c0876df6e1e19a30b4a779274b7d223feaf02acf
boot    05e184d1e64a4a2a0a2295d0c1ade10b66e3d82785c98bde62dc9cdb1d58bc21
archive 0326dcadb64d515b03ecaaf8344dbb593d5dc9afa0ce46477369f94201c42260
```

boot 镜像已传至 Windows `D:\Users\ls\Documents\OnePlus5_ios\boot-H103n-legacy.img`，
并已 `fastboot boot`。下一步看屏幕是否出现 `NativeLegacyDisplay` stage、
`backboardd`/`WindowServer`，或 display guard panic。

## 39. H103w–H103ad：cpus=1、GOP 自检与显示客户端诊断（2026-11-02）

### H103w：cpus=1 对照

H103v 同内核，只把 payload 的 `cpus=2` 改为 `cpus=1`。

结果：

- 每次都能到达：
  ```text
  KextLog: driverkitd is active
  ```
- 说明之前概率性停在 KextLog 之前主要是 SMP/早期启动时序竞态。
- 但 userland 显示客户端仍不稳定：
  - 有时 `display calls=0`；
  - 有时 `display calls=1`，但 `NATIVE_LEGACY_CLIENTS: C0 calls=0`。

产物：

```text
boot 79dc3a1720c5e552194f79a709ecf810205c395d331c2fcb9d69f7055d6e51aa
```

### H103x：GOP 自检图案

在 `NativeLegacyDisplay::startDisplay` 成功后，UFS 心跳每 10 秒在右上角画
256×256 品红色块。

结果：

- H103y/H103z/aa 均可见品红色块；
- 证明：
  ```text
  GOP mapping -> framebuffer -> 屏幕
  ```
  完全打通。

### H103y/H103z/H103aa 的状态行

新增：

```text
NATIVE_LEGACY_STATE: load=... stage=... reg=... surface=... probe=... dt=... start=... calls=... pattern=...
```

H103y：

```text
load=0 stage=10 reg=1 surface=0/1 probe=1 dt=1 start=1 calls=0 pattern=1
```

H103z 去掉显式 `registerService()` 与 `IOClass` 后：

```text
load=0 stage=10 reg=1 ... calls=0 pattern=1
```

说明 base `start` 自己会注册服务，显式注册不是必要动作。

H103aa 只设置：

```text
IOClass = IOMobileFramebufferLegacy
```

结果稳定为：

```text
load=0 stage=10 reg=1 surface=0/1 probe=1 dt=1 start=1 calls=1 pid=78 status=0 pattern=1
NATIVE_LEGACY_CLIENTS:
C0 pid=78 calls=0 fails=0 regs=0
```

结论：`IOClass=IOMobileFramebufferLegacy` 能稳定让 userland 找到并打开服务。

### H103ab：禁用 IOMFB loader jobs（失败）

payload 在 `sheng-disable-jobs` 中加入：

```text
com.apple.iomfb_fdr_loader
com.apple.iomfb_bics_daemon
```

结果：

- panic：
  ```text
  sheng watchdog timeout failed: 222
  ```
- 不能同时禁用这两个 job；该对照作废。

### H103ac：sheng-vsync=120（失败）

H103aa 同内核，payload 加：

```text
sheng-vsync=120
```

结果：

```text
display calls=0 pid=0 status=ffffffff pattern=1
```

- `IOMFBDisplayRefresh`/vsync 不是当前阻塞点；
- 该参数反而改变启动时序，使显示客户端不再打开；
- 回到 H103aa 配置。

### H103ad：恢复 H103aa 基线

无 `sheng-vsync`，保持 `IOClass=IOMobileFramebufferLegacy`、`cpus=1`。

结果：

```text
display calls=1 pid=70 status=0
NATIVE_LEGACY_CLIENTS:
C0 pid=70 calls=0 fails=0 regs=0
```

同时 UFS caller 可见：

```text
caller=SpringBoard/27
caller=backboardd/55
```

说明：

- `SpringBoard/27` 和 `backboardd/55` 都活着；
- 但打开显示的是 **pid 70**，不是 backboardd/55；
- pid 70 很可能是 `IOMFB_FOR_Loader`；
- 该客户端打开后没有发起任何 selector（`C0 calls=0`）；
- backboardd 可能被 loader 挡住，尚未自己打开显示。

### 当前基线

```text
kernel  b6e6653c44b1fce84534b6cf7186a3748411d096d6d05e5fabd2e949d95b8d8e
payload 965999e770257c244f1db83a97efc8b91ff8d8398b203de95743f1bb81fc9948
boot    7c0032f0994fc215aa9963de5e73b5c9d47a564514c86706eb37499ec309b2b9
cpus    1
IOClass IOMobileFramebufferLegacy
```

### 下一步 H103ae

H103aa 同内核，只禁用：

```text
com.apple.iomfb_fdr_loader
```

保留 `com.apple.iomfb_bics_daemon`，观察：

- `calls=1` 的 pid 是否从 70 变成 55（backboardd）；
- `C0 calls` 是否开始增长；
- 是否出现 `swap/xfer`。

## 40. H103ae–H103af：IOMFB loader 与客户端身份（2026-11-02）

### H103ae：只禁用 iomfb_fdr_loader（失败）

H103aa 同内核，payload 在 `sheng-disable-jobs` 中只加入：

```text
com.apple.iomfb_fdr_loader
```

保留 `com.apple.iomfb_bics_daemon`。

结果仍然 panic：

```text
panic(cpu 0 caller ...): sheng watchdog timeout failed: 222
  @kernel_exec.c:...
Hardware strut ... initialized
```

结论：

- 不能禁用 `com.apple.iomfb_fdr_loader`；
- H103ab 同时禁用 fdr_loader 与 bics_daemon 的 panic 不是叠加问题，单独禁用 fdr_loader 也会触发；
- 该 job 是启动必需路径的一部分，不能通过 disable-jobs 绕过。

产物：

```text
kernel  b6e6653c44b1fce84534b6cf7186a3748411d096d6d05e5fabd2e949d95b8d8e
payload b165da287833646a0ae26c7bfac28e18b62e5c55bfa31d741ce9bf332fbab600
boot    0b421b37b4e453245f5277f877aa0dc176339e5b11bbedbe94946557a690f113
```

### H103af：客户端进程名诊断

在 H103aa/H103ad 基线上增加：

- `native_legacy_client_comm[64]`；
- `native_legacy_client_enter` 通过 `proc_name(pid,...)` 记录客户端进程名；
- `NATIVE_LEGACY_STATE` 增加 `comm=...`；
- `NATIVE_LEGACY_CLIENTS` 的 `C0 ... comm=...`。

配置保持：

```text
cpus=1
IOClass=IOMobileFramebufferLegacy
无 sheng-vsync
不修改 sheng-disable-jobs 的 IOMFB 项
```

产物：

```text
kernel  a90a4190970e8f33c81afa027e1bad68939ccb68e84502e1d9d148eecbf77234
payload 454d37fd201bc8ad9fad883978d0ba9086260c85fe45474b14d7a6ac7a37afd4
boot    221ec5e155fa480ed1087eb9e03e45aa031e1289779acbba38da881f70074986
```

Windows 路径：

```text
D:\Users\ls\Documents\OnePlus5_ios\boot-H103af-comm.img
```

### 当前判断

- H103aa/H103ad 已证明显示后端工作：
  ```text
  load=0 stage=10 reg=1 start=1 calls=1 pattern=1
  ```
- 客户端能稳定打开（`calls=1`），但 `C0 calls=0`，从未发起 selector。
- H103ad 中客户端 pid 为 70，而 `backboardd` 是 55、`SpringBoard` 是 27。
- H103af 用 `comm=` 确认 pid 70 的真实身份。
- 若确认是 `IOMFB_FOR_Loader`，下一步不能禁 job，只能考虑：
  - 只拒绝该 loader 的显示 user client（按 comm/pid），让 backboardd 接手；
  - 或提供 loader 期望的显示固件路径；
  - 或延迟/绕过 loader，而不触发 watchdog。

## 41. Userdata 一分为二：Android 64 GiB + iOS 49.2 GiB（2026-11-02）

### 分区前审计

临时启动 TWRP（未刷入 recovery）：

```text
fastboot boot twrp.img
```

GPT 关键信息：

```text
UFS LUN0: 119,054,336 KiB ≈ 113.2 GiB
logical sector: 4096 bytes
userdata: /dev/block/sda13
start LBA: 0x14f40
end LBA:   0x1c627fa
size:      29,677,755 sectors ≈ 113.2 GiB
```

原始 GPT 已备份到 Windows：

```text
D:\Users\ls\Documents\OnePlus5_ios\gpt-primary.bin
D:\Users\ls\Documents\OnePlus5_ios\gpt-backup-full.bin
```

### 新 GPT

```text
userdata       Android   0x14f40  .. 0x1014f3f   64.0 GiB
userdata_ios   iOS       0x1014f40 .. 0x1c627fa   49.2 GiB
```

- 新 `userdata_ios` 类型设置为 Apple APFS；
- primary/backup GPT 的 entries CRC、header CRC 均重新计算并读回校验一致；
- 通过 TWRP `dd` 写回 `/dev/block/sda`；
- 重启 TWRP 后内核识别新分区。

确认：

```text
/dev/block/by-name/userdata      -> /dev/block/sda13
/dev/block/by-name/userdata_ios  -> /dev/block/sda14

blockdev --getsize64 userdata      = 68719476736   # 64 GiB
blockdev --getsize64 userdata_ios  = 52840607744   # 49.2 GiB
```

### Android userdata

Android 的 64 GiB `userdata` 已重新格式化为 ext4：

```text
Filesystem UUID: 33c9e7ae-b978-430c-900a-a10074a9f0dc
16777216 个 4K block
```

重启后 **Android 成功进入系统**。

现在：

- Android 使用标准 `userdata`；
- Android 的 `fastboot -w` / recovery 只影响 Android 的 64 GiB；
- iOS 的 49.2 GiB 保留为 `userdata_ios`，暂未格式化 APFS；
- H103 当前 iOS Data 仍使用 payload 中的 RAM seed（`XNU_OWNER_DATA_VOLATILE=1`），不依赖 `userdata`。

### 待验证

- 分区后 H103 iOS 是否仍能启动；
- 如果 `RootLayout`/`NativeUfsWarm` 仍硬编码 `userdata`，需要改成优先 `userdata_ios` 或加回退；
- 后续要做 iOS 持久化 Data 时，再在 `userdata_ios` 上创建 APFS container。

## 42. Userdata 拆分的修正：iOS SHNGROOT 恢复到 userdata_ios（2026-11-02）

### 根因

拆分后 H103ad/H103af 都停在：

```text
skip attaching fse to iOS using legacy IO matching ...
UUID 04769D26-2CA0-4825-B443-E2E5F88300F2
```

原因是把 `userdata` 当成了普通 iOS Data 分区。实际上 `userdata` 是
iOS 的 SHNGROOT 容器：

```text
0x000000  manifest
0x100000  OS window
0x16a600000 SystemOS window
```

Android 的 64 GiB `userdata` 格式化后，iOS 的 OS/SystemOS 窗口被覆盖，
所以根 UUID 匹配失败。

### 恢复

用 `unixnu build userdata` 从原 IPSW DMG 重新生成：

```text
OS       6,077,580,800 bytes @ 0x100000
SystemOS 3,303,014,400 bytes @ 0x16a600000
image    9,382,658,048 bytes (8.74 GiB)
```

镜像：

```text
D:\Users\ls\Documents\OnePlus5_ios\userdata-root.img
```

已由用户手动刷入：

```text
fastboot flash userdata_ios userdata-root.img
```

### 代码修改

- `drivers/NativeUfs/RootLayout.h` / tree `scripts/RootLayout.h`：
  ```text
  wanted = "userdata_ios"
  ```
- `drivers/NativeUfsProbe/NativeUfsProbe.cpp`：
  - GPT 扫描时捕获 `userdata_ios` 的 first/last LBA；
  - SHNGROOT manifest 从 `rootFirst` 读取，不再硬编码 `0x14f40`；
  - OS/SystemOS window 的绝对地址改为 `rootFirst * 4096 + offset`。

### H103ag 结果

H103ag 使用新的 UFS probe，`userdata_ios` 上的 SHNGROOT 被识别，iOS 再次启动到：

```text
KextLog: driverkitd is active
```

显示后端状态：

```text
NATIVE_LEGACY_STATE: load=0 stage=10 reg=1 surface=0/1 probe=1 dt=1 start=1 calls=0 pattern=1
```

- 品红色 GOP 自检块正常；
- iOS OS/SystemOS 恢复；
- Android 的 64 GiB `userdata` 未被触碰；
- 本次启动显示客户端 `calls=0`，仍需继续查 backboardd/SpringBoard 的显示连接。

产物：

```text
kernel 3f459752543807b06a2da151799ac6eb984d5188eb771b04ddaf13a24846cbc4
boot   503a0d567ccec5d999b6d9967d044b76a8cd16f4e4b46d8b7064c5ab6be2d359
```

## 43. H103ao–H103at：backboardd 启动确认与 Legacy 显示边界（2026-11-03）

### H103ao：SpringBoard exec 确认

在 `kern_exec` 中对 `SpringBoard` 做目标 panic。

结果：

```text
NATIVE_EXEC: pid=27 ppid=1 comm=SpringBoard
  argv0=/System/Library/CoreServices/SpringBoard.app/SpringBoard
panic: NATIVE_EXEC_TARGET: pid=27 comm=SpringBoard
```

结论：launchd 已经能拉起 SpringBoard。

### H103ap：backboardd exec 确认

只对 `backboardd` 做目标 panic。

结果：

```text
NATIVE_EXEC: pid=55 ppid=1 comm=backboardd
  argv0=/usr/libexec/backboardd
panic: NATIVE_EXEC_TARGET: pid=55 comm=backboardd
```

结论：backboardd 也能被 launchd 拉起。

### H103aq/H103as/H103at：显示打开路径

- H103aq：恢复干净配置，不拒绝 loader，不 panic；
  - 某些启动能到 backboardd；
  - `display calls=0`；
  - 没有 `NATIVE_DISPLAY_OPEN`。
- H103as：改为拒绝真实 loader 名：
  ```text
  IOMFB_FDR_Loader
  ```
  同时 `NativeSandboxGrantProcess=backboardd`；
  - backboardd 已 exec；
  - 仍未出现 `NATIVE_DISPLAY_OPEN`；
  - `display calls=0`。
- H103at：`IOClass=IOMobileFramebuffer`（基类）；
  - backboardd 已 exec；
  - 显示客户端仍是：
    ```text
    calls=1 pid=78 comm=IOMFB_FDR_Loader status=e00002c7
    ```
  - `status=e00002c7` 是拒绝 loader 返回的错误；
  - backboardd 仍然没有打开显示。

### 结论

Legacy framebuffer 后端已经完成：

```text
load=0 stage=10 reg=1 surface=0/1 probe=1 dt=1 start=1 pattern=1
```

GOP 自检色块可见，说明：

```text
IOMobileFramebuffer -> GOP -> 屏幕
```

完全打通。

但：

```text
backboardd 已经启动
却没有调用 IOServiceOpen 打开 Legacy display
```

因此继续调以下内容没有意义：

- `IOClass`
- `IOProviderClass`
- `NativeSandboxGrantProcess`
- `IOMFB_FDR_Loader` 拒绝
- `newUserClient`

下一步转向：

```text
backboardd / CoreAnimation / IOAccelerator / IOGPU / Metal
```

最可能的情况是 backboardd/CoreAnimation 进入 headless 模式，等待 GPU/Metal 设备。

### 产物

```text
H103ao kernel b974cebd86409f471c94071ce50cc8c1ccbd5906dd39e4393981ec6a61f0a542
H103ao boot   6463b0aead15c1d4edb0f4e101971627af2ce32a1f6d50f7ca70f07f1573ac4d

H103ap kernel 725edf18a32c0e1b2ece243ad0c16c08bd728ce571fc2e14aea3ea1ed147feff
H103ap boot   23dfa2b0d0169e93ae67873663f437fa097cc7e1ca6d0c9ef7840715457992e5

H103aq kernel df417f413bfeae02e5fd9b627342d7670adc1d7ff15437d14baf616db98ae5d1
H103aq boot   1e75019ea0a27d731947b826242431692eeeef5ff5aceaabcbf13096ad094f76

H103as kernel 37bf0159ca93fad2e538312ec6192966d46a533ef4f56b30e71f8d3de8854a50
H103as boot   96688b26148d912a238ac43328a39c7ae7567b792cd9cfd524b2b3035e114784

H103at kernel c93b972ce48207ae941b57bb3476e59cc456f1081934fdd723e275113de9ac69
H103at boot   9e363bef3ee556dd1a3dc6e61509612a10459c09f823592d67acc5908ddf6e43

## 44. H103au–H103bg：启动随机性、Data/内存/AMFI 对照与颜色状态面板（2026-11-03）

本轮把 H103at 之后的失败从“显示后端”进一步拆成三类，并加入了不依赖
滚屏日志的右上角颜色状态面板。所有镜像仍使用同一台 OnePlus 5、同一份
21H461 iPad7,1 OS/SystemOS 和 `userdata_ios` SHNGROOT；未修改 Android
`userdata`、AuthAPFS/Image4/TrustCache 验证或原始 userland。

### 44.1 H103au：client probe 对照

- 变量：H103at 内核不变，payload 仅追加
  `offline-mobile-graphics-client=1`。
- 结果：`display calls=0`、`pid=0`、`comm=`；backboardd 已 exec 但未
  打开显示。
- 结论：client probe 不能替代 backboardd 的显示初始化。

产物：

```text
H103au boot 2a263cce4b0ed4fdb7c35ef0e0ae35d39d36ebeef9143b9061865adac1f56288
```

### 44.2 H103av：心跳线程与 cpus=1/2 对照

- 在 NativeUfsProbe 中加入独立 `NATIVE_ALIVE` 线程，使用同一 H103at
  内核构造 cpus=1 与 cpus=2 两个 payload。
- cpus=1/2 都能到 `driverkitd is active`，但屏幕没有 `NATIVE_ALIVE`；
  不能再用“缺少心跳”证明内核死亡。
- 之后 H103aw 进一步加入 `NATIVE_UFS_THREAD: alive rc=...` 和
  `heartbeat rc=...`，确认线程创建路径必须单独记录；屏幕上仍未稳定
  显示这两行，说明心跳线程的启动/调度本身还需要独立诊断。

产物：

```text
H103av cpus1 payload efa95f92b33029365d5dae5d9149573e49f4ad9ccc2b97639e7044fd76742fba
H103av cpus2 payload 536247d449ea3e047e174fcac11265d0d6dea8519fc5e7142d292c1a885fb13c
H103av cpus1 boot    b59bfe2682691d6f03378a12cdb769a97c6fa94acc15a42fbe140a3b471cf2ed
H103av cpus2 boot    07886c18c0ccfd406573b8108fd6315c99c07272b65a33bd15f934ad2cd647a4
```

### 44.3 H103ax-1g：Data seed 1 GiB 对照

- 变量：H103aw 内核不变，Data seed 从 512 MiB 扩到 1 GiB，UUID
  `1A4CE9DF-34B6-4B52-9744-5052A9D20B23` 保持不变。
- 3 次启动仍有不同前沿：
  - 2 次 `calls=0`，最后是 `logd_helper/83` / `driverkitd`；
  - 1 次 `calls=1`，`IOMFB_FDR_Loader` 打开显示；
  - 未观测到 ENOSPC。
- 结论：1 GiB Data 不能单独消除 run-to-run 差异；Data 容量不是唯一原因。

产物：

```text
H103ax-1g kernel  a476ac6839e9ef93b3c4129a570ac19e22f2841ea849375ec125982fc9faf2ac
H103ax-1g payload f4a18517888c77fcf49fa4ee95edc008a00e93dab6a7d424f9346613d3f826b1
H103ax-1g boot    73cf3927771e3191f38ae8cf89daaf10a883e3d35ecdd9ef4780699c0297f84e
```

### 44.4 H103ay-4g：4 GiB owned memory 对照

- 变量：Data 仍为 1 GiB；`owned_memory_size` 从 `0x40000000`（实际
  1 GiB）改为 `0x100000000`（4 GiB）。注意 board.toml 原注释写 4 GiB，
  但值实际只有 1 GiB。
- 结果：
  - 1 次 `calls=0`，trustcache 循环，`driverkitd`；
  - 1 次 `calls=1`，出现 `backboardd/55`、`SpringBoard`、
    `DataMigrator`/`IMDPersistence`；
  - 1 次 `calls=0`，`eligibilityd`/`driverkitd`；
  - 仍无 ENOSPC。
- 结论：4 GiB 能让个别 boot 多走一段，但不能稳定让 backboardd
  打开 Legacy display。

产物：

```text
H103ay-4g payload 06daedf1f5246fe66beeebcf06faee265b4a5f6b2e1dfc8b464d912b8fe2c195
H103ay-4g boot    f148919d96db01302a53d72d6061445a89cfe652a9ca33fdc14f61a1ba41cd3e
```

### 44.5 H103az：AMFI gate 一次性 NOP（仅构建，未单独判定）

- 在 H103ay 内核的 AMFI 拒绝分支把
  `VM 0xfffffff00aab73b8` 的 `0x360010a0`（`tbz`）改为
  `0xd503201f`（`nop`），只跳过
  `com.apple.private.amfi.can-check-trust-cache` 权限门。
- 这是受控诊断，不是默认策略；后续 H103be 将该绕过与颜色面板一起上机。

产物：

```text
H103az kernel  8cf5f2e90000eff100f5672b17237829fcef5c38350b5a6b92af3423bee7153d
H103az payload 75ab9dcf96ff00b93c0982ac249360f92dba1bf12b53ce401a2a96a8c09969f4
H103az boot    3bb5b2bebe792d0b45ba9823d1e7bbc7adb78ca7adb040d47f63723e327990c5
```

### 44.6 H103ba–H103bd：右上角颜色状态面板

为避开滚屏日志，在 `NativeLegacyDisplay::drawTestPattern()` 把原来的
纯品红方块改成右上角 2×2 面板，由 UFS 心跳每 10 秒重绘：

- 左上：用户态前沿 / 显示阶段；
- 右上：显示客户端；
- 左下：client calls 计数；
- 右下：swap/transfer；
- 右下角另有 16×16 心跳闪烁块。

H103bd 的最终颜色语义：

```text
左上：
  红   还没有关键进程 / 早期
  黄   只有 logd_helper
  橙   只有 driverkitd
  青   只有 backboardd
  绿   SpringBoard 启动、没有 backboardd
  白   SpringBoard + backboardd 都启动
  紫   SpringBoard 已退出
  品红 backboardd 已退出

右上：
  黑   没有 client
  黄   IOMFB_FDR_Loader
  青   backboardd
  紫   其他 client

左下：黑 0；蓝 1–9；绿 10–99；橙 ≥100
右下：黑 无；紫 有 swap validation；白 有 transfer
```

上机结果：

- H103ba（2×2 初版，左上仅表示 display 后端）：3 次都只有左上绿，
  后代仍无显示客户端。
- H103bb（左上加入 logd_helper/driverkitd/backboardd/SpringBoard 位）：
  4 次中 1 次左上绿 + 右上黄 + 左下蓝，其余只有左上绿。
- H103bc（左上白色表示 SpringBoard+backboardd）：3 次都只有左上绿，
  说明这 3 次 backboardd 根本没有 exec。
- H103bd（加入 SpringBoard/backboardd 退出标志）：2 次只有左上绿；
  1 次左上白、右上黄、左下蓝。白色那次 `backboardd/55` 和
  `SpringBoard/27` 都在，但显示客户端仍只有 FDR loader；等待 10 分钟
  颜色不变。
- 结论：`backboardd` 启动本身有 run-to-run 差异；即使启动，也不会调用
  Legacy display 的 `newUserClient`。

产物：

```text
H103ba kernel  aaac1722f29b6ad38df7f1b0ebe12e09a70340e8aaadb62b76fc14f207028428
H103ba payload 048dd55afa428fdb1d3ccad933957490c7f7ee4c1feb57bdbe6916d58ade2652
H103ba boot    3195072d616d9be0411d73903c6468b9d6aac40423c3d92c342ad807cd8190f2

H103bb kernel  11e9655709e8dde6fe4fc3bf731d308776b7ac3841709281eb7f8db3c8086bb7
H103bb payload b67a54036d8891bab32ea421b59e275196aa46cda938c22d907469d8c267da36
H103bb boot    d9b91fc6a29ecb90822da84c31923539b5160b57927ccd1cc60cd24e0b1b2130

H103bc kernel  060689dc87083eafd3c10127bc569ae079bfadeb47dadae5e497a11578404d9b
H103bc payload ebfda045e4e44192fe590bb244576be7d9b37bc796e0671005815215fe23b948
H103bc boot    e5349527c74df987e5e21bcd670febe7be8eac41260a9556e719c34a458a42c6

H103bd kernel  51cffbc25e890be9a8d4b9ca44ca7e1895197ed89cced9e6edb6c865c6664c66
H103bd payload 29ef1f470870fca6061863a31a151bd56bc44e8467a89f8709a79425b378ccce
H103bd boot    8284f2616412cea7e98cdb9d929802b4b2cdd6bb7b8af536210a38c08c9910f3
```

### 44.7 H103be：AMFI gate NOP + 颜色面板

- 变量：H103bd 内核的 AMFI 权限门 NOP；其他与 H103bd 相同。
- 3 次结果：1 次左上白、右上黄、左下蓝；另外 2 次只有左上绿。
- 右上始终没有青色，说明 AMFI gate NOP 没有让 backboardd 打开显示。
- 结论：AMFI/trustcache 权限门不是 backboardd 不 open 的根因；H103be
  的 `Only allowed processes...` 循环消失，但显示客户端仍然只有 FDR
  loader。

产物：

```text
H103be kernel  ee38baaf27e395420b78eb9e43f95a89290a11b42ec5f1d8974e190d3f26c535
H103be payload 8fc8cc373aa7d923ed6a8d9508d507c78dc91ada7a512f1887db7dc234ad9d26
H103be boot    6ce2a1e2584055903edcb383432da9cc6eb478e7f44b21ec3e5059db7a4694ae
```

### 44.8 H103bf：DinnerABC 显示参数 parity

- 只改 payload，追加：
  `offline-mobile-graphics-client=1`、
  `offline-legacy-cpu-present=1`、
  `offline-legacy-unit-scale=2`、
  `sheng-vsync=60`、`sheng-vsync-fixed=1`。
- 3 次结果仍为 1 次左上白、2 次左上绿；右上没有青色。
- 结论：DinnerABC 的 CPU display 参数不能单独让本项目 backboardd 打开
  display；服务注册/display provider 差异仍需处理。

产物：

```text
H103bf kernel  51cffbc25e890be9a8d4b9ca44ca7e1895197ed89cced9e6edb6c865c6664c66
H103bf payload 1e2cb3e4b9c65903d3ac2389216e3b95c072d4ffead84b814369029aaf376a15
H103bf boot    768f8eb8c01995df59d6cd8eb24d7080d4dd29d0e41400bd2857193f6262aced
```

### 44.9 H103bg：BackBoard/CoreAnimation 软件渲染偏好

- 在 1 GiB Data seed 的 mobile/root `Library/Preferences` 中写入：
  `CAEnableDeepFramebuffer=false`、
  `CAEnableFramebufferCompression=false`、
  `CAEnableIOSurfaceCompression=false`、
  `CAForceSoftwareRendering=true`。
- 结果：右上仍无青色；backboardd 仍未打开 Legacy display。
- 结论：这组软件渲染偏好不足以把 backboardd 从 headless 状态拉回来。

产物：

```text
H103bg kernel  51cffbc25e890be9a8d4b9ca44ca7e1895197ed89cced9e6edb6c865c6664c66
H103bg payload 437a54dd94e52ed8104e96d7e7aaefe1f4673c307ca45ae8ec3dbc51ef4d78dd
H103bg boot    ae9c39268b5cd4e49ee1dcf4cdbb0e933b673ea63c4016995ab23fd50677e0a6
```

### 44.10 只读分析：backboardd 进入 headless 的路径

对 `/tmp/opencode/os-mnt/usr/libexec/backboardd` 做只读字符串/导入分析：

- 导入 `IOMobileFramebuffer`、`Metal`、`IOSurface`；
- 有 `BKDisplayStartWindowServer`、`BKDisplayIsHeadless`、
  `StartWindowServer: headless (display:%d/server:%d)`、
  `IOServiceOpen failed for class ...`；
- 使用 `CADisplay` / `CAWindowServerDisplay` / `CAContext`；
- 不能从日志证明它已经到达 `IOMobileFramebufferOpen`。

因此当前最符合证据的解释是：

```text
Legacy framebuffer 后端正常
  -> FDR loader 能打开
  -> backboardd 不把它当作可用 CADisplay/display provider
  -> BKDisplayStartWindowServer 走 headless
```

这不是继续调整 `newUserClient`、`IOClass`、`NativeSandboxGrantProcess`
或 TrustCache 能解决的问题。

### 44.11 当前结论与下一步

已排除的低成本变量：

- Data 512 MiB/1 GiB、4 GiB owned memory；
- AMFI selector 0x5f 权限门；
- `offline-mobile-graphics-client`；
- `offline-legacy-cpu-present`、`sheng-vsync`、`unit-scale`；
- Legacy `newUserClient` 拒绝路径；
- BackBoard/CoreAnimation 软件渲染偏好。

下一步只剩两条路线：

1. **移植 DinnerABC 的 CPU display provider/注册路径**，重点对比
   `package/BoardRuntime/LegacyDisplayProbe.cpp`、
   `NativeLegacyDisplay.cpp` 与 `NativeMobileDisplay` 的 service 注册、
   IORegistry 属性和 `CADisplay` 提供方式；DinnerABC 曾在 CPU 路径
   达到 `backboardd calls=77/97`。
2. 如果 provider 路径仍 headless，则提供 **IOAccelerator/IOGPU/Metal**
   服务，或走 QcomGPU A540 路线。

不再继续增加 boot-arg、`sheng-disable-jobs`、Data 容量或 AMFI 诊断。

## 45. A 线：H103cg provider 对齐计划（2026-11-04）

H103cg 仍是唯一接受的稳定基线：

```text
NATIVE_LEGACY_STATE:
  load=0 stage=10 reg=1 surface=0/1 probe=1 dt=1 start=1 calls=0 pattern=1
boot: 4560ac5b64662aa321348d530617894ef85bf27252eae04396f8a2048b1cea8a
```

A 只读对照（sagit / HackiOS / DinnerABC）结论：

- `sagit` 的 iOS 18 显示路径依赖物理 DPU 节点、`device_type=display-subsystem`、
  `canvas-width/height` 和 `registerService()` 前的完整 display 属性；
- `HackiOS` 显式发布 `TimingElements`/`ColorElements`、`/arm-io` 显示节点
  和空 `IOAcceleratorES`；
- `DinnerABC` 与 H103cg 同 OS，但 **不设置** `IOClass`、
  `NativeSandboxGrantProcess`、`IOFBDependentID`；H103at 已排除
  `IOClass`/`NativeSandboxGrantProcess`，`IOFBDependentID` 仍未单独验证；
- H103cg **没有发布 `IOAcceleratorES`**：`native_a540_accelerator_probe()`
  有定义但全树无调用点，`native_a540_accel_state` 为 0；
- H103cg 服务上几乎只有 `IOMFB Debug Info` + `IOFBDependentID`，
  没有 sagit 的 `DisplayWidth/Height`、`RowBytes`、`IOMFBDisplayRefresh`
  等属性。

执行顺序：

1. **A1**：发布最小裸 `IOAcceleratorES`（不设置 `MetalPluginName`/
   `MetalPluginClassName`），单变量验证 headless 是否由 accelerator 缺失导致；
2. **A2**：A1 不足时补最小 display 属性，不改 timing；
3. **A3**：仍不足时才做 `IOFBDependentID=0` / 去掉
   `NativeSandboxGrantProcess` / `IOClass` override；
4. **A4**：最后才补父节点 `compatible`/`canvas-width/height`。

完整差异表、验收标准和回退规则见
[A-provider-parity-plan-2026-11-04.md](A-provider-parity-plan-2026-11-04.md)。

下一步：A1。任何 display archive 改动仍可能触发 Data/APFS IOMedia race；
若停在 `Waiting on IOMedia UUID 04769D26-...`，记录后回退，不据此判定
provider 失败。

## 46. A1 结果：pre-root 裸 accelerator 不可行（2026-11-04）

候选 `boot-H103cw-a1.img`：

```text
boot:    43f831fb07fe5ad9f639a4147a26137d51f7972cb7fb74c4041639422245e28c
kernel:  4950bfa6c93a248f3e584b05f60e93705c0603f9eb366827004f8fcedaa99d19
payload: 3ac218e47edc83a80d2e746ea8ff75e320d6930c1826f7772eada4a925e82342
```

上机 panic：

```text
NATIVE_LEGACY: late probe enter
Can't load kext com.apple.iokit.IOGPUFamily - requests to user space are disabled.
OSMetaClass::preModLoad() wasn't called for class NativeA540AcceleratorProbe
```

原因：

- `NativeA540AcceleratorProbe` 的
  `OSKextLoadKextWithIdentifier("com.apple.iokit.IOGPUFamily")` 返回值
  判断写反，失败后继续 `static_init()`；
- pre-root late probe 阶段无法加载 `IOGPUFamily`，原始
  `IOAcceleratorES` 基类不可用。

结论：A1（pre-root 裸 `IOAcceleratorES`）不可行；A540 源码已回退，
H103cg 仍是唯一稳定基线。后续二选一：

1. **A2**：最小 display 属性（不依赖 IOGPUFamily）；
2. **A1b**：把 `IOGPUFamily` 放进 prelink roots，或 post-root publication
   （改变 prelink 布局/时序，风险更高，暂不执行）。

详见 [A-provider-parity-plan-2026-11-04.md](A-provider-parity-plan-2026-11-04.md)。

## 47. A2 结果：display 属性发布，但 archive 增大触发 root race（2026-11-04）

候选 `boot-H103cx-a2.img`：

```text
boot:    571d77b74176a89c60f773bca36b364ec90172b782ce142cb948360a79cbc822
kernel:  4fbd7673243bc9aae455017d7d8258a6dcb37d028808ce26343a9849689c526c
payload: 9a6efa2eb7cd371941b225590a9901c6c8ba9240204e49d7545356317732cf53
```

上机：

```text
NATIVE_LEGACY: display properties 1080x1920 row=4320
NATIVE_LEGACY: registerService object=...
NATIVE_LEGACY: base start ok primary=1 ...
NATIVE_LEGACY: phase=4 dt=1 probe=1 attach=1 start=1 reg=1

Waiting on <dict ... IOProviderClass=IOMedia ... UUID=04769D26-...>
```

结论：A2 属性已在服务上发布，但 Legacy archive 变大再次触发 Data/APFS
IOMedia 发布 race；未到 backboardd，`calls` 无法评估。A2 镜像不可用，
H103cg 保持。

下一步 A2c：保留 A2 属性，移除未使用的 A540 对象和 `drawTestPattern()`
里的 accel 引用，改用 5 成员 builder，把 Legacy archive 缩到
layout-sensitive 阈值以下，再评估 `calls`。若仍 root wait，则不是单纯
archive size，转向 runtime property injection 或修复 race。

## 48. A2/A2c 停止：边界恢复（2026-11-04）

用户指出 A2/A2c 违反了“不再做方块/灰条/display 时序/ordering 实验”的
边界：

- A2 修改了 display 属性和 `IOMFBDisplayRefresh`；
- A2c 移除了 `drawTestPattern()` 的 accel 方块，并改变 Legacy archive
  成员；
- 两者都属于 display archive/诊断实验，不属于已接受的 H103cg 基线。

已恢复：

- `NativeLegacyDisplay.cpp`、`LegacyDisplayProbe.cpp`、
  `NativeA540AcceleratorProbe.cpp` 回到 H103cg；
- macOS 源和 `scripts/build-op5-legacy-display.py` 回到 7 成员 builder；
- A1/A2/A2c 镜像均不使用；H103cg 是唯一稳定基线。

后续除用户明确要求外，不再做 display archive、方块/灰条、display
timing 或 ordering 实验。

## 49. H103cg-1：仅移动三个 fill 坐标仍 root wait（2026-11-04）

候选 `boot-H103cg-1.img`（boot `e1fb98d4...`、kernel `7edeaf8e...`、
payload `105c0c08...`）：

- 基线为 H103cg `5f29dde`；`LegacyDisplayProbe.cpp`/`NativeA540...`
  与 H103cg 完全一致；builder 为 H103cg 的 7 成员版本；
- 唯一源码差异是 `NativeLegacyDisplay.cpp` 中三个 16×16 指示块的坐标：
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
Data/APFS IOMedia 发布 race。H103cg exact 是唯一稳定点。源码已回退到
H103cg；`boot-H103cg-1.img` 不使用。

指示块不能通过“改 display archive 再重编”来移动。后续如果还要移动，
只能从 display archive 外部做覆盖/绘制，或先修复 Data/APFS race。
```
