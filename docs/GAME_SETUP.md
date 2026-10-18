# 游戏输入插件

插件支持 Windows x64，按 Segatools MU3 API 1.1 导出 `mu3_io_init/poll/get_opbtns/get_gamebtns/get_lever` 等函数。它只读取配套 IO4 HID，不上传固件，也不发送 Flash 命令。

## 安装

1. 正常关闭游戏及 AM daemon，备份原输入 DLL 和 `segatools.ini`。
2. 将构建得到的 `chunif4_mu3io.dll` 放到游戏启动目录。
3. 把 `game-io/segatools.example.ini` 中的配置合并到自己的 `segatools.ini`，保留其他已有设置。`[mu3io] path=chunif4_mu3io.dll`；`[io4] enable=1`。
4. 启动前连接一个 ChuniF4 设备。插件只接受 VID `0x0CA3` / PID `0x0021`、Input / Output 均为 64 字节的设备；多个匹配设备时拒绝任意选择。
5. 游戏测试菜单先检查「入力テスト」，再进入「レバー設定」，按画面提示扫过左右端，退出保存。

F1 / F2 / F3 分别提供测试、服务、投币操作键。十个游戏按键来自 MCU。配置中的旧鼠标 / XInput 绑定不会替换这个 DLL 的物理摇杆输入。

## 方向约定

固件 `raw_position=0/32768/65535` 表示物理左 / 中 / 右。插件使用 `api_position = raw_position - 32768`，得到 `-32768/0/32767`；mu3hook 再转换为从右到左递增的 ADC 值。旧实现 `32767 - raw_position` 会造成游戏左右反向。

DLL 变化后必须重启游戏，运行中的进程不会自动重新加载它。不要通过倒录固件校准点来绕过插件方向问题。

## 当前边界

- 无 LED 输出实现；两个 LED 回调仅兼容游戏插件加载要求。
- 设备启动时缺席会使 `mu3_io_init()` 返回连接错误；启动前应接入设备。成功初始化后的读取阶段支持断连后每秒搜索重连。
- 超过 100 ms 未收到有效报文则清空按键并将摇杆 API 位置归零，避免卡住输入。
- 不是通用 XInput / 键盘 / 任意 NAGEKI 的转换器。
- 仓库不提供游戏及 Segatools DLL，也不包含联网、机台身份、支付或服务器配置。
