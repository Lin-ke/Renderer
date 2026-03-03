import numpy as np
from PIL import Image

# 1. 定义图像的尺寸 (256x256)
width, height = 256, 256

# 2. 定义所需的颜色 (R=0.2, G=0, B=0.8)
# 并将其从 0.0-1.0 的浮点数范围转换为 0-255 的整数范围。
color_normalized = np.array([1,1,1])
color_int = (color_normalized * 255).astype(np.uint8)

# 3. 创建一个 NumPy 数组，并用选定的颜色填充它
# 数组形状为 (height, width, channels)，其中 channels 为 3 (RGB)
array = np.full((height, width, 3), color_int, dtype=np.uint8)

# 4. 从 NumPy 数组创建一个 PIL 图像对象
image = Image.fromarray(array)

# 5. 将图像保存为文件
image.save('generated_image.png')
