# # PNG转JPG批量转换工具
#
# 这个工具可以批量将PNG格式图片转换为JPG格式，并将透明背景替换为黑色。
#
# ## 功能特点
#
# - ✅ 批量处理多个PNG文件
# - ✅ 自动检测和处理透明背景
# - ✅ 将透明区域替换为黑色背景
# - ✅ 保持图片质量和清晰度
# - ✅ 支持递归处理子目录（高级版本）
#
# ## 文件说明
#
# ### 1. simple_converter.py（推荐使用）
# 简化版本，专门处理当前目录的PNG文件：
# - 自动处理当前目录下的所有PNG文件
# - 在当前目录创建`jpg_images`文件夹存放转换后的文件
# - 无需任何参数，直接运行即可
#
# ## 使用方法
#
# ### 简单使用（推荐）
# ```bash
# python simple_converter.py
# ```
#

import os
from PIL import Image

def convert_png_to_jpg_simple():
    """简单的PNG转JPG转换函数"""
    # 获取当前目录
    current_dir = os.path.dirname(os.path.abspath(__file__))
    
    # 创建输出目录
    output_dir = os.path.join(current_dir, 'jpg_images')
    os.makedirs(output_dir, exist_ok=True)
    
    # 统计变量
    processed_count = 0
    success_count = 0
    error_count = 0
    
    print("开始批量转换PNG到JPG...")
    print(f"输入目录: {current_dir}")
    print(f"输出目录: {output_dir}")
    print("-" * 40)
    
    # 遍历当前目录中的所有文件
    for filename in os.listdir(current_dir):
        # 只处理PNG文件
        if filename.lower().endswith('.png'):
            processed_count += 1
            input_path = os.path.join(current_dir, filename)
            output_filename = filename[:-4] + '.jpg'  # 移除.png，添加.jpg
            output_path = os.path.join(output_dir, output_filename)
            
            try:
                # 打开PNG图片
                with Image.open(input_path) as img:
                    # 处理透明背景
                    if img.mode in ('RGBA', 'LA', 'P'):
                        # 创建黑色背景的RGB图像
                        rgb_img = Image.new('RGB', img.size, (0, 0, 0))  # 黑色背景
                        
                        # 如果有透明通道，进行合成
                        if img.mode == 'RGBA':
                            # 分离通道
                            r, g, b, a = img.split()
                            # 合并RGB通道
                            rgb_image = Image.merge('RGB', (r, g, b))
                            # 将原图粘贴到黑色背景上，使用alpha通道作为遮罩
                            rgb_img.paste(rgb_image, mask=a)
                        else:
                            # 其他模式直接转换
                            rgb_img.paste(img.convert('RGB'))
                    else:
                        # 不透明图片直接转换
                        rgb_img = img.convert('RGB')
                    
                    # 保存为JPG
                    rgb_img.save(output_path, 'JPEG', quality=95)
                    print(f"✓ {filename} -> {output_filename}")
                    success_count += 1
                    
            except Exception as e:
                print(f"✗ 转换失败 {filename}: {str(e)}")
                error_count += 1
    
    # 输出统计结果
    print("-" * 40)
    print("转换完成!")
    print(f"处理文件数: {processed_count}")
    print(f"成功转换: {success_count}")
    print(f"转换失败: {error_count}")
    print(f"JPG文件已保存到: {output_dir}")

if __name__ == "__main__":
    convert_png_to_jpg_simple()