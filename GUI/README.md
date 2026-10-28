# GDDAQ-R5560

面向 Linux 的 Qt C++ 图形化采集软件。每个 IP / DAQ 在界面中称为一个 **Mod，固定 32 通道**；一台机箱中若有 4 个 DAQ，应配置 4 个 IP，对应 4 个 Mod，共 128 通道。支持 1～64 个 Mod，实际上限取决于主机、网络和磁盘，64 不是吞吐量承诺。

本程序对应本项目 FPGA 固件的 **125 MHz 时钟、事件格式 2**，同时支持 API `0x00020000` 的 Avalon FIFO 读出和 API `0x00030000` 的 DMA 读出。参数配置、同步控制和波形诊断均保留原 Avalon 地址与 TCP 协议；没有换成 AXI4-Lite。这不是 CAEN 原厂任意固件的通用控制软件。

## 与 GUI27xx 的一致之处

- 保留窄主控窗口、原图标和标志、`File` 菜单、`Exit / Basic / Logic / Online / Debug` 工具栏、Start/Stop、输出目录/文件前缀/Run Number/Save Data。
- 直接复用并小幅适配 `ControlPanel.cc/.hh`、`OutputSettingDialog.cc/.hh`、`images/`、资源文件，保留原作者信息。调整了本机 IP 获取、输出合法性检查和 Qt 6 字体枚举兼容性。
- Basic 沿用左侧 `Mod 0 / Mod 1 ...` 工具箱、右侧 `UserDPP` 通道表；切换 Mod 自动读取该 DAQ，`Read module / Write module` 只操作当前 DAQ。支持 GUI27xx 风格的选中行/列/单元格复制、从其他模块复制参数、Ctrl+C / Ctrl+V 矩形粘贴。
- 在线能量重建直接改编 GUI27xx `RealDecoderThread.cc` 的 UserDPP 公式，使用标准数学函数代替 ROOT TMath，按 R5560 的 24-bit 积分字段解码。
- 保留 SDK 原文件 `R5560_SDKLib.cpp/.h`。实际 TCP 适配器 `DeviceHandle.cc` 使用其中寄存器、内存、FIFO 和内部寄存器的报文布局；用 Qt 实现超时、完整收发和显式小端字节序。API 3 的事件数据由 `DmaReceiver.cc` 通过 ZeroMQ 接收。程序不链接旧 SDK，也不需要 FELib、Dig2 或 ROOT；仅 DMA 模式需要 libzmq 运行库。

27xx 的不同机型、PHA/PSD/ZLE/Scope 参数并不适用于当前 R5560 FPGA。本固件提供积分事件及单通道波形诊断，Debug 页可读取所选通道的波形。USB、ROOT HTTP 服务和 baseboard 时钟路由配置没有移植。

## Linux 编译和运行

依赖：C++17 编译器、CMake ≥3.16、Qt 5.12 或更新的 Qt 5 / Qt 6 的 Core/Widgets/Network/Test、Python 3（离线分析和对应测试使用）。DMA 采集及 DMA 自动测试还需要 **libzmq 4.3 或更新的 4.x 版本**；Ubuntu / Debian 安装 `libzmq5` 即可，程序运行时动态加载，不要求开发头文件。API 2 和 Demo 可在没有 libzmq 时运行。无需 root 权限运行。

Ubuntu / Debian 示例：

```bash
sudo apt install build-essential cmake qtbase5-dev python3 libzmq5
cd software/GUI5560
bash build_linux.sh
./build-linux/gddaq5560 --demo --modules 3
```

Qt 6 可以改装 `qt6-base-dev`。若安装了多个 Qt，可手动指定 `CMAKE_PREFIX_PATH`，或用 `-DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON` 选择 Qt 5。工程的 Qt 版本选择遵循 [Qt 官方 CMake 兼容说明](https://doc.qt.io/qt-6.8/cmake-qt5-and-qt6-compatibility.html)。

也保留 qmake 工程：

```bash
mkdir -p build-qmake
cd build-qmake
qmake ../gddaq5560.pro
make -j4
./gddaq5560 --demo
```

真实连接：

```bash
./build-linux/gddaq5560 --config examples/datagddaq.dat
```

不带参数运行时，通过 `File > Connect to device > Load configure file` 选择 `.dat`。Demo 全程不连接实际设备；多 DAQ 自动模拟共同 RUN，单 DAQ 外部 RUN 模式可使用 Logic 窗口中的 `Simulate shared RUN` 按钮。

Linux 自检：`bash tests/verify_linux.sh`。它编译、运行测试、完成三模块模拟采集、保存界面截图、解码文件并执行符合分析。可选 `docker build -t gddaq-r5560 .` 在 Ubuntu 24.04 / Qt 6 环境验证；Dockerfile 不负责宿主机图形显示转发。

当前开发机验证结果见 [VALIDATION.md](VALIDATION.md)。2026-10-24 已在独立 WSL2 Ubuntu 20.04 x86_64 环境，使用 GCC 9.4、CMake 3.16.3、Qt 5.12.8 和 libzmq 4.3.2 实际编译。Linux交付包内的 `RELEASE.json` 记录该版本的测试结果；物理网卡和实板长期稳定性仍需在采集电脑验证。

Ubuntu 20.04 构建修复：若旧源码出现 `test_r5560 ... undefined reference to pthread_create`，请更新 `CMakeLists.txt` 和 `build_linux.sh` 后重新执行 `bash build_linux.sh`，无需删除已有构建目录。核心读出使用 `std::call_once`，测试使用 `std::async`；现在核心库 `PUBLIC` 链接 `Threads::Threads`，同时覆盖GUI、benchmark和测试。qmake主程序及测试工程均显式使用Linux `-pthread`。构建脚本也兼容CMake 3.16，不依赖 `ctest --test-dir`。

Windows 本机验证使用 `D:\Qt\5.15.2\mingw81_64`。Qt 5 / MinGW 的 CMake 构建会自动将 Qt DLL、MinGW 运行库，以及 `platforms/qwindows.dll` 和 `platforms/qoffscreen.dll` 复制到可执行文件目录。`qwindows.dll` 用于正常桌面窗口，`qoffscreen.dll` 用于自动测试；只有后者时，双击程序会出现 `Could not find the Qt platform plugin "windows"`。更新后重新执行 CMake 构建即可补齐；移动程序时应保留 DLL 和 `platforms` 子目录，不能只复制 EXE。Linux 构建不执行这一步。qmake 构建仍需自行部署匹配的 Windows Qt 运行库和插件。

当前 FIFO 诊断版本输出在 `build-fifo-diagnostics/`，此前仅增加 DMA 构建标识的版本在 `build-dma-ready/`。Windows 的 CMake 构建还会从工作区 SDK 的 `COPY_TO_BIN/v14/x64/` 复制已有的 `libzmq-v140-mt-4_3_4.dll` 和 `libsodium.dll`；移动程序时保留这两个 DLL 及其系统运行库依赖。独立源码包不包含这些 Windows 二进制文件。Linux 默认查找 `libzmq.so.5`。需要指定另一份匹配库时，可设置 `R5560_ZMQ_LIBRARY` 为完整文件路径；旧 SDK 的 libzmq 4.0.4 不满足新版接收器要求。

## DMA 事件读出（2026-10-22）

GUI 根据连接时读到的 API 自动选择传输方式，操作仍使用原来的 Basic、Logic、Start/Stop 和 Debug 界面：

|功能|API 2 固件|API 3 DMA 固件|
|---|---|---|
|参数、状态、同步及底板 RUN|Avalon / TCP 8888|Avalon / TCP 8888，原地址保留|
|Debug 波形内存|Avalon / TCP 8888|Avalon / TCP 8888|
|32 字节事件|TCP 命令 4 读取 Avalon FIFO|FPGA FS_0 → 厂商 DMA → DDR → ZeroMQ TCP 5556|
|保存和离线分析|事件格式 2、`R5560D1` 文件|相同事件和文件格式|

配置文件的 `ports` 可以覆盖控制端口；DMA 事件端口固定为各 DAQ IP 的 5556。只开放 8888 不足以进行 DMA 采集。连接设备仅查询 API、状态和计数，不开启事件流或修改 DMA。API 3 不会退回旧 Avalon FIFO 数据口；加载新 FPGA 固件时必须使用支持 API 3 的 GUI。Demo 继续模拟 API 2。

API 3 连接及每轮准备时，GUI 额外只读查询 `DMA_BUILD_ID`（Avalon 地址 `0x01010016`），并在日志显示各 DAQ 的标识。`0x26102202 (ACK-to-READY repair)` 对应本次 ACK 到 READY 握手修正版；DMA 接口版本仍为 `0x00010000`，API 仍为 `0x00030000`。旧固件返回 `0xDEADBEEF`（或无有效标识的 0 / `0xFFFFFFFF`）时显示 `unknown`，继续按原接口兼容运行；其他未知非零值按十六进制记录，不因此阻断。API 2 不读取这个可选寄存器。Save Data 开启时，`run.json` 的每模块结果及 `.bin` JSON 文件头保存 `dma_build_id`、`dma_build_id_raw` 和 `dma_build_id_recognized`；恢复元数据也记录这些字段。原有事件字节及文件格式不变。该字段用于确认实际加载的固件，不代表已经通过实板重复数据验收。

启动时先确认设备 IDLE、FPGA 本地 FIFO 空、上一轮 DMA 已确认，再连接 ZeroMQ、完成握手并准备厂商 DMA。随后按原有逻辑 ARM；多 DAQ 全部 ARMED 后，通过 Mod 0 的 `sync_reg.1` 发出共同 RUN。**DMA 使能地址为内部寄存器 `0x001e0000`，公共 RUN 地址仍为 `0x00140001`**，两者用途不同。

停止先拉低共同 RUN 并发送 FPGA STOP，停止产生事件，但继续保持厂商 DMA 开启。FPGA 已变为 IDLE 或本地 FIFO 已空，仍可能有事件留在 DDR 或网络队列中。软件持续接收，核对本轮收到的字节数等于 `DMA_TX_WORDS × 8`，检查接收器没有尚未处理的消息或消息尾部，并核对每通道收到事件数与 Accepted 计数；保存数据时还要求文件 flush 成功。之后才写 `DMA_ACK_WORDS` 并关闭厂商 DMA。TX/ACK 的单位是 **64 位字**，正常 ARM 时在 FPGA 中一起清零；它们不是网络包数。

不能用 `DMAEnable=0` 代替 STOP 或排空。工作区厂商 data_mover 的可读源码显示，关闭会清理内部状态和部分数据；部分块依靠开启状态下的超时换块送出。本程序因此等待尾包，不通过关闭 DMA 强制刷新。源码中的约 1 秒换块注释不是实板停止时间保证；停止排空上限为 15 秒，超时、数量不符、事件残缺、连接中断或硬件错误均保留已收数据并将本次采集标为 incomplete。DMA 有待传数据且连续 10 秒无接收进展时也会报告故障，不在同一 run 内自动重连或拼接新流。

TX/ACK 用于防止尚未处理的数据被下一次正常启动静默清除。即使 FPGA 已 IDLE，只要 TX 与 ACK 不一致或本地仍有数据，连接后就进入 `RecoveryRequired`。先使用 **Stop / Recover** 单独保存残留原始字节；恢复期间开启 DMA，不预先关闭、清空或 ARM。只有保存量完整覆盖 `TX−ACK` 才确认排空。如果前一个进程已经消费了部分数据而没有完成确认，当前进程无法重新读取那些字节；恢复会保留可读残留并明确报告无法完整核对，不把原 run 改成 complete。

确认残留已尽量保存、仍需重新采集时，可在 `Device > Reset DMA epoch ...` 中显式确认丢弃剩余数据。该操作仅在 `RecoveryRequired`、所有模块已连接、没有采集/恢复/波形操作、设备已停止且物理 RUN 低时允许；对所有 DMA 模块关闭厂商 DMA、清除 FPGA 数据历元并检查 TX/ACK 和空状态。API 2 模块必须 IDLE，跳过 DMA 重置。此操作可能丢弃未读事件，已有文件保留，无法修复原先不完整的采集。

当前 32 字节事件不带 run/epoch 标签。字节总数、通道计数和额外消息检查能发现多类缺失或残留，但**无法绝对识别跨会话的旧消息**；例如等量旧事件替代新事件，不能仅靠总数证明数据来源。2026-10-24修复版PS新增内部状态握手，GUI在enable/quiesce/reset之后确认状态，显式丢弃时重建发送端；旧PS无此保证。该PS/Windows GUI组合已通过三轮短时实板计数核对和原始数据扫描，随后无线网络路径的Run70仍出现控制超时，故不宣称长期稳定性已解决。Linux版实板与专用交换机测试另行验证，旧Avalon测速不能作为DMA性能结论。

接收器先完整接收单条 ZeroMQ 消息（上限 32 MiB，拒绝多段消息及超限消息），然后再分片交给文件写入和解码。`--fifo-kib 64/128/256` 在 DMA 模式下只选择这个**本地处理分片上限**，不改变底板 DMA 包尺寸、DDR 缓冲区或 FPGA FIFO 深度；完整消息不因分片较小而截断。API 2 下该选项仍是一次 Avalon FIFO 请求的上限。

## 配置文件

原 `../pars/datagddaq.dat` 的结构可直接读取：

```json
{
  "modules": 3,
  "ip": ["162.105.54.90", "162.105.54.187", "162.105.54.201"],
  "par": "setting.json",
  "userpars": "../pars/init.txt"
}
```

`modules` 必须与 IP 数量一致；可选 `daq_ids` 为唯一 32-bit DAQ 编号数组，默认 0、1、2……；可选 `ports` 为端口数组，默认全为 8888。禁止重复 IP+端口或重复 DAQ ID。所有相对路径固定以 `.dat` 所在目录解析，与启动工作目录无关。

**连接只读取设备当前设置。** 需要应用配置时选择 `File > Load configured parameters`。程序先同时验证 `par` 与 `userpars`，全部成功后才写硬件，写后读回校验。`Load Settings` 则直接加载并应用所选 R5560 JSON 文件。`Save / Save As` 保存 Basic 编辑器内容；保存文件本身不写硬件，未写入的编辑会阻止 Start。

连接初始化时，每次 TCP 连接/寄存器请求允许等待 5 秒；仅通信失败时重新建立连接并完整重读，最多尝试 3 次，两次重试前分别等待 0.5、1 秒。已经连接的其他 DAQ 保持连接，只有全部初始化完成、处于 IDLE 且没有未确认 DMA 数据才允许 Start。固件版本不匹配不会重试；非 IDLE 或有未确认 DMA 数据的设备允许连接，但进入 `RecoveryRequired`，等待用户恢复。连接不会清空 FIFO、复位设备或写入参数。

2026-10-14 修正 Windows 偶发连接失败：网络收发改为工作线程中的 Qt 事件通知，避开 Qt 文档指出在 Windows 上可能偶发失败的阻塞 `waitFor*` 调用。2026-10-17 将运行期的 1.5 秒直接失败改为 **1.5 秒告警、同一次请求总计最多等待 5 秒**，详见下文。采集、FIFO 读取或参数写入不会重放。超时日志包含模块、IP/端口、命令、寄存器字地址、回复字节数、耗时、socket 状态及工作线程 UTC 时间。`reply 0/4 bytes` 表示尚未收到回复，`reply 1/4 bytes` 表示回复未完整到达，并不能仅据此判定 FPGA 寄存器有误。

出现连接错误可使用 `File > Disconnect`，再 `Connect to device`，无需退出软件；正在连接/重试时也可 Disconnect 取消。持续失败时保留 Debug 日志，检查对应 DAQ 网口的连接和板端通信服务，以及是否有其他采集程序正在访问同一 DAQ。参考：[Qt 5 网络等待函数的 Windows 注意事项](https://doc.qt.io/qt-5/qabstractsocket.html#waitForReadyRead)。

原 `pars/init.txt` 中 `/par/StartSource`、`EncodedClkIn`、`/par/SyncOutMode` 属于 27xx FELib，不能转为当前 Avalon 地址直接写入。会明确报错，原文件不被覆盖。配套的新格式示例：

- `examples/datagddaq.dat`：三个模块，使用用户原示例中的 IP。
- `examples/setting-r5560.json`：每个模块完整 32 通道，独立参数与使能掩码；同步默认 `0x03`，外部 RUN=SYNC1、T0 关闭。
- `examples/init-r5560.txt`：支持 `模块索引 /ch/起始..结束/par/参数名 值`，例如 `0 /ch/0..31/par/THRE 80`，支持十六进制值和 `#` 注释。

参数含义与范围：

|参数|范围|单位/说明|
|---|---:|---|
|POL|0～1|0 负极性，1 正极性|
|THRE|0～16383|14-bit ADC 阈值|
|TRIGFL、TRIGFLFG|1～127|采样点，FL+FG ≥ FL|
|BLLEN|1～1023|采样点|
|XIAOFFSET|0～255|采样点|
|XIASL、XIASG|1～1023|采样点；本 RTL 避免零长度积分窗|
|TAU|1～4294967295|在线公式使用 ns；RTL 仅存储该寄存器|

一个采样点 8 ns。示例 SL=625（5 µs）、SG=125（1 µs）。其余默认值是调试示例，应根据探测器调整；不是现有硬件参数的实测结果。硬件虽然保留 32-bit 寄存器读回，GUI 会按实际使用位宽检查，防止高位被截断造成误解。

### 首次配置与 XIASL 为 0 的报错

当前 FPGA 的通道参数寄存器复位值为 0，连接时 GUI 读取实际值，不自动把示例参数写入设备。`XIASL` 的有效范围是 **1～1023 个采样点**；显示 0 时必须先完成配置。

Basic 的 `Write module` 只检查并写入**当前显示 Mod 的全部 32 个通道**。其他 DAQ 即使参数仍为 0，也不会阻止该模块写入。表格的选中范围只影响 `Copy`，不限制当前模块的写入范围；只填写 ch0 时，当前模块其他通道尚未配置仍会被拒绝。

左侧切换到某个 Mod 时会自动读取该 DAQ 的实际参数；`Read module` 也只刷新当前 DAQ。**未写入的编辑不会在切换后保留，请先 Write module 再切换。** 读取/写入期间暂停编辑和模块切换，完成后保持所选模块，避免异步返回的数据显示到错误模块。

建议先填写一个通道的完整参数，使用 `Row` 选择目标通道并 `Copy`，然后 `Write module` 写入当前 DAQ。其他 DAQ 可逐个配置；若参数相同，切换到目标 Mod，使用 `Copy from module ...` 选择源 Mod：软件读取源 DAQ，把其 32 通道参数及 Enable 复制到当前编辑器，保留当前模块同步配置；再点击 `Write module` 才写入当前 DAQ。此操作不会批量写入其他模块。

2026-10-14 按 GUI27xx 的当前模块操作方式修正了原先 Basic `Read All / Write All` 对所有 DAQ 生效的问题。范围错误会显示模块、通道、当前值和范围，并定位对应参数格，例如 `Mod 1: Ch 0 XIASL: got 0; expected 1..1023`。当前模块写入前检查失败会提示 `No registers were written`，保留当前页编辑值，并记录到 Debug 日志。这种错误发生在发送写命令之前，不等同于 FPGA 写后读回失败。

`File > Load Settings / Load configured parameters` 仍按完整配置文件对全部模块应用；保存文件也包含全部模块。`Logic > Write All` 用于全系统同步设置，Start 会检查所有参与采集的模块。这些全系统操作与 Basic 当前模块读写是不同的操作范围。

## 采集和同步操作

Basic 参数复制沿用 GUI27xx 的 `Select: Row / Column / Item`、`copy from ch - XX`、`Copy` 操作：

- 在当前 Mod 中，通过 `copy from` 选择源通道（0～31）。源数据取自当前表格，包含尚未 Write module 的编辑。
- 选择 `Row`，再在表格中选择目标通道行；按住 Ctrl 可多选不连续通道，Shift 可选择连续范围。点击 `Copy`，只向选中行复制全部参数和 Enable 状态。
- 选择 `Item`，可只选若干通道的某几个参数格；点击 `Copy`，每个目标格取源通道同一列的值，其余参数保持原值。Enable 只在其单元格被选中时复制。
- 选择 `Column`，可将源通道的某项参数复制到当前 Mod 的整列；选中多个列则复制多项参数。
- 没有选中目标时给出提示，不执行复制。源通道自身不变，其他 Mod 不受影响。复制只修改编辑器；`Write module` 才写入当前设备并读回校验，`File > Save` 保存编辑后的完整配置。
- `Copy from module ...` 从指定 DAQ 读取全部 32 通道及 Enable，复制到当前 Mod 编辑器；只有按下 `Write module` 才写入当前 DAQ。局部通道复制使用上述表格选择操作。采集期间复制控件与参数编辑一同禁用。

界面示例见 [参数复制截图](docs/BasicSettings-copy.png)。

1. 先用 CAEN 的底板配置工具设置主从时钟、时钟路由与 SYNC 接线；各 DAQ 使用同一个参考时钟，并分配同一个物理 RUN。软件中的 `Clock Locked` 只能确认本机锁定，不能证明各模块已同相。
2. 连接所有模块，确认均为 Idle。设置 Basic 参数与输出。Logic 中选择共同的 RUN/T0 输入并 Write All；跨模块通道参数可以不同，同步路由设置必须一致。单模块支持软件启动，多模块要求外部共同 RUN。
3. 多 DAQ 点击 Start 后，程序先通过配置列表中的第一个模块（Mod 0）将底板 `sync_reg.1` 置 0，确认 Mod 0 的 FPGA RUN 输入为低；随后准备所有模块、创建输出文件并 ARM。所有 DAQ 报告 ARMED 后，仅向 Mod 0 写 `sync_reg.1=1`，由底板分发共同 RUN。全部 DAQ 报告 RUNNING 后才显示 Running；5 秒内未全部运行则报错并尝试停止。单 DAQ 外部 RUN 模式仍手动施加沿，单 DAQ 软件启动模式保持原行为。
4. Online 显示 32 通道输入/输出率、Accepted/Rejected、主机收到的事件数、拒绝比例、MB/s，以及选择通道的能谱。支持诊断版本 v1 的固件还显示两类拒绝计数、通道/DMA FIFO 峰值和最长 DMA 停顿，详见下节。能量公式与实际幅度标定需使用脉冲源核验；原始积分一直保存在文件中。
5. 多 DAQ 点击 Stop 时，先通过 Mod 0 将 `sync_reg.1` 置 0、确认其 FPGA RUN 输入为低，再向各 DAQ 发送 STOP 并排空 FIFO。物理 RUN 提前下降也会结束整组采集并拉低底板寄存器。正常停止不发 CLEAR；未确认所有模块退出、RUN 变低及数据保存完成时，不报告正常完成。

T0 是时间标记，不清零事件时间戳。Logic 显示各模块的 T0 计数和最后一个 T0 的时间（8 ns/tick），并写入 run.json；只有各模块捕获的是同一个标记时才适合相互比较。每次共同 RUN 上升沿开启新的时间历元；一个 GUI run 只允许一次。`START_COUNT` 是固件累计值，GUI 使用本次的增量。

发生掉线、时钟错误、固件溢出错误、分包损坏、磁盘失败或停止超时，整组尝试停止并标为 Fault；已收到的文件保留，run.json 标为 incomplete。不能确认 STOP 的掉线模块应先在硬件上拉低 RUN。故障模块可能继续运行或因残留 FIFO 停在 DRAINING；新版允许重新连接后按下述流程恢复。修改参数若部分写入失败也会中止并要求重新连接/读取，不能将其视为已全部成功。

### Online FIFO 与拒绝原因诊断（2026-10-23）

本轮 GUI 构建目录为 `build-fifo-diagnostics/`，配套诊断固件构建标识为 `0x26102301`。是否显示数值取决于硬件诊断接口版本 `0x00010000` 及完整有效快照，不只依据构建标识。`0x26102202` 等旧固件仍可采集；没有映射的、未知版本或尚未读到的诊断显示 **N/A**，不显示为零，也不会据旧总拒绝计数推算分类。

Online 保留 Module 选择和原有 Rate / Energy Spectrum 操作。新增数值均对应当前所选的 32 通道 DAQ：

|显示项|含义与单位|
|---|---|
|Processing busy rejected|本轮因事件处理忙、且下游 FIFO 不忙而拒绝的累计触发数|
|FIFO busy rejected|本轮因下游 FIFO 忙而拒绝的累计触发数；处理和 FIFO 同时忙时归入此类|
|FIFO peak / depth (events)|每通道 FIFO 最大占用 / 配置深度，单位是完整的 32 字节事件|
|DMA FIFO peak|当前 DAQ 的 FPGA DMA 出口 FIFO 最大占用 / 配置深度，单位同上；不是 PS DDR 缓冲或网络队列占用|
|Longest DMA stall|DMA 输出存在有效数据但下游未准备好（TVALID=1、TREADY=0）的最长连续时间，包括正在进行中的停顿|

两种拒绝分类互斥：`Rejected = Processing busy rejected + FIFO busy rejected (mod 2^32)`。这些列沿用 Accepted/Rejected 的累计计数风格，并非每秒速率；32 位拒绝计数长时间运行可能回卷。Input / s、Output / s 仍为速率，Received 仍为主机的 64 位计数。

硬件实时的峰值、最长停顿及分类计数在 ARM 时清零，STOP 后保留，下一次 ARM 才开始新统计历元。ARM 不直接改写已经公布的寄存器快照；后续 SNAPSHOT 才公布清零后的新值，GUI 等完整快照后更新显示，不将混合新旧字段拼成一组。Longest stall 根据快照的 `clock_hz` 换算：125 MHz 时一周期为 8 ns；小于 1 ms 显示 `us`，其余显示 `ms`，同时保留原始周期数。时钟未知时显示 N/A，不擅自用默认频率代替。深度从硬件读取，不能用峰值列推断整机 1 GB DDR 的使用量。

界面显示整组诊断寄存器**读取完成的 UTC 时间**、距该次完成的时长、快照序号及启动计数；该时间不是硬件精确采样时刻。距最后一次成功读取完成超过 5 秒或模块断线时，保留最后有效值并标记 **STALE**；这不表示当前占用仍等于旧值。新历元还没有完整快照时，先显示 N/A，避免把上一轮峰值误认成本轮结果。此处的刷新只显示工作线程已有快照，不由界面额外连接或重试硬件。

### 断线后的 Stop / Recover（2026-10-16）

旧版 `build-connection` 在连接时要求 IDLE，导致 `Device is not IDLE; stop and drain before changing settings`；此时主机尚无采集会话，旧 Stop 按钮不会向设备发送停止命令。该日志意味着设备已回复寄存器，不等于 TCP 连接失败，也不能说明第一次断线的原因。关闭 GUI 或网线断开不会自动让 FPGA 返回 IDLE。

1. 关闭旧 GUI，运行最新版 `build-dma/gddaq5560.exe`（Linux 更新源码后重新编译）。连接原来的 DAQ 配置。日志显示各 DAQ 的状态、状态字和错误字；任何 DAQ 非 IDLE 或有未确认 DMA 数据时，主界面进入 `RecoveryRequired`。
2. 在 Output settings 中选择可写且有空间的输出目录，点击 **Stop / Recover**。多 DAQ 先通过 Mod 0 将底板 `sync_reg.1` 拉低，再对配置中的所有已连接 DAQ 关闭软件 RUN、发送 STOP，随后将残留 FIFO 数据保存到独立的 `recovery-UTC时间-随机编号/` 子目录。此恢复始终保存数据，不受 Save Data 勾选影响；连接本身不会启动恢复。
3. 目录中的 `M0.fifo.raw`、`M1.fifo.raw` 等是无文件头的小端原始字节；`M0.json` 等记录原硬件 DAQ ID、RUN ID、参数、状态、错误及保存字节数，`recovery.json` 记录整组结果。恢复不写通道参数/同步配置/DAQ ID/RUN ID，不 ARM、不 CLEAR，也不清除错误标志。
4. 全部 DAQ 报告 IDLE 且事件数据已排空才算恢复成功；DMA 模块还必须完整核对 `TX−ACK` 的字节数和接收器中的残留，不能只检查 FPGA FIFO 空。成功后可再次配置和采集，Run Number 不增加。若存在硬件错误，先检查原因，再在 Debug 中使用 `Clear hardware error flags (IDLE)`；多 DAQ 新采集由程序控制底板 RUN；单 DAQ 外部 RUN 模式仍需手动提供共同上升沿。
5. **Cancel recovery** 可中止恢复；已保存文件保留，未读取 FIFO 不会被清空。单个 DAQ 失败时其余 DAQ 继续保存；停止后 15 秒仍不能排空则报错，另外网络请求仍受各自超时限制。失败后保留文件和日志，Disconnect/Connect 可重试。没有有效会话或可恢复连接时，Stop 禁用，不再出现可点但无动作的情况。

恢复只能保存板上还剩的数据。断线前可能有 FIFO 字被板端取出、但未完整送达主机，因此恢复文件的事件边界标记为未知，不追加到旧 `.bin`，不能直接交给现有离线解码/符合分析工具。`recovery_complete` 只表示本次残留数据读取和排空成功，**不会把原采集标为完整，也不能补回丢失数据**。恢复期间禁止启动、改参数和波形抓取；关闭窗口会等当前恢复结束。

### 底板 RUN 自动控制（2026-10-19）

- 本工程要求底板将软件 `sync_reg.1` 路由到 FPGA 的 `SYNC_1(0)`，这是可配置的连接，写寄存器本身不会设置路由。新版默认 `sync_config=3`（`0x03`）：外部 RUN=SYNC1，T0 关闭。旧参数文件中的 `41`（`0x29`）选择 RUN=SYNC0、T0=SYNC1，需要在 **Logic → Run / Sync** 改成 External shared RUN、RUN input=SYNC 1、取消 Capture T0 marker，再 Write All。多 DAQ Start 会在任何控制写入前拒绝错误的 RUN 输入，不会静默覆盖旧参数。
- 当前 Windows 版本为 `build-dma/gddaq5560.exe`，保留此前的底板 RUN 控制。固件 `0x20261019` 及后续本项目固件的上电默认配置为 `0x03`；已有软件若重新写入旧配置，仍会覆盖固件默认值。单 DAQ 软件模式继续支持，需明确选择 Software start。只有确认独立 T0 接线后才启用 T0，不能与 RUN 共用 SYNC1。
- 多 DAQ 使用配置文件顺序中的 **Mod 0**，不是当前参数窗口选中的模块，也不是 DAQ ID 最小的模块。
- 通过现有 Mod 0 工作线程和 TCP 连接发送与 `NI_InternalWriteReg(value, 0x00140001, handle)` 完全相同的报文：命令 **5**，一个 32 位字。不能用用户 FPGA 寄存器写命令 0 替代；无需额外安装 SciSDK，也不创建与采集并发的第二条正常控制连接。
- 底板须预先把软件 **sync_reg.1（第二个寄存器）** 路由到所有 DAQ 的 **SYNC_1（第二条线）**；界面编号差异见下节。跨硬件还须配置 SYNC OUT/IN 分发和共同参考时钟。本程序不自动修改底板路由或时钟。软件写一个寄存器，由物理线路分发同步沿，不依次启动各个 DAQ。
- 启动前置 0；所有模块 ARMED 后置 1；正常停止、故障停止、Stop / Recover 以及正常关闭中的停止流程均尝试置 0。取消准备/ARM 会禁止排队中的 RUN=1。命令 5 无应答，日志区分“已发送”和 FPGA 状态确认；长时间断网仍无法保证拉低。
- DEMO 多模块也自动启动；单模块原有两种启动方式保持不变。运行记录保留 `shared-external-RUN`，另加 `run_control` 描述控制模块、内部地址及发送/确认时间。

### 同机箱四 DAQ 的底板 RUN 路由与诊断

依据工作区根目录的 CAEN `UM6952_R5560_R5560SE_manual_rev7.pdf`，印刷页 39–40、44–45，底板可为三条 DAQ 同步线分别选择来源。同机箱四个 DAQ 共用底板设置，独立 DAQ 网口负责读出；四个 DAQ 都 ARMED 不代表同步线来源已配置。

1. 在前面板进入 **SETTINGS → DAQ SYNC**；或者通过**底板控制网口的 IP** 打开 Web 页，进入 **I/O Settings → DAQ Sync In**。这里是底板管理页面，和四个 DAQ 各自的读出 IP 不同；使用前面板不需要更改采集网线。
2. 将 **第二条 DAQ 同步线** 的来源设为软件 **sync_reg.1（第二个 SYNC REG）**，保存/Apply。说明书 Web 截图的线编号是 `0/1/2`，选 `1`；触摸屏线编号是 `1/2/3`，选 `2`。来源菜单与目标线编号是两回事：厂商旧界面把两个软件来源显示为 **SYNC REG 1 / SYNC REG 2**，当前软件写 `0x00140001` 应选择 **SYNC REG 2**。若实际界面明确按 `0/1` 从零编号，则选择 `1`。
3. GUI 中各 DAQ 选择 External shared RUN、RUN input=SYNC 1、T0 关闭（`sync_config=0x03`），写入并读回。随后 Start：程序先拉低、全部 ARM、再拉高一次。已有 `0x20261019` 固件不需要因修改底板路由重新编译。

编号依据：[厂商同步应用说明 AN4](https://www.sci-compiler.com/application-notes/attachments/AN4-R5560-Multi-Device-Syncronization.pdf) 第 5、7、8 页的界面使用 Sync Reg 1，而配套代码调用 `SetSyncRegister(0, value, ...)`；[示例实现](https://gitlab.sci-compiler.com/examples/application-notes/sync-multiple-x5560/-/blob/0f5a46f2999308243fa01441013b9cbe5a1829f7/Software/C/lib/src/VTC_A_lib.c#L141) 的地址为 `(20 << 16) + index`，因此示例对应 `0x00140000`。本工程按用户要求使用 SciSDK `sync_reg.1`，即 `0x00140001`，需要选下一个软件来源，不能照抄 AN4 截图中的来源名称。不同底板系统版本若标签有变化，应以实际 RUN 输入状态验证映射。

“sync_reg.1=1 sent”仅表示命令 5 报文已发送，该命令无 ACK。“FPGA RUN input is LOW”只是拉高前电平低，**不能证明路由正确**。`SYNC_STATUS.bit5` 才是所选 RUN 的实际输入电平。

新版在全部 ARMED、请求 RUN 后仍等待启动、全部 RUNNING、以及启动失败即将 STOP 时，输出每个 DAQ 的 `RUN diagnostics`。包含 RUN 电平、ARMED/RUNNING、CLOCK_GOOD、状态/错误字、本次 `start_delta`、准备时同步配置、最近状态读取 UTC 和样本年龄；使用工作线程现有快照，不额外发网络请求。`sync_config_at_prepare` 是准备时确认的配置，不声称在日志时刻重新读回。样本较旧时应结合对应线程的网络日志判断。

- 四个 DAQ 都是 `RUN=0 ARMED=1 RUNNING=0 start_delta=0`：各输入尚未看到上升沿，优先核查底板同步线来源、寄存器编号和控制写入路径。
- 部分 DAQ 为 `RUN=1 RUNNING=1 start_delta=1`，其余仍 ARMED：共享沿未到达所有 DAQ，按日志模块定位分发/配置问题。
- 四个 DAQ 都为 `RUN=1 RUNNING=1 start_delta=1`：本次共同启动已被各固件确认；这仍不替代用共同脉冲测量跨 DAQ 时间偏差。

启用 Save Data 时，`run.json` 的 `run_diagnostics` 保留上述启动过程和 STOP 前的快照；最终 `results` 记录停止后的状态，两者不会互相覆盖。未启用保存时仍输出日志。此改动不自动设置底板、不重复发送 RUN=1，也不放宽启动超时。

### 短时网络异常与故障停止（2026-10-17）

读出速度优化和耗时日志见“读取效率与诊断”一节；网络事务仍保持原有超时和失败处理。

- 普通请求在 1.5 秒未完成时记录 `Slow transaction`，继续接收原请求，最多等待到该请求发出后的 5 秒。收到部分头部/数据不会重新计时，也不会重发命令。5 秒内完整到达则记录 `Slow transaction completed`，继续原采集并照常检查 FPGA 错误标志；网络恢复不代表 FIFO 一定没有溢出。
- FIFO 回复的 4 字节数量头和后续数据共享同一个 5 秒期限。`reply 0/4` 不是“FIFO 空”：空 FIFO 也应返回数量为 0 的完整 4 字节头。协议长度错误、明确断线等仍立即判故障，不强行等到 5 秒。
- 最终失败时立刻进入 Fault。多 DAQ 优先通过仍可用的 Mod 0 拉低共同 RUN，再通知其他 DAQ 停止/排空；如果 Mod 0 自身故障，其他模块立即 STOP，不等待 Mod 0 重连。故障 DAQ 若发生传输错误，丢弃旧连接，最多用两次新连接尝试 STOP；整个停止恢复最多约 5 秒，每个连接/请求最多 1.5 秒，后续操作使用剩余总预算。新连接先验证固件 API/时钟；多 DAQ 的 Mod 0 还会先写底板 `sync_reg.1=0`，然后写软件 RUN=0 和 STOP，并读取状态确认已进入 DRAINING 或 IDLE（Mod 0 同时确认物理 RUN 低）。
- 新控制连接不读 FIFO、不重发失败命令、不写通道参数/同步配置/DAQ ID/RUN ID、不 ARM、不 CLEAR，也不自动续接原采集文件。确认停止后仍保持 Fault，原 run 标记 incomplete；残留数据需 Disconnect/Connect 后用 Stop / Recover 单独保存。
- 日志明确区分 `STOP confirmed` 和 `STOP NOT confirmed`。`run.json` 各模块结果新增 `fault_utc`、`last_status`、`fault_stop_attempted`、`fault_stop_confirmed`；确认 STOP 不等于 FIFO 已排空。工作线程日志中的 `worker_utc` / `request_start_utc` 为 UTC，主窗口行首仍为本地显示时间，便于识别 GUI 消息排队造成的显示延迟。
- 最终传输失败时，若有异常携带的已接收字节且本次输出文件已打开，会另存为 `原文件名.partial-reply.raw` 及同名加 `.json` 的说明文件。异常可能来自 Avalon TCP 控制/旧 FIFO，也可能来自 DMA 事件流，说明文件按实际来源记录 `transport`、`data_kind` 和是否包含线缆协议头；不能仅凭固件为 API 3 就把控制回复当成事件。
- 在 DMA 采集中，完整消息可能尚未全部交给解码器。即使故障来自控制查询、硬件错误或其他处理，软件也会在关闭/替换连接前提取接收器当前持有的未消费消息尾部，保存为独立的 `原文件名.dma-pending.raw` 和说明文件。它不包含 Avalon 数量头，不计入原 run 已处理的字节数，不自动解码或拼入 `.bin`。这些附件仅供诊断和后续人工核对，不能直接交给离线事件解码器。Save Data 关闭且没有输出文件时，只记录字节数；手动恢复过程本身始终有输出文件。此保全仅覆盖已由接收器持有的字节，不承诺重放板端或网络中其余数据。

等待和重连在 DAQ 工作线程中进行，GUI 可继续响应，但同一个 TCP 连接中的命令保持串行：等待半包时点击 Stop，需待当前回复完成或最终失败，不能插入另一个读取导致报文错位。5 秒是网络请求上限，不保证 FPGA 能缓存 5 秒数据；若实际 FIFO 溢出，仍会报告采集失败。长期断网且 STOP 无法确认时，软件无法保证设备停止；外部 RUN 模式可拉低公共 RUN，软件模式必须恢复控制连接再停止。

## 输出和离线分析

保存结构延续 GUI27xx：`输出目录/Run号/前缀_R号_M模块.bin`，另有 `run.json` 保存配置、DAQ ID、同步方式、运行结果、异常与计数。已有 Run 目录或数据文件不会覆盖；成功完成后 Run Number 自动加一。未选 Save Data 时仍在线统计，但不创建数据文件。

`.bin` 格式：8 字节 `R5560D1\n`、4 字节小端 JSON 头长度、UTF-8 JSON 头、连续的原始 32 字节事件。JSON 头含 module_index / daq_id / run_id、125 MHz、事件格式、参数快照和模拟标记。

|事件字（32 bit 小端）|内容|
|---|---|
|0|当前触发时间戳低 32 bit|
|1|低 16 bit=当前时间戳高位；高 16 bit=前次触发时间戳高位|
|2|31:27 通道号；26:24 保留 0；23:0 baseline A|
|3|23:0 baseline B|
|4、5、6|23:0 SL1、SG、SL2 积分|
|7|前次触发时间戳低 32 bit|

时间戳 48 bit，单位 8 ns；模块号由文件头关联，不改动 FPGA 事件包。没有 magic 的旧裸 FIFO 文件、27xx 数据文件不能直接当成本格式读取。

`Tools > Offline analysis / Coincidence` 可以解码 CSV，或选取同一已完成 run 的两个模块各一个通道做符合；定义 `dt=(B时间+修正值)-A时间`，导出所有 `|dt|≤半窗宽` 的事件对，同一事件可参与多对。数据通过临时 SQLite 数据库排序，不假定 FIFO 输出已按事件时间排好。窗口边界包含在内，偏移和窗口单位 ns。

符合分析依赖事先验证的公共时钟、RUN 和线缆延迟标定。工具验证文件元数据，但不能由元数据证明物理同步。检测到 48-bit 回卷或跨度 ≥半个回卷周期（约 13 天）时拒绝分析，建议拆分为较短 run。临时磁盘空间需容纳参与通道的事件索引。分析取消/失败后新 CSV 可能不完整，日志会提示；旧 CSV 从不覆盖。

也可命令行使用，无额外 Python 包依赖：

```bash
python3 tools/r5560_data.py decode data_R1_M0.bin events.csv
python3 tools/r5560_data.py coincidence 1/data_R1_M0.bin 1/data_R1_M1.bin pairs.csv \
  --channel-a 0 --channel-b 0 --window-ns 40 --offset-ns 0
```

计数寄存器 Accepted/Rejected 是 32-bit，可在长时间高计数率运行时回卷；主机 Received 使用 64-bit。`complete` 表示文件流程和排空成功，不表示探测器没有拒绝事件，也不保证断电时操作系统缓存已落到介质。

## 读取效率与诊断（2026-10-19）

以下是 API 2 的 Avalon FIFO 调度，2026-10-19 的 `build-blocksize` 和新版 `build-dma` 均保留；Linux 源码相同。API 3 使用前述 DMA 事件流，状态和计数查询仍走 TCP 控制连接。

- 默认每次请求最多 16384 个 32-bit 字（64 KiB），也可按下节选择 128/256 KiB。只要收到非空数据，就继续处理命令、必要的状态检查并读取下一块；非空短包不再触发固定等待。只有 FIFO 返回空时才退避，避免空载忙轮询。Stop / Recover 保存残留数据时也采用这一规则。
- 原来的 250 ms 状态检查包含三个独立请求，现改为从 SYNC_STATUS 到 START_COUNT 的一次 8 字读取。该范围没有读取副作用，不包含 FIFO 数据口或 SYNC_COMMAND。仍检查时钟、错误和重复 RUN，不降低检查频率；这组硬件寄存器不是原子快照。
- 采集中每轮 FIFO 读取后最多执行一项通道计数查询，避免整组统计查询连续阻塞事件读出。每组结束后约一秒启动下一组；四组通道数据齐全后才整体更新显示。T0 的计数读取仍覆盖 SNAPSHOT 前后，变化时保留上次一致的 T0 记录。停止后排空 FIFO，再完整读取最终计数。
- 每个 DAQ 仍使用一个工作线程和一条正常 TCP 连接，不增加并发 FIFO 请求，不重发超时事务，不在断线后续接旧 run。没有改事件格式、FPGA、寄存器配置或单次网络请求期限。

Debug 的 Run log 在采集期间约每 10 秒显示 `Mod N: Readout interval:`，结束或故障时显示尾段 `Readout final interval:`。包含该模块的 MB/s、每秒 FIFO 请求数、平均 KiB/请求、空回复数和非空短回复数，以及以下耗时占该时间段的比例：

|字段|含义|
|---|---|
|receive（旧日志为 FIFO）|API 2 为发送请求至收到完整 FIFO 回复的耗时，包含板端处理及网络等待；API 3 为读取 DMA 事件流及本地分片的耗时，不是 Avalon 请求时间；JSON 中保留原 `fifo_ms` 字段名|
|disk|主机文件写入，正常结束时包含 flush；可能写入操作系统缓存，不代表存储介质极限|
|decode|事件解码、能量计算及直方图统计|
|status|状态、错误和启动计数查询|
|counters|T0、各通道计数查询|
|waveform|波形诊断处理|
|idle|空 FIFO 后的线程等待|

日志每次只描述最近一个区间，各比例不包含所有循环/控制开销，因此不要求恰好相加为 100%。平均包大小包含空回复；short 不包含空回复。API 2 请求数统计完整收到的回复，失败请求的等待时间仍计入耗时；API 3 的 calls 和平均大小对应主机本地读取调用/分片，不能直接当作 ZeroMQ 消息数和板端包尺寸。仅输出汇总，不逐包刷日志。

开启 Save Data 时，`run.json.results[N].readout` 保存从 ARM 到正常完成或检测到故障的累计字节数、完整 FIFO 请求数、空/短包数及各项毫秒数；故障后的 STOP 恢复不计入该采集阶段。未开启保存仍输出区间日志。结束后的空闲轮询不会继续改变这些统计。

Online 的 MB/s 是所选 DAQ 实际收到的事件字节率，不是带宽上限测试。事件为 32 字节；应结合实际接信号的通道数、输入计数率、Accepted/Rejected 与日志判断。旧固件的 Rejected 无法区分事件处理忙和下游 FIFO 忙；支持诊断 v1 时分别显示两类累计数。FIFO busy 也不能直接等同网络丢包，应结合 FIFO peak 和 DMA stall 判断。进行吞吐比较时建议关闭 Continuous 波形诊断，保持相同输入、保存设置、网络和参数。

### 比较 64 / 128 / 256 KiB 请求

当前 `build-dma-ready` 版本继续支持 `--fifo-kib 64`、`--fifo-kib 128` 和 `--fifo-kib 256`，默认仍为 64。Windows 可双击同目录 `start-64KiB.cmd`、`start-128KiB.cmd`、`start-256KiB.cmd`；它们启动同一个程序，窗口标题显示所选大小。启动后按原有方式连接和采集，不要同时启动多个程序连接相同 DAQ。Linux 可执行 `./gddaq5560 --fifo-kib 128`。

API 2 下块大小控制本次采集的 Avalon FIFO 请求上限，API 3 下控制完整 ZeroMQ 消息接收后的本地处理分片上限；均不修改 FPGA 参数或底板 DMA 包尺寸，采集中不能切换。一次返回可小于上限或为空。`Readout interval` 的 `request` 字段、`run.json.fifo_request_bytes` 和 `results[].readout.request_bytes` 保存选择；short 根据当前上限计算。手动 Stop / Recover 使用 64 KiB 上限，同样根据传输方式解释为请求或本地分片。

`r5560_readout_benchmark` 可在真实硬件上自动比较三档。先结束其他采集程序，保持相同输入信号，确认所有配置中的 DAQ 已空闲。以下命令会控制共享 RUN 开始/停止，每档预热 2 秒，测量 20 秒，重复三轮并旋转顺序；每轮均保存原始数据，输出目录必须尚不存在：

```sh
./r5560_readout_benchmark --config ../pars/datagddaq.dat --output ./block-comparison --seconds 20 --repeats 3
```

Windows 使用同名 `.exe`。工具读取并使用当前硬件参数，不加载 par/userpars、不清 FIFO、不自动恢复非 IDLE 设备。发生连接、启动、运行或停止错误就停止后续测试；无事件数据也停止比较。`--sizes 64,128,256` 可指定测试档位及初始顺序。`--demo` 仅用于工具流程自测，不代表设备吞吐。

输出包含 `benchmark.log`、`comparison.json` 和各 run 子目录中的原始数据及 `run.json`。每 DAQ 的速率用预热结束至测量结束之间已发布的快照差值计算，使用该模块自身的实际时间差；总速率为各模块速率之和，排除启动和停止排空时间。统计请求数、平均包大小、FIFO 平均耗时、空/短回复及 Accepted/Rejected 差值。通道计数刷新比字节统计慢，计数差值用于辅助判断，不是与字节窗口完全同步的测量。

需要提前结束自动对比时，在本次输出目录内创建名为 `STOP` 的文件。工具会停止当前测量，通过原有 STOP/排空流程结束采集，并将比较标记为取消/失败，保留已保存数据。

API 2 满包比例较低时，大请求不一定提高实际返回量；API 3 的不同本地分片大小也不能解释为底板 DMA 包尺寸改变。最终选择应根据同一硬件输入下多轮实测，并检查 Rejected 和硬件错误；本地 TCP/ZeroMQ 模拟结果不能替代实板结论。VALIDATION.md 中 2026-10-19 的实测表仅适用于当时的 Avalon 固件，本轮未进行实板 DMA 对比。

## Debug 波形诊断

先将含 `WAVE_VERSION=0x00010000` 的新版 FPGA 固件加载到需要诊断的 DAQ。GUI 对旧固件仍允许原有事件采集，但点击波形抓取会提示固件不支持，不读取未知内存。

点击工具栏 **Debug → Waveform**，沿用 GUI27xx 的左侧 **Mod → ch-XX → Sent** 操作：

1. 选择 DAQ 和 0～31 通道；设置 Samples（1～16384）、Pretrigger（小于 Samples）和触发等待超时。
2. **Sent** 等待该通道 `user_ch.trigger`。可在 Idle 时独立诊断，也可在 Running 时抓取；无需为调试启动公共 RUN。阈值、极性和快速滤波器参数仍在 Basic 中设置，修改前应停止波形抓取。
3. **Force** 为没有脉冲时的基线检查提供软件触发。如果通道触发先到则保留它的波形，并按样点触发标记显示实际来源。
4. 勾选 **Continuous** 后再点 Sent/Force，软件读完一帧后再 ARM；每次最多显示一帧，重试间隔 250 ms，超时或错误会停止连续模式。**Stop** 或关闭 Debug 会取消后续抓取，不停止正常事件采集。
5. 拖框放大、滚轮缩放时间轴、Shift+滚轮缩放 ADC 轴；右键、双击或 Auto scale 恢复。Save CSV 保存当前完整波形及模块、通道、序号、预触发、模拟标记等信息，保留全部样点。

当前 FPGA 源码输出 `user_ch.data_user`，即经过 POL 极性修正的 14 bit 无符号样点，8 ns/点；最长采样窗口 131.072 us，不抽取。POL=1 保持输入，POL=0 输出 `16383-ADC[13:0]`；应在 Basic 中正确设置 POL，使脉冲朝正方向。GUI 直接显示接收到的 ADC counts，不再做极性翻转。此前生成的原始 ADC 版 BIT/BIN 仍输出原始 ADC，需重新编译并加载极性修正版 FPGA 才会改变波形来源；Avalon 地址和数据格式兼容，GUI 不自动辨别这两版的波形来源。红色虚线为触发位置，底部红色短线为通道 trigger 标记；数字滤波器触发有处理延迟，模拟脉冲可出现在红线之前。波形横轴以触发点为零，CSV 时间单位为 ns。

在 **Baseline / Noise** 中选择无脉冲的样点区间（First sample 从 0 开始），绿色区域显示统计范围：Mean、去直流后的 RMS noise（总体标准差）、峰峰值和最小/最大值，单位均为 ADC counts。默认统计前 512 点；应手动避开脉冲及滤波触发延迟范围。当前没有将 ADC counts 换算为电压。保留的 Run log 页签用于查看采集日志和导出日志。

波形采用独立冻结 RAM，经同一 DAQ 工作线程每次最多 1024 字分块读取，不进入原事件 `.bin`。正常 List 读出和停止命令可穿插执行。全局一次只允许一个诊断抓取，避免误操作；连续诊断占用部分网络带宽且波形间有空隙，不能用来替代连续事件采集。准备、ARM、排空和故障阶段禁用新抓取；正式采集 Start 前须先停止当前波形抓取。

Demo 包含模拟脉冲和基线，可直接检查显示操作；模拟数据不代表设备真实噪声或吞吐量。Qt Widgets 绘图无需额外安装 Qt Charts；Windows 和 Linux 使用相同源代码。

## 维护与寄存器生成

`r5560_registers.h` 直接复制自 FPGA 自动生成的头文件。完整仓库内运行 `python3 tools/check_register_map.py` 检查一致性。增加/修改寄存器时应先更新 FPGA 寄存器表、重新生成定义，再调整 `Config.cc` 的参数描述，而不是复制大型地址判断逻辑。

新增核心代码：Config（配置与范围）、DeviceHandle（SDK TCP 控制协议与事件传输选择）、DmaReceiver（ZeroMQ 完整消息接收）、ReadoutThread（模块读出、DMA 生命周期与写盘）、Acquisition（全局状态与同步）、EventDecoder（分包和能量）、SettingsWindows（参数/同步/在线）、AnalysisDialog（离线工具入口）。各 DAQ 的控制、接收器调用和文件写入由对应工作线程协调，libzmq 另有内部网络线程；GUI 读取有锁的统计快照，不接收逐事件 UI 消息。
