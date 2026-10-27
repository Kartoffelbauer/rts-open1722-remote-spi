# Troubleshooting

Back to [Getting Started](getting-started.md).

## Check the toolchain (no board needed)

```bash
west --version
west topdir                      # expect /workdir
echo $ZEPHYR_BASE                # /workdir/zephyr
west config build.cmake-args     # should show the OPENOCD / openocd-runner flags
ls /workdir/zephyr-sdk-*/gnu/arm-zephyr-eabi/bin | head
```

## Build the stock blinky sample

```bash
west build -p always -b nucleo_f439zi $ZEPHYR_BASE/samples/basic/blinky -d build
```

If this ends with a memory usage table and `build/zephyr/zephyr.elf` exists, Zephyr, the SDK and the toolchain all work. If west can't find the workspace, run `cd /workdir` first.

## Common errors

### `Kein OpenOCD auf dem Host erreichbar`

The container can't reach OpenOCD on the host.

- Make sure OpenOCD is still running (see [step 2](getting-started.md#2-start-openocd-on-the-host)).
- The error lists the addresses that were tried. Check that one of them is where OpenOCD is listening.
- Windows: the Windows host IP is read when the container starts. If it changed since then (for example after a Windows restart), reopen the container. It is stored in `.devcontainer/.openocd-host`.
- Linux: OpenOCD must be started with `bindto 172.17.0.1`. Check the address with `ip -4 addr show docker0`.
- If you created the container before the automatic host detection was added, rebuild it (**Dev Containers: Rebuild Container**).
- To test a specific address, set it inline: `OPENOCD_REMOTE_HOST=<IP> west flash -d build/requester`.
- Linux with `ufw`: the firewall blocks the container from reaching the host. Allow it with `sudo ufw allow in on docker0 to any port 3333`.

### OpenOCD on Linux: `LIBUSB_ERROR_ACCESS` or `open failed`

Your user can't access the ST-LINK. Install the udev rules (they come with the `openocd` package; otherwise copy `contrib/60-openocd.rules` from OpenOCD to `/etc/udev/rules.d/`), run `sudo udevadm control --reload && sudo udevadm trigger`, and replug the board.

### OpenOCD on Linux: `couldn't bind ... to socket` / `Cannot assign requested address`

`docker0` doesn't exist yet, so OpenOCD can't bind to `172.17.0.1`. Start Docker first (`sudo systemctl start docker`), then OpenOCD.