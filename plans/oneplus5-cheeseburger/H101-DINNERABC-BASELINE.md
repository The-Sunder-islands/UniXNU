# DinnerABC 基线重建与当前阻塞（2026-11-01）

- 目的：按用户要求，停止 B 线反复的 AMFI/job-skip 诊断，改为直接使用 DinnerABC 的 P21 基线。
- 结论：**没有拿到可工作的纯 DinnerABC 镜像**。本机远程构建出的镜像是一个混合体，仍停在早期 VM/驱动异常，不能宣称启动成功。

## 1. 已完成的远程构建

在 macOS 构建机 `192.168.10.103` 上，从 DinnerABC 源码重建：

- 工作副本：`/Users/lss/DinnerABC/dinner-p21`
- 官方框架：`/Users/lss/DinnerABC/dinner-p21/.work/p21-official-framework`
- 内核源码/补丁：DinnerABC 仓库
- 板级运行时：DinnerABC `package/BoardRuntime`
- 8 字节 VerifiedRoot policy：用 `VR_HASH_BYTES=8` 从 OS/SystemOS DMG 生成
  - OS manifest：11,870,824 B
  - SystemOS manifest：6,451,744 B
- 内核构建通过（`p21-compile-dinner.py`），预链接、payload、boot image 均生成。

关键产物（当前项目 `out/cheeseburger/boot/`）：

| 镜像 | SHA-256 | 说明 |
| --- | --- | --- |
| `boot-dinner-up1.img` | `65892f803ac79cb96a3569aed09bf0ede9b5d14886f6e9c72574e8d471bac1bc` | 首次 DinnerABC 内核，正 slide |
| `boot-dinner-pos.img` | `e31c99138140f26a911a70f0473a5138bc77392e6651f926fdc04179fa8e713b` | 正 slide + rebase bindings |
| `boot-dinner-noperiph.img` | `6508bad0159cbb4d1f89ce9f7e8b3d01ce22d31d32f8748307901b403afcef14` | 跳过 HID/按键/触摸探针 |
| `boot-dinner-panic.img` | `95eeecc31b79059dd34dd00aa1e982b7597f811d952ff46ec04f6a94bfeac37a` | 加 panic-screen（cave 覆盖数据） |
| `boot-dinner-neg.img` | `1f16ba2534df9e89715d520c6b57740c33fd8cf8e6ceae6454faaf30691a88dd` | 负 slide + 低 `ARM64_LINK_ADDR` + 原始 bindings |
| `boot-dinner-panic-neg.img` | `fa461b041cf4c3d4602f3bdc842218b45a3b31e2da923be7125db1b93f45a7b0` | 负 slide 的 panic-screen（cave 覆盖数据） |
| `boot-dinner-panic-safe.img` | `1fda8f1f6922cb7318d1900f77d366fbcdba09d3e721cce69be7a48dd4c7cf20` | 4 KiB 对齐安全 cave 的 panic-screen |
| `boot-dinner-2g.img` | `f8dc3199236923fe17fccef61ec32092bd6d15a1bdd91b9df7dcd59984ce5dd1` | 2 GiB ADT + 安全 panic-screen |
| `boot-dinner-neg2g-nocomp.img` | `52ebb9cc04872501a005d26fa2bdc42227fd401036a2971491427795bfc4412c` | 2 GiB + `vm_compressor=0`/`memorystatus_freeze=0` |

## 2. 镜像成分（重要：不是纯 DinnerABC）

| 组件 | 来源 |
| --- | --- |
| XNU 源码、官方框架、板级补丁 | DinnerABC |
| `package/BoardRuntime` | DinnerABC |
| `board.toml`、启动参数、prelink roots | DinnerABC |
| UEFI / DarwinBoot | 当前 B 线 `uefi-cheeseburger-old.img.gz-dtb` |
| ADT | 当前 B 线 H91 ADT，删除 `boot-manifest-hash` 后使用 |
| TrustCache | 当前 B 线 H91 payload 的原始 bundle（`7416ca4f...`） |
| Data seed | 当前 B 线 H91 `scratch-data.img`（344 KiB） |
| Payload FAT / boot image 打包 | 当前 B 线 H91 payload 结构 + `make-bootimg.py` |
| VerifiedRoot | 当前 B 线 `kits/VerifiedRoot` 的 8 字节实现 |
| Prelink slide / bindings | 手工实验：先 `+0x5000000` + rebase，后 `-0xc2d000` + 低 `ARM64_LINK_ADDR` + 原始 bindings |

因此，这份镜像不能作为“DinnerABC 纯基线”成功或失败的证据。

## 3. 实机结果

### 正 slide `+0x5000000` + rebase bindings

- 越过 `arm_vm_init` 后出现 kext range 错误：
  ```text
  Kext com.apple.kec.Libm module start pointer is outside of kext range
  (start 0xfffffff00ab9502c - kext starts at 0xfffffff00a306680)
  Kext com.apple.kec.corecrypto ... outside of kext range
  Kext com.apple.kec.pthread ... outside of kext range
  ```
- 结论：正 slide 的 prelink 布局不适用于 DinnerABC 的 kext loader。

### 负 slide `-0xc2d000` + 低 `ARM64_LINK_ADDR=0xfffffff001004000` + 原始 bindings

- 越过 kext range，但出现同一 Data Abort：
  ```text
  MACH Reboot
  esr ≈ 0x96000000...（Data Abort）
  far ≈ 0x115500
  ```
- 2 GiB ADT 已生效（VM map 地址变化），但停点不变。
- `vm_compressor=0` / `memorystatus_freeze=0` 也无效。
- 仅凭寄存器图，PC 附近可能落在 `memorystatus_freeze`/VM 初始化；尚不能可靠符号化。

### panic-screen

- 固定/手工 cave 地址会覆盖真实数据，导致 VM map 初始化挂住；
- safe cave `0xfffffff001165000`（4 KiB 全零）仍不能稳定给出 panic 文本；
- 当前 panic-screen 不是可靠诊断手段。

## 4. 当前阻塞

1. 缺少 DinnerABC 真正可工作的候选：
   - `out/p21/phone-ui-d2/`
   - `out/p21/touch-window2g/`
   - `out/p21/fb-qup3/`
   - 以及其中的 `uefi/boot-p21-up1.img`、`kernel/kernel.macho`、`payload/`、`config/`
2. 缺少 DinnerABC 原构建tree的 `out/p12/policies`、`out/p11/storage-abi`、source-tree 等私有输入。
3. 从源码重建的 native kernel/prelink/policy/Data/firmware 组合与 DinnerABC 原候选不一致，导致同一 Data Abort。

## 5. 建议

- 停止继续用 boot args（2 GiB、compressor、slide、panic-screen）猜测。
- 优先从 DinnerABC 原构建机/NAS 复制一个已确认能进桌面的候选目录，直接 `fastboot boot`。
- 若必须从源码重建，需使用 DinnerABC 原候选的 `source-tree`/native kernel/prelink/policy/Data，而不是当前混合输入。
