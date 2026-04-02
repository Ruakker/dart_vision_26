# DartVision

基于LicheeRV Nano的视觉识别项目，使用Sophgo SDK进行NPU加速推理。

## 项目结构

```
DartVision/
├── CMakeLists.txt          # CMake构建配置
├── .clangd                 # clangd LSP配置
├── README.md
├── build/                  # 编译输出目录
│   └── bin/
│       └── dart_vision     # 可执行文件
├── src/                    # 源代码
│   ├── main.c
│   ├── config.c
│   ├── uart.c
│   ├── tdl.c              # TDL SDK封装
│   ├── venc.c             # 视频编码
│   ├── kalman.c           # 卡尔曼滤波
│   ├── cJSON.c
│   ├── serial/            # 串口通信
│   ├── middleware_utils.c
│   └── sample_utils.c
├── include/               # 头文件
│   └── dart_vision/
├── config/                # 配置文件
├── scripts/               # 脚本
├── assets/                # 资源文件
└── sdk/                   # SDK依赖
    ├── prebuilt/          # 预编译库
    │   ├── lib/
    │   └── usr/lib/
    ├── opencv/            # OpenCV头文件和库
    ├── tdl_sdk/           # TDL SDK头文件
    ├── middleware/        # Middleware头文件
    ├── osdrv/             # OS驱动头文件
    └── cvi_rtsp/          # RTSP头文件
```

## 依赖

- RISC-V交叉编译工具链 (riscv64-linux-musl)
- Sophgo SDK预编译库
- OpenCV (已包含在sdk目录)
- CMake 3.15+
- Ninja (可选，推荐)

## 编译

### 设置环境变量

```bash
export COMPILER=/opt/riscv64-linux-musl/bin
```

### 使用Ninja编译（推荐）

```bash
cd build
cmake -G Ninja ..
ninja
```

### 使用Make编译

```bash
cd build
cmake ..
make
```

## 运行

将编译好的可执行文件传输到LicheeRV Nano开发板：

```bash
scp build/bin/dart_vision root@<板子IP>:/root/
```

在开发板上运行：

```bash
./dart_vision
```

## 开发

项目已配置clangd支持，可在支持LSP的编辑器中获得：
- 代码补全
- 语法检查
- 跳转到定义
- 函数签名提示

## 注意事项

- 本项目仅能在LicheeRV Nano或兼容的RISC-V开发板上运行
- 需要确保开发板上有对应的运行时库
- SDK库路径已配置为 `/mnt/system/lib` 和 `/mnt/system/usr/lib`
