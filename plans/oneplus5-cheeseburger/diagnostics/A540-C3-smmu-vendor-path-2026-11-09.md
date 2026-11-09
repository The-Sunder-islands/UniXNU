# A540 C3：msm8998 GPU SMMU 的厂商路径与只读 survey（2026-11-09）

本文回答 H236t 之后的第一个问题：**A540 的 `qcom,smmu-v2` 到底是谁在驱动、CB0
在什么偏移、写之前必须满足什么条件**。结论来自三份本地资料：

1. 本机 ROM `OnePlus5Hydrogen_23_OTA_065_all_2012030353_f743fd2428a00.zip`
   的 `boot.img` 内单一 DTB（kernel 偏移 13,179,047，421,383 B）；
2. LineageOS `android_kernel_oneplus_msm8998`（msm-4.4）的
   `drivers/iommu/arm-smmu.c`、`drivers/gpu/msm/kgsl_iommu.c`；
3. 同一内核的 `arch/arm/boot/dts/qcom/msm8998-gpu.dtsi` 与
   `msm-arm-smmu-impl-defs-8998.dtsi`。

## 1. 设备树里 GPU SMMU 的真实节点

H236 计划 §4.5 引用的是 `arm,smmu-kgsl@5040000`，这一节在 DTB 里确实存在；
但同一个 DTB 里还有一份 `qcom,kgsl-iommu`。两者的关系是：

```text
qcom,kgsl-iommu {
    compatible = "qcom,kgsl-smmu-v2";
    reg = <0x5040000 0x10000>;
    qcom,protect = <0x40000 0x10000>;
    qcom,micro-mmu-control = <0x6000>;
    clocks = gcc_gpu_cfg_ahb_clk / gcc_bimc_gfx_clk / gcc_gpu_bimc_gfx_clk;
    gfx3d_user  { iommus = <&kgsl_smmu 0>; qcom,gpu-offset = <0x48000>; };
    gfx3d_secure{ iommus = <&kgsl_smmu 2>; };
};

arm,smmu-kgsl@5040000 {
    compatible = "qcom,smmu-v2";
    reg = <0x5040000 0x10000>;
    qcom-tz-device-id = "GPU";
    qcom,dynamic; qcom,register-save; qcom,skip-init;
    vdd-supply = <&gdsc_gpu_cx>;
    clocks = gcc_gpu_cfg_ahb_clk / gcc_bimc_gfx_clk / gcc_gpu_bimc_gfx_clk;
    attach-impl-defs = <0x6000 0x2378 0x6060 0x1055 ... 0x6b68 0x2aaa2f82>;
    phandle = <0x1b5>;
};
```

`qcom,kgsl-iommu` 的两个子节点 `iommus` 都指向 phandle `0x1b5`，即
`arm,smmu-kgsl@5040000`。因此**真正驱动寄存器的是
`drivers/iommu/arm-smmu.c`（compatible `qcom,smmu-v2`），kgsl 只是通过
IOMMU API 建 domain、拿 CB 号**。`qcom,micro-mmu-control = <0x6000>` 与
`attach-impl-defs` 的 `0x6000` 也互相印证：实现定义页 1 就在
`base + 6 * (1 << pgshift)`。

## 2. 寄存器地图（size = 0x10000，pgshift = 12 时）

厂商 `arm-smmu.c` 的定义：

```c
ARM_SMMU_GR0(smmu)   = base
ARM_SMMU_GR1(smmu)   = base + (1 << pgshift)          /* 0x1000 */
ARM_SMMU_CB_BASE     = base + (size >> 1)             /* 0x8000 */
ARM_SMMU_CB(smmu,n)  = n * (1 << pgshift)             /* 0x1000 * n */

sCR0        0x000          ID0..ID3      0x020..0x02c
sGFSR       0x048          sGFSYNR0..2   0x050..0x058
SMR[n]      0x800 + 4n     S2CR[n]       0xc00 + 4n
CBAR[n]     GR1 + 4n       CBA2R[n]      GR1 + 0x800 + 4n
IMPL_DEF0   base + 2*0x1000 = 0x2000
IMPL_DEF1   base + 6*0x1000 = 0x6000   (micro_mmu_ctrl: bit2 halt, bit3 idle)
CB0         0x8000         CB1 0x9000   CB2 0xa000 (secure)
```

`kgsl_iommu.c` 也写死了 `KGSL_IOMMU_CB0_OFFSET = 0x8000`、CB shift 12，
所以 **H236t 的 `CB = 0x8000` 本身没有错**；错的是写之前没有确认 bank 状态。

## 3. 厂商的上下文编程顺序与精确值

`arm_smmu_init_context_bank()`（`arm_smmu.c`）：

1. `CBA2R = 1`（AArch64；且注释明确要求 **先于 CBAR**）；
2. `CBAR = cfg->cbar | (NSH<<8) | (WB<<12) | (vmid<<0)`；
   `qcom,dynamic` 路径下 `vmid = cbndx + 2`（CB0 → 2），
   `cbar = CBAR_TYPE_S1_TRANS_S2_BYPASS = 0x10000`，
   所以 CB0 实际是 **`0x1f302`**（H236t 写的 `0x1f300` 少了 VMID）；
3. `TTBR0/TTBR1 = root | (asid << 48)`；
4. `TTBCR = tcr`，`TTBCR2 = tcr >> 32`；
5. `MAIR0/MAIR1`；
6. `ACTLR = 0x70000000`（`SMMU_MODEL_QCOM_V2`：OSH/ISH/NSH 全 1）；
7. `SCTLR = 0x10e7`（`CFCFG|CFIE|CFRE|EAE_SBOP|M|S1_ASIDPNE`）。
   本 kit 为了不依赖中断处理改用 `0x1167`（HUPCF 代替 CFCFG），可接受；
8. 清 `FSR`、`TLBIASID/TLBIALL`、`TLBSYNC`，最后才置 `SCTLR.M`。

`arm_smmu_device_reset()` 在 `qcom,skip-init` 下**不清 SMR/S2CR、不清 CB**，
只清 sGFSR、做全局 TLB 失效、再按 NS 别名改 sCR0。

`arm_smmu_impl_def_programming()` 的顺序是：

```text
arm_smmu_halt()            读 0x6000，置 bit2 (LOCAL_HALT_REQ)
                           轮询 bit3 (IDLE)，30 ms 超时
写 attach-impl-defs        全部按 base + offset 写
arm_smmu_resume()          清 bit2
```

也就是说，**写实现定义寄存器之前必须先 halt micro-MMU**。GPU 的
`0x6000 = 0x2378` 是 `qcom,skip-init` 留下的值。

## 4. `skip-init` + `dynamic` 的含义（本轮关键）

`arm_smmu_attach_dynamic()` 只做三件事：分配 ASID、建页表、记 `cbndx`；
**它不写 SMR/S2CR**。在 `qcom,dynamic` 模型里，stream → context 的映射
（SMR/S2CR）由 bootloader/TZ 预先写好，Linux 复用。GPU 的 `iommus` 是
`<&kgsl_smmu 0>`，即 **SID 0 → CB0**。

因此 H236t 的 70 秒阻塞最可能是：

- bootloader 已把 CB0 置为 enabled（`SCTLR.M=1`），并且 SMR0/S2CR0 已把
  SID 0 指到 CB0；
- C3 没有先读 CB0，就直接写 `CBA2R`（0x1800）和 `CBAR`（0x1000）。
  ARM 规定对 enabled bank 修改 `CBA2R/CBAR` 是 unpredictable；在 msm8998
  的总线上表现为长时间无响应，随后系统继续但不是正常路径；
- `qcom,register-save` 还意味着 SMMU 掉电/复位后寄存器由驱动保存恢复，
  不能在未 halt 的情况下重排。

## 5. ID 读数的异常

H236r 读到：

```text
ID0=00188c41  ID1=00188c81  ID2=48016403  sCR0=00200626
```

按 ARM 解码：

- ID0：S1TS=0、S2TS=0、SMS=0、NUMSMRG=0x41、NUMIRPT=0x18；
- ID1：PAGESIZE=0（4K）、NUMPAGENDXB=0 → 页数=2、NUMS2CB=0x18、NUMCB=0x81=129；
- ID2：IAS=3、OAS=0、UBS=4、PTFS 只有 64K（bit14=1，bit12=0）。

`NUMCB=129 > pages=2` 在硬件上不可能，且 ID2 的 OAS=0 与 40 位 PA 矛盾。
**这组读数不能当作真实 ID**。只剩三种解释，survey 必须一次分清：

1. 该窗口不是 SMMU 的 NS 视图（厂商对部分 SMMU 用 `+0x400` 的 NS 别名）；
2. SMMU 处于未供电/未时钟/复位状态，总线返回的是无意义值；
3. 读到的其实是别的块（例如 0x5040000 被 UEFI/ABL 重映射）。

survey 因此同时读 `0x000/0x020..` 与 `0x400/0x420..`，并把同一寄存器读两次。

## 6. 只读 survey（H236u，`sheng-gpu-power=8`）

`NativeLegacyProbeSupport.cpp` 的 mode 8 在 C2 完成后，**每个心跳只读一个
寄存器**，经 `gc=80+k` 通道输出。这样即使某个读操作卡住，之前的值也已经
在上一个心跳打印出来。

顺序（`gc` 下标从 80 开始）：

```text
 80 0x0000 scr0            81 0x0020 id0            82 0x0024 id1
 83 0x0028 id2             84 0x002c id3            85 0x0048 sgfsr
 86 0x0400 ns_scr0         87 0x0420 ns_id0         88 0x0424 ns_id1
 89 0x0428 ns_id2          90 0x0800 smr0           91 0x0804 smr1
 92 0x0808 smr2            93 0x0c00 s2cr0          94 0x0c04 s2cr1
 95 0x0c08 s2cr2           96 0x1000 cbar0          97 0x1800 cba2r0
 98 0x2000 impl_def0       99 0x6000 micro_mmu_ctrl 100 0x8000 cb0_sctlr
101 0x8004 cb0_actlr      102 0x8010 cb0_ttbcr2    103 0x8020 cb0_ttbr0_lo
104 0x8024 cb0_ttbr0_hi   105 0x8030 cb0_ttbcr     106 0x8034 cb0_contextidr
107 0x8058 cb0_fsr        108 0x9000 cb1_sctlr     109 0xa000 cb2_sctlr
110 0x0020 id0_again      111 0x8000 cb0_again
```

其中 `0x600d0058` / `0x600d0059` 是起止标记，`0xdea00041` 表示映射失败。
boot 参数：`sheng-gpu-power=8`。

## 7. 判读与下一步

| survey 结果 | 结论 | 下一步 |
| --- | --- | --- |
| `cb0_sctlr` bit0=1，`smr0` bit31=1 且 `s2cr0` 低 8 位=0 | bootloader 已把 SID0→CB0 完整映射 | 复用：读出 TTBR0，按厂商页表走一遍看空闲 IOVA；不要写 CBA2R/CBAR |
| `cb0_sctlr` bit0=0，但 SMR/S2CR 已有效 | 映射在、bank 关 | 先 halt micro-MMU，再按 §3 顺序写 CB0 |
| `smr0` bit31=0（无效） | 固件没留映射 | 写 SMR0/S2CR0（SID 0 → CB0）后再使能 |
| `id0/id1/id2` 仍异常，`ns_*` 正常 | 需要 +0x400 NS 别名 | 全局改用 `base+0x400` |
| 两次读 `id0` 不同 / 全 0 / 全 F | 供电、时钟或映射问题 | 先查 `gdsc_gpu_cx`、三路 GCC 时钟和 0x5040000 的归属 |
| `micro_mmu_ctrl` bit3=1 | micro-MMU 处于 halt/idle | 写 CB 前先 resume（清 bit2） |

只有 survey 确认 CB0 的启用状态和 SMR/S2CR 语义之后，才允许再写 SMMU 寄存器。
H236t 的直接写路径已加上保护：先读 `CB0.SCTLR`，bit0=1 时直接以
`fail step 9 (cb0 enabled)` 退出，不再触碰 enabled bank。
