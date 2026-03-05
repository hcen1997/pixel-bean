一、Arduino Grbl相关软件安装与选择
Grbl 是一个开源的嵌入式 CNC 铣床控制器，运行在 Arduino 上。以下是关于 Grbl 的一些关键信息：
1）兼容性：Grbl 可以在标准的 Arduino 板（如 Uno）上运行，使用 Atmega 328 处理器。
2）功能：它支持符合标准的 G 代码，包括弧线、圆和螺旋运动。Grbl 包含完整的加速度管理和前瞻性控制，以实现平滑的转角。
3）性能：Grbl 实现了精确的定时和异步操作，保持高达 30 kHz 的稳定、无抖动的控制脉冲。
4）许可证：Grbl 是根据 GPLv3 许可证发布的免费软件。
5）开发者：Grbl 是由国外多位贡献者开发的，包括 Sungeun (Sonny) K. Jeon 和 Simen Svale Skogsrud。

1、安装Grbl库
打开https://github.com/，搜索：grbl

1）LaserGRBL
LaserGRBL 是一款开源的控制软件，专门用于激光切割和雕刻机。它提供了一种简单的方法来控制这些设备，非常适合爱好者和小型工作室使用。我偶然先选择它的原因，主要是支持20种语言，重要的是支持中文，便于学习。

LaserGRBL的开源仓库：https://github.com/arkypita/LaserGRBL
打开LaserGRBL软件，是这个界面，设置为中文

参考项目 ： https://makelog.dfrobot.com.cn/article-314626.html