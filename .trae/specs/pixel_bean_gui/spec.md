# PixelBean GUI - 产品需求文档

## Overview
- **Summary**: 将现有的GCode单步调试器和键盘控制拼豆机功能合并为一个GUI应用程序，提供直观的用户界面来控制拼豆机、录制和执行GCode指令。
- **Purpose**: 简化用户操作，提供可视化界面，同时保留两个脚本的核心功能。
- **Target Users**: 拼豆机用户，需要直观控制设备和调试GCode指令的开发者。

## Goals
- 合并GCode单步调试和键盘控制功能到一个GUI应用中
- 提供直观的界面元素，如按钮、滑块和文本框
- 支持GCode录制和执行
- 保持与原有功能的兼容性
- 提供实时状态反馈

## Non-Goals (Out of Scope)
- 不改变原有GCode指令的执行逻辑
- 不添加新的硬件控制功能
- 不支持网络远程控制
- 不修改底层串口通信协议

## Background & Context
- 现有两个脚本：gcode-single-step-cli.py（GCode调试）和keyboard-bean-down.py（键盘控制）
- 两个脚本都使用串口与拼豆机通信
- 键盘控制脚本支持录制GCode到文件
- GCode调试脚本支持单步执行和行号控制

## Functional Requirements
- **FR-1**: 提供键盘控制界面，支持方向键移动和空格键下豆
- **FR-2**: 提供GCode文件加载和单步执行功能
- **FR-3**: 支持从指定行号开始执行GCode
- **FR-4**: 提供GCode录制功能，记录用户操作到文件
- **FR-5**: 显示实时串口通信状态和设备响应
- **FR-6**: 支持设备初始化和标定功能

## Non-Functional Requirements
- **NFR-1**: 界面响应时间不超过100ms
- **NFR-2**: 支持Windows、MacOS和Linux平台
- **NFR-3**: 代码结构清晰，易于维护
- **NFR-4**: 提供详细的错误处理和用户反馈

## Constraints
- **Technical**: 使用Python 3.7+，tkinter库，pyserial库，pynput库
- **Business**: 保持与现有硬件和通信协议的兼容性
- **Dependencies**: 需要安装pyserial和pynput库

## Assumptions
- 用户已安装Python 3.7或更高版本
- 用户已安装所需的依赖库
- 拼豆机通过串口连接到计算机

## Acceptance Criteria

### AC-1: 键盘控制功能
- **Given**: 应用程序已启动且串口连接正常
- **When**: 用户按下方向键
- **Then**: 拼豆机按照指定方向移动
- **Verification**: `human-judgment`

### AC-2: GCode录制功能
- **Given**: 录制功能已开启
- **When**: 用户执行移动或下豆操作
- **Then**: 相应的GCode指令被记录到文件
- **Verification**: `programmatic`

### AC-3: GCode单步执行功能
- **Given**: GCode文件已加载
- **When**: 用户点击"下一步"按钮
- **Then**: 下一条GCode指令被执行
- **Verification**: `programmatic`

### AC-4: 行号控制功能
- **Given**: GCode文件已加载
- **When**: 用户输入行号并点击"跳转"按钮
- **Then**: 程序从指定行号开始执行
- **Verification**: `programmatic`

### AC-5: 实时状态显示
- **Given**: 应用程序已启动
- **When**: 设备发送状态信息
- **Then**: 状态信息在界面上实时显示
- **Verification**: `human-judgment`

## Open Questions
- [ ] 是否需要支持多个串口设备的选择？
- [ ] 是否需要添加GCode语法高亮？
- [ ] 是否需要支持GCode文件编辑功能？