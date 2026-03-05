import tkinter as tk
from tkinter import ttk, filedialog, messagebox
import serial
import serial.tools.list_ports
import sys
import threading
import time
from pynput import keyboard

class PixelBeanGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("PixelBean GUI")
        self.root.geometry("800x600")
        
        # 全局变量
        self.serial_conn = None
        self.current_x = 0
        self.current_y = 0
        self.running = True
        self.last_status_time = 0
        self.pressed_keys = set()
        self.key_listening_enabled = True
        self.recording = False
        
        # 串口配置
        self.SERIAL_PORT = "COM5"
        self.BAUDRATE = 115200
        self.TIMEOUT = 2
        
        # 移动步长
        self.STEP_XY = 1
        self.FAST_MOVE_F = 8000
        self.shift_step = 16
        self.normal_step = 287
        
        # 标定坐标
        self.CALIB_POINTS = {
            "0,0": {"name": "q", "x": 0, "y": 0},
            "0,25": {"name": "w", "x": 0, "y": 25},
            "25,0": {"name": "e", "x": 25, "y": 0},
            "25,25": {"name": "r", "x": 25, "y": 25},
            "0,12": {"name": "t", "x": 0, "y": 12},
            "12,0": {"name": "y", "x": 12, "y": 0}
        }
        
        # 初始化指令
        self.INIT_COMMANDS = [
            "M0",          # 电机使能
            "G24 S0.4",    # 设置倍率0.4（1步=0.01mm）
            "G1 X0 Y0 F500" # 归位初始点
        ]
        self.bean_down_gcode = "G31 P70 Q118 R175"
        
        # GCode相关
        self.gcode_file = "D:/work/trea/pixel_bean/grbl-code/gcode-log.txt"
        self.gcode_lines = []
        self.current_line = 0
        
        # 创建主框架
        self.main_frame = ttk.Frame(self.root, padding="10")
        self.main_frame.pack(fill=tk.BOTH, expand=True)
        
        # 创建顶部状态和串口控制区域
        top_frame = ttk.Frame(self.main_frame, padding="10")
        top_frame.pack(fill=tk.X, pady=5)
        
        # 设备状态
        self.status_frame = ttk.LabelFrame(top_frame, text="设备状态", padding="10")
        self.status_frame.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=5)
        
        self.status_var = tk.StringVar(value="未连接设备")
        self.status_label = ttk.Label(self.status_frame, textvariable=self.status_var, font=("Arial", 10, "bold"))
        self.status_label.pack(side=tk.LEFT, fill=tk.X, expand=True)
        
        # 串口控制区
        self.serial_frame = ttk.LabelFrame(top_frame, text="串口控制", padding="10")
        self.serial_frame.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=5)
        
        self.port_var = tk.StringVar()
        self.port_combo = ttk.Combobox(self.serial_frame, textvariable=self.port_var, width=20)
        self.port_combo.pack(side=tk.LEFT, padx=5)
        
        self.refresh_button = ttk.Button(self.serial_frame, text="刷新串口", command=self._refresh_ports)
        self.refresh_button.pack(side=tk.LEFT, padx=5)
        
        self.connect_button = ttk.Button(self.serial_frame, text="连接", command=self._connect_serial)
        self.connect_button.pack(side=tk.LEFT, padx=5)
        
        self.disconnect_button = ttk.Button(self.serial_frame, text="断开", command=self._disconnect_serial, state=tk.DISABLED)
        self.disconnect_button.pack(side=tk.LEFT, padx=5)
        
        # 创建串口消息显示区
        self.message_frame = ttk.LabelFrame(self.main_frame, text="串口消息", padding="10")
        self.message_frame.pack(fill=tk.BOTH, expand=True, pady=5)
        
        # 创建滚动条
        self.message_scrollbar = ttk.Scrollbar(self.message_frame)
        self.message_scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
        
        # 创建消息文本框
        self.message_text = tk.Text(self.message_frame, wrap=tk.WORD, height=10, yscrollcommand=self.message_scrollbar.set)
        self.message_text.pack(fill=tk.BOTH, expand=True)
        self.message_scrollbar.config(command=self.message_text.yview)
        
        # 禁用文本框编辑
        self.message_text.config(state=tk.DISABLED)
        
        # 创建功能标签页
        self.notebook = ttk.Notebook(self.main_frame)
        self.notebook.pack(fill=tk.BOTH, expand=True, pady=5)
        
        # 键盘控制标签页
        self.keyboard_frame = ttk.Frame(self.notebook, padding="10")
        self.notebook.add(self.keyboard_frame, text="键盘控制")
        
        # GCode调试标签页
        self.gcode_frame = ttk.Frame(self.notebook, padding="10")
        self.notebook.add(self.gcode_frame, text="GCode调试")
        
        # 初始化键盘控制界面
        self._init_keyboard_tab()
        
        # 初始化GCode调试界面
        self._init_gcode_tab()
        
        # 刷新串口列表
        self._refresh_ports()
        
    def _init_keyboard_tab(self):
        """初始化键盘控制标签页"""
        # 控制按钮
        control_frame = ttk.LabelFrame(self.keyboard_frame, text="控制", padding="10")
        control_frame.pack(fill=tk.X, pady=5)
        
        self.record_button = ttk.Button(control_frame, text="开始录制", command=self._toggle_recording)
        self.record_button.pack(side=tk.LEFT, padx=5)
        
        self.init_button = ttk.Button(control_frame, text="初始化设备", command=self._init_machine)
        self.init_button.pack(side=tk.LEFT, padx=5)
        
        self.help_button = ttk.Button(control_frame, text="操作说明", command=self._show_help)
        self.help_button.pack(side=tk.LEFT, padx=5)
        
        # 键盘监听免打扰窗
        ttk.Label(control_frame, text="键盘监听免打扰窗:", font=("Arial", 9, "bold")).pack(side=tk.RIGHT, padx=5)
        self.dnd_entry = ttk.Entry(control_frame, width=15)
        self.dnd_entry.pack(side=tk.RIGHT, padx=5)
        self.dnd_entry.insert(0, "点击此处获取焦点")
        # 添加点击事件，点击时清空提示文本
        def on_dnd_click(event):
            if self.dnd_entry.get() == "点击此处获取焦点":
                self.dnd_entry.delete(0, tk.END)
        self.dnd_entry.bind("<Button-1>", on_dnd_click)
        
        # 状态显示
        status_frame = ttk.LabelFrame(self.keyboard_frame, text="当前状态", padding="10")
        status_frame.pack(fill=tk.X, pady=5)
        
        # 位置信息
        self.position_var = tk.StringVar(value="X: 0, Y: 0")
        position_label = ttk.Label(status_frame, text="位置:", font=("Arial", 9, "bold"))
        position_label.pack(side=tk.LEFT, padx=5)
        self.position_label = ttk.Label(status_frame, textvariable=self.position_var)
        self.position_label.pack(side=tk.LEFT, padx=5)
        
        # 键盘监听状态
        self.listening_var = tk.StringVar(value="开启")
        listening_label = ttk.Label(status_frame, text="键盘监听:", font=("Arial", 9, "bold"))
        listening_label.pack(side=tk.LEFT, padx=5)
        self.listening_status_label = ttk.Label(status_frame, textvariable=self.listening_var)
        self.listening_status_label.pack(side=tk.LEFT, padx=5)
        
        # 录制状态
        self.recording_var = tk.StringVar(value="关闭")
        recording_label = ttk.Label(status_frame, text="录制状态:", font=("Arial", 9, "bold"))
        recording_label.pack(side=tk.LEFT, padx=5)
        self.recording_status_label = ttk.Label(status_frame, textvariable=self.recording_var)
        self.recording_status_label.pack(side=tk.LEFT, padx=5)
        
        # 方向键步长
        self.normal_step_var = tk.StringVar(value=f"{self.normal_step} * 0.01mm")
        normal_step_label = ttk.Label(status_frame, text="方向键步长:", font=("Arial", 9, "bold"))
        normal_step_label.pack(side=tk.LEFT, padx=5)
        self.normal_step_label = ttk.Label(status_frame, textvariable=self.normal_step_var)
        self.normal_step_label.pack(side=tk.LEFT, padx=5)
        
        # Shift精调步长
        self.shift_step_var = tk.StringVar(value=f"{self.shift_step*self.STEP_XY:.2f} * 0.01mm")
        shift_step_label = ttk.Label(status_frame, text="Shift精调步长:", font=("Arial", 9, "bold"))
        shift_step_label.pack(side=tk.LEFT, padx=5)
        self.shift_step_label = ttk.Label(status_frame, textvariable=self.shift_step_var)
        self.shift_step_label.pack(side=tk.LEFT, padx=5)
        
        # 1q相关信息
        self.calib_var = tk.StringVar(value="未标定")
        calib_label = ttk.Label(status_frame, text="标定原点:", font=("Arial", 9, "bold"))
        calib_label.pack(side=tk.LEFT, padx=5)
        self.calib_label = ttk.Label(status_frame, textvariable=self.calib_var)
        self.calib_label.pack(side=tk.LEFT, padx=5)
        
        # 启动按键监听
        self._start_key_listener()
        
    def _init_gcode_tab(self):
        """初始化GCode调试标签页"""
        # 合并所有操作到一个组
        file_ops_frame = ttk.LabelFrame(self.gcode_frame, text="文件操作", padding="10")
        file_ops_frame.pack(fill=tk.X, pady=5)
        
        # 文件选择
        ttk.Label(file_ops_frame, text="文件路径:").pack(side=tk.LEFT, padx=5)
        self.file_var = tk.StringVar(value=self.gcode_file)
        self.file_entry = ttk.Entry(file_ops_frame, textvariable=self.file_var, width=40)
        self.file_entry.pack(side=tk.LEFT, padx=5)
        
        self.browse_button = ttk.Button(file_ops_frame, text="浏览", command=self._browse_file)
        self.browse_button.pack(side=tk.LEFT, padx=5)
        
        self.load_button = ttk.Button(file_ops_frame, text="加载", command=self._load_gcode)
        self.load_button.pack(side=tk.LEFT, padx=5)
        
        # 行号控制
        ttk.Label(file_ops_frame, text="起始行号:").pack(side=tk.LEFT, padx=5)
        self.line_var = tk.StringVar(value="1")
        self.line_entry = ttk.Entry(file_ops_frame, textvariable=self.line_var, width=10)
        self.line_entry.pack(side=tk.LEFT, padx=5)
        
        self.jump_button = ttk.Button(file_ops_frame, text="跳转", command=self._set_start_line)
        self.jump_button.pack(side=tk.LEFT, padx=5)
        
        # 添加分隔线
        ttk.Separator(file_ops_frame, orient=tk.VERTICAL).pack(side=tk.LEFT, fill=tk.Y, padx=10)
        
        # 执行控制
        self.next_button = ttk.Button(file_ops_frame, text="执行", command=self._execute_current, state=tk.DISABLED)
        self.next_button.pack(side=tk.LEFT, padx=5)
        
        self.skip_button = ttk.Button(file_ops_frame, text="跳过", command=self._skip_current, state=tk.DISABLED)
        self.skip_button.pack(side=tk.LEFT, padx=5)
        
        self.restart_button = ttk.Button(file_ops_frame, text="重新开始", command=self._restart, state=tk.DISABLED)
        self.restart_button.pack(side=tk.LEFT, padx=5)
        
        # 文件预览窗口
        preview_frame = ttk.LabelFrame(self.gcode_frame, text="文件预览", padding="10")
        preview_frame.pack(fill=tk.BOTH, expand=True, pady=5)
        
        self.preview_text = tk.Text(preview_frame, wrap=tk.WORD, height=5)
        self.preview_text.pack(fill=tk.BOTH, expand=True)
        self.preview_text.config(state=tk.DISABLED)
        
        # 执行状态
        self.exec_status_var = tk.StringVar(value="就绪")
        self.exec_status_label = ttk.Label(preview_frame, textvariable=self.exec_status_var)
        self.exec_status_label.pack(side=tk.BOTTOM, pady=5)
        
    def _refresh_ports(self):
        """刷新串口列表"""
        ports = [p.device for p in serial.tools.list_ports.comports()]
        self.port_combo['values'] = ports
        if ports:
            self.port_combo.current(0)
            self.status_var.set("就绪")
        else:
            self.status_var.set("未检测到串口")
    
    def _connect_serial(self):
        """连接串口"""
        port = self.port_var.get()
        if not port:
            messagebox.showerror("错误", "请选择串口")
            return
        
        try:
            self.serial_conn = serial.Serial(
                port=port,
                baudrate=self.BAUDRATE,
                timeout=self.TIMEOUT,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                bytesize=serial.EIGHTBITS,
                dsrdtr=False
            )
            self.status_var.set(f"已连接到 {port}")
            self.connect_button.config(state=tk.DISABLED)
            self.disconnect_button.config(state=tk.NORMAL)
            
            # 启动串口读取线程
            self.read_thread = threading.Thread(target=self._read_serial, daemon=True)
            self.read_thread.start()
        except Exception as e:
            messagebox.showerror("错误", f"串口连接失败：{e}")
    
    def _disconnect_serial(self):
        """断开串口"""
        if self.serial_conn and self.serial_conn.is_open:
            self.serial_conn.close()
        self.status_var.set("未连接设备")
        self.connect_button.config(state=tk.NORMAL)
        self.disconnect_button.config(state=tk.DISABLED)
        self._append_message("✅ 串口已断开")
    
    def _append_message(self, message):
        """向消息文本框中添加消息并自动滚动到底部"""
        # 启用文本框编辑
        self.message_text.config(state=tk.NORMAL)
        
        # 添加消息，包含时间戳
        timestamp = time.strftime("%H:%M:%S")
        self.message_text.insert(tk.END, f"[{timestamp}] {message}\n")
        
        # 自动滚动到底部
        self.message_text.see(tk.END)
        
        # 限制消息数量，防止内存占用过大
        lines = self.message_text.get(1.0, tk.END).count('\n')
        if lines > 1000:  # 保留1000行
            # 删除前200行
            self.message_text.delete(1.0, f"{200}.0")
        
        # 禁用文本框编辑
        self.message_text.config(state=tk.DISABLED)
    
    def _read_serial(self):
        """异步读取串口消息"""
        while self.running:
            try:
                if self.serial_conn and self.serial_conn.is_open:
                    if self.serial_conn.in_waiting > 0:
                        response = self.serial_conn.read(1024).decode('utf-8').strip()
                        if response:
                            if "模式：" in response:
                                current_time = time.time()
                                if current_time - self.last_status_time > 0.5:
                                    # 只在消息区显示状态，不在顶部标签显示详细状态
                                    self._append_message(f"📊 状态：{response}")
                                    self.last_status_time = current_time
                            else:
                                # 其他消息立即显示
                                self._append_message(f"📥 机器响应：{response}")
            except Exception as e:
                self._append_message(f"❌ 串口读取错误：{e}")
            time.sleep(0.05)
    
    def _send_gcode(self, gcode, desc=""):
        """发送GCode指令"""
        message = f"📤 发送指令 {desc if desc else ''}：{gcode}"
        print(message)
        self._append_message(message)
        
        if not self.serial_conn or not self.serial_conn.is_open:
            error_message = "❌ 串口未连接"
            messagebox.showerror("错误", error_message)
            self._append_message(error_message)
            return False
        
        gcode_clean = gcode.strip()
        if not gcode_clean:
            return False
        
        try:
            self.serial_conn.write((gcode_clean + "\n").encode('utf-8'))
            
            # 录制功能
            if self.recording:
                with open("D:/work/trea/pixel_bean/grbl-code/gcode-log.txt", "a", encoding="utf-8") as f:
                    f.write(f"{gcode_clean}\n")
                record_message = f"📹 已记录指令：{gcode_clean}"
                print(record_message)
                self._append_message(record_message)
            
            return True
        except Exception as e:
            error_message = f"❌ 指令发送失败：{e}"
            messagebox.showerror("错误", error_message)
            self._append_message(error_message)
            return False
    
    def _init_machine(self):
        """执行机器初始化指令"""
        if not self.serial_conn or not self.serial_conn.is_open:
            messagebox.showerror("错误", "串口未连接")
            return
        
        init_message = "\n🔧 开始执行初始化指令..."
        print(init_message)
        self._append_message(init_message)
        
        for cmd in self.INIT_COMMANDS:
            self._send_gcode(cmd, "初始化")
            time.sleep(1.2)
        
        complete_message = "✅ 初始化完成！"
        print(complete_message)
        self._append_message(complete_message)
    
    def _move_xy(self, dx, dy):
        """移动X/Y坐标"""
        self.current_x += dx
        self.current_y += dy
        gcode = f"G1 X{self.current_x} Y{self.current_y} F{self.FAST_MOVE_F}"
        self._send_gcode(gcode, f"移动：X{dx} Y{dy}")
        self.position_var.set(f"X: {self.current_x}, Y: {self.current_y}")
    
    def _toggle_recording(self):
        """切换录制状态"""
        self.recording = not self.recording
        if self.recording:
            timestamp = time.strftime("%Y-%m-%d %H:%M:%S")
            with open("D:/work/trea/pixel_bean/grbl-code/gcode-log.txt", "a", encoding="utf-8") as f:
                f.write(f"\n; 录制开始时间：{timestamp}\n")
            self.record_button.config(text="停止录制")
            self.recording_var.set("开启")
            # 移除弹窗，只更新状态
            self._append_message("✅ 录制已开启，开始记录G-code到D:/work/trea/pixel_bean/grbl-code/gcode-log.txt")
        else:
            self.record_button.config(text="开始录制")
            self.recording_var.set("关闭")
            # 移除弹窗，只更新状态
            self._append_message("✅ 录制已关闭")
    
    def _start_key_listener(self):
        """启动按键监听"""
        def on_press(key):
            try:
                self.pressed_keys.add(key)
                
                # F1键：切换按键监听
                if key == keyboard.Key.f1:
                    self.key_listening_enabled = not self.key_listening_enabled
                    status = "开启" if self.key_listening_enabled else "关闭"
                    self.listening_var.set(status)
                    # 移除弹窗，只更新状态
                    self._append_message(f"✅ 按键监听已{status}")
                    return
                
                # F2键：切换录制
                if key == keyboard.Key.f2:
                    self._toggle_recording()
                    return
                
                # 如果按键监听已关闭，不处理其他按键
                if not self.key_listening_enabled:
                    return
                
                # 检查Shift键
                shift_pressed = any(k in [keyboard.Key.shift_l, keyboard.Key.shift_r] for k in self.pressed_keys)
                
                # 方向键控制
                if key == keyboard.Key.up:
                    if shift_pressed:
                        self._move_xy(-self.shift_step*self.STEP_XY, 0)
                    else:
                        self._move_xy(-self.normal_step, 0)
                elif key == keyboard.Key.down:
                    if shift_pressed:
                        self._move_xy(self.shift_step*self.STEP_XY, 0)
                    else:
                        self._move_xy(self.normal_step, 0)
                elif key == keyboard.Key.left:
                    if shift_pressed:
                        self._move_xy(0, -self.shift_step*self.STEP_XY)
                    else:
                        self._move_xy(0, -self.normal_step)
                elif key == keyboard.Key.right:
                    if shift_pressed:
                        self._move_xy(0, self.shift_step*self.STEP_XY)
                    else:
                        self._move_xy(0, self.normal_step)
                # 空格键：下豆
                elif key == keyboard.Key.space:
                    self._send_gcode(self.bean_down_gcode, "下豆")
                # 字符键
                elif hasattr(key, 'char'):
                    if key.char == '1':
                        # 跳转到原点
                        self._move_xy(-self.current_x, -self.current_y)
                    elif key.char == 'q':
                        # 标定原点
                        self.CALIB_POINTS["0,0"]["calib_x"] = self.current_x
                        self.CALIB_POINTS["0,0"]["calib_y"] = self.current_y
                        self.calib_var.set(f"X{self.current_x} Y{self.current_y}")
                        message = f"✅ 标定完成！q → 实际坐标：X{self.current_x} Y{self.current_y}"
                        print(message)
                        self._append_message(message)
                    elif key.char == 'b':
                        # 下豆
                        self._send_gcode("G31 P70 Q115 R150", "下豆")
                    elif key.char == '-':
                        # 步长除以2
                        new_step = self.STEP_XY / 2
                        if new_step >= 0.1:
                            self.STEP_XY = new_step
                            self.shift_step_var.set(f"{self.shift_step*self.STEP_XY:.2f} * 0.01mm")
                            message = f"✅ 步长已调整为：{self.STEP_XY*self.shift_step:.2f} * 0.01mm"
                            print(message)
                            self._append_message(message)
                        else:
                            message = "❌ 步长不能小于0.1mm"
                            print(message)
                            self._append_message(message)
                    elif key.char == '=':
                        # 步长乘以2
                        new_step = self.STEP_XY * 2
                        if new_step <= 10000:
                            self.STEP_XY = new_step
                            self.shift_step_var.set(f"{self.shift_step*self.STEP_XY:.2f} * 0.01mm")
                            message = f"✅ 步长已调整为：{self.STEP_XY*self.shift_step:.2f} * 0.01mm"
                            print(message)
                            self._append_message(message)
                        else:
                            message = "❌ 步长不能大于10000mm"
                            print(message)
                            self._append_message(message)
            except Exception as e:
                messagebox.showerror("错误", f"按键处理出错：{e}")
        
        def on_release(key):
            try:
                if key in self.pressed_keys:
                    self.pressed_keys.remove(key)
            except Exception as e:
                pass
        
        listener = keyboard.Listener(on_press=on_press, on_release=on_release)
        listener.start()
    
    def _show_help(self):
        """显示操作说明弹窗"""
        help_text = """
        🎮 下豆控制操作指南：
        ------------------------
        【移动控制】
        ↑ → 上移 (Y+1mm)    ↓ → 下移 (Y-1mm)
        ← → 左移 (X-1mm)    → → 右移 (X+1mm)

        【下豆控制】
        空格键 → 发送下豆指令 (G31 P70 Q115 R150)

        【快速操作】
        1/q → 快速回到默认点 (X0 Y0)

        【调整步长】
        - → 步长除以2       = → 步长乘以2

        【其他操作】
        F1 → 切换按键监听    F2 → 切换录制功能
        h → 查看帮助        i → 执行初始化脚本
        x → 退出程序
        ------------------------
        """
        help_window = tk.Toplevel(self.root)
        help_window.title("操作说明")
        help_window.geometry("400x300")
        
        text = tk.Text(help_window, wrap=tk.WORD, padx=10, pady=10)
        text.pack(fill=tk.BOTH, expand=True)
        text.insert(tk.END, help_text)
        text.config(state=tk.DISABLED)
        
        # 添加关闭按钮
        close_button = ttk.Button(help_window, text="关闭", command=help_window.destroy)
        close_button.pack(pady=10)
    
    def _browse_file(self):
        """浏览文件"""
        file_path = filedialog.askopenfilename(
            title="选择GCode文件",
            filetypes=[("GCode文件", "*.gcode"), ("所有文件", "*.*")]
        )
        if file_path:
            self.file_var.set(file_path)
    
    def _update_preview(self):
        """更新文件预览窗口"""
        if not self.gcode_lines:
            return
        
        # 计算显示范围
        current_idx = self.current_line
        start_idx = max(0, current_idx - 2)
        end_idx = min(len(self.gcode_lines), current_idx + 3)
        
        # 启用文本框编辑
        self.preview_text.config(state=tk.NORMAL)
        self.preview_text.delete(1.0, tk.END)
        
        # 显示预览内容
        for i in range(start_idx, end_idx):
            line_data = self.gcode_lines[i]
            line_num = line_data['line_num']
            content = line_data['content']
            
            # 为当前行添加标记
            if i == current_idx:
                self.preview_text.insert(tk.END, f">> {line_num}: {content}\n", "current_line")
            else:
                self.preview_text.insert(tk.END, f"   {line_num}: {content}\n")
        
        # 禁用文本框编辑
        self.preview_text.config(state=tk.DISABLED)
        
        # 为当前行添加特殊样式
        self.preview_text.tag_configure("current_line", background="#E0E0FF")
    
    def _load_gcode(self):
        """加载GCode文件"""
        file_path = self.file_var.get()
        if not file_path:
            messagebox.showerror("错误", "请选择GCode文件")
            return
        
        try:
            with open(file_path, 'r', encoding='utf-8') as f:
                self.gcode_lines = []
                for line_num, line in enumerate(f, 1):
                    stripped_line = line.strip()
                    if stripped_line:
                        self.gcode_lines.append({
                            'line_num': line_num,
                            'content': stripped_line,
                            'executed': False
                        })
            
            if not self.gcode_lines:
                messagebox.showerror("错误", "GCode文件为空")
                return
            
            self.gcode_file = file_path
            self.current_line = 0
            
            self.exec_status_var.set(f"已加载 {len(self.gcode_lines)} 条指令")
            self.next_button.config(state=tk.NORMAL)
            self.skip_button.config(state=tk.NORMAL)
            self.restart_button.config(state=tk.NORMAL)
            
            # 更新预览
            self._update_preview()
            
        except Exception as e:
            messagebox.showerror("错误", f"加载文件失败：{e}")
    
    def _set_start_line(self):
        """设置起始行号"""
        if not self.gcode_lines:
            messagebox.showerror("错误", "请先加载GCode文件")
            return
        
        try:
            line_num = int(self.line_var.get())
            for i, line_data in enumerate(self.gcode_lines):
                if line_data['line_num'] >= line_num:
                    self.current_line = i
                    self.exec_status_var.set(f"已设置从行号 {line_num} 开始执行")
                    # 更新预览
                    self._update_preview()
                    return
            # 如果没有找到，使用最后一行
            self.current_line = len(self.gcode_lines) - 1
            self.exec_status_var.set(f"未找到行号 {line_num}，使用最后一行开始执行")
            # 更新预览
            self._update_preview()
        except ValueError:
            messagebox.showerror("错误", "请输入有效的行号")
    
    def _execute_current(self):
        """执行当前指令"""
        if not self.gcode_lines:
            messagebox.showerror("错误", "请先加载GCode文件")
            return
        
        if self.current_line >= len(self.gcode_lines):
            messagebox.showinfo("信息", "已执行完所有指令")
            return
        
        line_data = self.gcode_lines[self.current_line]
        cmd = line_data['content']
        
        if cmd.startswith(';'):
            message = f"📝 注释行（不发送）[行{line_data['line_num']}]：{cmd}"
            print(message)
            self._append_message(message)
        else:
            if self.serial_conn and self.serial_conn.is_open:
                message = f"📤 发送指令到串口 [行{line_data['line_num']}]：{cmd}"
                print(message)
                self._append_message(message)
                self._send_gcode(cmd)
            else:
                message = f"📤 模拟发送指令 [行{line_data['line_num']}]：{cmd}"
                print(message)
                self._append_message(message)
            
            # 解析G1指令，更新XY坐标
            if cmd.startswith('G1'):
                # 提取X和Y坐标
                parts = cmd.split()
                for part in parts:
                    if part.startswith('X'):
                        try:
                            self.current_x = float(part[1:])
                        except ValueError:
                            pass
                    elif part.startswith('Y'):
                        try:
                            self.current_y = float(part[1:])
                        except ValueError:
                            pass
                # 更新位置显示
                self.position_var.set(f"X: {self.current_x}, Y: {self.current_y}")
        
        line_data['executed'] = True
        self.current_line += 1
        
        if self.current_line < len(self.gcode_lines):
            next_line = self.gcode_lines[self.current_line]
            status_message = f"执行到行 {line_data['line_num']}，下一行：{next_line['line_num']}"
            self.exec_status_var.set(status_message)
            self._append_message(status_message)
        else:
            status_message = f"已执行完所有 {len(self.gcode_lines)} 条指令"
            self.exec_status_var.set(status_message)
            self._append_message(status_message)
        
        # 更新预览
        self._update_preview()
    
    def _skip_current(self):
        """跳过当前指令"""
        if not self.gcode_lines:
            messagebox.showerror("错误", "请先加载GCode文件")
            return
        
        if self.current_line >= len(self.gcode_lines):
            messagebox.showinfo("信息", "已执行完所有指令")
            return
        
        line_data = self.gcode_lines[self.current_line]
        skip_message = f"⏭️  跳过指令：{line_data['content']}"
        print(skip_message)
        self._append_message(skip_message)
        
        self.current_line += 1
        
        if self.current_line < len(self.gcode_lines):
            next_line = self.gcode_lines[self.current_line]
            status_message = f"跳过行 {line_data['line_num']}，下一行：{next_line['line_num']}"
            self.exec_status_var.set(status_message)
            self._append_message(status_message)
        else:
            status_message = f"已执行完所有 {len(self.gcode_lines)} 条指令"
            self.exec_status_var.set(status_message)
            self._append_message(status_message)
        
        # 更新预览
        self._update_preview()
    
    def _restart(self):
        """重新开始"""
        if not self.gcode_lines:
            messagebox.showerror("错误", "请先加载GCode文件")
            return
        
        self.current_line = 0
        for line in self.gcode_lines:
            line['executed'] = False
        
        restart_message = "🔄 已重新开始执行"
        self.exec_status_var.set(restart_message)
        self._append_message(restart_message)
        
        # 更新预览
        self._update_preview()
    
    def on_close(self):
        """窗口关闭时的处理"""
        self.running = False
        self._disconnect_serial()
        self.root.destroy()

def main():
    # 检查依赖
    try:
        import serial
    except ImportError:
        print("❌ 缺少pyserial库！执行安装：pip install pyserial")
        sys.exit(1)
    try:
        from pynput import keyboard
    except ImportError:
        print("❌ 缺少pynput库！执行安装：pip install pynput")
        sys.exit(1)
    
    # 创建主窗口
    try:
        print("启动GUI...")
        root = tk.Tk()
        print("创建应用实例...")
        app = PixelBeanGUI(root)
        print("设置关闭回调...")
        root.protocol("WM_DELETE_WINDOW", app.on_close)
        print("进入主循环...")
        root.mainloop()
    except Exception as e:
        print(f"❌ GUI启动失败：{e}")
        import traceback
        traceback.print_exc()

if __name__ == "__main__":
    main()