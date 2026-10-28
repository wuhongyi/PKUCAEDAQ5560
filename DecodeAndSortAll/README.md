# R5560 DecodeAndSortAll

将 GUI5560 保存的 `data_R70_M0.bin` 等文件转换为 CERN ROOT，面向 Linux，C++17，无 Qt、SDK 或硬件连接依赖。

使用方式与 Hongyi Wu 的 [PKUCAENDAQ/DecodeAndSortAll](https://github.com/wuhongyi/PKUCAENDAQ/tree/main/DecodeAndSortAll) 对齐：`make`、`./decodeandsort RunNumber`、`UserDefine.hh`、`par.dat` 五列格式和 `tree` 树名。参考提交为 `bbd4542265a20cb12713c2dda2a1292ad44441d2`。R5560 的文件解码按本工程实际格式实现。

## Linux 编译

需要 GCC 9 或更新编译器、CERN ROOT 6.20 或更新版本（含 Core、RIO、Tree、Hist），以及 make。nlohmann/json 已随源码提供。先加载你安装的 ROOT 环境：

```bash
source /your/root/bin/thisroot.sh
root-config --version
make -j4
```

生成 `decodeandsort` 和 `DecodeAndSortAll`，两者是同一程序。也可用 CMake：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure
```

## 转换

直接指定 GUI 的运行目录，会读取其中全部 `.bin`：

```bash
mkdir -p /data/root
./decodeandsort /data/70 -o /data/root/data_0070_notwave.root
```

按参考程序习惯，编辑 `UserDefine.hh` 中的 `RAWFILEPATH`、`ROOTFILEPATH`，再 `make`：

```bash
./decodeandsort 70
```

该方式读取 `RAWFILEPATH/70/*.bin`，输出 `ROOTFILEPATH/data_0070_notwave.root`。GUI5560 的运行目录不补零；文件前缀和模块编号从实际文件及头信息识别，无需手填模块数。纯数字位置参数解释为 Run Number；当前目录中的数字目录请写 `./70`。

也可只转换指定 DAQ，或指定几路 DAQ 文件：

```bash
./decodeandsort /data/70/data_R70_M0.bin /data/70/data_R70_M1.bin -o run70_M01.root
```

每个 DAQ 固定 32 通道。一台硬件中四个 DAQ 分别为四个 `mod`，不会合成一个 128 通道编号。输入必须来自同一轮、同一时间基准；多个 DAQ 要使用共享 RUN 模式。离线排序不会校正未同步的硬件时钟。

## 滚动窗口排序（默认）

默认采用参考工程的流程：**读入一段数据 → 内存容器排序 → 写出较早部分 → 保留尾部 → 继续读取**。各 DAQ 保持独立读取位置，每轮推进到共同时间窗口；不会先把整个 Mod 0 读完再处理 Mod 1。

1. 每路能量和基线仍按该文件原始顺序计算。
2. 各模块数据读入 `std::vector<Event>`，默认窗口约 **1 秒**。
3. 按 `(ts, mod, record)` 排序，把最早 **90%** 写入 ROOT，保留最新 **10%**。
4. 向同一容器补充下一段数据，重复上述过程；全部文件结束时写完剩余事件。

滚动模式**不生成中间排序文件**。文件里的相同时间事件全部保留，不使用可能因重复键丢数据的 `map::insert`。空 DAQ、提前结束的 DAQ 和长时间无事件的区间均可处理。偏移后的 `ts` 用于窗口推进和排序。

ROOT 写入默认使用 **LZ4 level 4**，优先考虑转换速度。文件通常比原版 ZLIB level 4 更大，但分支、数值和精度完全相同。如更重视磁盘空间，可加 `--compression zlib` 恢复原来的压缩方式：

```bash
./decodeandsort /data/70 -o run70.root --compression zlib
```

默认可直接运行原来的命令。参数在 `UserDefine.hh` 中：

```cpp
#define TIMEBUFF 1000000000LL  // ns
#define PROCESSBUFF 0.9       // 每轮写出比例
```

也可不重新编译，在命令行覆盖：

```bash
./decodeandsort /data/70 -o run70.root --window-ns 1000000000 --write-fraction 0.9
```

`--memory-mib` 是容器容量上限（默认256 MiB），ROOT压缩、直方图和各文件读取缓冲另占内存。窗口数据装不下时会报错，可增大 `--memory-mib` 或缩短 `--window-ns`，不会偷偷退回磁盘排序或丢弃事件。

**窗口排序依赖事件乱序跨度能被窗口/保留尾部覆盖。** 若后来读到的事件比已经写出的事件更早，程序会明确报出模块、通道和原始事件序号，终止转换并清理未完成输出。可增大窗口、降低写出比例（保留更多尾部）后重试。一秒窗口加10%尾部不代表严格保证容忍100毫秒乱序，尾部是按事件数保留的。

通道时间偏移也会增加乱序跨度。如果 `par.dat` 中人为设置远大于窗口的偏移，需相应扩大窗口并预留容器内存，或使用 external 模式。

需要处理任意大跨度乱序时，可显式选择旧的全量外排序：

```bash
./decodeandsort /data/70 -o run70.root --sort-mode external --memory-mib 256 --temp-dir /fast-disk/tmp
```

只有 external 模式使用磁盘中间文件，每轮最多合并32个文件。`--no-sort` 或 `--sort-mode none` 保留模块/文件原顺序。ROOT元数据记录模式、窗口参数、写出比例、窗口次数和容器最高事件数。

输入文件不修改；输出已存在则拒绝覆盖。转换期间持续写入临时 ROOT，成功后才发布最终 `.root`，错误不会留下貌似完整的最终文件。输出盘需支持硬链接（通常 Linux ext4/xfs、Windows NTFS 均支持）；临时 ROOT 与最终输出位于同一文件系统。

## 可选时间偏移和能量门

```bash
./decodeandsort /data/70 -o run70.root --par par.dat
```

格式与参考程序一致，示例见 `par.example.dat`：

```text
mod ch ts_offset ch_low ch_high
0   0     0       -1e300  1e300
1   0   -16       -1e300  1e300
```

`ts_offset` 是有符号整数纳秒，**加到** `ts` 后再排序，允许负时间。能量门作用于全精度 `energy`，上下界均包含；未列出的通道不偏移、不筛选。默认不自动读取工作目录的 `par.dat`；通过 `--par` 或 Run Number 模式的 `PARAMETERFILE` 明确选择。所有事件先更新基线，再应用能量门；元数据记录筛除数量和全部规则。

BL、SL、SG、TAU 等能量参数直接使用每个 `.bin` 文件头中该轮的 `settings`，无需参考项目的 `parfdk.dat`。不会把目前 GUI 的新设置用于旧数据。

## ROOT 内容

树名 `tree`，每条 entry 是一个通道事件，尚未进行符合事件组合。

| 分支 | ROOT 类型 | 含义 |
|---|---|---|
| `mod`, `ch` | UShort_t | GUI 模块编号、通道 0–31 |
| `sr`, `fw` | UShort_t | 固定 125 MHz、4（沿用参考程序 OPEN 类型标识） |
| `ts` | Long64_t | 纳秒时间，已加可选通道偏移；排序使用此值 |
| `timestamp` | ULong64_t | 原始 48 位触发时间，单位 8 ns，未加偏移 |
| `previous` | ULong64_t | 上一次原始触发时间，单位 8 ns；可能对应被拒绝的触发 |
| `energy` | Double_t | 与 GUI5560 相同的 UserDPP 重建能量，未标定，保留负值与小数 |
| `energyxia` | UShort_t | 参考程序兼容字段：能量截断并限制到 0–65535 |
| `baseline` | Double_t | 该事件用于能量校正的 VDC 基线估计 |
| `baselineA`, `baselineB`, `sl1`, `sg`, `sl2` | UInt_t | 原始 24 位积分量 |
| `daq_id`, `run` | UInt_t | 文件头的 DAQ ID 和运行编号 |
| `record` | ULong64_t | 本模块文件内的原始事件序号，从 0 开始 |

`mod/ch/sr/fw/ts/energyxia` 的名字及类型与参考工程一致。**`energy` 在本程序是 Double_t，不是参考工程的 UShort_t**；旧分析代码用 `SetBranchAddress("energy", ...)` 时需相应调整。`fw=4` 是分析分类，实际 FPGA API/构建号保存在元数据。当前32字节积分事件不含波形、CFD 或 PSD 信息，因此不生成这些虚构分支。

每通道提供 `energy_M0_C0` 等 TH1D，16384 个 bin，范围 0–16384；负能量和超范围能量分别计入 underflow/overflow。若使用能量门，直方图对应输出事件。

`r5560_metadata` 是包含 JSON 的 TObjString，保存所有原始文件头、完整 `run.json`（若存在）、每模块/通道计数、排序与筛选参数、源文件名及参考版本。`sort_mode` 为 `window`、`external` 或 `none`；滚动模式另有 `rolling_sort` 对象记录窗口参数和容器峰值，`compression` 记录 ROOT 压缩算法。

```cpp
TFile f("run70.root");
auto tree = f.Get<TTree>("tree");
tree->Draw("energy", "mod==0 && ch==0");
tree->Scan("mod:ch:ts:energy", "", "", 10);
```

## 数据检查

- 文件以 `R5560D1\n` 开始，随后是小端 uint32 JSON 长度、JSON 文件头、连续32字节事件。支持本工程 API 0x00020000/0x00030000、event_format=2、125 MHz；Avalon 与 DMA 文件采用相同事件解码。
- 校验头长度、每通道设置、保留位、事件长度及能量。完整轮还与 `run.json` 的事件、字节和通道计数对账；DMA 轮核对 TX/ACK 字数。
- 同一通道时间倒退时报出源文件和字节位置，不猜测回卷或拼接不同时间周期。48 位计时约 26 天回卷，超过该边界的轮需单独设计周期处理。
- 目录模式要求 `run.json` 所列各 DAQ 文件齐全；指定文件模式允许选择部分模块。有 `run.json` 且状态非 `complete` 时，默认拒绝。显式 `--allow-incomplete` 可分析格式有效的不完整轮，原始失败状态仍保存于 ROOT；不跳过坏事件或忽略不完整尾事件。
- 没有 `run.json` 时仍可根据自描述文件头转换，同时打印“采集完整性未知”提示。解码成功不等于证明未发生硬件丢失。
- 不支持无头恢复 `.fifo.raw`、协议残帧 `.partial-reply.raw` 或 `.dma-pending.raw`；它们不能与正常采集文件直接拼接。
- 请在采集停止、文件不再写入后转换。

## 开发检查

```bash
make test                         # 不需要 ROOT 的解码/滚动排序/外排单元检查
python3 tests/test_cli.py ./decodeandsort
```

`tests/check_root.cc` 是独立原始数据与 ROOT 对照检查工具。开发验证产物保存在 `build-windows/`，不属于发布源代码。

## 来源

参考流程与命名来自 Hongyi Wu 的 PKUCAENDAQ DecodeAndSortAll；能量计算移植自本工程 `GUI5560/EventDecoder.cc`，其算法源自 Hongyi Wu 的 GUI27xx UserDPP。排序、R5560文件校验和ROOT封装按此处格式单独实现。

参考项目要求使用者引用：

- [The upgrade of the general-purpose digital data acquisition system (GDDAQ)](https://doi.org/10.1016/j.nima.2026.171315)
- [A Trigger-Guided FPGA Digital CFD Without an Additional CFD Validation Threshold for High-Efficiency HPGe Detector Timing](https://doi.org/10.1109/TNS.2026.3711002)

nlohmann/json 3.10.5 复用 `GUI27xx/nlohmann/json.hpp`，其 MIT 许可及 Niels Lohmann 版权声明完整保留在该头文件中。参考仓库的引用要求见其 README；本工程未将参考仓库重新声明为 MIT 授权。
