> CLI 向导默认使用 0A 保存并应用。双扇区记录格式与保存语义见 [FLASH_PERSISTENCE.md](FLASH_PERSISTENCE.md)。

# STM32F411CEU6 音击摇杆校准（2026-10-17）

## 数据路径

`firmware/` 使用 ADC1_IN0 / PA0、12 位右对齐、连续转换、DMA2_Stream0 单 halfword 循环更新 `adc_raw`；USB 使用修改过的 ST HID 类，并非 `usbd_custom_hid_if.c`。原有 `lever_calibration.c/.h` 已登记在 Keil 工程中，原主循环已调用 `LeverCal_Convert()`。

两条接收路径为：Interrupt OUT / EP0 SET_REPORT → 63 字节归一化命令 → USB 中断内复制入有界队列 → 主循环每毫秒取一条命令、推进采样、映射、发送 IO4。USB ISR 内不采样、不延时、不写 Flash。

有效配置与待提交配置独立：采集期间持续使用上一份有效配置，首次校准前使用原来的 0..4095 → 0..65535 全量程映射。失败、取消和 USB 重连不替换有效配置。05提交仅写RAM；09/0A写Flash，MCU复位或断电重启后自动加载最新有效记录。普通USB重连取消待提交会话。

## 算法与接口

- 物理左、中、右分别精确输出 `0x0000 / 0x8000 / 0xFFFF`。三点可按任意顺序采集，必须全部完成后提交。
- 同时接受 `left < center < right` 和 `left > center > right`，后者自动设置 `invert`，不需要上位机额外反相。
- 单侧 ADC 行程至少 64 个计数，拒绝同值、不单调、超范围和过小跨度。直接 Capture API 忽略超出 0..4095 的输入。
- 每次位置命令接收后，按主循环 1 ms 节拍读取 200 次 DMA 最新值，四舍五入取平均。相同 tick 不重复计数，主循环跳过的 tick 不补假样本；通常约 200 ms，超过 2000 ms 未完成则失败。依赖当前连续 ADC/DMA 持续转换，软件并未测量实际 ADC 转换时间。
- 200 次读数的最大值减最小值须 ≤64 ADC 计数，否则判定移动或噪声过大。失败的重采集清除该位置的完成位，不能误用先前的点。
- `deadzone=N` 的单位是 **ADC 原始计数，中心左右各 N 个**，默认 0。`center-N .. center+N`（含边界）输出 `0x8000`；两侧剩余行程分别重新线性映射至端点，保持连续。N 必须严格小于两个半行程。设置命令先暂存，提交时按实际锚点验证。
- 映射本身不增加额外低通滤波；200 点平均只用于校准采集。
- 现有接口保留：`LeverCal_Init/Begin/CaptureLeft/CaptureCenter/CaptureRight/Commit/Cancel/IsValid/GetConfig/Convert`。Capture 前须 Begin，直接 Capture 接口参数应为调用者已处理的采样值。
- 新增接口：`LeverCal_HandleCommand(data,63,now)`、`LeverCal_Task(adc,now,adc_available)`、`LeverCal_WriteStatus(status[29])`。所有校准接口仅由主循环调用。

另外修复了原 HID 输入报文的生命周期：异步发送使用静态缓冲，上一帧传输未完成时不改写；单字 DMA 的 HT/TC 中断在启动后关闭，DMA 本身持续运行，错误中断保留，避免每次 ADC 转换都打断主循环。ADC/DMA 错误或未运行时采集失败。

## 上位机协议 v1

这是本工程私有校准扩展，不宣称是标准 IO4 命令。原 `0x10` Report ID 不变；使用 `0xC0 + LC` 标识，普通 `10 01` 等报文不会进入校准。

完整 Output Report **64 字节，以下偏移含 Report ID**：

| 偏移 | 字段 |
|---|---|
| 0 | `10`，HID Report ID |
| 1 | `C0`，私有校准扩展命令 |
| 2..3 | `4C 43`，ASCII LC |
| 4 | `01`，协议版本 |
| 5 | seq，主机递增 8 位序号 |
| 6 | op，见下表 |
| 7..8 | param，uint16 小端，仅 op08 使用；其余须为 0 |
| 9..63 | 必须全 0 |

| op | 动作 | 完成条件 |
|---|---|---|
| 01 | Begin | 重建待提交会话，保留有效校准；deadzone 从有效配置继承 |
| 02 | Capture left | 稳定采集完成后 ACK |
| 03 | Capture center | 稳定采集完成后 ACK |
| 04 | Capture right | 稳定采集完成后 ACK |
| 05 | Commit RAM | 三点、跨度、deadzone 验证成功后原子发布到主循环有效配置 |
| 06 | Cancel | 中止采集、丢弃待提交值，保留有效配置 |
| 07 | Query | 返回当前状态；采样时不会中止采样 |
| 08 | Set pending deadzone | param 为 0..4094，须在会话内 |
| 09 | Save current | 保存已生效配置，须无活动会话；最终 ACK 确认 persisted |
| 0A | Commit and save | 保存完整待提交三点，回读成功后启用；最终 ACK 确认 persisted |

例：`10 C0 4C 43 01 01 01 00 00` 再补 55 个零，开始会话；把 seq 改成 02、op 改成 02 则采左端。不要发送只有 2 字节的短命令。

Interrupt OUT 必须含 Report ID 且长 64 字节。EP0 用 `wValue=0x0210`，支持含 ID 的 64 字节和不含 ID 的 63 字节两种形式；实际收到长度必须等于 SETUP 声明长度。短帧、ID 错误、版本错误、非零保留字节均不能执行有效校准动作。无关普通 IO4 命令仍保留现有计数行为。

主机必须逐条等待匹配 `seq + op` 的最终回执，再发送下一条修改命令。采样中 Begin/Commit/位置/Deadzone 返回 BUSY；Cancel/Query 可用。只有一个主机写入者；协议不提供重传去重或持久事务。不要盲目重试超时命令，应先查询。完成采样时回执归还给原采样命令的 seq/op。

队列容量 4；满时丢弃最新校准扩展命令，计数增加，主机会等待超时而不会收到伪成功。重新枚举时清队列，主循环通过 connection generation 取消旧会话。

## 状态回传

IO4 Input Report 保持 Report ID `01`、总长 64 字节；摇杆仍是 byte1..2，按钮仍是 byte29..32，系统/USB 状态 byte33..34 不变。在原保留区 byte35..63 加入 29 字节状态扩展。旧主机能否接受非零保留区仍需实机游戏联调验证。

下表偏移相对 byte35；所有 uint16 字段均小端。

| 相对偏移 | 字段 |
|---|---|
| 0..2 | `4C 43 01` |
| 3 / 4 / 5 | 最近命令 seq / op / result |
| 6 | flags：bit0 有效、bit1 会话、bit2 采样、bit3 反向、bit4 persisted、bit5 loaded_at_boot、bit6 saving、bit7 flash_supported |
| 7 | 已采集位置位图：bit0左、bit1中、bit2右 |
| 8 | 当前采样位置：1左、2中、4右；0空闲 |
| 9..10 | 已采样数 |
| 11..12 | 当前 raw ADC |
| 13..14 / 15..16 / 17..18 | 有效 left / center / right |
| 19..20 | 有效 deadzone |
| 21..22 / 23..24 / 25..26 | 待提交 left / center / right |
| 27..28 | USB 校准命令队列丢弃累计数低16位 |

result：0成功、1采样中、2非法命令、3无会话、4忙、5缺位置、6非法锚点或跨度过小、7非法死区、8采样不稳定、9 ADC/DMA不可用、10超时、11保存中、12Flash失败、13无有效配置、14不支持的芯片/布局、15回读失败。flags 是即时状态，Query 的 result=0 只代表查询成功，采样是否进行应看 busy/point。

## 使用

关闭游戏或其他会写此 HID 的程序，连接已烧入新版固件的设备。上位机依赖 Python 和 `hidapi`（安装包名为 hidapi，导入名为 hid）。

```powershell
python host/lever_calibrate.py wizard --deadzone 0
```

向导执行 Begin → Set deadzone → 提示物理左端并按 Enter → 中心 → 右端 → Commit and save Flash（0A）。每次按 Enter 后继续保持约 200 ms，直到打印结果再移动。错误或 Ctrl+C 时尝试 Cancel，已提交旧配置保留。参数 `--serial` 用于多设备精确选择，默认不在多个匹配设备中随意挑选。

也可用单命令：

```powershell
python host/lever_calibrate.py begin
python host/lever_calibrate.py left
python host/lever_calibrate.py center
python host/lever_calibrate.py right
python host/lever_calibrate.py deadzone --value 8
python host/lever_calibrate.py commit
python host/lever_calibrate.py status
python host/lever_calibrate.py cancel
```

## 构建与验证

工程入口为 `firmware/MDK-ARM/ChuniF4.uvprojx`，target `ChuniF4`；公开工程已关闭自动烧录。

根目录运行 `python scripts/check.py`。软件测试覆盖映射、Flash 故障注入、主机协议与状态、USB 路由、实际主循环握手和游戏方向转换；不连接实际 USB 或烧录。Keil 构建方法见 [BUILD.md](BUILD.md)，实物与软件证据的边界见 [VALIDATION.md](VALIDATION.md)。

Flash 后端和记录格式见 [FLASH_PERSISTENCE.md](FLASH_PERSISTENCE.md)。上位机图形使用步骤见 [HOST_GUIDE.md](HOST_GUIDE.md)。
