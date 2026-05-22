import cv2
import socket
import struct
import numpy as np
import time
import sys
import os
import json
from datetime import datetime

# ================= 配置区域 =================
BOARD_IP = '10.249.210.1' 
BOARD_PORT = 8899
# CHESSBOARD_SIZE = (6, 9)    # 棋盘格内角点数量 (行, 列)
# SQUARE_SIZE_MM = 29.2       # 每个格子的边长 (mm)
CHESSBOARD_SIZE = (7, 11)    # 棋盘格内角点数量 (行, 列)
SQUARE_SIZE_MM = 96.0       # 每个格子的边长 (mm)
# ===========================================

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

def save_results(mtx, dist, error, img_shape, frames):
    """
    保存标定结果和图片到带时间戳的文件夹中
    """
    # 1. 创建带时间戳的文件夹
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    save_dir = f"calib_output_{timestamp}"
    
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

    # 3. 保存采集的图片
    print("💾 正在保存采集的图片...")
    for i, frame in enumerate(frames):
        img_name = os.path.join(save_dir, f"img_{i:02d}.jpg")
        cv2.imwrite(img_name, frame)
    
    print(f"✅ 所有数据已保存至 '{save_dir}/'")
    print(f"   - 结果文件: calibration_result.json")
    print(f"   - 图片数量: {len(frames)} 张")

def main():
    print(f"🚀 启动标定客户端")
    print(f"目标地址: {BOARD_IP}:{BOARD_PORT}")
    
    client_socket = None

    # --- 🔄 循环极速重连 ---
    print("正在尝试连接...", end="", flush=True)
    while True:
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(2.0) 
            sock.connect((BOARD_IP, BOARD_PORT))
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

    objp = np.zeros((CHESSBOARD_SIZE[0] * CHESSBOARD_SIZE[1], 3), np.float32)
    objp[:, :2] = np.mgrid[0:CHESSBOARD_SIZE[0], 0:CHESSBOARD_SIZE[1]].T.reshape(-1, 2) * SQUARE_SIZE_MM

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
            
            ret, corners = cv2.findChessboardCorners(gray, CHESSBOARD_SIZE, 
                cv2.CALIB_CB_ADAPTIVE_THRESH + cv2.CALIB_CB_NORMALIZE_IMAGE)

            # --- UI ---
            cv2.putText(display_frame, f"Count: {len(all_imgpoints)}", (10, 40), 
                        cv2.FONT_HERSHEY_SIMPLEX, 1.0, (0, 255, 0), 2)

            corners2 = None
            if ret:
                term = (cv2.TERM_CRITERIA_EPS + cv2.TERM_CRITERIA_MAX_ITER, 30, 0.001)
                corners2 = cv2.cornerSubPix(gray, corners, (11,11), (-1,-1), term)
                cv2.drawChessboardCorners(display_frame, CHESSBOARD_SIZE, corners2, ret)

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
        save_results(mtx, dist, ret, img_shape, saved_frames)
        
    else:
        print(f"\n⚠️ 样本不足，未计算。")

if __name__ == "__main__":
    main()
