# 第三方组件说明 / Third-Party Notices

DartVision 本体代码以 [MIT License](LICENSE) 发布。仓库中引入或依赖的第三方组件遵循各自的许可证，逐项列在下方。

> 说明：`sdk/`（Sophgo / CVITEK 平台 SDK）**不随本仓库分发**，需要按 [README.md 的「平台 SDK」](README.md#平台-sdk) 自行获取。下表仍列出它的许可证信息，以便使用者了解约束。

## 1. 随仓库分发的第三方源码

| 组件 | 位置 | 许可证 | 版权 |
| --- | --- | --- | --- |
| cJSON | `src/cJSON.c`、`include/cJSON.h` | MIT | Copyright (c) 2009-2017 Dave Gamble and cJSON contributors |

cJSON 许可证原文见 `src/cJSON.c` 文件头。

## 2. 从 Sophgo / CVITEK SDK 派生的代码

以下文件包含或改编自 SDK 的 sample 代码，运行时依赖 SDK 提供的头文件与预编译库：

| 文件 | 来源 |
| --- | --- |
| `src/middleware_utils.c` / `src/middleware_utils.h` | `sdk/middleware` 中 VI/ISP/VPSS/VENC/RTSP 初始化示例 |
| `src/sample_utils.c` / `src/sample_utils.h` | `sdk/tdl_sdk/sample` 中的模型信息与推理封装 |

这些派生代码遵循 SDK 上游的许可条款（见下节）。若你重新分发包含此类派生的目标文件，请同时保留 Sophgo / CVITEK 的版权声明。

## 3. 构建与运行时依赖（需自行获取，不随仓库分发）

| 组件 | 许可证 | 说明 |
| --- | --- | --- |
| Sophgo TDL SDK (`sdk/tdl_sdk`) | BSD 2-Clause，Copyright (c) 2022 Sophgo Technologies | 见 `sdk/tdl_sdk/LICENSE`、`sdk/tdl_sdk/ThirdPartyNotices.txt` |
| Cvitek middleware / osdrv (`sdk/middleware`、`sdk/osdrv`) | "Copyright (C) Cvitek Co., Ltd. 2019-2020. All rights reserved."（SDK 内未附独立 LICENSE 文件，按 SDK 发布条款使用） | 头文件与预编译库 |
| Cvitek cvi_rtsp (`sdk/cvi_rtsp`) | 同上；内部集成 live555（LGPL-2.1） | 见 `sdk/cvi_rtsp/README.md` |
| OpenCV 3.2.0 (`sdk/opencv`) | BSD 3-Clause | 交叉编译后的头文件与库 |
| inih (`sdk/middleware/3rdparty/inih`) | BSD 3-Clause，Copyright (c) 2009 Ben Hoyt | INI 配置解析 |
| Eigen / nlohmann-json / sse2neon / Darknet | MPL-2.0 / MIT / MIT / 自定义 | 见 `sdk/tdl_sdk/ThirdPartyNotices.txt` |

**分发注意事项**

- 上述 SDK 组件的再分发条件由版权方决定，本仓库不对其授权做任何转授。仅通过本仓库的脚本下载 SDK 时，请遵守 SDK 原始发布页面的条款。
- 生产固件中的厂商运行库位于板端 `/mnt/system/lib`、`/mnt/system/usr/lib`、`/mnt/system/usr/lib/3rd`，来自 Sipeed / Sophgo 官方系统镜像，同样不在本仓库中。
- 若你把 live555（LGPL-2.1）静态链接进闭源固件，需自行评估 LGPL 的动态链接/可替换要求。

## 4. 串口通信协议实现

`src/serial/drv_cmd_com.c`、`src/serial/drv_CRC.c` 及 `include/tool/` 下的协议实现来自 HERO 战队（RoboMaster）电控组，随本项目一同以 MIT 许可开源。原始作者信息保留在文件头注释中。

## 5. 数据集与模型权重

`assets/files/root/model/*.cvimodel` 为本项目自行训练的 YOLOv8 检测模型导出的 CV181x 量化模型。训练数据、标注工具与训练脚本不在本仓库内；如果你要再训练，请自备数据集并遵守其使用条款。
