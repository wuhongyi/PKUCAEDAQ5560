.. GUI.rst --- 
.. 
.. Description: 
.. Author: Hongyi Wu(吴鸿毅)
.. Email: wuhongyi@qq.com 
.. Created: 六 2月  3 21:00:35 2024 (+0800)
.. Last-Updated: 四 10月 29 16:37:53 2026 (+0800)
..           By: Hongyi Wu(吴鸿毅)
..     Update #: 29
.. URL: http://wuhongyi.cn 

=================================
图形交互界面
=================================

---------------------------------
配置文件
---------------------------------


配置文件放置于 **pars** 文件夹内，

json 格式文件，主要包含模块的 IP 等信息。


.. code:: json

   {
   "modules": 4,
   "ip": ["162.105.54.47", "162.105.54.194", "162.105.54.142", "162.105.54.232"],
    
   "par": "setting.json",

   "userpars": "../pars/init.txt"
    
   }

其中，参数 modules 为系统使用数据采集卡数量，参数 ip ，依次为系统每个模块 IPV4 的 IP，不是 Base board 的IP。
   



#################################
Linux
#################################

进入 GUI 目录，执行以下命令即可弹出主控制界面

.. code:: bash
   
  ./gddaq5560




#################################
Windows
#################################


Coming soon





	   
---------------------------------
主控制界面
---------------------------------


.. image:: /_static/img/gddaqmain.png

主界面左上角 File 下拉选择 “connect to device”，弹出如下界面中，点击文件选择框，选择提前配置好的参数文件。

.. image:: /_static/img/connecttocrate.png

当配置文件加载后，会在对话框中间显示配置主要配置信息，PID 或者 IP 信息。然后点击 “connect”，该弹出界面自动关闭。所有配置文件中的模块均完成初始化。
	   

主界面左上角 File 下拉选择 “Output Configure”，弹出如下界面。本界面用于设置输出数据的文件夹，文件名称以及运行编号。按 OK 按钮关闭界面。以上信息只能在本界面进行修改，获取主界面显示该信息。


.. image:: /_static/img/outputsettings.png


----

完成系统初始化之后，主控制界面上方的 Basic、Logic、Online、Debug 四个按钮将会浮起，点击即可弹出相应的子界面，再点击即可隐藏。


.. image:: /_static/img/screenfulldaq.png
    :scale: 50%

在弹出的 Basic、Logic、Debug 子界面，当系统为多个模块时通过点击左侧的 “Mod 0”、“Mod 1”、“Mod 2” 等切换标签进行采集模块的切换，切换时会自动读取右边页面中的信息。每个子界面上方有多个标签页，点击该标签即可切换到相应参数配置页面。






**基础参数设置**

.. image:: /_static/img/demobasicsetting.png

- 参数 Enable
    - 控制通道是否开启
- 参数 Polarity
    - 极性选择
    - 1 为输入正信号，0为输入负信号。
    - 只要极性设置正确，在 Debug 页面看到的波形为正信号。
- 参数 Threshold
    - 快梯形触发滤波的参数
- 参数 FL
    - 单位为 8 ns
    - 参数范围 1-127  
    - 快梯形触发滤波参数
- 参数 FL+FG
    - 单位为 8 ns
    - 参数范围 1-127
    - 快梯形触发滤波器参数
- 参数 BL 
    - 基线跟踪算法的参数
    - 参数范围 1-1023
    - 建议直接设置 1023，参数越低会影响能量分辨
- 参数 XIA offset
    - 单位 8 ns
    - 参数范围 0-255
    - 由于流水线处理延迟以及触发点在脉冲的上升沿，需要给予一个offset补偿能量梯形计算开始的时间
- 参数 XIA SL
    - 单位 8 ns
    - 参数范围 1-1023  
    - 能量梯形的上升沿  
- 参数 XIA SG
    - 单位 8 ns
    - 参数范围  1-1023
    - 能量梯形的平台
      
      

**逻辑参数设置**

.. image:: /_static/img/demologicsettings.png

参数 Start source 选择 External Shared Run(multi-module)，Run input 选择 SYNC 1。这样才能使四个 32 通道的模块同步运行。


	   

**实时计数率监视**
	   

.. image:: /_static/img/demoonline.png


**波形监视与调试**

.. image:: /_static/img/demodebugscope.png



	   




  
.. 
.. GUI.rst ends here
