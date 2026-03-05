import re
from datetime import datetime
import os

# ====================== 核心配置（含校准偏移） ======================
# 基础路径配置
INPUT_FILE = r"D:\work\trea\pixel_bean\grbl-code\26x26_grid.gcode"
OUTPUT_FILE = r"D:\work\trea\pixel_bean\grbl-code\26x26_grid_calibrated_line.gcode"

# 原始坐标基准（标定针→机头）
PIN_ORIGIN_X = -441.8
PIN_ORIGIN_Y = 1535.9
NOZZLE_ORIGIN_X = 1000
NOZZLE_ORIGIN_Y = 1400

# 校准偏移量（实测得出：X-40，Y+50）
CALIB_OFFSET_X = -220
CALIB_OFFSET_Y = 0

# 下豆指令配置
DROP_BEAN_CMD = "G31 P70 Q115 R150"

# 要生成的线段范围：从(2,5)到(7,9)
LINE_START = (3, 6)  # 起始点(列,行)
LINE_END = (9, 9)    # 结束点(列,行)

# ====================== 计算总偏移量 ======================
# 基础偏移（标定针→机头）+ 校准偏移（实测修正）
TOTAL_OFFSET_X = (NOZZLE_ORIGIN_X - PIN_ORIGIN_X) + CALIB_OFFSET_X
TOTAL_OFFSET_Y = (NOZZLE_ORIGIN_Y - PIN_ORIGIN_Y) + CALIB_OFFSET_Y

print(f"=== 偏移配置 ===")
print(f"基础偏移（标定针→机头）：X+{NOZZLE_ORIGIN_X - PIN_ORIGIN_X}, Y+{NOZZLE_ORIGIN_Y - PIN_ORIGIN_Y}")
print(f"校准偏移（实测修正）：X{CALIB_OFFSET_X}, Y{CALIB_OFFSET_Y}")
print(f"总偏移：X+{TOTAL_OFFSET_X}, Y+{TOTAL_OFFSET_Y}")
print(f"生成线段范围：({LINE_START[0]},{LINE_START[1]}) → ({LINE_END[0]},{LINE_END[1]})\n")

# ====================== 工具函数：解析原始GCode获取点阵坐标映射 ======================
def parse_grid_coords():
    """
    解析原始GCode文件，生成 {点阵点: 原始标定针坐标} 的映射字典
    返回：{(bx, by): (pin_x, pin_y)}
    """
    grid_map = {}
    pattern = r'G1 X([-+]?\d+\.?\d*) Y([-+]?\d+\.?\d*) F\d+.*; 点\((\d+),(\d+)\)'
    
    with open(INPUT_FILE, 'r', encoding='utf-8') as f:
        for line in f:
            match = re.match(pattern, line.strip())
            if match:
                pin_x = float(match.group(1))
                pin_y = float(match.group(2))
                bx = int(match.group(3))
                by = int(match.group(4))
                grid_map[(bx, by)] = (pin_x, pin_y)
    return grid_map

# ====================== 核心：生成指定线段的GCode ======================
def generate_line_gcode(grid_map):
    """
    生成从(2,5)到(7,9)的线段GCode（含校准偏移+下豆指令）
    """
    # 1. 提取线段上的所有点阵点（按顺序）
    line_points = []
    # 计算线段的步长（简单线性插值，保证连续）
    bx_start, by_start = LINE_START
    bx_end, by_end = LINE_END
    # 计算X/Y方向的总步数
    steps_x = bx_end - bx_start
    steps_y = by_end - by_start
    total_steps = max(abs(steps_x), abs(steps_y)) if max(abs(steps_x), abs(steps_y)) > 0 else 1
    
    # 生成插值点（保证从起点到终点连续）
    for step in range(total_steps + 1):
        bx = round(bx_start + (steps_x / total_steps) * step)
        by = round(by_start + (steps_y / total_steps) * step)
        if (bx, by) in grid_map:
            line_points.append((bx, by))
    
    # 2. 生成GCode内容
    gcode_content = []
    # 文件头
    gcode_content.append(f"; PixelBean 校准后线段GCode\n")
    gcode_content.append(f"; 生成时间: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
    gcode_content.append(f"; 线段范围：({bx_start},{by_start}) → ({bx_end},{by_end})\n")
    gcode_content.append(f"; 总偏移：基础偏移+校准偏移 (X+{TOTAL_OFFSET_X}, Y+{TOTAL_OFFSET_Y})\n")
    gcode_content.append(f"G90  ; 绝对坐标模式\n")
    gcode_content.append(f"M0   ; 电机使能\n\n")
    
    # 生成线段的每个点GCode
    for bx, by in line_points:
        # 获取原始标定针坐标
        pin_x, pin_y = grid_map[(bx, by)]
        # 计算机头坐标（基础偏移+校准偏移）
        nozzle_x = round(pin_x + TOTAL_OFFSET_X, 1)
        nozzle_y = round(pin_y + TOTAL_OFFSET_Y, 1)
        # 移动指令
        gcode_content.append(f"G1 X{nozzle_x} Y{nozzle_y} F6000  ; 点({bx},{by})\n")
        # 下豆指令
        gcode_content.append(f"{DROP_BEAN_CMD}  ; 下豆 - 点({bx},{by})\n") 
    
    # 文件尾
    gcode_content.append(f"M0   ; 电机禁用\n")
    
    # 写入文件
    with open(OUTPUT_FILE, 'w', encoding='utf-8') as f:
        f.writelines(gcode_content)
    
    print(f"✅ 线段GCode生成完成！")
    print(f"📄 输出文件：{OUTPUT_FILE}")
    print(f"📝 生成的点列表：{line_points}")

# ====================== 运行程序 ======================
if __name__ == "__main__":
    # 解析原始点阵坐标映射
    grid_map = parse_grid_coords()
    if not grid_map:
        print(f"❌ 错误：未从原始文件中解析出点阵坐标！")
    else:
        # 生成指定线段的GCode
        generate_line_gcode(grid_map)