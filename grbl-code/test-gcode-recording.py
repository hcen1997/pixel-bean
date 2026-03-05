# 测试录制功能
# 模拟按键操作并检查G-code是否被记录

import time

# 模拟录制开始
timestamp = time.strftime("%Y-%m-%d %H:%M:%S")
with open("gcode-log.txt", "a", encoding="utf-8") as f:
    f.write(f"\n; 录制开始时间：{timestamp}\n")
    f.write("G1 X10 Y10 F8000\n")  # 模拟移动
    f.write("G31 P70 Q115 R150\n")  # 模拟下豆
    f.write("G1 X20 Y20 F8000\n")  # 模拟移动
    f.write("G31 P70 Q115 R150\n")  # 模拟下豆

print("测试录制完成，检查gcode-log.txt文件")
print("文件内容：")

# 读取文件内容
with open("gcode-log.txt", "r", encoding="utf-8") as f:
    content = f.read()
    print(content)