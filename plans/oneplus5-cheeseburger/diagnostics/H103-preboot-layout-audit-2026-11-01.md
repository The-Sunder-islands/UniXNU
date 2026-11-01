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

尚未上机。下一步：

```sh
fastboot boot out/cheeseburger/boot/boot-H103-preboot.img
```

观察 `NATIVE_PREBOOT: status=0 stage=5`、`NATIVE_H82_DATA: status=0 stage=4`、`NATIVE_ENV`，以及 launchd 是否越过 `driverkitd is active`。
