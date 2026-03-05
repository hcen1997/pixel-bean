#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
PixelBean 坐标映射工具（修正版）
将豆子坐标(bead_x, bead_y) 映射到 GCode坐标(gcode_x, gcode_y)
使用仿射变换 + 最小二乘法拟合

标定点数据（修正后）：
1. (0,0)   -> (-450, 1550)
2. (25,0)  -> (6650, 1550)
3. (0,25)  -> (-250, 8675)
4. (25,25) -> (6900, 8650)
5. (0,12)  -> (-300, 4950)
6. (12,0)  -> (2975, 1500)
"""

import numpy as np
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D

# 设置matplotlib支持中文
plt.rcParams['font.sans-serif'] = ['SimHei']  # 使用黑体
plt.rcParams['axes.unicode_minus'] = False  # 解决负号显示问题

# ====================== 输入数据（修正版） ======================
quick_gcode= """
G1 X3000 Y5500 F10000 ; 0,0
G1 X3400 Y12700 F10000 ; 0,25
G1 X10200 Y5400 F10000 ; 25,0
G1 X10500 Y12500 F10000 ; 25,25
G1 X3200 Y8950 F10000 ; 0,12
G1 X6500 Y5550 F10000 ; 12,0
"""
def quickgcode_to_calibration_points(gcode):
    """将快速标定GCode转换为标定点坐标"""
    points = []
    for line in gcode.splitlines():
        if line.startswith('G1'):
            parts = line.split(';')
            # 提取GCode坐标
            gcode_parts = parts[0].split()
            x = float(gcode_parts[1].replace('X', ''))
            y = float(gcode_parts[2].replace('Y', ''))
            # 提取豆子坐标
            bean_coord = parts[1].strip()
            bean_x, bean_y = map(int, bean_coord.split(','))
            # 添加到结果列表
            points.append(((bean_x, bean_y), (x, y)))
    return points

calibration_points = [
    # 四个角点
    ((0, 0), (-450, 1550)),      # 点1: 左下角
    ((25, 0), (6650, 1550)),     # 点2: 右下角
    ((0, 25), (-250, 8675)),     # 点3: 左上角
    ((25, 25), (6900, 8650)),    # 点4: 右上角
    # 两个中间点
    ((0, 12), (-300, 4950)),     # 点5: 左边中间
    ((12, 0), (2975, 1500)),     # 点6: 下边中间
]

calibration_points = quickgcode_to_calibration_points(quick_gcode)

# ====================== 数据可视化函数 ======================
def plot_calibration_points_2d(points):
    """2D可视化：显示豆子坐标和GCode坐标的对应关系"""
    bead_coords = np.array([p[0] for p in points])
    gcode_coords = np.array([p[1] for p in points])
    
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 6))
    
    # 豆子坐标空间
    ax1.scatter(bead_coords[:, 0], bead_coords[:, 1], c='blue', s=100, marker='o')
    for i, (x, y) in enumerate(bead_coords):
        ax1.annotate(f'{i+1}', (x, y), xytext=(5, 5), textcoords='offset points')
    ax1.set_xlabel('豆子 X')
    ax1.set_ylabel('豆子 Y')
    ax1.set_title('豆子坐标空间')
    ax1.grid(True)
    ax1.axis('equal')
    
    # GCode坐标空间
    ax2.scatter(gcode_coords[:, 0], gcode_coords[:, 1], c='red', s=100, marker='s')
    for i, (x, y) in enumerate(gcode_coords):
        ax2.annotate(f'{i+1}', (x, y), xytext=(5, 5), textcoords='offset points')
    ax2.set_xlabel('GCode X')
    ax2.set_ylabel('GCode Y')
    ax2.set_title('GCode坐标空间')
    ax2.grid(True)
    
    plt.tight_layout()
    plt.show()

def plot_mapping_surface(M, points):
    """3D可视化：显示映射曲面"""
    fig = plt.figure(figsize=(15, 6))
    
    # X映射曲面
    ax1 = fig.add_subplot(121, projection='3d')
    X_bead = np.linspace(0, 25, 30)
    Y_bead = np.linspace(0, 25, 30)
    X_bead, Y_bead = np.meshgrid(X_bead, Y_bead)
    Z_gcode_x = M[0,0] * X_bead + M[0,1] * Y_bead + M[0,2]
    
    ax1.plot_surface(X_bead, Y_bead, Z_gcode_x, alpha=0.7, cmap='viridis')
    ax1.scatter([p[0][0] for p in points], [p[0][1] for p in points], 
                [p[1][0] for p in points], color='red', s=50)
    ax1.set_xlabel('豆子 X')
    ax1.set_ylabel('豆子 Y')
    ax1.set_zlabel('GCode X')
    ax1.set_title('GCode X 映射曲面')
    
    # Y映射曲面
    ax2 = fig.add_subplot(122, projection='3d')
    Z_gcode_y = M[1,0] * X_bead + M[1,1] * Y_bead + M[1,2]
    
    ax2.plot_surface(X_bead, Y_bead, Z_gcode_y, alpha=0.7, cmap='plasma')
    ax2.scatter([p[0][0] for p in points], [p[0][1] for p in points], 
                [p[1][1] for p in points], color='red', s=50)
    ax2.set_xlabel('豆子 X')
    ax2.set_ylabel('豆子 Y')
    ax2.set_zlabel('GCode Y')
    ax2.set_title('GCode Y 映射曲面')
    
    plt.tight_layout()
    plt.show()

# ====================== 拟合仿射变换 ======================
def fit_affine_transform(points):
    """
    拟合从(bead_x, bead_y)到(gcode_x, gcode_y)的仿射变换
    
    参数:
        points: 列表，每个元素为 ((bead_x, bead_y), (gcode_x, gcode_y))
    
    返回:
        M: 变换矩阵 (2x3)
        stats: 统计信息字典
    """
    # 准备数据矩阵
    A_list = []  # 系数矩阵 [x, y, 1]
    b_x_list = []  # X目标值
    b_y_list = []  # Y目标值
    
    for (bead, gcode) in points:
        bx, by = bead
        gx, gy = gcode
        
        A_list.append([bx, by, 1])
        b_x_list.append(gx)
        b_y_list.append(gy)
    
    A = np.array(A_list)
    b_x = np.array(b_x_list)
    b_y = np.array(b_y_list)
    
    # 使用最小二乘法求解
    coeff_x, residuals_x, rank_x, s_x = np.linalg.lstsq(A, b_x, rcond=None)
    coeff_y, residuals_y, rank_y, s_y = np.linalg.lstsq(A, b_y, rcond=None)
    
    # 构建变换矩阵
    M = np.array([coeff_x, coeff_y])
    
    # 计算预测值和误差
    predicted_x = A @ coeff_x
    predicted_y = A @ coeff_y
    
    errors_x = b_x - predicted_x
    errors_y = b_y - predicted_y
    
    # 计算统计信息
    rmse_x = np.sqrt(np.mean(errors_x**2))
    rmse_y = np.sqrt(np.mean(errors_y**2))
    max_error_x = np.max(np.abs(errors_x))
    max_error_y = np.max(np.abs(errors_y))
    
    # 计算R²分数
    ss_res_x = np.sum(errors_x**2)
    ss_tot_x = np.sum((b_x - np.mean(b_x))**2)
    r2_x = 1 - (ss_res_x / ss_tot_x) if ss_tot_x != 0 else 1
    
    ss_res_y = np.sum(errors_y**2)
    ss_tot_y = np.sum((b_y - np.mean(b_y))**2)
    r2_y = 1 - (ss_res_y / ss_tot_y) if ss_tot_y != 0 else 1
    
    stats = {
        'rmse_x': rmse_x,
        'rmse_y': rmse_y,
        'max_error_x': max_error_x,
        'max_error_y': max_error_y,
        'r2_x': r2_x,
        'r2_y': r2_y,
        'predicted_x': predicted_x,
        'predicted_y': predicted_y,
        'residuals_x': errors_x,
        'residuals_y': errors_y
    }
    
    return M, stats

# ====================== 映射器类 ======================
class BeadToGCodeMapper:
    """豆子坐标到GCode坐标的映射器"""
    
    def __init__(self, calibration_points):
        """
        初始化映射器
        
        参数:
            calibration_points: 列表，每个元素为 ((bead_x, bead_y), (gcode_x, gcode_y))
        """
        self.calibration_points = calibration_points
        self.M, self.stats = fit_affine_transform(calibration_points)
        
        # 计算边界
        bead_xs = [p[0][0] for p in calibration_points]
        bead_ys = [p[0][1] for p in calibration_points]
        self.bead_bounds = (min(bead_xs), max(bead_xs), min(bead_ys), max(bead_ys))
        
        print(f"映射器初始化完成")
        print(f"  RMSE: X={self.stats['rmse_x']:.2f}, Y={self.stats['rmse_y']:.2f}")
        print(f"  R²: X={self.stats['r2_x']:.6f}, Y={self.stats['r2_y']:.6f}")
        print(f"  豆子坐标范围: X[{self.bead_bounds[0]}, {self.bead_bounds[1]}], "
              f"Y[{self.bead_bounds[2]}, {self.bead_bounds[3]}]")
    
    def bead_to_gcode(self, bead_x, bead_y):
        """
        将豆子坐标转换为GCode坐标
        
        参数:
            bead_x, bead_y: 豆子坐标（整数或浮点数）
        
        返回:
            (gcode_x, gcode_y): GCode坐标
        """
        gx = self.M[0,0] * bead_x + self.M[0,1] * bead_y + self.M[0,2]
        gy = self.M[1,0] * bead_x + self.M[1,1] * bead_y + self.M[1,2]
        return gx, gy
    
    def gcode_to_bead(self, gcode_x, gcode_y):
        """
        将GCode坐标反转换为豆子坐标（如果矩阵可逆）
        
        参数:
            gcode_x, gcode_y: GCode坐标
        
        返回:
            (bead_x, bead_y): 豆子坐标
        """
        # 提取线性部分
        A = self.M[:, :2]  # 2x2矩阵
        b = self.M[:, 2]   # 平移向量
        
        # 解方程 A * [bx; by] + b = [gx; gy]
        # 即 A * [bx; by] = [gx; gy] - b
        try:
            bead = np.linalg.solve(A, np.array([gcode_x, gcode_y]) - b)
            return bead[0], bead[1]
        except np.linalg.LinAlgError:
            print("警告：变换矩阵不可逆，无法进行反向转换")
            return None, None
    
    def generate_gcode(self, bead_x, bead_y, feedrate=5000, comment=""):
        """
        生成单条GCode命令
        
        参数:
            bead_x, bead_y: 豆子坐标
            feedrate: 进给速度
            comment: 注释
        
        返回:
            GCode命令字符串
        """
        gx, gy = self.bead_to_gcode(bead_x, bead_y)
        if comment:
            return f"G1 X{gx:.1f} Y{gy:.1f} F{feedrate}  ; {comment}"
        else:
            return f"G1 X{gx:.1f} Y{gy:.1f} F{feedrate}"
    
    def generate_grid_gcode(self, start_x, start_y, end_x, end_y, step_x=1, step_y=1, feedrate=5000):
        """
        生成网格点的GCode
        
        参数:
            start_x, start_y: 起始豆子坐标
            end_x, end_y: 结束豆子坐标
            step_x, step_y: X和Y方向的步长
            feedrate: 进给速度
        
        返回:
            GCode命令列表
        """
        gcodes = []
        x = start_x
        while x <= end_x + 1e-9:  # 添加小容差处理浮点误差
            y = start_y
            while y <= end_y + 1e-9:
                gcodes.append(self.generate_gcode(x, y, feedrate, f"点({x:.1f},{y:.1f})"))
                y += step_y
            x += step_x
        return gcodes
    
    def print_summary(self):
        """打印映射器摘要信息"""
        print("\n" + "=" * 60)
        print("映射器配置摘要")
        print("=" * 60)
        
        print(f"\n变换矩阵 M (2x3):")
        print(f"  [[{self.M[0,0]:.4f}, {self.M[0,1]:.4f}, {self.M[0,2]:.2f}],")
        print(f"   [{self.M[1,0]:.4f}, {self.M[1,1]:.4f}, {self.M[1,2]:.2f}]]")
        
        print(f"\n映射公式:")
        print(f"  GCode_X = {self.M[0,0]:.4f}*bx + {self.M[0,1]:.4f}*by + {self.M[0,2]:.2f}")
        print(f"  GCode_Y = {self.M[1,0]:.4f}*bx + {self.M[1,1]:.4f}*by + {self.M[1,2]:.2f}")
        
        print(f"\n拟合统计:")
        print(f"  X轴 - RMSE: {self.stats['rmse_x']:.2f}, 最大误差: {self.stats['max_error_x']:.2f}, R²: {self.stats['r2_x']:.6f}")
        print(f"  Y轴 - RMSE: {self.stats['rmse_y']:.2f}, 最大误差: {self.stats['max_error_y']:.2f}, R²: {self.stats['r2_y']:.6f}")
        
        print(f"\n各点误差:")
        for i, ((bx, by), (gx, gy)) in enumerate(self.calibration_points):
            pred_gx, pred_gy = self.bead_to_gcode(bx, by)
            err_x = pred_gx - gx
            err_y = pred_gy - gy
            print(f"  点{i+1}: 豆子({bx:2d},{by:2d}) -> 实际({gx:5.0f},{gy:5.0f}) | "
                  f"预测({pred_gx:6.1f},{pred_gy:6.1f}) | 误差({err_x:+5.1f},{err_y:+5.1f})")

# ====================== 主程序 ======================
def main():
    print("=" * 60)
    print("PixelBean 坐标映射计算 (修正版)")
    print("=" * 60)
    
    # 显示标定点
    print("标定点数据:")
    for i, ((bx, by), (gx, gy)) in enumerate(calibration_points):
        print(f"  点{i+1}: 豆子({bx:2d}, {by:2d}) -> GCode({gx:5.0f}, {gy:5.0f})")
    
    # 创建映射器
    mapper = BeadToGCodeMapper(calibration_points)
    
    # 打印摘要
    mapper.print_summary()
    
    # 可视化
    print("\n生成可视化图表...")
    plot_calibration_points_2d(calibration_points)
    plot_mapping_surface(mapper.M, calibration_points)
    
    # 测试关键点
    print("\n" + "=" * 60)
    print("关键点映射测试")
    print("=" * 60)
    
    test_points = [
        (0, 0), (12, 0), (25, 0),
        (0, 12), (12, 12), (25, 12),
        (0, 25), (12, 25), (25, 25),
        (6, 6), (18, 18), (8, 20)
    ]
    
    print(f"\n{'豆子坐标':<12} {'GCode坐标':<20} {'GCode命令':<40}")
    print("-" * 72)
    
    for bx, by in test_points:
        gx, gy = mapper.bead_to_gcode(bx, by)
        gcode = mapper.generate_gcode(bx, by, feedrate=8000)
        print(f"({bx:2d}, {by:2d})    -> ({gx:7.1f}, {gy:7.1f})    -> {gcode}")
    
    # 生成26*26点阵中所有坐标的GCode
    print("\n" + "=" * 60)
    print("26*26点阵所有坐标GCode生成")
    print("=" * 60)
    
    # 生成26*26点阵（0-25, 0-25）
    gcodes = []
    for bx in range(26):
        for by in range(26):
            gcodes.append(mapper.generate_gcode(bx, by, feedrate=6000, comment=f"点({bx},{by})"))
    
    print(f"\n26*26点阵GCode ({len(gcodes)}个点):")
    # 只打印前10个和后10个作为示例
    for i, gcode in enumerate(gcodes[:10]):
        print(f"  {gcode}")
    if len(gcodes) > 20:
        print("  ... (省略中间点) ...")
    for i, gcode in enumerate(gcodes[-10:]):
        print(f"  {gcode}")
    
    # 导出到文件
    output_file = r"D:\work\trea\pixel_bean\grbl-code\26x26_grid.gcode"
    with open(output_file, 'w', encoding='utf-8') as f:
        f.write("; PixelBean 26*26点阵\n")
        f.write("; 生成时间: 2026-03-03\n")
        f.write("G90  ; 绝对坐标模式\n")
        f.write("M0   ; 电机使能\n\n")
        
        for gcode in gcodes:
            f.write(gcode + "\n")
        f.write("\nM0   ; 电机禁用\n")
    
    print(f"\nGCode已保存到: {output_file}")

# ====================== 命令行接口 ======================
if __name__ == "__main__":
    import sys
    
    if len(sys.argv) > 1 and sys.argv[1] == "--interactive":
        # 交互模式
        mapper = BeadToGCodeMapper(calibration_points)
        print("\n进入交互模式，输入 'x,y' 获取GCode坐标，输入 'q' 退出")
        
        while True:
            try:
                cmd = input("\n豆子坐标 (格式: x,y): ").strip()
                if cmd.lower() == 'q':
                    break
                
                parts = cmd.split(',')
                if len(parts) != 2:
                    print("格式错误，请输入 x,y")
                    continue
                
                x = float(parts[0].strip())
                y = float(parts[1].strip())
                
                gx, gy = mapper.bead_to_gcode(x, y)
                gcode = mapper.generate_gcode(x, y, feedrate=8000)
                
                print(f"GCode坐标: ({gx:.1f}, {gy:.1f})")
                print(f"GCode命令: {gcode}")
                
            except KeyboardInterrupt:
                break
            except Exception as e:
                print(f"错误: {e}")
    
    else:
        # 正常模式
        main()