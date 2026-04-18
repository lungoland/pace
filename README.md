# pace

Minimal cross-platform PC remote-control agent for Home Assistant.

`pace` connects to MQTT, listens for command messages, and executes a small allowlist of machine actions.

Current docs below describe the behavior implemented in this repository today.


## Supported actions

- `stop` (stops the agent)
- `lock`
- `sleep`
- `reboot`
- `shutdown`

## Supported sensors

> Not Implemented yet

- `proc` (Check if a process is running)
- `focus` (Returns the active window - if applicable?)

Following sensors may not be applicable for windows/requires kernel access.
Also see [CVE-2020-14979
](https://www.cvedetails.com/cve/CVE-2020-14979/)
- `cpu` with temperature
- `mem`
- `gpu` with temperature

## MQTT interface

All topics are prefixed with `pace/{node_id}/`, where `node_id` is configured via `PACE_MQTT_NODE_ID`.

| Direction | Topic | Purpose |
|-----------|-------|---------|
| publish | `pace/{node_id}/availability` | `online` / `offline` (retained, LWT) |
| subscribe | `pace/{node_id}/command/{name}/set` | Trigger a command |
| publish | `pace/{node_id}/command/{name}/status` | Command result |
| __TODO:__ subscribe | `pace/{node_id}/sensor/{name}/set` | Configures a sensor of given name |
| publish | `pace/{node_id}/sensor/{name}/state` | Periodic sensor reading |

Command payloads - if applicable - and sensor state values are JSON. The availability topic uses a plain string (`online` or `offline`).

Eventually a Home-Assistant discovery payload will be implemented.

## Build

### Install dependencies

The CMake presets expect `cmake`, `ninja`, a C++ toolchain, and a `vcpkg` checkout exposed through `VCPKG_ROOT`.

For native Linux builds on Ubuntu/Debian:

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build pkg-config git curl zip unzip tar
git clone https://github.com/microsoft/vcpkg.git /opt/vcpkg
/opt/vcpkg/bootstrap-vcpkg.sh
export VCPKG_ROOT=/opt/vcpkg
```

For Windows cross-compilation from Linux, install the MinGW toolchain as well:

```bash
sudo apt-get update
sudo apt-get install -y gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64
export VCPKG_ROOT=/opt/vcpkg
```

Project libraries are resolved from `vcpkg.json` during `cmake --preset ...`, so no separate package install step is required after this host setup.

### In devcontainer (recommended)

1. Open folder in VS Code.
2. Reopen in Dev Container.
3. Build:

```bash
cmake --preset pace-linux-debug
cmake --build --preset pace-linux-debug
ctest --preset pace-linux-debug
```

### Local build (Linux)

Set `VCPKG_ROOT` to your vcpkg checkout, then:

```bash
cmake --preset pace-linux
cmake --build --preset pace-linux
cmake --install build/pace-linux --prefix dist/pace-linux
```

The install tree is written to `dist/pace-linux/`.

### Portable Linux build

If the target machine has an older `libstdc++`, use:

```bash
cmake --preset pace-linux-static
cmake --build --preset pace-linux-static
cmake --install build/pace-linux-static --prefix dist/pace-linux-static
```

This statically links `libstdc++` and `libgcc` into `pace`.

### Windows cross-compile with static dependency linkage

```bash
cmake --preset pace-windows-static
cmake --build --preset pace-windows-static
cmake --install build/pace-windows-static --prefix dist/pace-windows-static
```

The install tree is written to `dist/pace-windows-static/`. For Windows cross-builds, the install step stages `pace.exe` together with the required vcpkg and MinGW runtime DLLs.

## Runtime configuration

Configuration can be provided either via process environment variables or a local `.env` file in the project working directory. If both are present, process environment variables win.

Environment variables:

- `PACE_MQTT_BROKER` (default in code: `tcp://localhost:1883`)
- `PACE_MQTT_CLIENT_ID` (default: `pace-agent`)
- `PACE_MQTT_NODE_ID` (default in code: `pace-node`)
- `PACE_MQTT_USERNAME` (default: empty)
- `PACE_MQTT_PASSWORD` (default: empty)
- `PACE_MQTT_QOS` (default: `1`)

Example `.env`:

```dotenv
PACE_MQTT_BROKER=tcp://mqtt.local:1883
PACE_MQTT_CLIENT_ID=pace-agent
PACE_MQTT_NODE_ID=workstation-01
PACE_MQTT_USERNAME=your-user
PACE_MQTT_PASSWORD=your-password
PACE_MQTT_QOS=1
```


## Security notes

- This service can run privileged system commands.
- Use broker authentication and TLS in production.
- Restrict network access to the MQTT broker.
