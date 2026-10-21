#!/usr/bin/env bash
# Richtet den Zephyr-Workspace im Volume /workdir ein.
# Idempotent: Beim ersten Start dauert es (Download), danach nur Sekunden.
set -euo pipefail

WORKDIR=/workdir
VENV="$WORKDIR/.venv"
ZEPHYR_REVISION="${ZEPHYR_REVISION:-main}"
SDK_TOOLCHAINS="${SDK_TOOLCHAINS:-}"

# Volume gehört evtl. root, falls es anders angelegt wurde
sudo chown "$(id -u):$(id -g)" "$WORKDIR"

# 1) Python venv + west
if [ ! -x "$VENV/bin/python" ]; then
  echo ">>> Erstelle Python venv"
  python3 -m venv "$VENV"
fi
# shellcheck disable=SC1091
source "$VENV/bin/activate"
pip install --quiet --upgrade pip west

# 2) Zephyr holen
if [ ! -d "$WORKDIR/.west" ]; then
  echo ">>> west init ($ZEPHYR_REVISION)"
  west init -m https://github.com/zephyrproject-rtos/zephyr --mr "$ZEPHYR_REVISION" "$WORKDIR"
fi
cd "$WORKDIR"
echo ">>> west update"
west update

# 3) Python-Abhängigkeiten (Fallback für ältere Zephyr-Versionen ohne 'west packages')
echo ">>> Python-Abhängigkeiten"
west packages pip --install || pip install -r "$WORKDIR/zephyr/scripts/requirements.txt"

# West-Builds nutzen OpenOCD auf dem Host (siehe openocd-remote) als Flash-Runner
west config build.cmake-args -- "-DOPENOCD=/usr/local/bin/openocd-remote -DBOARD_FLASH_RUNNER=openocd"

# 4) CMake-Package registrieren (liegt in ~/.cmake, muss nach jedem Rebuild neu)
west zephyr-export

# 5) Zephyr SDK ins Volume installieren bzw. neu registrieren
SDK_DIR="$(compgen -G "$WORKDIR/zephyr-sdk-*" | head -n1 || true)"
if [ -z "$SDK_DIR" ]; then
  echo ">>> Installiere Zephyr SDK"
  args=(-b "$WORKDIR")
  if [ -n "$SDK_TOOLCHAINS" ]; then
    # shellcheck disable=SC2206
    args+=(-t $SDK_TOOLCHAINS)
  fi
  (cd "$WORKDIR/zephyr" && west sdk install "${args[@]}")
else
  echo ">>> Registriere vorhandenes SDK: $SDK_DIR"
  "$SDK_DIR/setup.sh" -c
fi

echo ">>> Fertig. Test: west build -p always -b nrf52840dk/nrf52840 \$ZEPHYR_BASE/samples/basic/blinky -d build"