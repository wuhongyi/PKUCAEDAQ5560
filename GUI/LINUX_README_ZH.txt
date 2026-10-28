GUI5560 Linux 源码版使用说明
交付标识：ps-repair-20261024

1. 版本范围

这是当前GUI5560源码的Linux交付包，配套本工程已修复的PS nibridge/sysmanager：参数配置和同步控制使用Avalon/TCP 8888，API 3事件通过DMA/ZeroMQ TCP 5556读取。保留GUI27xx的操作样式，每个DAQ固定32通道，示例为4个DAQ。
包含PS发送器状态读取、quiesce/enable/reset确认、异常停止、FIFO拒绝分类、FIFO峰值及DMA最长停顿诊断。不会因切换Linux就改变事件格式、参数寄存器或FIFO深度。
包内没有Windows EXE/DLL，也没有冒充Linux可执行文件。Linux构建与测试的实际验证状态见RELEASE.json中的linux_validation；若为false，表示该项尚未在Linux执行，不能用Windows测试代替。
README.md和VALIDATION.md保留项目历史说明，涉及旧构建目录、旧测试时间的描述应按其日期理解；本次交付版本以本说明和RELEASE.json为准。

2. Ubuntu 20.04安装依赖

在Linux采集电脑上执行：
  sudo apt update
  sudo apt install build-essential cmake qtbase5-dev python3 libzmq5

构建需要C++17编译器、CMake至少3.16及Qt Core/Widgets/Network/Test。libzmq5是DMA运行及DMA测试必需的运行库，程序动态加载libzmq.so.5，不需要旧CAEN SDK、ROOT、FELib或Windows的DLL。
如果机器已有多套Qt，优先使用同一套系统Qt完成编译和运行，不要混入其他环境的Qt插件。运行真实GUI需要正常Linux桌面；offscreen仅用于自动测试。

3. 解包、编译、测试

把.tar.gz及同名.sha256文件复制到Linux的同一个目录，然后：
  sha256sum -c GUI5560-linux-ps-repair-20261024.tar.gz.sha256
  tar -xzf GUI5560-linux-ps-repair-20261024.tar.gz
  cd GUI5560
  sha256sum -c SHA256SUMS
  bash build_linux.sh

build_linux.sh默认使用4个并行编译任务，生成build-linux/，并自动运行CTest。内存较小的机器可以执行：
  JOBS=2 bash build_linux.sh

已包含Ubuntu20.04线程库显式链接及CMake3.16兼容的CTest调用。无需手工在链接命令中补pthread。
先做不连接硬件的桌面测试：
  ./build-linux/gddaq5560 --demo --modules 4 --fifo-kib 256

可选更完整的离线检查（会再次调用构建/CTest，然后模拟采集、生成截图及离线解码）：
  bash tests/verify_linux.sh

无桌面登录环境下只运行自动测试或--smoke-test；不要把QT_QPA_PLATFORM=offscreen长期设置后再期待真实GUI窗口出现。

4. 四DAQ配置及输出目录

examples/datagddaq-4daq-current.dat对应当前4个DAQ，按Mod0至Mod3顺序为162.105.54.47、162.105.54.194、162.105.54.142、162.105.54.232；端口8888，DAQ ID为0/1/2/3。par和userpars均为空，供本次稳定性对照使用，保持板上现有参数。
examples/datagddaq.dat是另一份通用占位模板，IP为192.168.50.21至192.168.50.24，不能据此认定硬件实际地址。其par/userpars也为空。
如果继续使用当前地址，先复制连接配置：
  cp examples/datagddaq-4daq-current.dat examples/my-crate.dat
  nano examples/my-crate.dat

另提供examples/datagddaq-with-example-parameters.dat作为可选参数文件格式示例，它的par/userpars使用相对.dat所在目录的路径，指向同目录中的setting-r5560.json和init-r5560.txt。该配置仍采用占位IP；本次稳定性对照不使用它。
可选参数文件包含4个模块、各32通道、示例sync_config=19及通用探测器参数；这些不是实板参数备份，不应默认加载。连接只读取硬件当前值，不会自动应用参数文件。为比较Windows和Linux稳定性，保持现有探测器参数，不点击File > Load configured parameters来覆盖它们。若需要迁移真实参数，使用GUI导出的本工程R5560设置文件，并保持模块顺序一致。

真实运行命令（连接实际硬件）：
  ./build-linux/gddaq5560 --config examples/my-crate.dat --fifo-kib 256

输出目录在GUI的Output设置中选择，例如/home/你的用户名/daqdata；先创建该目录并确认当前用户可写：
  mkdir -p "$HOME/daqdata"

不要沿用Windows的E:/daqdata路径。运行号使用尚未存在的新目录，长时间测试建议开启Save Data，结束后保留该轮run.json、各DAQ数据文件和Debug日志。不要用sudo启动GUI。
--fifo-kib 256在DMA模式下仅控制本地处理分片，保持它与上一轮测试一致；它不会改变PS的DMA消息、DDR缓冲或FPGA FIFO尺寸。

5. 专用交换机直连

把Linux电脑的有线网口和四个DAQ的独立网口接同一台专用千兆交换机。采集流量应走该有线口，避免仍经Wi-Fi、上级路由器或NAT转发。
先确认各DAQ现有IP、掩码及静态/DHCP方式，再给Linux有线口配置同一子网内未被使用的地址。不要仅照抄示例就修改DAQ地址。孤立交换机本身不提供DHCP；如果DAQ依赖DHCP，需先确定其断开校园网后的地址获取方式。
若暂时保留DAQ原来的校园网地址做隔离测试，Linux有线口也需采用匹配网段且唯一的静态地址；不需要为专用采集链路设置默认网关。不要把隔离网络的重复地址接回校园网。
连接前可用下列只读命令确认路径：
  ip -br address
  ip route get 162.105.54.47

结果应指向有线网卡及其源地址。控制端口为8888，DMA事件端口为5556。不要在正在采集时用端口扫描工具探测5556，因为修复版PS会把额外连接视为潜在第二接收端而停止本轮。
本软件包不会修改Linux网卡、DAQ网络配置、PS程序或FPGA固件。

6. 稳定性测试记录

先确认四台均已连接且IDLE，再Start。多DAQ全部ARMED后软件通过第一个DAQ拉高底板共同RUN；Stop时拉低RUN并排空数据。不要同时运行另一个GUI或DMA接收程序连接同一批DAQ。
保持输入通道数、计数率、探测器参数、Save Data和DMA本地分片与Windows测试一致，才能比较无线/路由路径与有线直连的差异。
观察Processing busy rejected、FIFO busy rejected、各通道FIFO峰值、DMA FIFO峰值、Longest DMA stall及诊断快照是否标记STALE。停止后核对run.json为complete，接收字节、发送计数、ACK及事件数一致。FIFO rejected为0不能替代整轮完整性核对。
若再次故障，保留日志和run.json；不要为了消除报错直接清空残留数据。此次Linux迁移不承诺已经解决所有网络中断。

7. 故障排查

找不到Qt平台插件xcb：确认使用正常桌面会话和系统Qt；先检查QT_PLUGIN_PATH/QT_QPA_PLATFORM_PLUGIN_PATH是否指向其他软件的Qt。不要从Windows拷贝platforms目录到Linux。
DMA提示缺少ZeroMQ：确认已安装libzmq5，可用ldconfig -p | grep libzmq查看；需要指定库时用R5560_ZMQ_LIBRARY设置为本机libzmq.so.5的完整路径。不要使用旧SDK的libzmq4.0.4。
连接失败：先检查.dat中的IP、ip route get选择的网卡及DAQ是否已启动服务；只有8888可达不足以证明5556数据通路可用。运行采集时不探测5556。
参数文件找不到：相对路径以.dat文件目录解析，检查par/userpars和文件是否成套迁移。

交付包由release/package_linux.py从当前源码生成；SHA256SUMS覆盖源码、资源、示例和RELEASE.json。包不包含真实用户数据或历史测试目录。
