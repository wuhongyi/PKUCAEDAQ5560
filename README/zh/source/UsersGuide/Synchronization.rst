.. Synchronization.rst --- 
.. 
.. Description: 
.. Author: Hongyi Wu(吴鸿毅)
.. Email: wuhongyi@qq.com 
.. Created: 三 2月  7 20:07:01 2024 (+0800)
.. Last-Updated: 三 10月 28 21:20:01 2026 (+0800)
..           By: Hongyi Wu(吴鸿毅)
..     Update #: 5
.. URL: http://wuhongyi.cn 

=================================
时间同步和采集
=================================

在多模块的采集系统中，一个最基本的要求是所有模块同步采集。其要求所有模块共用一个时钟源，然后同时开始和结束数据采集。



登陆 Base Board，检查 DAQ Sync In 1 为 Sync Reg 2。该设置为一个模块上的 4 个 DAQ通过控制 base board 的寄存器来控制同步启动采集。

.. image:: /_static/img/R5560IOSettings.png










	   
.. 
.. Synchronization.rst ends here
