# Getting Started

Steps from plugging in the board to flashing the app.

If something goes wrong, see [Troubleshooting](troubleshooting.md). For the hardware you need, see [Hardware](hardware.md).

## 1. Connect the board

Plug the Nucleo's ST-LINK USB port into your PC (the one on the debugger side, not the user USB port). Use a data cable, not a charge-only one.

## 2. Start OpenOCD on Windows

Open PowerShell (not WSL or the container):

```powershell
openocd -f board/st_nucleo_f4.cfg -c "bindto 0.0.0.0" -c "reset_config srst_only srst_nogate connect_assert_srst"
```

Check for these two lines in the output:

- `Cortex-M4 ... processor detected`
- `Listening on port 3333 for gdb connections`

Leave the window open for the whole session.

## 3. Open the project in the container

In WSL run `code .` in the RTS folder, then use **Reopen in Container** if VS Code doesn't do it for you. Open a terminal in VS Code.

If you cloned without `--recurse-submodules`, fetch Open1722 once:

```bash
git submodule update --init
```

## 4. Build the app

Run this from `/workspaces/RTS`:

```bash
west build -p always -b nucleo_f439zi app -d build
```

`-p always` does a clean rebuild, which also replaces the earlier blinky build in `build/`. The build should end with a memory usage table and no errors.

## 5. Flash it

If `OPENOCD_REMOTE_HOST` is already in your `containerEnv` and you rebuilt the container, just run:

```bash
west flash
```

Otherwise, set the variable inline:

```bash
OPENOCD_REMOTE_HOST=<IP> west flash
```

## Next steps

- Understand the setup: [Architecture](architecture.md)
- See what is planned: [Project Plan](project-plan.md)