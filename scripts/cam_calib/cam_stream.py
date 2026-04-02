#!/usr/bin/python

import socket
import struct
import cv2
import time
from maix import image, app, camera
import sys
import numpy as np # 确保引入numpy

# ================= 配置 =================
SERVER_PORT = 8899
WIDTH = 1280
HEIGHT = 720
# =======================================

def main():
    print("正在初始化摄像头 (用于标定)...")
    # 使用 NV21 以获得 720P 分辨率
    cam = camera.Camera(WIDTH, HEIGHT, image.Format.FMT_YVU420SP)

    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    
    try:
        server_socket.bind(('0.0.0.0', SERVER_PORT))
        server_socket.listen(1)
    except Exception as e:
        print(f"❌ 端口绑定失败: {e}")
        return

    print(f"=== 标定专用流 (灰度) 启动: {SERVER_PORT} ===")

    while not app.need_exit():
        try:
            conn, addr = server_socket.accept()
            print(f"✅ 连接: {addr}")
            conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

            while not app.need_exit():
                img = cam.read()
                if img is None: continue

                # 1. 获取原始数据 (包含 Y 和 UV)
                # NV21 格式下，image2cv 返回的高度是 height * 1.5
                raw_frame = image.image2cv(img, ensure_bgr=False, copy=False)
                
                # 2. 【关键步骤】只切取 Y 通道 (亮度/灰度)
                # 抛弃底部的 UV 数据，只保留上面的真实图像部分
                # 这样得到的 gray_frame 就是标准的 1280x720 灰度图
                gray_frame = raw_frame[:HEIGHT, :]

                # 3. 压缩发送 (灰度图压缩更快)
                encode_param = [int(cv2.IMWRITE_JPEG_QUALITY), 90] # 标定建议画质高一点
                _, img_encoded = cv2.imencode('.jpg', gray_frame, encode_param)

                data = img_encoded.tobytes()
                size_pack = struct.pack('<L', len(data))

                try:
                    conn.sendall(size_pack + data)
                except socket.error:
                    break

        except Exception as e:
            print(f"错误: {e}")
            if 'conn' in locals(): conn.close()
        finally:
            if 'conn' in locals(): conn.close()

    server_socket.close()

if __name__ == "__main__":
    main()