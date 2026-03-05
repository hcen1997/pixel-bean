import os
from PIL import Image

def compress_image(input_path, output_path, quality=80, resize_width=None):
    """
    压缩单张图片
    :param input_path: 原始图片路径
    :param output_path: 压缩后图片保存路径
    :param quality: 压缩质量（1-95，数值越小压缩率越高，默认80）
    :param resize_width: 按宽度等比例缩放（None则不缩放，比如传800表示宽度缩到800px）
    """
    try:
        # 打开图片
        with Image.open(input_path) as img:
            # 如果需要缩放，按宽度等比例调整
            if resize_width and img.width > resize_width:
                # 计算等比例高度
                ratio = resize_width / img.width
                resize_height = int(img.height * ratio)
                img = img.resize((resize_width, resize_height), Image.Resampling.LANCZOS)
            
            # 处理PNG透明图片（避免压缩后背景变黑）
            if img.mode in ("RGBA", "P"):
                img = img.convert("RGB")
            
            # 保存压缩后的图片
            img.save(
                output_path,
                "JPEG",
                quality=quality,
                optimize=True,  # 开启优化
                progressive=True  # 渐进式JPEG（加载更快）
            )
        print(f"✅ 压缩完成：{input_path} -> {output_path}")
    except Exception as e:
        print(f"❌ 压缩失败 {input_path}：{str(e)}")

def batch_compress_images(input_dir, output_dir, quality=80, resize_width=None):
    """
    批量压缩文件夹下的所有图片
    :param input_dir: 原始图片文件夹
    :param output_dir: 压缩后图片保存文件夹
    """
    # 支持的图片格式
    supported_formats = (".jpg", ".jpeg", ".png", ".bmp", ".webp")
    
    # 创建输出文件夹（不存在则创建）
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)
    
    # 遍历文件夹下的所有文件
    for filename in os.listdir(input_dir):
        # 只处理图片文件
        if filename.lower().endswith(supported_formats):
            input_path = os.path.join(input_dir, filename)
            # 统一输出为JPG格式（压缩率更高）
            output_filename = os.path.splitext(filename)[0] + ".jpg"
            output_path = os.path.join(output_dir, output_filename)
            # 压缩单张图片
            compress_image(input_path, output_path, quality, resize_width)

# ---------------------- 用法示例 ----------------------
if __name__ == "__main__":
    # 用法1：压缩单张图片
    # compress_image(
    #     input_path="原始图片.jpg",  # 你的原始图片路径
    #     output_path="压缩后图片.jpg",  # 保存路径
    #     quality=70,  # 压缩质量（建议70-85）
    #     resize_width=1000  # 缩放到宽度1000px（不需要则传None）
    # )

    # 用法2：批量压缩文件夹里的所有图片（推荐你用这个）
    batch_compress_images(
        input_dir=r"D:\work\trea\pixel_bean\grbl-code",  # 放你原始图片的文件夹名
        output_dir=r"D:\work\trea\pixel_bean\grbl-code",  # 压缩后保存的文件夹名
        quality=80,  # 压缩质量，80兼顾画质和体积
        # resize_width=1200  # 宽度缩到1200px，适合上传/视频
    )