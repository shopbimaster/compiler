#!/bin/bash
# 临时脚本：健壮地下载并编译安装 ANTLR4 C++ runtime（实验用，可删）
# 多镜像回退 + 宿主代理探测 + zip 完整性校验
V=4.13.1
WORK=/tmp/antlrbuild
rm -rf "$WORK"; mkdir -p "$WORK"; cd "$WORK" || exit 1

HOSTIP=$(ip route show default | awk '/default/ {print $3}')
echo "宿主IP: ${HOSTIP:-未知}"
PROXY=""
if [ -n "$HOSTIP" ]; then
  # 探测宿主代理端口（Clash 常见 7897 / 7890）
  for port in 7897 7890 10809 1080; do
    if timeout 3 bash -c "echo > /dev/tcp/$HOSTIP/$port" 2>/dev/null; then
      PROXY="http://$HOSTIP:$port"; echo "探测到宿主代理: $PROXY"; break
    fi
  done
fi

# 候选下载源（官方 + GitHub tag）
URLS=(
  "https://www.antlr.org/download/antlr4-cpp-runtime-${V}-source.zip"
  "https://github.com/antlr/antlr4/archive/refs/tags/${V}.zip"
)

download() {
  local url="$1" out="$2"
  echo "  尝试直连: $url"
  if timeout 120 wget -q -O "$out" "$url" && unzip -t "$out" >/dev/null 2>&1; then return 0; fi
  if [ -n "$PROXY" ]; then
    echo "  直连失败，尝试经代理: $url"
    if https_proxy="$PROXY" http_proxy="$PROXY" timeout 120 wget -q -O "$out" "$url" \
        && unzip -t "$out" >/dev/null 2>&1; then return 0; fi
  fi
  return 1
}

OK=0
for u in "${URLS[@]}"; do
  if download "$u" "$WORK/rt.zip"; then
    echo "下载成功: $(stat -c %s "$WORK/rt.zip") bytes，来自 $u"; OK=1; break
  fi
done
[ "$OK" = 1 ] || { echo "ANTLR_RT_EXIT=DOWNLOAD_FAILED"; exit 1; }

echo "=== 解压 ==="
cd "$WORK" && unzip -q rt.zip
# 官方 zip 顶层含 runtime/；GitHub tag zip 顶层是 antlr4-4.13.1/runtime/Cpp
if [ -d "$WORK/runtime" ]; then
  SRC="$WORK/runtime/Cpp"; [ -d "$SRC" ] || SRC="$WORK"
elif [ -d "$WORK/antlr4-${V}/runtime/Cpp" ]; then
  SRC="$WORK/antlr4-${V}/runtime/Cpp"
else
  # 官方源码包解压后顶层直接是 CMakeLists.txt + runtime/
  SRC="$WORK"
fi
echo "源目录: $SRC"
ls "$SRC/CMakeLists.txt" 2>/dev/null || { echo "找不到 CMakeLists.txt"; echo "ANTLR_RT_EXIT=NO_CMAKE"; exit 1; }

echo "=== cmake 配置 ==="
cmake -S "$SRC" -B "$WORK/bld" -DCMAKE_BUILD_TYPE=Release \
  -DANTLR_BUILD_CPP_TESTS=OFF -DANTLR4_INSTALL=ON || { echo "ANTLR_RT_EXIT=CMAKE_CONFIG_FAIL"; exit 1; }
echo "=== 编译（并行 $(nproc)）==="
cmake --build "$WORK/bld" --parallel "$(nproc)" || { echo "ANTLR_RT_EXIT=BUILD_FAIL"; exit 1; }
echo "=== 安装 ==="
sudo cmake --install "$WORK/bld" && sudo ldconfig
echo "ANTLR_RT_EXIT=0"
