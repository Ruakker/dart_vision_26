#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# DartVision SDK 获取脚本
#
# Sophgo / CVITEK 平台 SDK 体积约 560 MB 且为厂商版权代码，不纳入本仓库。
# 本脚本把 SDK 安装到 ./sdk/，支持三种来源：
#
#   ./scripts/fetch_sdk.sh --from-dir /path/to/sdk      # 软链接已有 SDK 目录
#   ./scripts/fetch_sdk.sh --tarball sdk.tar.gz         # 从本地压缩包安装
#   ./scripts/fetch_sdk.sh --url https://.../sdk.tar.gz # 从 URL 下载安装
#   ./scripts/fetch_sdk.sh --check                      # 仅检查安装是否完整
#
# 详细说明见 README.md 的「平台 SDK」一节
# ---------------------------------------------------------------------------
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SDK_DIR="${REPO_ROOT}/sdk"

# CMakeLists.txt 依赖的必需子目录
REQUIRED_SUBDIRS=(tdl_sdk middleware osdrv cvi_rtsp opencv prebuilt)
# 用于校验安装完整性的代表文件
REQUIRED_FILES=(
  "tdl_sdk/include/cvi_tdl.h"
  "middleware/include"
  "osdrv/interdrv"
  "cvi_rtsp/include/cvi_rtsp"
  "opencv/include"
  "prebuilt/usr/lib"
)

# 临时目录由 EXIT trap 统一清理：RETURN trap 是全局的，函数返回后仍会挂到外层，
# 到 main 返回时局部变量已消失，在 set -u 下会报 "tmp: unbound variable"。
TMP_DIR=""
make_tmp() {
  [ -n "${TMP_DIR}" ] || TMP_DIR="$(mktemp -d)"
}
cleanup() {
  [ -n "${TMP_DIR}" ] && rm -rf "${TMP_DIR}"
  return 0
}
trap cleanup EXIT

info()  { printf '\033[1;34m[SDK]\033[0m %s\n' "$*"; }
warn()  { printf '\033[1;33m[SDK] 警告:\033[0m %s\n' "$*" >&2; }
die()   { printf '\033[1;31m[SDK] 错误:\033[0m %s\n' "$*" >&2; exit 1; }

usage() {
  cat <<'EOF'
DartVision SDK 获取脚本

Sophgo / CVITEK 平台 SDK 体积约 560 MB 且为厂商版权代码，不纳入本仓库。
本脚本把 SDK 安装到 ./sdk/，支持三种来源：

  ./scripts/fetch_sdk.sh --from-dir /path/to/sdk      # 软链接已有 SDK 目录
  ./scripts/fetch_sdk.sh --tarball sdk.tar.gz         # 从本地压缩包安装
  ./scripts/fetch_sdk.sh --url https://.../sdk.tar.gz # 从 URL 下载安装
  ./scripts/fetch_sdk.sh --check                      # 仅检查安装是否完整

详细说明见 README.md 的「平台 SDK」一节
EOF
  exit "${1:-0}"
}

# 校验某个目录是否符合 SDK 布局
check_layout() {
  local root="$1" missing=0 rel
  for rel in "${REQUIRED_FILES[@]}"; do
    if [ ! -e "${root}/${rel}" ]; then
      warn "缺少 ${root}/${rel}"
      missing=1
    fi
  done
  return "${missing}"
}

check_installed() {
  if [ ! -d "${SDK_DIR}" ]; then
    die "./sdk/ 不存在。请先运行本脚本安装 SDK，用法见 README.md 的「平台 SDK」一节。"
  fi
  if check_layout "${SDK_DIR}"; then
    info "SDK 安装完整：${SDK_DIR}"
    return 0
  fi
  die "SDK 目录不完整，请重新安装（见 README.md 的「平台 SDK」）。"
}

# 逐个必需子目录建立符号链接，保留 sdk/README.md 等仓库内文件
link_from_dir() {
  local src="$1" sub
  [ -d "${src}" ] || die "源目录不存在：${src}"
  check_layout "${src}" || die "源目录 ${src} 不是完整的 SDK 布局（见 README.md 的「平台 SDK」）。"

  mkdir -p "${SDK_DIR}"
  for sub in "${REQUIRED_SUBDIRS[@]}"; do
    local target="${SDK_DIR}/${sub}"
    if [ -e "${target}" ] || [ -L "${target}" ]; then
      warn "${target} 已存在，跳过（如需替换请先手动删除）"
      continue
    fi
    ln -s "$(cd "${src}/${sub}" && pwd)" "${target}"
    info "已链接 ${sub} -> ${src}/${sub}"
  done
  check_installed
}

# 把解压出的目录树铺平到 sdk/
install_tree() {
  local tree="$1" sub
  # 若压缩包只有单一顶层目录（如 sdk/ 或 sdk-xxx/），自动下钻一层
  if [ ! -d "${tree}/tdl_sdk" ]; then
    local inner
    inner="$(find "${tree}" -maxdepth 2 -type d -name tdl_sdk | head -n1)"
    [ -n "${inner}" ] || die "压缩包内找不到 tdl_sdk/，布局见 README.md 的「平台 SDK」。"
    tree="$(dirname "${inner}")"
  fi
  check_layout "${tree}" || die "压缩包内 SDK 布局不完整（见 README.md 的「平台 SDK」）。"

  mkdir -p "${SDK_DIR}"
  for sub in "${REQUIRED_SUBDIRS[@]}"; do
    local target="${SDK_DIR}/${sub}"
    if [ -e "${target}" ] || [ -L "${target}" ]; then
      warn "${target} 已存在，跳过（如需替换请先手动删除）"
      continue
    fi
    mv "${tree}/${sub}" "${target}"
    info "已安装 ${sub}"
  done
  check_installed
}

extract_tarball() {
  local archive="$1"
  [ -f "${archive}" ] || die "压缩包不存在：${archive}"
  make_tmp
  info "解压 ${archive} ..."
  tar -xf "${archive}" -C "${TMP_DIR}" || die "解压失败：${archive}"
  install_tree "${TMP_DIR}"
}

download_and_extract() {
  local url="$1"
  command -v curl >/dev/null 2>&1 || die "未找到 curl，无法下载。可先手动下载再用 --tarball。"
  make_tmp
  info "下载 ${url} ..."
  curl -fL --retry 3 -o "${TMP_DIR}/sdk.tar.gz" "${url}" || die "下载失败：${url}"
  extract_tarball "${TMP_DIR}/sdk.tar.gz"
}

main() {
  [ $# -gt 0 ] || usage 1
  case "$1" in
    --check)          check_installed ;;
    --from-dir)       [ $# -ge 2 ] || die "--from-dir 需要目录参数"; link_from_dir "$2" ;;
    --tarball)        [ $# -ge 2 ] || die "--tarball 需要文件参数"; extract_tarball "$2" ;;
    --url)            [ $# -ge 2 ] || die "--url 需要链接参数"; download_and_extract "$2" ;;
    -h|--help)        usage 0 ;;
    *)                die "未知参数：$1（--help 查看用法）" ;;
  esac
}

main "$@"
