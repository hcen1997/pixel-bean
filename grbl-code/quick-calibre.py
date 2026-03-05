import serial
import serial.tools.list_ports
import sys
import threading
import time
from pynput import keyboard  # 用于监听WASD按键

# ====================== 核心配置 ======================
# 串口配置
SERIAL_PORT = "COM5"
BAUDRATE = 115200
TIMEOUT = 2

# 移动步长（mm）- 可根据需要调整
STEP_XY = 1  # WASD单次移动1mm（因G24 S0.4，实际1步=0.01mm）
FAST_MOVE_F = 10000  # 快速移动速度

# 6个标定坐标（初始默认值，会被实际标定值覆盖）
CALIB_POINTS = {
    "0,0": {"name": "q", "x": 0, "y": 0},
    "0,25": {"name": "w", "x": 0, "y": 25},
    "25,0": {"name": "e", "x": 25, "y": 0},
    "25,25": {"name": "r", "x": 25, "y": 25},
    "0,12": {"name": "t", "x": 0, "y": 12},
    "12,0": {"name": "y", "x": 12, "y": 0}
}

# 初始化指令
INIT_COMMANDS = [
    "M0",          # 电机使能
    "G24 S0.4",    # 设置倍率0.4（1步=0.01mm）
    "G22 A20000",  # 加速参数（1cm移动2秒）
    "G1 X0 Y0 F500" # 归位初始点
]

# 全局变量
serial_conn = None
current_x = 0  # 记录当前X坐标（逻辑值）
current_y = 0  # 记录当前Y坐标（逻辑值）
running = True  # 程序运行标志
last_status_time = 0  # 上次状态打印时间戳

class Calibrator:
    def __init__(self):
        self._init_serial()
        # 移除自动初始化，改为手动执行
        # self._init_machine()
        self._print_help()
        self._start_key_listener()

    def _init_serial(self):
        """初始化串口连接"""
        global serial_conn
        # 检测串口
        available_ports = [p.device for p in serial.tools.list_ports.comports()]
        if SERIAL_PORT not in available_ports:
            print(f"❌ 错误：未找到串口{SERIAL_PORT}，可用串口：{available_ports}")
            if available_ports:
                choice = input(f"是否使用{available_ports[0]}替代？(y/n)：").strip().lower()
                if choice == "y":
                    serial_port = available_ports[0]
                else:
                    sys.exit(1)
            else:
                print("❌ 无可用串口，程序退出")
                sys.exit(1)
        else:
            serial_port = SERIAL_PORT

        # 打开串口
        try:
            serial_conn = serial.Serial(
                port=serial_port,
                baudrate=BAUDRATE,
                timeout=TIMEOUT,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                bytesize=serial.EIGHTBITS,
                dsrdtr=False  # 禁用DTR信号，避免影响装机
            )
            print(f"✅ 串口已打开：{serial_port} ({BAUDRATE}波特率)")
            time.sleep(0.5)  # 串口稳定延迟
        except Exception as e:
            print(f"❌ 串口打开失败：{e}")
            sys.exit(1)

    def _read_serial(self):
        """异步读取串口消息"""
        global last_status_time
        try:
            if serial_conn and serial_conn.is_open:
                # 非阻塞读取
                if serial_conn.in_waiting > 0:
                    response = serial_conn.read(1024).decode('utf-8').strip()
                    if response:
                        # 检查是否是状态消息（包含"模式："）
                        if "模式：" in response:
                            # 限制状态消息的打印频率，每500ms打印一次
                            current_time = time.time()
                            if current_time - last_status_time > 0.5:
                                print(f"📊 状态：{response}")
                                last_status_time = current_time
                        else:
                            # 其他消息立即打印
                            print(f"📥 机器响应：{response}")
        except Exception as e:
            pass
    
    def _send_gcode(self, gcode, desc=""):
        """发送GCode指令"""
        if not serial_conn or not serial_conn.is_open:
            print("❌ 串口未连接")
            return
        
        # 发送指令
        gcode_clean = gcode.strip()
        if not gcode_clean:
            return
        
        print(f"\n📤 发送指令 {desc if desc else ''}：{gcode_clean}")
        try:
            serial_conn.write((gcode_clean + "\n").encode('utf-8'))
            return True
        except Exception as e:
            print(f"❌ 指令发送失败：{e}")
            return False

    def _init_machine(self):
        """执行机器初始化指令"""
        print("\n🔧 开始执行初始化指令...")
        time.sleep(1.2)  # 指令执行间隔
        for cmd in INIT_COMMANDS:
            self._send_gcode(cmd, "初始化")
            time.sleep(1.2)  # 指令执行间隔
        print("✅ 初始化完成！")

    def _move_xy(self, dx, dy):
        """移动X/Y坐标（相对移动）"""
        global current_x, current_y
        # 更新当前坐标
        current_x += dx * STEP_XY
        current_y += dy * STEP_XY
        # 生成移动指令
        gcode = f"G1 X{current_x} Y{current_y} F{FAST_MOVE_F}"
        self._send_gcode(gcode, f"移动：X{dx*STEP_XY} Y{dy*STEP_XY}")

    def _jump_to_calib_point(self, point_key):
        """跳转到指定标定坐标"""
        if point_key not in CALIB_POINTS:
            print(f"❌ 无效的标定点：{point_key}")
            return
        point = CALIB_POINTS[point_key]
        
        # 检查是否已经标定
        if point.get('calib_x') is None:
            print(f"❌ 标定点{point['name']}尚未标定，无法跳转")
            return
        
        # 根据标定坐标生成GCode指令
        gcode_x = point['calib_x']  
        gcode_y = point['calib_y']  
        gcode = f"G1 X{gcode_x} Y{gcode_y} F{FAST_MOVE_F}"
        
        self._send_gcode(gcode, f"跳转{point['name']}")

    def _calibrate_current_point(self, point_key):
        """标定当前位置为指定标定点"""
        if point_key not in CALIB_POINTS:
            print(f"❌ 无效的标定点：{point_key}")
            return
        
        # 记录当前坐标为标定值
        CALIB_POINTS[point_key]["calib_x"] = current_x
        CALIB_POINTS[point_key]["calib_y"] = current_y
        
        print(f"\n✅ 标定完成！")
        print(f"   {CALIB_POINTS[point_key]['name']} → 实际坐标：X{current_x} Y{current_y}")

    def _print_calib_status(self):
        """打印当前标定状态"""
        print("\n📊 当前标定状态：")
        print("-" * 50)
        for key, point in CALIB_POINTS.items():
            status = f"X{point['calib_x']} Y{point['calib_y']}" if point['calib_x'] else "未标定"
            print(f"   {point['name']:<12} | {status}")
        print("-" * 50)

    def _print_final_calib_result(self):
        """打印最终标定结果（退出时）"""
        print("\n🎉 最终标定结果汇总：")
        print("=" * 60)
        print(f"{'标定点':<12} | {'标定坐标':<15} | 对应GCode指令")
        print("-" * 60)
        for key, point in CALIB_POINTS.items():
            if point['calib_x']:
                calib_coord = f"X{point['calib_x']} Y{point['calib_y']}"
                gcode = f"G1 X{point['calib_x']} Y{point['calib_y']} F{FAST_MOVE_F} ; {point['name']}"
            else:
                calib_coord = "未标定"
                gcode = "无"
            print(f"{point['name']:<12} | {calib_coord:<15} | {gcode}")
        print("=" * 60)

    def _print_help(self):
        """打印帮助信息"""
        help_text = """
🎮 快速标定器操作指南：
------------------------
【移动控制】
↑ → 上移 (Y+1mm)    ↓ → 下移 (Y-1mm)
← → 左移 (X-1mm)    → → 右移 (X+1mm)

【跳转标定点（数字键1-6对应qwerty标定的位置）】
1 → 跳转到q标定的原点(0,0)       2 → 跳转到w标定的右上(0,25)
3 → 跳转到e标定的左下(25,0)    4 → 跳转到r标定的右下(25,25)
5 → 跳转到t标定的上中(0,12)      6 → 跳转到y标定的右中(12,0)

【标定当前位置】
q → 标定为原点(0,0)       w → 标定为右上(0,25)
e → 标定为左下(25,0)    r → 标定为右下(25,25)
t → 标定为上中(0,12)      y → 标定为右中(12,0)

【其他操作】
h → 查看帮助        s → 查看当前标定状态
i → 执行初始化脚本  x → 退出程序
- → 步长除以2       = → 步长乘以2
（退出时自动打印最终标定结果）
------------------------
        """
        print(help_text)

    def _on_key_press(self, key):
        """按键监听回调"""
        try:
            global STEP_XY
            # 基础移动控制（方向键）
            if key == keyboard.Key.up:  # 上（Y+）
                self._move_xy(-100, 0)
            elif key == keyboard.Key.down:  # 下（Y-）
                self._move_xy(100, 0)
            elif key == keyboard.Key.left:  # 左（X-）
                self._move_xy(0, -100)
            elif key == keyboard.Key.right:  # 右（X+）
                self._move_xy(0, 100)
            # 其他按键（字符键）
            elif hasattr(key, 'char'):
                # 跳转标定点（数字键1-6对应qwerty标定的位置）
                if key.char == '1':  # 跳转到q标定的原点(0,0)
                    self._jump_to_calib_point("0,0")
                elif key.char == '2':  # 跳转到w标定的右上(0,25)
                    self._jump_to_calib_point("0,25")
                elif key.char == '3':  # 跳转到e标定的左下(25,0)
                    self._jump_to_calib_point("25,0")
                elif key.char == '4':  # 跳转到r标定的右下(25,25)
                    self._jump_to_calib_point("25,25")
                elif key.char == '5':  # 跳转到t标定的上中(0,12)
                    self._jump_to_calib_point("0,12")
                elif key.char == '6':  # 跳转到y标定的右中(12,0)
                    self._jump_to_calib_point("12,0")
                # 标定当前位置（qwerty键对应123456的6个点）
                elif key.char == 'q':  # 标定为原点(0,0)
                    self._calibrate_current_point("0,0")
                elif key.char == 'w':  # 标定为右上(0,25)
                    self._calibrate_current_point("0,25")
                elif key.char == 'e':  # 标定为左下(25,0)
                    self._calibrate_current_point("25,0")
                elif key.char == 'r':  # 标定为右下(25,25)
                    self._calibrate_current_point("25,25")
                elif key.char == 't':  # 标定为上中(0,12)
                    self._calibrate_current_point("0,12")
                elif key.char == 'y':  # 标定为右中(12,0)
                    self._calibrate_current_point("12,0")
                
                # 调整步长
                elif key.char == '-':
                    new_step = STEP_XY / 2
                    if new_step >= 0.1:
                        STEP_XY = new_step
                        print(f"✅ 步长已调整为：{STEP_XY:.2f}mm")
                    else:
                        print("❌ 步长不能小于0.1mm")
                elif key.char == '=':
                    new_step = STEP_XY * 2
                    if new_step <= 100:
                        STEP_XY = new_step
                        print(f"✅ 步长已调整为：{STEP_XY:.2f}mm")
                    else:
                        print("❌ 步长不能大于100mm")
                
                # 其他功能键
                elif key.char == 'h':  # 查看帮助
                    self._print_help()
                elif key.char == 's':  # 查看标定状态
                    self._print_calib_status()
                elif key.char == 'i':  # 执行初始化脚本
                    self._init_machine()
                elif key.char == 'x':  # 退出
                    global running
                    running = False
                    print("\n🛑 收到退出指令，正在汇总标定结果...")
                    self._print_final_calib_result()
                    return False  # 停止监听
        except AttributeError:
            # 非字符键（如方向键），忽略
            pass
        except Exception as e:
            print(f"\n❌ 按键处理出错：{e}")

    def _start_key_listener(self):
        """启动按键监听线程"""
        listener = keyboard.Listener(on_press=self._on_key_press)
        listener.start()
        print("✅ 按键监听已启动，按'h'查看帮助")

    def run(self):
        """主运行循环"""
        global running
        try:
            while running:
                # 异步读取串口消息
                self._read_serial()
                time.sleep(0.05)  # 减少CPU占用
        except KeyboardInterrupt:
            running = False
            self._print_final_calib_result()
        finally:
            # 退出前关闭串口
            if serial_conn and serial_conn.is_open:
                serial_conn.close()
                print("\n✅ 串口已关闭")
            print("👋 程序正常退出")

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

    # 启动标定程序
    calibrator = Calibrator()
    calibrator.run()

if __name__ == "__main__":
    main()