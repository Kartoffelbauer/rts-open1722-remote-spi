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

OpenOCD isn't running on Windows, or the Windows gateway IP changed.

- Make sure OpenOCD is still running (see [step 2](getting-started.md#2-start-openocd-on-windows)).
- Check the current gateway IP with `ip route show default` in WSL and update `OPENOCD_REMOTE_HOST` if needed.