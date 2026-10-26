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

Each board gets its own role, and each role has its own build directory:

Requester
```bash
west build -p always -b nucleo_f439zi app -d build/requester -- -DROLE=requester
```

Responder
```bash
west build -p always -b nucleo_f439zi app -d build/responder -- -DROLE=responder
```

`-p always` does a clean rebuild. Each build should end with a memory usage table and no errors. If you leave out `-DROLE`, the build stops and tells you to set it.

## 5. Flash it

Plug in the board you want to flash, so that OpenOCD sees it, then flash the matching build:

Requester
```bash
west flash -d build/requester
```

Responder
```bash
west flash -d build/responder
```

If `OPENOCD_REMOTE_HOST` isn't in your `containerEnv`, set it inline: `OPENOCD_REMOTE_HOST=<IP> west flash -d build/requester`.

## Next steps

- Understand the setup: [Architecture](architecture.md)
- See what is planned: [Project Plan](project-plan.md)