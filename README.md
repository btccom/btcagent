# BTCAgent

[Chinese Version/中文版本](README-zhCN.md)

BTCAgent is a kind of stratum proxy which use [customize protocol](https://github.com/btccom/btcpool/blob/master/docs/AGENT.md) to communicate with the pool. It's very efficient and designed for huge mining farm. And you still can find each miner at the pool.

* With 10,000 miners:
  * Bandwith: less than 150kbps
  * Memory: less than 64MBytes
  * CPU: 1 Core

Supported platforms:
* Windows
* Linux
* Other platforms supported by the programming language [golang](https://go.dev/)

Note:
* This is still a testbed and work in progress, all things could be changed.
* Now it could only work with `btcpool`.

## Architecture

![Architecture](docs/architecture.png)

## Build

1. Install golang from https://go.dev/

2. Install git from https://git-scm.com/

3. Run the following commands:
   ```bash
   git clone https://github.com/btccom/btcagent.git
   cd btcagent
   go build
   ```

4. You will get the executable file `btcagent` (or `btcagent.exe` on Windows).

## Download

If you don't want to build it yourself, you can also download the built binary here:

https://github.com/btccom/btcagent/releases

Download `agent_conf.default.json` (configuration file template) and `btcagent-xxx-xxx` binary suitable for your platform, give `btcagent-xxx-xxx` execution permission (Linux/macOS), and rename it to `btcagent`.

Example of granting execution permissions and renaming:
```bash
chmod +x btcagent-linux-x64
mv btcagent-linux-x64 btcagent
```

Which binary should I download?
* 32-bit Windows system: `btcagent-windows-x86.exe`
* 64-bit Windows system: `btcagent-windows-x64.exe`
* 32-bit Linux system: `btcagent-linux-x86`
* 64-bit Linux system: `btcagent-linux-x64`
* Raspberry Pi running a 32-bit system: `btcagent-linux-arm`
* Raspberry Pi running a 64-bit system: `btcagent-linux-arm64`
* Mac with Intel CPU: `btcagent-macos-x64`
* Mac with M1 chip: `btcagent-macos-arm64`

## Run

```bash
# Create a config file from the template
cp agent_conf.default.json agent_conf.json

# Then modify `agent_conf.json` according to your needs

# Create a log directory
mkdir log

# Launch BTCAgent
./btcagent -c agent_conf.json -l log -alsologtostderr
```

See [ConfigFileDetails.md](docs/ConfigFileDetails.md) for more details about [agent_conf.json](agent_conf.default.json).

## Run as a systemd service

**Only for Linux with systemd.**

```bash
# Create a systemd service file
cat << EOF | sudo tee /etc/systemd/system/btcagent.service >/dev/null
[Unit]
Description=BTCAgent
After=network.target
StartLimitIntervalSec=0

[Service]
Type=simple
Restart=always
RestartSec=1
User=root
ExecStart=$PWD/btcagent -c $PWD/agent_conf.json -l $PWD/log

[Install]
WantedBy=multi-user.target
EOF

# Launch service
sudo systemctl start btcagent

# Make the service start automatically after booting
sudo systemctl enable btcagent

# Check service status
sudo systemctl status btcagent

# View service startup records
sudo journalctl -u btcagent

# View log
less log/*INFO
```

If you no longer use btcagent service, you can delete it like this:

```bash
# Stop service
sudo systemctl stop btcagent

# Disable automatic startup
sudo systemctl disable btcagent

# Remove service
sudo rm /etc/systemd/system/btcagent.service
```
