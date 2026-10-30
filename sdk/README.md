# sdk/

这里是 **Sophgo / CVITEK 平台 SDK 的安装位置**。目录内容**不纳入版本控制**（见仓库根目录 `.gitignore`），需要自行获取。

```bash
./scripts/fetch_sdk.sh --from-dir /path/to/your/sdk   # 软链接已有 SDK
./scripts/fetch_sdk.sh --tarball sdk.tar.gz           # 从本地压缩包安装
./scripts/fetch_sdk.sh --url <url>                    # 从 URL 下载安装
./scripts/fetch_sdk.sh --check                        # 检查是否安装完整
```

安装完成后本目录应包含：

```
sdk/
├── tdl_sdk/     TDL NPU 推理库与头文件
├── middleware/  VI/ISP/VPSS/VENC/RTSP 中间件
├── osdrv/       驱动 uapi 头文件
├── cvi_rtsp/    RTSP 服务端
├── opencv/      交叉编译的 OpenCV 3.2.0
├── prebuilt/    预编译运行库（lib、usr/lib、usr/lib/3rd）
└── README.md    本文件
```

完整说明：**[README.md 的「平台 SDK」](../README.md#平台-sdk)** · 许可证：[THIRD_PARTY_NOTICES.md](../THIRD_PARTY_NOTICES.md)
