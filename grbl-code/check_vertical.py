import cv2
import numpy as np
import matplotlib.pyplot as plt
from sklearn.cluster import DBSCAN  # 用于空间聚类融合线条

def analyze_lines(image_path,
                  angle_tol=10,    # 判定为垂直/水平的角度容差（度）
                  min_line_len=50, # 最小线段长度（像素）
                  max_line_gap=10, # 线段最大间隙（像素）
                  merge_threshold=20 # 相邻线条融合阈值（像素）
                  ):
    # 1. 读取并预处理图像
    img = cv2.imread(image_path)
    if img is None:
        raise ValueError(f"无法读取图像: {image_path}")

    gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    # 二值化（可根据实际情况调整阈值）
    _, thresh = cv2.threshold(gray, 127, 255, cv2.THRESH_BINARY_INV)
    # 边缘检测
    edges = cv2.Canny(thresh, 50, 150, apertureSize=3)

    # 2. 霍夫变换检测线段
    lines = cv2.HoughLinesP(
        edges,
        rho=1,
        theta=np.pi / 180,
        threshold=50,
        minLineLength=min_line_len,
        maxLineGap=max_line_gap
    )

    if lines is None:
        print("未检测到任何线段")
        return [], []

    # 3. 按角度分类：垂直/水平
    vertical_lines = []  # 存储 (x1, y1, x2, y2, angle_deg)
    horizontal_lines = []

    for line in lines:
        x1, y1, x2, y2 = line[0]
        dx = x2 - x1
        dy = y2 - y1
        angle_rad = np.arctan2(dy, dx)
        angle_deg = np.degrees(angle_rad)

        # 标准化角度到 [-90, 90]
        if angle_deg > 90:
            angle_deg -= 180
        elif angle_deg < -90:
            angle_deg += 180

        # 判定垂直（接近 ±90°）
        if abs(abs(angle_deg) - 90) <= angle_tol:
            vertical_lines.append((x1, y1, x2, y2, angle_deg))
        # 判定水平（接近 0°）
        elif abs(angle_deg) <= angle_tol:
            horizontal_lines.append((x1, y1, x2, y2, angle_deg))

    # ========== 核心新增：融合20px内的相邻线条（最佳实践） ==========
    def merge_nearby_lines(lines, is_vertical, merge_threshold):
        """
        融合邻近线条：垂直线条按x坐标聚类，水平线条按y坐标聚类
        :param lines: 原始检测的线条列表
        :param is_vertical: 是否为垂直线
        :param merge_threshold: 融合阈值（像素）
        :return: 融合后的线条列表
        """
        if len(lines) < 2:
            return lines
        
        # 提取聚类特征：垂直线取中点x，水平线取中点y
        if is_vertical:
            # 垂直线：特征为 (中点x, 角度)
            features = np.array([[(x1+x2)/2, angle] for x1,y1,x2,y2,angle in lines])
        else:
            # 水平线：特征为 (中点y, 角度)
            features = np.array([[(y1+y2)/2, angle] for x1,y1,x2,y2,angle in lines])
        
        # DBSCAN聚类（空间邻近聚类的最佳实践）
        # eps=merge_threshold：20px内的视为同一类
        # min_samples=1：允许单个线条作为一类
        db = DBSCAN(eps=merge_threshold, min_samples=1).fit(features)
        labels = db.labels_
        
        merged_lines = []
        # 遍历每个聚类，合并为一条线
        for label in np.unique(labels):
            cluster_lines = [lines[i] for i in np.where(labels == label)[0]]
            
            if is_vertical:
                # 垂直线合并：取x均值，y取最大范围，角度均值
                x_mid = np.mean([(x1+x2)/2 for x1,y1,x2,y2,_ in cluster_lines])
                y_min = np.min([min(y1,y2) for x1,y1,x2,y2,_ in cluster_lines])
                y_max = np.max([max(y1,y2) for x1,y1,x2,y2,_ in cluster_lines])
                angle_avg = np.mean([angle for _,_,_,_,angle in cluster_lines])
                merged_lines.append((x_mid, y_min, x_mid, y_max, angle_avg))
            else:
                # 水平线合并：取y均值，x取最大范围，角度均值
                y_mid = np.mean([(y1+y2)/2 for x1,y1,x2,y2,_ in cluster_lines])
                x_min = np.min([min(x1,x2) for x1,y1,x2,y2,_ in cluster_lines])
                x_max = np.max([max(x1,x2) for x1,y1,x2,y2,_ in cluster_lines])
                angle_avg = np.mean([angle for _,_,_,_,angle in cluster_lines])
                merged_lines.append((x_min, y_mid, x_max, y_mid, angle_avg))
        
        return merged_lines
    
    # ========== 新增函数：计算直线方程 ==========
    def get_line_equation(x1, y1, x2, y2, is_vertical):
        """
        计算直线方程：
        - 垂直线：x = 常数
        - 水平线/斜线：y = kx + b
        """
        if is_vertical:
            # 垂直线（x坐标固定）
            x_const = round((x1 + x2) / 2, 2)
            return f"x = {x_const} "
        else:
            # 非垂直线：计算斜率k和截距b
            if x2 - x1 == 0:
                y_const = round((y1 + y2) / 2, 2)
                return f"y = {y_const}"
            k = (y2 - y1) / (x2 - x1)
            b = y1 - k * x1
            # 格式化输出（保留2位小数）
            k = round(k, 4)
            b = round(b, 2)
            return f"y = {k}x + {b}"
    
    # 融合垂直线和水平线
    merged_vertical = merge_nearby_lines(vertical_lines, is_vertical=True, merge_threshold=merge_threshold)
    merged_horizontal = merge_nearby_lines(horizontal_lines, is_vertical=False, merge_threshold=merge_threshold)

    # 4. 分析融合后垂直线条间距和垂直度 + 打印直线方程
    print("="*80)
    print(f"原始垂直线条: {len(vertical_lines)} 条 → 融合后: {len(merged_vertical)} 条")
    
    # 按照方程的c项偏移排列垂直线（x常数）
    def get_vertical_c(line):
        x1, _, x2, _, _ = line
        return (x1 + x2) / 2
    merged_vertical.sort(key=get_vertical_c)
    
    if len(merged_vertical) > 1:
        # 计算间距
        xs = np.array([(x1 + x2) / 2 for x1, y1, x2, y2, _ in merged_vertical])
        spacing = np.diff(xs)  # 相邻间距（像素）

        print("融合后垂直线条间距（像素）:")
        print(np.round(spacing, 2))
        print(f"平均间距: {np.mean(spacing):.2f} px, 标准差: {np.std(spacing):.2f} px")

        # 垂直度分析
        angles_v = np.array([a for _, _, _, _, a in merged_vertical])
        print(f"融合后垂直线条角度分布（度）: 平均={np.mean(angles_v):.2f}, 标准差={np.std(angles_v):.2f}")
    
    # 打印每条垂直线的方程
    print("\n【垂直线条详细信息（含直线方程）】")
    for idx, line in enumerate(merged_vertical):
        x1, y1, x2, y2, angle = line
        equation = get_line_equation(x1, y1, x2, y2, is_vertical=True)
        print(f"垂直线 {idx+1}: 角度={angle:.2f}° | 方程: {equation} | 端点: ({round(x1,1)},{round(y1,1)}) - ({round(x2,1)},{round(y2,1)})")

    # 分析融合后水平线条间距 + 打印直线方程
    print("\n" + "="*80)
    print(f"原始水平线条: {len(horizontal_lines)} 条 → 融合后: {len(merged_horizontal)} 条")
    
    # 按照方程的c项偏移排列水平线（y截距）
    def get_horizontal_c(line):
        _, y1, _, y2, _ = line
        return (y1 + y2) / 2
    merged_horizontal.sort(key=get_horizontal_c)
    
    if len(merged_horizontal) > 1:
        ys = np.array([(y1 + y2) / 2 for x1, y1, x2, y2, _ in merged_horizontal])
        spacing_h = np.diff(ys)

        print("融合后水平线条间距（像素）:")
        print(np.round(spacing_h, 2))
        print(f"平均间距: {np.mean(spacing_h):.2f} px, 标准差: {np.std(spacing_h):.2f} px")
    
    # 打印每条水平线的方程
    print("\n【水平线条详细信息（含直线方程）】")
    for idx, line in enumerate(merged_horizontal):
        x1, y1, x2, y2, angle = line
        equation = get_line_equation(x1, y1, x2, y2, is_vertical=False)
        print(f"水平线 {idx+1}: 角度={angle:.2f}° | 方程: {equation} | 端点: ({round(x1,1)},{round(y1,1)}) - ({round(x2,1)},{round(y2,1)})")
    print("="*80)

    # 5. 在图像上可视化融合后的结果
    result = img.copy()
    # 绘制融合后的垂直线（红色）并标注信息
    for idx, line in enumerate(merged_vertical):
        x1, y1, x2, y2, angle = line
        equation = get_line_equation(x1, y1, x2, y2, is_vertical=True)
        # 绘制线条
        cv2.line(result, (int(round(x1)), int(round(y1))), (int(round(x2)), int(round(y2))), (0, 0, 255), 2)
        # 计算标注位置（线条中间偏上）
        text_x = int(round(x1)) + 5
        text_y = int(round((y1 + y2) / 2)) - 10
        # 标注线条信息（英文）
        cv2.putText(result, f"Vertical {idx+1}", (text_x, text_y), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 0, 255), 1)
        cv2.putText(result, f"Angle: {angle:.1f}°", (text_x, text_y + 20), cv2.FONT_HERSHEY_SIMPLEX, 0.4, (0, 0, 255), 1)
        cv2.putText(result, f"Equation: {equation}", (text_x, text_y + 40), cv2.FONT_HERSHEY_SIMPLEX, 0.4, (0, 0, 255), 1)
    # 绘制融合后的水平线（绿色）并标注信息
    for idx, line in enumerate(merged_horizontal):
        x1, y1, x2, y2, angle = line
        equation = get_line_equation(x1, y1, x2, y2, is_vertical=False)
        # 绘制线条
        cv2.line(result, (int(round(x1)), int(round(y1))), (int(round(x2)), int(round(y2))), (0, 255, 0), 2)
        # 计算标注位置（线条中间偏左）
        text_x = int(round((x1 + x2) / 2)) - 100
        text_y = int(round(y1)) - 10
        # 标注线条信息（英文）
        cv2.putText(result, f"Horizontal {idx+1}", (text_x, text_y), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 0), 1)
        cv2.putText(result, f"Angle: {angle:.1f}°", (text_x, text_y + 20), cv2.FONT_HERSHEY_SIMPLEX, 0.4, (0, 255, 0), 1)
        cv2.putText(result, f"Equation: {equation}", (text_x, text_y + 40), cv2.FONT_HERSHEY_SIMPLEX, 0.4, (0, 255, 0), 1)

    # 显示结果
    plt.figure(figsize=(12, 8))
    plt.subplot(121), plt.imshow(cv2.cvtColor(img, cv2.COLOR_BGR2RGB)), plt.title('Original')
    plt.subplot(122), plt.imshow(cv2.cvtColor(result, cv2.COLOR_BGR2RGB)), plt.title('Merged Lines (20px Threshold)')
    
    # 保存结果到图片文件
    save_path = 'merged_lines_result.png'
    cv2.imwrite(save_path, result)
    print(f"\n✅ 结果已保存到: {save_path}")
    
    plt.show()

    return merged_vertical, merged_horizontal

# ========== 运行示例 ==========
if __name__ == "__main__":
    # 替换为你的图片路径
    vertical_lines, horizontal_lines = analyze_lines(
        r"D:\work\trea\pixel_bean\grbl-code\IMG_20260304_140346.jpg",
        merge_threshold=20  # 20px内的线条融合为一条
    )

# 分析： 
# 原始垂直线条: 582 条 → 融合后: 50 条
#  【垂直线条详细信息（含直线方程）】
# 垂直线 1: 角度=89.18° | 方程: x = 14.94  | 端点: (14.9,1778) - (14.9,3817)
# 垂直线 2: 角度=-90.00° | 方程: x = 16.78  | 端点: (16.8,1662) - (16.8,3770)
# 垂直线 3: 角度=89.49° | 方程: x = 384.14  | 端点: (384.1,603) - (384.1,3779)
# 垂直线 4: 角度=-89.82° | 方程: x = 386.56  | 端点: (386.6,198) - (386.6,3870)        
# 垂直线 5: 角度=-89.98° | 方程: x = 549.87  | 端点: (549.9,313) - (549.9,3515)        
# 垂直线 6: 角度=89.47° | 方程: x = 550.0  | 端点: (550.0,223) - (550.0,3665)
# 垂直线 7: 角度=-89.85° | 方程: x = 780.03  | 端点: (780.0,460) - (780.0,2765)        
# 垂直线 8: 角度=89.64° | 方程: x = 822.83  | 端点: (822.8,958) - (822.8,2267)
# 垂直线 9: 角度=-90.00° | 方程: x = 823.9  | 端点: (823.9,1027) - (823.9,2687)        
# 垂直线 10: 角度=-90.00° | 方程: x = 1126.19  | 端点: (1126.2,195) - (1126.2,2506)    
# 垂直线 11: 角度=89.43° | 方程: x = 1160.5  | 端点: (1160.5,392) - (1160.5,493)       
# 垂直线 12: 角度=-89.96° | 方程: x = 1167.03  | 端点: (1167.0,191) - (1167.0,2490)    
# 垂直线 13: 角度=-89.93° | 方程: x = 1311.04  | 端点: (1311.0,832) - (1311.0,3112)    
# 垂直线 14: 角度=89.71° | 方程: x = 1313.4  | 端点: (1313.4,1195) - (1313.4,1754)     
# 垂直线 15: 角度=-89.81° | 方程: x = 1525.14  | 端点: (1525.1,586) - (1525.1,2832)    
# 垂直线 16: 角度=89.05° | 方程: x = 1528.5  | 端点: (1528.5,854) - (1528.5,914)       
# 垂直线 17: 角度=89.67° | 方程: x = 1563.67  | 端点: (1563.7,879) - (1563.7,2784)     
# 垂直线 18: 角度=-89.84° | 方程: x = 1565.66  | 端点: (1565.7,587) - (1565.7,2860)    
# 垂直线 19: 角度=-89.90° | 方程: x = 1850.72  | 端点: (1850.7,586) - (1850.7,2801)    
# 垂直线 20: 角度=-90.00° | 方程: x = 1891.0  | 端点: (1891.0,2716) - (1891.0,2839)    
# 垂直线 21: 角度=89.02° | 方程: x = 1927.62  | 端点: (1927.6,1958) - (1927.6,2558)    
# 垂直线 22: 角度=-89.90° | 方程: x = 1929.07  | 端点: (1929.1,544) - (1929.1,2716)    
# 垂直线 23: 角度=-90.00° | 方程: x = 1965.0  | 端点: (1965.0,2668) - (1965.0,2719)    
# 垂直线 24: 角度=89.46° | 方程: x = 2009.41  | 端点: (2009.4,2011) - (2009.4,2794)    
    # 垂直线 25: 角度=-90.00° | 方程: x = 2011.17  | 端点: (2011.2,504) - (2011.2,2668)    
# 垂直线 26: 角度=89.16° | 方程: x = 2046.5  | 端点: (2046.5,2528) - (2046.5,2596)     
    # 垂直线 27: 角度=-90.00° | 方程: x = 2051.82  | 端点: (2051.8,491) - (2051.8,2705)    
    # 垂直线 28: 角度=-89.94° | 方程: x = 2093.04  | 端点: (2093.0,571) - (2093.0,2871)    
# 垂直线 29: 角度=-90.00° | 方程: x = 2133.43  | 端点: (2133.4,599) - (2133.4,2834)    
    # 垂直线 30: 角度=89.42° | 方程: x = 2133.86  | 端点: (2133.9,602) - (2133.9,2714)     
# 垂直线 31: 角度=90.00° | 方程: x = 2169.0  | 端点: (2169.0,2702) - (2169.0,2825)     
    # 垂直线 32: 角度=-89.71° | 方程: x = 2172.84  | 端点: (2172.8,571) - (2172.8,2910)    
    # 垂直线 33: 角度=-89.90° | 方程: x = 2214.43  | 端点: (2214.4,613) - (2214.4,2921)    
# 垂直线 34: 角度=90.00° | 方程: x = 2217.0  | 端点: (2217.0,2542) - (2217.0,2598)     
# 垂直线 35: 角度=89.81° | 方程: x = 2292.38  | 端点: (2292.4,1441) - (2292.4,2781)    
# 垂直线 36: 角度=-89.87° | 方程: x = 2295.29  | 端点: (2295.3,653) - (2295.3,2992)    
# 垂直线 37: 角度=90.00° | 方程: x = 2331.0  | 端点: (2331.0,1605) - (2331.0,1661)     
# 垂直线 38: 角度=-89.87° | 方程: x = 2335.79  | 端点: (2335.8,695) - (2335.8,2966)    
# 垂直线 39: 角度=-88.09° | 方程: x = 2384.0  | 端点: (2384.0,609) - (2384.0,669)      
# 垂直线 40: 角度=-89.88° | 方程: x = 2459.93  | 端点: (2459.9,585) - (2459.9,3116)    
# 垂直线 41: 角度=89.71° | 方程: x = 2460.06  | 端点: (2460.1,587) - (2460.1,2989)     
# 垂直线 42: 角度=-90.00° | 方程: x = 2498.0  | 端点: (2498.0,2797) - (2498.0,2859)    
# 垂直线 43: 角度=-89.97° | 方程: x = 2541.55  | 端点: (2541.6,451) - (2541.6,2796)    
# 垂直线 44: 角度=89.69° | 方程: x = 2788.91  | 端点: (2788.9,1232) - (2788.9,3337)    
# 垂直线 45: 角度=-89.98° | 方程: x = 2790.68  | 端点: (2790.7,237) - (2790.7,3873)    
# 垂直线 46: 角度=-89.90° | 方程: x = 2853.66  | 端点: (2853.7,194) - (2853.7,3655)    
# 垂直线 47: 角度=89.74° | 方程: x = 2856.85  | 端点: (2856.8,462) - (2856.8,3606)     
# 垂直线 48: 角度=-90.00° | 方程: x = 2951.67  | 端点: (2951.7,214) - (2951.7,614)     
# 垂直线 49: 角度=89.34° | 方程: x = 2970.62  | 端点: (2970.6,321) - (2970.6,3429)     
# 垂直线 50: 角度=-90.00° | 方程: x = 2989.0  | 端点: (2989.0,2331) - (2989.0,3490)    

 
# 平均间距: 97.43 px, 标准差: 163.17 px

# 【水平线条详细信息（含直线方程）】
# 水平线 1: 角度=9.16° | 方程: y = 0.0x + 99.0 | 端点: (551,99.0) - (613,99.0)
# 水平线 2: 角度=0.06° | 方程: y = 0.0x + 179.49 | 端点: (410,179.5) - (2948,179.5)    
# 水平线 3: 角度=-0.10° | 方程: y = 0.0x + 246.32 | 端点: (837,246.3) - (1876,246.3)   
# 水平线 4: 角度=0.10° | 方程: y = 0.0x + 288.4 | 端点: (828,288.4) - (1938,288.4)     
# 水平线 5: 角度=0.11° | 方程: y = 0.0x + 330.7 | 端点: (821,330.7) - (1887,330.7)     
# 水平线 6: 角度=0.14° | 方程: y = 0.0x + 372.91 | 端点: (832,372.9) - (1928,372.9)    
# 水平线 7: 角度=-0.09° | 方程: y = 0.0x + 436.98 | 端点: (789,437.0) - (2548,437.0)   
# 水平线 8: 角度=0.07° | 方程: y = 0.0x + 495.47 | 端点: (829,495.5) - (2001,495.5)    
# 水平线 9: 角度=0.04° | 方程: y = 0.0x + 537.9 | 端点: (816,537.9) - (1937,537.9)     
# 水平线 10: 角度=0.14° | 方程: y = 0.0x + 575.83 | 端点: (842,575.8) - (2177,575.8)   
# 水平线 11: 角度=0.00° | 方程: y = 0.0x + 621.42 | 端点: (829,621.4) - (1972,621.4)   
# 水平线 12: 角度=-0.08° | 方程: y = 0.0x + 662.32 | 端点: (859,662.3) - (1972,662.3)  
# 水平线 13: 角度=0.14° | 方程: y = 0.0x + 703.46 | 端点: (862,703.5) - (1967,703.5)   
# 水平线 14: 角度=0.23° | 方程: y = 0.0x + 745.46 | 端点: (909,745.5) - (1953,745.5)   
# 水平线 15: 角度=-0.01° | 方程: y = 0.0x + 781.15 | 端点: (861,781.1) - (2468,781.1)  
# 水平线 16: 角度=0.05° | 方程: y = 0.0x + 820.92 | 端点: (863,820.9) - (2467,820.9)   
# 水平线 17: 角度=0.04° | 方程: y = 0.0x + 868.36 | 端点: (833,868.4) - (1971,868.4)   
# 水平线 18: 角度=0.00° | 方程: y = 0.0x + 910.73 | 端点: (834,910.7) - (1970,910.7)   
# 水平线 19: 角度=-0.13° | 方程: y = 0.0x + 950.18 | 端点: (839,950.2) - (1972,950.2)  
# 水平线 20: 角度=0.12° | 方程: y = 0.0x + 991.93 | 端点: (833,991.9) - (1900,991.9)   
# 水平线 21: 角度=0.00° | 方程: y = 0.0x + 1032.36 | 端点: (819,1032.4) - (1926,1032.4)
# 水平线 22: 角度=0.00° | 方程: y = 0.0x + 1971.08 | 端点: (1344,1971.1) - (2447,1971.1)

# 水平线 37: 角度=1.57° | 方程: y = 0.0x + 2838.0 | 端点: (1858,2838.0) - (1931,2838.0)
# 水平线 38: 角度=0.00° | 方程: y = 0.0x + 2875.0 | 端点: (2093,2875.0) - (2177,2875.0)
# : (388,3874.9) - (2786,3874.9)
# 水平线 42: 角度=-0.58° | 方程: y = 0.0x + 4093.5 | 端点: (2661,4093.5) - (2759,4093.5)
# ================================================================================

# ✅ 结果已保存到: merged_lines_result.png


标准测试区间
# 水平线 23: 角度=0.13° | 方程: y = 0.0x + 2264.81 | 端点: (847,2264.8) - (1940,2264.8)
# 水平线 24: 角度=0.00° | 方程: y = 0.0x + 2305.29 | 端点: (826,2305.3) - (1963,2305.3)
# 水平线 25: 角度=0.33° | 方程: y = 0.0x + 2347.33 | 端点: (837,2347.3) - (1973,2347.3)
# 水平线 26: 角度=-0.08° | 方程: y = 0.0x + 2386.35 | 端点: (826,2386.4) - (1927,2386.4)
# 水平线 27: 角度=0.00° | 方程: y = 0.0x + 2426.8 | 端点: (820,2426.8) - (1966,2426.8) 
# 水平线 28: 角度=0.00° | 方程: y = 0.0x + 2468.25 | 端点: (823,2468.2) - (1933,2468.2)
# 水平线 29: 角度=0.07° | 方程: y = 0.0x + 2509.06 | 端点: (838,2509.1) - (1946,2509.1)
# 水平线 30: 角度=0.08° | 方程: y = 0.0x + 2548.05 | 端点: (826,2548.0) - (1892,2548.0)
# 水平线 31: 角度=0.00° | 方程: y = 0.0x + 2590.25 | 端点: (819,2590.2) - (1831,2590.2)
# 水平线 32: 角度=-0.10° | 方程: y = 0.0x + 2630.08 | 端点: (830,2630.1) - (1900,2630.1)
# 水平线 33: 角度=0.00° | 方程: y = 0.0x + 2672.73 | 端点: (831,2672.7) - (1986,2672.7)
# 水平线 34: 角度=-0.15° | 方程: y = 0.0x + 2712.4 | 端点: (789,2712.4) - (1966,2712.4)
# 水平线 35: 角度=0.15° | 方程: y = 0.0x + 2752.44 | 端点: (811,2752.4) - (1964,2752.4)
# 水平线 36: 角度=0.00° | 方程: y = 0.0x + 2794.57 | 端点: (817,2794.6) - (1977,2794.6)
# 间距40.7像素 标准差1.3px  波动2%
# 垂直线 25: 角度=-90.00° | 方程: x = 2011.17  | 端点: (2011.2,504) - (2011.2,2668)    
# 垂直线 27: 角度=-90.00° | 方程: x = 2051.82  | 端点: (2051.8,491) - (2051.8,2705)    
# 垂直线 28: 角度=-89.94° | 方程: x = 2093.04  | 端点: (2093.0,571) - (2093.0,2871)    
# 垂直线 30: 角度=89.42° | 方程: x = 2133.86  | 端点: (2133.9,602) - (2133.9,2714)     
# 垂直线 32: 角度=-89.71° | 方程: x = 2172.84  | 端点: (2172.8,571) - (2172.8,2910)    
# 垂直线 33: 角度=-89.90° | 方程: x = 2214.43  | 端点: (2214.4,613) - (2214.4,2921)    
# 间距40.6px 标准差0.8px 波动1%
# 综上。 所有垂直线的间距均在标准测试区间内，符合要求。 水平，垂直运动在千分之5内不存在误差