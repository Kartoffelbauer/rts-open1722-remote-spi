# Getting Started

Steps from plugging in the board to flashing the app.

The build runs in a dev container on both Windows (WSL) and Linux. OpenOCD always runs on the host, where the board is plugged in. Where the steps differ, there is a **Windows** and a **Linux** variant.

If something goes wrong, see [Troubleshooting](troubleshooting.md). For the hardware you need, see [Hardware](hardware.md).

## 1. Connect the board

Plug the Nucleo's ST-LINK USB port into your PC (the one on the debugger side, not the user USB port). Use a data cable, not a charge-only one.

## 2. Start OpenOCD on the host

### Windows

Install OpenOCD with winget (`winget install xpack-dev-tools.openocd-xpack`), then open a new PowerShell window so `openocd` is on the PATH. If OpenOCD doesn't find the ST-LINK, install ST's ST-LINK USB driver ([STSW-LINK009](https://www.st.com/en/development-tools/stsw-link009.html)) and replug the board.

Open PowerShell (not WSL or the container):

```powershell
openocd -f board/st_nucleo_f4.cfg -c "bindto 0.0.0.0" -c "reset_config srst_only srst_nogate connect_assert_srst"
```

If Windows Firewall asks, allow OpenOCD on private networks so the container can reach it.

### Linux

Install OpenOCD with your package manager (`sudo dnf install openocd` or `sudo apt install openocd`). The package also installs the udev rules that let you use the ST-LINK without `sudo`.

Make sure Docker is running (see [step 3](#3-open-the-project-in-the-container)), then open a normal terminal (not the container):

```bash
openocd -f board/st_nucleo_f4.cfg -c "bindto 172.17.0.1" -c "reset_config srst_only srst_nogate connect_assert_srst"
```

`172.17.0.1` is Docker's bridge address (`docker0`). Binding to it lets the container reach OpenOCD without exposing it to your network.

### Both

Check for these two lines in the output:

- `Cortex-M4 ... processor detected`
- `Listening on port 3333 for gdb connections`

Leave the window open for the whole session.

## 3. Open the project in the container

### Windows

You need [WSL 2](https://learn.microsoft.com/windows/wsl/install) with Ubuntu (`wsl --install -d Ubuntu`), [Docker Desktop](https://docs.docker.com/desktop/setup/install/windows-install/), and the VS Code extensions **WSL** and **Dev Containers**.

In Docker Desktop, open **Settings → Resources → WSL Integration**, turn on **Ubuntu**, and click **Apply & restart**. Check it in the Ubuntu terminal with `docker run --rm hello-world`. Keep Docker Desktop running while you work.

Clone the repo inside WSL (for example to `~/RTS`), not on the Windows drive. The container setup has to be started from WSL, and a Windows checkout has CRLF line endings that break flashing.

In WSL run `code .` in the RTS folder, then use **Reopen in Container** if VS Code doesn't do it for you. Open a terminal in VS Code.

### Linux

You need [Docker Engine](https://docs.docker.com/engine/install/) with your user in the `docker` group (`sudo usermod -aG docker $USER`, then log out and back in).

Start the Docker daemon:

```bash
sudo systemctl start docker
```

To also start it at every boot, use this instead:

```bash
sudo systemctl enable --now docker
```

With SELinux enforcing (Fedora, RHEL; check with `getenforce`), the container can't read the mounted repo folder. Give the folder the container label once, from the repo folder:

```bash
chcon -R -t container_file_t .
```

To undo it, restore the default labels with `restorecon -R -v .`. A full SELinux relabel also resets the label, so after one you need to run `chcon` again.

Run `code .` in the repo folder, then use **Reopen in Container** if VS Code doesn't do it for you. Open a terminal in VS Code.

### Both

If you cloned without `--recurse-submodules`, fetch Open1722 once:

```bash
git submodule update --init
```

## 4. Build the app

Run this from the workspace folder in the container, `/workspaces/<repo folder>` (for example `/workspaces/RTS`):

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

`west flash` finds OpenOCD on the host by itself, so there is nothing to configure:

| Host | Address used |
|---|---|
| Windows (WSL) | The Windows host IP, read from `ip route show default` in WSL each time the container starts |
| Linux | `host.docker.internal`, which points to Docker's bridge address `172.17.0.1` |

To force a specific address, set it inline: `OPENOCD_REMOTE_HOST=<IP> west flash -d build/requester`.

## Run the core tests

The GBB framing and parsing in `app/src/common/gbb.h` has a ztest suite in `app/tests/common`. It runs on `native_sim`, so no board is needed. Run it from the workspace folder in the container:

```bash
west build -p always -b native_sim/native/64 app/tests/common -d build/tests -t run
```

The output ends with a summary line per suite and `PROJECT EXECUTION SUCCESSFUL` or `PROJECT EXECUTION FAILED`.

To run it through twister instead (results go to `twister-out/`):

```bash
west twister -T app/tests -p native_sim/native/64
```

The 32-bit `-p native_sim` works too, but only on amd64 hosts, because it needs `gcc-multilib`.

## Next steps

- Understand the setup: [Architecture](architecture.md)
- See what is planned: [Project Plan](project-plan.md)
