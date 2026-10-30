# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

DartVision is a C11 embedded vision and target-tracking application for the LicheeRV Nano / CV181x RISC-V platform. It uses the Sophgo SDK (not vendored in git) for camera middleware, hardware video encoding, RTSP, and CVI TDL YOLOv8 NPU inference. The host build cross-compiles a board executable; it is not runnable on the development host.

## Build

The project requires CMake 3.15+, a RISC-V musl cross-toolchain, and either Ninja or GNU Make. The platform SDK must be placed under `sdk/` first (it is gitignored): run `./scripts/fetch_sdk.sh --from-dir /path/to/sdk`, see the「平台 SDK」section of `README.md`.

```bash
export COMPILER=/opt/riscv64-linux-musl/bin
./scripts/fetch_sdk.sh --from-dir /path/to/sdk   # 仅第一次需要
cmake -S . -B build -G Ninja
cmake --build build
```

With GNU Make instead of Ninja, drop `-G Ninja`. Do not reuse a build directory that was configured with a different compiler: the compilers come from `$COMPILER` and are baked into `CMakeCache.txt`.

The resulting target binary is `build/bin/dart_vision`. `CMakeLists.txt` sets the compilers directly from `$COMPILER`; configure will fail if it is unset or does not contain `riscv64-unknown-linux-musl-gcc` and `g++`.

There is no CTest configuration, test target, or host-executable test suite. Verify changes by cross-compiling, then exercising the affected camera, inference, UART, or streaming workflow on the board.

## Board Deployment And Runtime

Deploy the executable and its required configuration files:

```bash
scp build/bin/dart_vision root@<board-ip>:/root/
scp assets/files/root/config/config.json root@<board-ip>:/root/config/
scp assets/files/root/config/camera_info.json root@<board-ip>:/root/config/
```

On the board, invoke a bundled `.cvimodel` or another YOLOv8 model path:

```bash
./dart_vision model/<model>.cvimodel
./dart_vision model/<model>.cvimodel --raw
```

The application reads `/root/config/config.json` and `/root/config/camera_info.json`, communicates through `/dev/ttyS0` at 115200 baud, and expects vendor runtime libraries under `/mnt/system/lib`, `/mnt/system/usr/lib`, and `/mnt/system/usr/lib/3rd`. Sensor selection comes from `/mnt/data/sensor_cfg.ini`; the documented target sensor is `GCORE_GC4653_MIPI_720P_60FPS_10BIT`.

Before using UART0 for the protocol, disable its console/getty and set `consoledev=null` in `/boot/uEnv.txt`; see `assets/files/Instruction.md`. That file also documents the board autostart script.

## Architecture

`src/main.c` owns process startup and teardown. It parses `YOLOV8_MODEL_PATH [--raw]`, loads application, calibration, and Kalman JSON configuration, initializes UART plus CVITEK VI/ISP/VPSS/VENC/RTSP middleware and TDL, then starts five threads.

The camera pipeline fans out from VPSS group 0:

- **VPSS channel 1 -> `src/tdl.c`**: obtains inference frames, runs YOLOv8 through CVI TDL, rejects invalid boxes using aspect ratio and green-YUV checks, scores candidates against Kalman prediction, transforms rotated/cropped coordinates into calibrated yaw/pitch and monocular distance, and updates the constant-velocity four-state Kalman tracker.
- **VPSS channel 0 -> `src/venc.c`**: obtains streaming frames, optionally draws detections/tracking overlays (disabled by `--raw`), sends H.264 through RTSP, and optionally records the elementary stream.

`src/uart.c` has independent receive and send threads. The send thread waits on the tracking condition variable and publishes `DetectionPacket` values; the receive thread parses feedback packets and writes feedback logs when recording is enabled.

`State` in `include/dart_vision/state.h` is the synchronization boundary across those threads. Use its `state_update_*` and `state_copy_*` helpers instead of accessing `last_obj` or `track` without their mutexes. `track_seq` with `track_cv` prevents missed tracking-update notifications.

`src/config.c` defines the JSON-backed operational parameters: exposure and optical-axis correction, detection filtering/scoring, recording, and Kalman confirmation/loss/noise parameters. Camera intrinsics (`fx`, `fy`, `cx`, `cy`) live in `camera_info.json`; recalibrate with the tools under `scripts/cam_calib/`, which use board TCP port 8899.

## Repository Boundaries

- Treat `sdk/` as the vendor platform SDK. Application work normally belongs in `src/` and `include/dart_vision/`; do not modify SDK code merely to adjust application behavior.
- `assets/files/root/` mirrors the board's `/root/` deployment layout, including default configuration and model artifacts.
- `scripts/fetch_sdk.sh` validates or links a platform SDK into `sdk/`; `README.md`'s「平台 SDK」section explains why the SDK is not vendored.
- `scripts/cam_calib/` holds the chessboard-calibration client (PC) and the board-side frame server; see README "相机标定".
