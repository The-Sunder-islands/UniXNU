# 设备与构建链路操作手册（2026-11-08）

本文件记录"Linux 侧工作区 / Mac 构建机 / Windows 控制手机"三台机器的连接方式与
常用命令，供新会话直接读取，不必重新摸索。

---

## 1. 三台机器的角色

| 角色 | 位置 | 用途 |
| --- | --- | --- |
| **Linux 工作区**（当前主机） | `/mnt/data_2048/project/ios/UniXNU` | 源码、文档、打包、日志解析；**不能**运行 adb/fastboot |
| **Mac 构建机** | `lss@192.168.10.103`，`~/UniXNU` | Xcode 构建内核/驱动/显示归档、prelink、插件 |
| **Windows 控制机** | `ls@192.168.10.225` | **唯一**的 adb/fastboot 出口；镜像中转 |

临时目录统一用 `/tmp/opencode`（已预创建）。

### Windows 上的固定路径

```text
adb / fastboot : C:\Users\ls\AppData\Local\Android\Sdk\platform-tools
镜像目录       : D:\Users\ls\Documents\OnePlus5_ios
```

### Mac 上的固定脚本

```text
~/run-h103n-build.sh    内核构建（会自动应用 patches/xnu 并重置 .cache/xnu-work）
~/prelink-h105.sh       生成 kernel.macho（prelink 后的最终内核）
```

---

## 2. SSH 辅助脚本（Linux 侧，/tmp/opencode）

```text
rssh.sh         -> ssh 到 Mac（密码由 askpass.sh 提供）
rscp.sh         -> scp 到/从 Mac
askpass-win.sh  -> Windows 的 SSH 密码
known_hosts     -> Mac 的 host key
known_hosts_win -> Windows 的 host key
```

典型用法：

```sh
cd /tmp/opencode
./rssh.sh 'cd ~/UniXNU && ls'                       # Mac 上执行
./rscp.sh 本地文件 'lss@192.168.10.103:~/UniXNU/路径'  # 传到 Mac
./rscp.sh 'lss@192.168.10.103:路径' 本地文件           # 从 Mac 取回
```

访问 Windows（注意必须带密码/主机指纹参数）：

```sh
DISPLAY=:0 SSH_ASKPASS=/tmp/opencode/askpass-win.sh SSH_ASKPASS_REQUIRE=force \
  setsid -w ssh -o StrictHostKeyChecking=no \
  -o UserKnownHostsFile=/tmp/opencode/known_hosts_win \
  -o PreferredAuthentications=password -o PubkeyAuthentication=no \
  ls@192.168.10.225 '<PowerShell 命令>'
```

传文件到 Windows：

```sh
DISPLAY=:0 SSH_ASKPASS=/tmp/opencode/askpass-win.sh SSH_ASKPASS_REQUIRE=force \
  setsid -w scp -o StrictHostKeyChecking=no \
  -o UserKnownHostsFile=/tmp/opencode/known_hosts_win \
  -o PreferredAuthentications=password -o PubkeyAuthentication=no \
  boot-HNNN.img boot-HNNN.json 'ls@192.168.10.225:D:/Users/ls/Documents/OnePlus5_ios/'
```

---

## 3. 启动镜像到手机（只用 fastboot boot）

**永远不要 flash 任何分区。** 流程：

```sh
# 3.1 镜像传到 Windows（见上）
# 3.2 让手机进 bootloader 并启动
W='C:\Users\ls\AppData\Local\Android\Sdk\platform-tools'
DISPLAY=:0 SSH_ASKPASS=/tmp/opencode/askpass-win.sh SSH_ASKPASS_REQUIRE=force \
  setsid -w ssh -o StrictHostKeyChecking=no \
  -o UserKnownHostsFile=/tmp/opencode/known_hosts_win \
  -o PreferredAuthentications=password -o PubkeyAuthentication=no \
  ls@192.168.10.225 \
  "& \"$W\adb.exe\" reboot bootloader; Start-Sleep -Seconds 8; \
   & \"$W\fastboot.exe\" boot D:\\Users\\ls\\Documents\\OnePlus5_ios\\boot-HNNN.img"
```

- 手机在 **Android** 里：可以直接 `adb reboot bootloader`（上面的命令已包含）；
- 手机在 **bootloader**：直接 `fastboot boot`；
- 手机在 **TWRP**：同样先 `adb reboot bootloader`，再 `fastboot boot`；
- 若 `adb devices` 显示 `offline`，通常 `fastboot` 仍可用。

**镜像大小上限约 81,147,904 B**（payload 76 MiB）；超过会停在 OnePlus logo
（H86f 已确认，见 STATUS）。

---

## 4. 拉取内核持久日志（TWRP）

手机进入 **TWRP**（adb 显示 `recovery`）后：

```sh
cd /tmp/opencode
DISPLAY=:0 SSH_ASKPASS=/tmp/opencode/askpass-win.sh SSH_ASKPASS_REQUIRE=force \
  setsid -w ssh -o StrictHostKeyChecking=no \
  -o UserKnownHostsFile=/tmp/opencode/known_hosts_win \
  -o PreferredAuthentications=password -o PubkeyAuthentication=no \
  ls@192.168.10.225 \
  '$W="C:\Users\ls\AppData\Local\Android\Sdk\platform-tools"; \
   & "$W\adb.exe" shell "blockdev --flushbufs /dev/block/by-name/userdata_ios"; \
   & "$W\adb.exe" shell "dd if=/dev/block/by-name/userdata_ios of=/tmp/h103log.bin bs=4096 skip=12892347 count=8192"; \
   & "$W\adb.exe" pull /tmp/h103log.bin D:\Users\ls\Documents\OnePlus5_ios\h103log-hNNN.bin'

# 取回 Linux
DISPLAY=:0 SSH_ASKPASS=/tmp/opencode/askpass-win.sh SSH_ASKPASS_REQUIRE=force \
  setsid -w scp -o StrictHostKeyChecking=no \
  -o UserKnownHostsFile=/tmp/opencode/known_hosts_win \
  -o PreferredAuthentications=password -o PubkeyAuthentication=no \
  'ls@192.168.10.225:D:/Users/ls/Documents/OnePlus5_ios/h103log-hNNN.bin' \
  /tmp/opencode/h103log-hNNN.bin
```

- `skip=12892347` = `总块数 − 8192`（`userdata_ios` 约 49.2 GiB）；分区大小变了要重算：
  `blockdev --getsize64 /dev/block/by-name/userdata_ios` / 4096 − 8192；
- TWRP 的 `dd` **不支持 `iflag=direct`**，不要加。

---

## 5. 日志解析（Linux）

```sh
cd /mnt/data_2048/project/ios/UniXNU
python3 scripts/h103-log-pull.py --input /tmp/opencode/h103log-hNNN.bin \
        --slots 256 --output /tmp/opencode/hNNN-log.txt
```

**重要：不要直接信任合并文本** —— 窗口里混有多次启动的旧 slot 和 msgbuf 残留。
按 boot 戳过滤的可靠做法（本会话反复用到）：

```python
import pathlib, importlib.util
spec = importlib.util.spec_from_file_location('h103', 'scripts/h103-log-pull.py')
h = importlib.util.module_from_spec(spec); spec.loader.exec_module(h)
window = pathlib.Path('/tmp/opencode/h103log-hNNN.bin').read_bytes()
header = h.decode_header(window)
bodies = h._slot_bodies(window, header)
texts = []
for b in bodies:
    s = h._slot_stamp(b)
    if s and s['build'] == 'HNNN':          # 只取本轮
        texts.append((s['seq'], h._slot_text(b)))
texts.sort()
merged = "\n".join(t for _, t in texts)
```

---

## 6. 构建链路（Mac）

### 6.1 改动类型 → 需要重建什么

| 改动 | 需要执行 |
| --- | --- |
| `drivers/NativeMobileDisplay/*`（显示归档） | `scripts/build-op5-legacy-display.py` |
| `drivers/NativeUfsProbe`、`NativeRamBlock` 等单驱动 | `scripts/build-arm-driver.py <名字>` |
| `patches/xnu/*.patch` | 只需内核构建（脚本自动应用） |
| 插件（`drivers/NativeMetal`） | `build-adreno-metal-plugin.py` + 信任缓存 + addon |

**驱动对象不会随内核自动重编**：改了驱动一定要先 `build-arm-driver.py`，
否则链接会报 `undefined _符号`。

### 6.2 标准顺序

```sh
# 1) 把改动过的文件 scp 到 Mac
# 2) 显示归档（仅当 NativeMobileDisplay 改动）
cd ~/UniXNU/out/cheeseburger/tree && \
  SDKROOT=$(xcrun --sdk iphoneos --show-sdk-path) \
  python3 scripts/build-op5-legacy-display.py
# 3) 单驱动
SDKROOT=$(xcrun --sdk iphoneos --show-sdk-path) \
  python3 scripts/build-arm-driver.py NativeUfsProbe
# 4) 内核（会重置 .cache/xnu-work 并应用 patches/xnu）
rm -rf ~/UniXNU/out/cheeseburger/tree/.cache/xnu-work
cd ~ && bash ~/run-h103n-build.sh > ~/hNNN-build.log 2>&1; echo rc=$?
# 5) prelink
cd ~/UniXNU/out/cheeseburger/kernel && bash ~/prelink-h105.sh
sha256sum kernel.macho
```

已知问题：

- `prepare-xnu-source.py` 偶发 `RemoteDisconnected` / `IncompleteRead`：
  删掉 `.cache/xnu-work` 重跑即可；
- 构建**不要**用 `sleep` 轮询等待；命令返回即完成。

### 6.3 打包（Linux）

```sh
# payload 基线：payload-HNNN.img（76 MiB FAT，含 kernel.macho / adt / trustcache /
# scratch-data.img / gpuaddon / unixnu.toml）
cp payload-<上一版>.img payload-HNNN.img
mcopy -o -i payload-HNNN.img kernel-HNNN.macho '::/EFI/UniXNU/Payloads/kernel.macho'
# 需要时还要替换 gpuaddon / unixnu.toml（boot args 就写在这里）

cd /mnt/data_2048/project/ios/UniXNU-cheeseburger
EDK2_MSM8998=/mnt/data_2048/project/ios/edk2-msm8998 \
  python3 scripts/make-bootimg.py \
  --kernel /tmp/opencode/fw-H103ea.img.gz-dtb \
  --payload /tmp/opencode/payload-HNNN.img \
  --out /mnt/data_2048/project/ios/UniXNU/out/cheeseburger/boot/boot-HNNN.img
```

- boot args 在 `payload` 里的 `::/EFI/UniXNU/unixnu.toml`，用 `mtype`/`mcopy` 改；
- 产物大小应保持 **81,147,904 B**。

---

## 7. 上机节奏与判读

| 场景 | 需要等多久 |
| --- | --- |
| GPUCC/GPU 上电等**早探针**（~40 s） | **约 2 分钟**即可回 TWRP |
| 插件/ABI 路径（250–500 s 才发生） | **10 分钟** |
| 只想看是否黑屏/挂死 | 看屏幕即可，挂死会立刻黑屏 |

**日志窗口的四个坑（务必记住）**

1. 晚探针（~35 s）早于日志 ring 起点（~40 s）→ 打在那里**拉不到**；
2. 一次打印几十行 → 被写入器快照**整批丢弃**；
3. 缓慢分散打印 → 会被 ~371 s 的窗口重置吞掉中段；
4. **可靠方式**：把数值放进心跳行（`NATIVE_ACCEL_HB … gc=<index>:<value>`），
   每拍一个，带序号；解析时按 index 映射。

---

## 8. 设备现状与安全约束

- 只用 `fastboot boot`；**不写** boot/userdata/GPT/固件分区；
- `userdata` 是 Android 的 64 GiB，`userdata_ios` 是 iOS SHNGROOT（49.2 GiB），
  后者是内核读取的对象；
- 手机可停留在 Android / TWRP / fastboot；三者都能通过上面的命令推到 bootloader；
- 总线挂死时（黑屏、adb/fastboot 无响应）→ 长按电源 ~10 秒强制重启。

---

## 9. 当前进度指针

- 路线 C 计划：`plans/oneplus5-cheeseburger/diagnostics/A540-route-C-plan-2026-11-08.md`
- C1 GPU 上电记录：`plans/oneplus5-cheeseburger/diagnostics/A540-C1-gpu-power-2026-11-08.md`
- 全部实验历史：`plans/oneplus5-cheeseburger/STATUS.md`（最新是 H235）
- 下一步：**C1e-2** —— SPMI APID 写入 + PM8005 S1 电压，再重试 GX GDSC。
