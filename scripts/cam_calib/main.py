import cv2
import socket
import struct
import numpy as np
import time
import sys
import os
import json
import argparse
from datetime import datetime

# ================= 默认参数 =================
DEFAULT_PORT = 8899
DEFAULT_CHESSBOARD = (6, 9)     # 棋盘格内角点数量 (列, 行)
DEFAULT_SQUARE_SIZE_MM = 29.2   # 每个格子的边长 (mm)
# ===========================================


def parse_args():
    parser = argparse.ArgumentParser(
        description="摄像头内参标定客户端：从开发板接收图像流，采集棋盘格并计算内参",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument(
        "--host",
        default=os.environ.get("DART_BOARD_IP"),
        help="开发板 IP 地址，也可用环境变量 DART_BOARD_IP 指定",
    )
    parser.add_argument("--port", type=int, default=DEFAULT_PORT,
                        help="开发板 cam_stream.py 监听的 TCP 端口")
    parser.add_argument("--cols", type=int, default=DEFAULT_CHESSBOARD[0],
                        help="棋盘格列方向内角点数")
    parser.add_argument("--rows", type=int, default=DEFAULT_CHESSBOARD[1],
                        help="棋盘格行方向内角点数")
    parser.add_argument("--square-mm", type=float, default=DEFAULT_SQUARE_SIZE_MM,
                        help="棋盘格单个方格边长（毫米）")
    parser.add_argument("--output-dir", default=None,
                        help="标定结果输出目录（默认 calib_output_<时间戳>）")
    args = parser.parse_args()

    if not args.host:
        parser.error("缺少开发板地址：请用 --host <IP> 指定，或设置环境变量 DART_BOARD_IP")
    return args

def recvall(sock, count):
    buf = b''
    while count:
        try:
            newbuf = sock.recv(count)
            if not newbuf: return None
            buf += newbuf
            count -= len(newbuf)
        except socket.error:
            return None
    return buf

def calculate_reprojection_error(objpoints, imgpoints, rvecs, tvecs, mtx, dist):
    mean_error = 0
    for i in range(len(objpoints)):
        imgpoints2, _ = cv2.projectPoints(objpoints[i], rvecs[i], tvecs[i], mtx, dist)
        error = cv2.norm(imgpoints[i], imgpoints2, cv2.NORM_L2) / len(imgpoints2)
        mean_error += error
    return mean_error / len(objpoints)

def save_results(mtx, dist, error, img_shape, frames, out_dir=None):
    """
    保存标定结果和图片到带时间戳的文件夹中
    """
    # 1. 创建带时间戳的文件夹
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    save_dir = out_dir if out_dir else f"calib_output_{timestamp}"
    
    if not os.path.exists(save_dir):
        os.makedirs(save_dir)
        print(f"\n📂 创建输出目录: {save_dir}")

    # 2. 保存 JSON 数据
    # Numpy 数组不能直接被 JSON 序列化，需要用 .tolist() 转为普通列表
    calib_data = {
        "calibration_time": timestamp,
        "image_width": img_shape[0],
        "image_height": img_shape[1],
        "rms_error": error,
        "camera_matrix": mtx.tolist(),        # 内参矩阵 (3x3)
        "distortion_coefficients": dist.tolist() # 畸变系数 (1x5)
    }

    json_path = os.path.join(save_dir, "calibration_result.json")
    with open(json_path, "w") as f:
        json.dump(calib_data, f, indent=4) # indent=4 让文件排版美观可读

    # 2.1 同时输出一份可直接部署到板端 /root/config/ 的 camera_info.json
    board_camera_info = {
        "calibration_time": timestamp,
        "image_width": img_shape[0],
        "image_height": img_shape[1],
        "rms_error": error,
        "camera_matrix": mtx.tolist(),
        "distortion_coefficients": dist.tolist(),
    }
    camera_info_path = os.path.join(save_dir, "camera_info.json")
    with open(camera_info_path, "w") as f:
        json.dump(board_camera_info, f, indent=4)

    # 3. 保存采集的图片
    print("💾 正在保存采集的图片...")
    for i, frame in enumerate(frames):
        img_name = os.path.join(save_dir, f"img_{i:02d}.jpg")
        cv2.imwrite(img_name, frame)
    
    print(f"✅ 所有数据已保存至 '{save_dir}/'")
    print(f"   - 结果文件: calibration_result.json / camera_info.json（后者可直接拷到板端）")
    print(f"   - 图片数量: {len(frames)} 张")

def main():
    args = parse_args()
    board_ip = args.host
    board_port = args.port
    chessboard_size = (args.cols, args.rows)
    square_size_mm = args.square_mm

    print(f"🚀 启动标定客户端")
    print(f"目标地址: {board_ip}:{board_port}")
    print(f"棋盘格: {chessboard_size[0]}×{chessboard_size[1]} 内角点, 方格 {square_size_mm} mm")
    
    client_socket = None

    # --- 🔄 循环极速重连 ---
    print("正在尝试连接...", end="", flush=True)
    while True:
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(2.0) 
            sock.connect((board_ip, board_port))
            sock.settimeout(None)
            client_socket = sock
            print("\n✅ 连接成功！")
            break
        except (socket.error, socket.timeout):
            print(f"\r❌ 连接失败，正在立即重试...", end="", flush=True)
            if sock: sock.close()
        except KeyboardInterrupt:
            print("\n\n用户取消，退出程序。")
            return
    # -------------------------

    objp = np.zeros((chessboard_size[0] * chessboard_size[1], 3), np.float32)
    objp[:, :2] = np.mgrid[0:chessboard_size[0], 0:chessboard_size[1]].T.reshape(-1, 2) * square_size_mm

    all_objpoints = [] 
    all_imgpoints = [] 
    saved_frames = [] # 用于保存采集到的原始图片
    img_shape = None

    print("\n" + "="*40)
    print(" 📷 操作指南 ")
    print(" [空格] / [C] : 采集当前图片")
    print(" [退格] / [U] : 撤回上一张 (Undo)")
    print(" [回车] / [Q] : 结束并计算保存")
    print("="*40 + "\n")
    print("正在接收图像流...")

    try:
        while True:
            size_data = recvall(client_socket, 4)
            if size_data is None:
                print("\n⚠️ 连接断开"); break
            
            size = struct.unpack('<L', size_data)[0]
            img_data = recvall(client_socket, size)
            if img_data is None:
                print("\n⚠️ 数据不完整"); break

            nparr = np.frombuffer(img_data, np.uint8)
            frame = cv2.imdecode(nparr, cv2.IMREAD_COLOR)
            
            if frame is None: continue

            if img_shape is None:
                img_shape = frame.shape[:2][::-1]
                print(f"📷 分辨率: {img_shape}")

            display_frame = frame.copy()
            gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
            
            ret, corners = cv2.findChessboardCorners(gray, chessboard_size, 
                cv2.CALIB_CB_ADAPTIVE_THRESH + cv2.CALIB_CB_NORMALIZE_IMAGE)

            # --- UI ---
            cv2.putText(display_frame, f"Count: {len(all_imgpoints)}", (10, 40), 
                        cv2.FONT_HERSHEY_SIMPLEX, 1.0, (0, 255, 0), 2)

            corners2 = None
            if ret:
                term = (cv2.TERM_CRITERIA_EPS + cv2.TERM_CRITERIA_MAX_ITER, 30, 0.001)
                corners2 = cv2.cornerSubPix(gray, corners, (11,11), (-1,-1), term)
                cv2.drawChessboardCorners(display_frame, chessboard_size, corners2, ret)

            cv2.imshow('Calibration PC Client', display_frame)
            
            # --- 控制 ---
            key = cv2.waitKey(1) & 0xFF

            if key == ord('c') or key == ord(' '):
                if ret and corners2 is not None:
                    all_objpoints.append(objp)
                    all_imgpoints.append(corners2)
                    saved_frames.append(frame.copy()) # 保存原始帧副本
                    print(f"📥 采集成功! 当前总数: {len(all_imgpoints)}")
                    
                    cv2.rectangle(display_frame, (0,0), (img_shape[0], img_shape[1]), (0, 255, 0), 10)
                    cv2.imshow('Calibration PC Client', display_frame)
                    cv2.waitKey(50) 
                else:
                    print("⚠️ 未检测到角点")

            elif key == ord('u') or key == 8: 
                if len(all_imgpoints) > 0:
                    all_imgpoints.pop()
                    all_objpoints.pop()
                    saved_frames.pop() # 同时也删除保存的图片
                    print(f"↩️ 已撤回。当前总数: {len(all_imgpoints)}")
                    
                    cv2.rectangle(display_frame, (0,0), (img_shape[0], img_shape[1]), (0, 0, 255), 10)
                    cv2.imshow('Calibration PC Client', display_frame)
                    cv2.waitKey(50)

            elif key == ord('q') or key == 13: 
                print("\n🛑 结束采集")
                break

    except KeyboardInterrupt:
        print("\n用户中断")
    finally:
        if client_socket: client_socket.close()
        cv2.destroyAllWindows()

    # --- 计算与保存 ---
    if len(all_imgpoints) >= 3:
        print("\n" + "="*30)
        print(f"正在计算内参 (样本数: {len(all_imgpoints)})...")
        
        ret, mtx, dist, rvecs, tvecs = cv2.calibrateCamera(all_objpoints, all_imgpoints, img_shape, None, None)

        print(f"\n✅ 标定完成! RMS Error: {ret:.4f}")
        print(f"\n[内参矩阵 K]:\n{mtx}")
        print(f"\n[畸变系数 D]:\n{dist}")
        
        mean_error = calculate_reprojection_error(all_objpoints, all_imgpoints, rvecs, tvecs, mtx, dist)
        print(f"\n📊 平均重投影误差: {mean_error:.4f} px")

        # 调用保存函数
        save_results(mtx, dist, ret, img_shape, saved_frames, args.output_dir)
        
    else:
        print(f"\n⚠️ 样本不足，未计算。")

if __name__ == "__main__":
    main()
