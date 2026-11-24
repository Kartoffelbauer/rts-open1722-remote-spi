#!/usr/bin/env bash
# Sets up the Zephyr workspace in the /workdir volume.
# Idempotent: the first start takes a while (downloads), later starts only seconds.
set -euo pipefail

WORKDIR=/workdir
VENV="$WORKDIR/.venv"
ZEPHYR_REVISION="${ZEPHYR_REVISION:-main}"
SDK_TOOLCHAINS="${SDK_TOOLCHAINS:-}"

# The volume may belong to root if it was created some other way
sudo chown "$(id -u):$(id -g)" "$WORKDIR"

# 1) Python venv + west
if [ ! -x "$VENV/bin/python" ]; then
  echo ">>> Creating Python venv"
  python3 -m venv "$VENV"
fi
# shellcheck disable=SC1091
source "$VENV/bin/activate"
pip install --quiet --upgrade pip west

# 2) Fetch Zephyr
if [ ! -d "$WORKDIR/.west" ]; then
  echo ">>> west init ($ZEPHYR_REVISION)"
  west init -m https://github.com/zephyrproject-rtos/zephyr --mr "$ZEPHYR_REVISION" "$WORKDIR"
fi
cd "$WORKDIR"
echo ">>> west update"
west update

# 3) Python dependencies (fallback for older Zephyr versions without 'west packages')
echo ">>> Python dependencies"
west packages pip --install || pip install -r "$WORKDIR/zephyr/scripts/requirements.txt"

# West builds use OpenOCD on the host (see openocd-remote) as the flash runner
west config build.cmake-args -- "-DOPENOCD=/usr/local/bin/openocd-remote -DBOARD_FLASH_RUNNER=openocd"

# 4) Register the CMake package (lives in ~/.cmake, so it's needed after every rebuild)
west zephyr-export

# 5) Install the Zephyr SDK into the volume, or register the existing one again
SDK_DIR="$(compgen -G "$WORKDIR/zephyr-sdk-*" | head -n1 || true)"
if [ -z "$SDK_DIR" ]; then
  echo ">>> Installing Zephyr SDK"
  args=(-b "$WORKDIR")
  if [ -n "$SDK_TOOLCHAINS" ]; then
    # shellcheck disable=SC2206
    args+=(-t $SDK_TOOLCHAINS)
  fi
  (cd "$WORKDIR/zephyr" && west sdk install "${args[@]}")
else
  echo ">>> Registering existing SDK: $SDK_DIR"
  "$SDK_DIR/setup.sh" -c
fi

echo ">>> Done. Test: west build -p always -b nucleo_f439zi \$ZEPHYR_BASE/samples/basic/blinky -d build"
