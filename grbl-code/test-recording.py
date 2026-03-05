import keyboard
import time

# 模拟测试F2录制功能
print("测试F2录制功能...")
print("按F2键开始/停止录制")
print("按ESC键退出测试")

recording = False

def on_key_press(key):
    global recording
    try:
        if key.name == 'f2':
            recording = not recording
            if recording:
                timestamp = time.strftime("%Y-%m-%d %H:%M:%S")
                with open("gcode-log.txt", "a", encoding="utf-8") as f:
                    f.write(f"\n; 录制开始时间：{timestamp}\n")
                print(f"✅ 录制已开启，开始记录G-code到gcode-log.txt")
            else:
                print(f"✅ 录制已关闭")
        elif key.name == 'esc':
            print("退出测试")
            return False
    except:
        pass

# 启动按键监听
listener = keyboard.Listener(on_press=on_key_press)
listener.start()

# 模拟发送G-code
print("\n模拟发送G-code...")
print("按F2开始录制，然后按任意键发送模拟G-code")

while True:
    try:
        time.sleep(0.1)
        if recording:
            # 模拟发送G-code
            gcode = f"G1 X{int(time.time() % 100)} Y{int(time.time() % 100)} F1000"
            with open("gcode-log.txt", "a", encoding="utf-8") as f:
                f.write(f"{gcode}\n")
            print(f"📹 已记录指令：{gcode}")
            time.sleep(1)
    except KeyboardInterrupt:
        break

listener.stop()
print("测试完成")