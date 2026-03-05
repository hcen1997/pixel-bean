import argparse
import os
import sys
import serial
import serial.tools.list_ports

class GCodeDebugger:
    """GCode单步调试器（带串口通信，默认COM5/115200）"""
    
    def __init__(self, gcode_file, baudrate=115200, default_port='COM5', no_serial=False):
        self.gcode_file = gcode_file
        self.baudrate = baudrate
        self.default_port = default_port
        self.no_serial = no_serial
        self.serial_port = None  # 串口对象
        self.gcode_lines = []
        self.current_line = 0
        
        # 初始化流程
        self._load_gcode()
        if not self.no_serial:
            self._init_serial()
    
    def _load_gcode(self):
        """读取GCode文件，过滤空行并保存有效指令"""
        if not os.path.exists(self.gcode_file):
            print(f"❌ 错误：GCode文件不存在 → {self.gcode_file}")
            sys.exit(1)
        
        with open(self.gcode_file, 'r', encoding='utf-8') as f:
            for line_num, line in enumerate(f, 1):
                stripped_line = line.strip()
                if stripped_line:  # 过滤空行
                    self.gcode_lines.append({
                        'line_num': line_num,
                        'content': stripped_line,
                        'executed': False
                    })
        
        if not self.gcode_lines:
            print(f"❌ 错误：GCode文件为空 → {self.gcode_file}")
            sys.exit(1)
        
        print(f"✅ 成功加载GCode文件：{self.gcode_file}")
        print(f"📝 共加载 {len(self.gcode_lines)} 条有效指令\n")
    
    def _init_serial(self):
        """初始化串口：自动检测串口，默认使用COM5"""
        # 列出所有可用串口
        available_ports = list(serial.tools.list_ports.comports())
        if not available_ports:
            print(f"❌ 错误：未检测到任何串口！")
            sys.exit(1)
        
        # 打印串口列表
        print(f"🔌 检测到可用串口：")
        for i, port in enumerate(available_ports):
            print(f"   [{i+1}] {port.device} - {port.description}")
        
        # 选择串口（默认COM5，若存在则自动使用；否则选第一个）
        target_port = None
        # 优先找默认串口COM5
        for port in available_ports:
            if port.device == self.default_port:
                target_port = port.device
                break
        # 若没有COM5，使用第一个串口
        if not target_port:
            target_port = available_ports[0].device
            print(f"\n⚠️  未找到默认串口{self.default_port}，自动使用第一个串口：{target_port}")
        else:
            print(f"\n✅ 选中默认串口：{target_port}")
        
        # 打开串口
        try:
            self.serial_port = serial.Serial(
                port=target_port,
                baudrate=self.baudrate,
                timeout=2,  # 超时时间2秒
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                bytesize=serial.EIGHTBITS
            )
            print(f"✅ 串口已成功打开：{target_port} (波特率：{self.baudrate})")
        except Exception as e:
            print(f"❌ 串口打开失败：{e}")
            sys.exit(1)
    
    def set_start_line(self, line_num):
        """设置从指定行号开始执行"""
        # 查找对应的行号
        for i, line_data in enumerate(self.gcode_lines):
            if line_data['line_num'] >= line_num:
                self.current_line = i
                print(f"✅ 已设置从行号 {line_num} 开始执行")
                return
        # 如果没有找到指定行号，使用最后一行
        self.current_line = len(self.gcode_lines) - 1
        print(f"⚠️  未找到行号 {line_num}，使用最后一行开始执行")
    
    def _preview_current(self):
        """预览当前待执行的指令"""
        if self.current_line >= len(self.gcode_lines):
            return None
        
        line_data = self.gcode_lines[self.current_line]
        print(f"\n=====================================")
        print(f"📌 当前进度：{self.current_line + 1}/{len(self.gcode_lines)}")
        print(f"📄 原文件行号：{line_data['line_num']}")
        print(f"🔍 待执行指令：{line_data['content']}")
        print(f"=====================================\n")
        return line_data
    
    def _send_to_serial(self, cmd):
        """发送指令到串口，并返回机器响应"""
        try:
            # 清空接收缓冲区
            self.serial_port.flushInput()
            # 发送指令（添加换行符，符合GCode串口协议）
            send_data = (cmd + '\n').encode('utf-8')
            self.serial_port.write(send_data)
            # 读取响应（最多读取1024字节）
            response = self.serial_port.read(1024).decode('utf-8').strip()
            return response
        except Exception as e:
            return f"发送失败：{str(e)}"
    
    def _execute_current(self):
        """执行当前指令：注释行仅打印，指令行发送到串口"""
        if self.current_line >= len(self.gcode_lines):
            return False
        
        line_data = self.gcode_lines[self.current_line]
        cmd = line_data['content']
        
        # 区分注释行和实际指令行
        if cmd.startswith(';'):
            print(f"📝 注释行（不发送）[行{line_data['line_num']}]：{cmd}")
        else:
            # 发送到串口并获取响应
            if not self.no_serial:
                print(f"📤 发送指令到串口 [行{line_data['line_num']}]：{cmd}")
                response = self._send_to_serial(cmd)
                print(f"📥 机器返回：{response}")
            else:
                print(f"📤 模拟发送指令 [行{line_data['line_num']}]：{cmd}")
        
        # 标记为已执行
        line_data['executed'] = True
        self.current_line += 1
        return True
    
    def run(self):
        """启动单步调试主循环"""
        print("\n🚀 GCode串口单步调试器已启动（输入 'h' 查看帮助）")
        print("============================================\n")
        
        try:
            while self.current_line < len(self.gcode_lines):
                # 预览当前指令
                current_data = self._preview_current()
                if not current_data:
                    break
                
                # 获取用户输入
                user_input = input("请选择操作 [n:下一步 | s:跳过 | r:重新开始 | q:退出] → ").strip().lower()
                
                # 处理用户指令
                if user_input == 'n' or user_input == '':  # 回车默认下一步
                    self._execute_current()
                elif user_input == 's':  # 跳过当前指令
                    print(f"⏭️  跳过指令：{current_data['content']}")
                    self.current_line += 1
                elif user_input == 'r':  # 重新开始
                    print("🔄 重新开始调试")
                    self.current_line = 0
                elif user_input == 'q':  # 退出
                    print("🛑 调试器已退出")
                    break
                elif user_input == 'h':  # 帮助
                    self._show_help()
                else:
                    print("❌ 无效指令！输入 'h' 查看帮助")
            
            # 调试完成/退出后关闭串口
            if not self.no_serial and self.serial_port:
                self.serial_port.close()
                print(f"\n✅ 串口已关闭")
            
            # 显示调试总结
            self._show_summary()
        
        except KeyboardInterrupt:
            # 捕获Ctrl+C，安全退出
            if not self.no_serial and self.serial_port:
                self.serial_port.close()
                print(f"\n⚠️  用户中断操作，串口已关闭")
            sys.exit(0)
    
    def _show_help(self):
        """显示帮助信息"""
        help_text = """
📖 GCode串口单步调试器帮助：
------------------------
n / 回车 → 执行当前指令（注释行仅打印，指令行发送到串口）
s       → 跳过当前指令（不发送，直接到下一条）
r       → 重新开始调试（回到第一条指令）
q       → 退出调试器（自动关闭串口）
h       → 显示此帮助信息
------------------------
        """
        print(help_text)
    
    def _show_summary(self):
        """显示调试总结"""
        executed = sum(1 for line in self.gcode_lines if line['executed'])
        total = len(self.gcode_lines)
        print(f"\n📊 调试总结：")
        print(f"   总指令数：{total}")
        print(f"   已执行：{executed}")
        print(f"   未执行：{total - executed}")
        
        # 列出未执行的指令（可选）
        unexecuted = [line for line in self.gcode_lines if not line['executed']]
        if unexecuted:
            print(f"\n⚠️  未执行的指令（前5条）：")
            for line in unexecuted[:5]:  # 只显示前5条
                print(f"   行{line['line_num']}：{line['content']}")
            if len(unexecuted) > 5:
                print(f"   ... 还有 {len(unexecuted) - 5} 条未执行指令")

def main(run_file=None, start_line=None):
    # CLI参数解析
    parser = argparse.ArgumentParser(description='📝 GCode串口单步调试器 - 逐行预览并发送GCode到机器')
    parser.add_argument('--gcode_file', type=str,
                        default=run_file or r"D:\work\trea\pixel_bean\grbl-code\gcode-log.txt",
     help='要调试的GCode文件路径（如：gcode-log.txt）')
    parser.add_argument('--baud', type=int, default=115200, help='串口波特率（默认：115200）')
    parser.add_argument('--port', type=str, default='COM5', help='默认串口（默认：COM5）')
    parser.add_argument('--start_line', type=int, default=start_line or 1, help='从指定行号开始执行（默认：1）')
    parser.add_argument('--no_serial', action='store_true', help='跳过串口初始化（用于测试）')
    args = parser.parse_args()
    
    # 启动调试器
    debugger = GCodeDebugger(args.gcode_file, args.baud, args.port, args.no_serial)
    # 设置起始行号
    debugger.set_start_line(args.start_line)
    debugger.run()

if __name__ == "__main__":
    # 检查pyserial是否安装
    try:
        import serial
    except ImportError:
        print("❌ 缺少pyserial库！请先执行安装命令：")
        print("   pip install pyserial")
        sys.exit(1)
    
    # 检查是否有命令行参数
    if len(sys.argv) > 1:
        # 有命令行参数，使用命令行参数
        main()
    else:
        # 没有命令行参数，使用默认值
        run_file = r"D:\work\trea\pixel_bean\grbl-code\gcode-log.txt"
        start_line = 8
        main(run_file, start_line)