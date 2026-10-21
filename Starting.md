## Steps: from plugging in the board to flashing the app

**1. Connect the board.** Plug the Nucleo's ST-LINK USB port into your PC (the one on the debugger side, not the user USB port). Use a data cable, not a charge-only one.

**2. Start OpenOCD on Windows.** Open PowerShell, not WSL or the container:
```powershell
openocd -f board/st_nucleo_f4.cfg -c "bindto 0.0.0.0" -c "reset_config srst_only srst_nogate connect_assert_srst"
```
Check for `Cortex-M4 ... processor detected` and `Listening on port 3333 for gdb connections`. Leave the window open for the whole session.

**3. Open the project in the container.** In WSL run `code .` in the RTS folder, then use "Reopen in Container" if VS Code doesn't do it for you. Open a terminal in VS Code.

**4. Build the app.** Run this from `/workspaces/RTS`:
```bash
west build -p always -b nucleo_f439zi app -d build
```
`-p always` does a clean rebuild, which also replaces the earlier blinky build in `build/`. The build should end with a memory usage table and no errors.

**5. Flash it.** If `OPENOCD_REMOTE_HOST` is already in your `containerEnv` and you rebuilt the container, just run:
```bash
west flash
```
Otherwise, use the variable inline:
```bash
OPENOCD_REMOTE_HOST=<IP> west flash
```

## Troubleshooting

**Check the toolchain (no board needed):**
```bash
west --version
west topdir                      # expect /workdir
echo $ZEPHYR_BASE                # /workdir/zephyr
west config build.cmake-args     # should show the OPENOCD / openocd-runner flags
ls /workdir/zephyr-sdk-*/gnu/arm-zephyr-eabi/bin | head
```

**Build the stock blinky sample:**
```bash
west build -p always -b nucleo_f439zi $ZEPHYR_BASE/samples/basic/blinky -d build
```
If this ends with a memory usage table and `build/zephyr/zephyr.elf` exists, Zephyr, the SDK and the toolchain all work. If west can't find the workspace, run `cd /workdir` first.

## Errors

**`Kein OpenOCD auf dem Host erreichbar`:** OpenOCD isn't running on Windows, or the Windows gateway IP changed. Check `ip route show default` in WSL.