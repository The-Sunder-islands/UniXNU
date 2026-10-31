# OnePlus 5（cheeseburger）B 线当前基线

- 记录日期：2026-10-31
- 用途：为 H96 及之后的实验提供可回退、可归因的 H91 基线。
- 本文只记录已能在仓库或本地产物中核验的事实；无法核验的内容明确标为“缺失”。

## 1. 仓库状态

主仓 `UniXNU`：

| 项目 | 值 |
| --- | --- |
| 分支 | `main` |
| HEAD | `93c7fb649d75dfaaf2983b0ac52ff2c74d0c3c1a` |
| HEAD 说明 | `docs: finalize H95 state and preserve VerifiedRoot truncation patch` |
| 工作区 | 干净（2026-10-31 调查开始时） |

设备仓 `UniXNU-cheeseburger`：

| 项目 | 值 |
| --- | --- |
| 分支 | `main` |
| HEAD | `5ad54e21a067ee747d67009122c87510dfdd3803` |
| HEAD 说明 | `ufs: add warm-link NativeUfs, PMGR integration, and ADT auth inputs` |
| 工作区 | 干净（2026-10-31 调查开始时） |

主仓关键子模块指针：

| 子模块 | 提交 |
| --- | --- |
| `drivers/NativeRamBlock` | `f3cffc9924c1538c9dd48bc4d1faf0e455876918` |
| `drivers/NativeUfs` | `43a4bc71e5473438d7ccbbac03c97c28aaf2e2f3` |
| `drivers/DarwinBoot` | `d099354d0eab34df5088ca1ba89c9c88bc3260bb` |
| `kits/VerifiedRoot` | `2854c61f6c4dfda870ac3d93e1f03d9181edb041` |
| `kits/NativePrelink` | `2b6ffb50117eee820b98aca9bd51b5c7dfb2970e` |

注意：H91 镜像构建时的主仓/设备仓提交没有被记录到构建产物中；上面的提交是
2026-10-31 调查开始时的仓库状态，不能当作 H91 的精确构建来源。H91 的
`kernel.macho` 是唯一可核验的内核产物。

## 2. H91 产物与哈希

H91 是当前最佳可回退基线，名称为 `H91-stallpanic`。设备仓中保留：

| 文件 | 大小 | SHA-256 |
| --- | ---: | --- |
| `out/cheeseburger/boot/boot-H91-stallpanic.img` | 81,147,904 | `cd81c5b8ec91f103f3e3c0fd7de7bfb26633f4fbabecd34490e5627dd6f3ff9b` |
| `out/cheeseburger/boot/payload-H91-stallpanic.img` | 79,691,776 | `e7fa5cdff58217886ebf269b30683c78da83c409ae179c356650f69869f08942` |
| payload 内 `Payloads/kernel.macho` | 52,088,832 | `7f02891c32825b75fcb76f2969ea2c1a541662d4c369d29fc85732b19f0e532d` |
| payload 内 `Payloads/kernel.macho.adt` | 14,604 | `83d733d770a9ec6961f60c1af13f4eeeb8552c776098241c0c536d13f810e54a` |
| payload 内 `Payloads/kernel.macho.trustcache` | 61,356 | `7416ca4f8377146626bb284e2133c76bee6cb4e3ee0af5d648b186235a7323a7` |
| boot image 内 raw kernel 输入 | 6,291,456 | `6da8ee305b9d16043071d59a2b4500c84ec5f96528fd1819d637e44d85d7c28d` |

`kernel.macho` 的哈希与 `STATUS.md` H91 记录一致。`kernel.macho.adt` 与
`kernel.macho.trustcache` 的哈希此前未记录，本文补齐。

## 3. H91 启动参数

从 payload 内 `EFI/UniXNU/unixnu.toml` 直接读出（原样）：

```text
-v cpus=1 debug=0x14e boot-progress=2 offline-legacy-gop=1
offline-legacy-unit-scale=1 native-ufs-probe=0 sheng-disable-watchdogd=1
sheng-lite=1 native-skip-coredump=1 native-skip-root-auth=1
amfi_get_out_of_my_way=1 amfi=0x94 amfi_allow_any_signature=1
cs_enforcement_disable=1 native-skip-exec-signature=1
launchd_unsecure_cache=1 dyld_flags=0x0000008A max_task_pmem=0
-apfs_shared_datavolume allow-root-hash-mismatch=1 sheng-pmgr=2
```

说明：

- `sheng-pmgr=2` 表示真实 PMGR；H91 不是纯 Probe 无 PMGR 构建。
- 没有 `native-ufs-root=1`，因此 H91 仍走 Probe/窗口路径，不启用完整
  `startRoot()`。
- `boot-manifest-hash` 仍是 ADT 中的 48 字节全零占位。
- `allow-root-hash-mismatch=1`、`native-skip-root-auth=1` 和 permissive AMFI
  参数都是 bring-up 临时项，不是最终安全方案。

## 4. H91 ADT 中的关键属性

从 payload 内 `kernel.macho.adt` 解析：

| 路径 | 值/长度 | 说明 |
| --- | --- | --- |
| `/chosen/boot-manifest-hash` | 48 字节全零 | 临时占位，不是真实 owner manifest |
| `/chosen/memory-map/TrustCache` | 16 字节全零 | DarwinBoot 从 `kernel.macho.trustcache` 填充地址/长度；文件有效则不是空侧车 |
| `/chosen/crypto-hash-method` | `sha2-384\0` | Image4 算法选择 |
| `/chosen/amfi-only-platform-code` | `uint32_le(1)` | AMFI 保持 platform-only 策略 |
| `/chosen/system-volume-auth-blob` | 229 字节 IM4P | SystemVolume root hash；不是 owner IM4M |
| `/chosen/root-matching` | UUID `04769D26-2CA0-4825-B443-E2E5F88300F2` | OS root 卷选择 |

`kernel.macho.trustcache` 可以被 DarwinBoot 的 `ValidTrustCaches` 接受。
H91 内核的 `_load_static_trust_cache` 在 range 长度为 0 或静态 module
`loadModule` 失败时都会 panic；H91 能启动到 `driverkitd`，说明这次启动中
静态 TrustCache range 非零且静态 module 至少加载成功。精确的
`NATIVE_TRUSTCACHE` 日志文本和 module cdhash 列表目前没有保存到仓库，
仍属缺失证据。

## 5. 设备状态（按 STATUS H94/H95，未在 2026-10-31 重新连接核验）

- userdata 已恢复为原始 OS/SystemOS 镜像：
  - OS 窗口 SHA-256：`e5634d04a8ac35f024845f583394e0a35deb26a04cb0c00286a8674e25a9a4f2`
  - SystemOS 窗口 SHA-256：`b4908cde41d23e6bc05e6af51a2c9eeb00cf0f863a6a18b1600c96a3fd70414f`
  - 完整 userdata image SHA-256：`fd43ee7035ce185ce4215ac93fdc006fd784a54b3b7442e21457c226cbe03093`
- Data 卷在 H92/H93 覆盖中丢失，用户已批准。
- 设备当前运行 H91 内核；停在 `FileSet KC` / `driverkitd`。
- 本次工作没有刷写设备、没有改动 userdata/GPT/固件分区。

## 6. 最后一次已知日志与首个未解决错误

来自 `STATUS.md` H91/H95 记录，而不是本次重新采集：

- UFS 没有硬卡死；没有触发 `NATIVE_UFS_STALL`。
- SystemOS 读持续完成，记录中出现 `ok=1 ticks≈96`，`winBytes` 约 3.23 GB。
- 最后可见阶段：`KextLog: Loading FileSet KC(s)`、`KextLog: driverkitd is active`。
- 反复出现 `_check_cdhash_in_trustcache` 的
  `Only allowed processes can check the trustcache` 循环。
- 首个未解决错误应按“AMFI 调用方权限拒绝”处理，而不是“TrustCache
  blob 查询未命中”；两者在 H96 调查中已用 AMFI 二进制路径区分。

缺失：H91 的完整串口/屏幕日志文件、首次到达 `driverkitd` 的时间、循环的
精确 PID/进程名、每次循环的返回码。

## 7. 复现步骤

### 7.1 只启动 H91（用户执行，不刷写）

```sh
cd /path/to/UniXNU-cheeseburger
sha256sum out/cheeseburger/boot/boot-H91-stallpanic.img
# 期望 cd81c5b8ec91f103f3e3c0fd7de7bfb26633f4fbabecd34490e5627dd6f3ff9b
fastboot boot out/cheeseburger/boot/boot-H91-stallpanic.img
```

禁止：

- 不要 `fastboot flash userdata`。
- 不要刷写 ABL/XBL/modem/persist 等固件分区。
- 不要在未确认回退镜像可用前启动会修改 Data 的镜像。

### 7.2 通过/失败判定

- 通过（基线可复现）：出现 UEFI/UniXNU 菜单，XNU 控制台继续到
  `KextLog: driverkitd is active`，且没有 `NATIVE_UFS_STALL`。
- 失败：在到达 `driverkitd` 前 panic、黑屏、UFS timeout，或 H91 哈希不匹配。
- 无论通过/失败，记录最后 30 行可见日志和首个错误；不要把“多一行日志”
  当成进展。

## 8. H91 已知缺口

1. H91 的精确构建提交、CI run、kernel.json 和 CFLAGS 未保存。
2. H91 的完整控制台日志未保存；当前只能引用 STATUS 摘要。
3. 静态 TrustCache 的实际 module 数和 cdhash 列表未记录。
4. Cryptex1/SystemOS trust cache 是否加载、加载到哪一层，未记录。
5. 调用 AMFI selector `0x5f` 的进程身份未记录；这是 H96a 的首要目标。
6. UFS Probe 的请求级生命周期日志未保存；H91 只能证明“没有硬 stall”。

## 9. 回退

- 保留 `boot-H91-stallpanic.img`、`payload-H91-stallpanic.img` 和
  `kernel.macho` 原文件，不覆盖、不重命名。
- H96 实验使用独立名称（`H96a-*`），启动时只 `fastboot boot` 新镜像。
- 如需回到 H91，重新 `fastboot boot` 上表中的 H91 boot image。
- 不执行 `git reset --hard`、`git clean -fdx` 或批量删除。
