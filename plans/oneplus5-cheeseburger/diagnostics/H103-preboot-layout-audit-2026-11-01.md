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
