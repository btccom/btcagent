# BTCAgent

[English Version/英文版本](README.md)

BTCAgent是定制的高效的专用矿池代理系统。其采用了自定义[通信协议](https://github.com/btccom/btcpool/blob/master/docs/AGENT.md)，专为了矿场解决带宽问题而设计。

由于Stratum协议采用了TCP长连接模式，一旦有新任务下发，则导致数千连接同时收到数据，此时会导致矿场带宽拥堵不堪，BTCAgent会极大优化下行带宽，减少至数KB，消除新任务下发延时。

同时，解决了传统代理在矿池侧只能看到一个矿机的问题，BTCAgent依然可以在矿池侧看到所有矿机，极大的方便监控与管理。

1万台矿机的性能测试：

* 出口带宽: 上行低于 150kbps，下行低于50kbps。
  * 首次启动时会做矿机难度适配，瞬时带宽会超过上述值
* 内存: 小于 64M
* CPU负载: 低于0.05（单核）

支持的操作系统：
* Windows
* Linux
* 其他 [golang](https://go.dev/) 编程语言支持的系统

提示：

* BTCAgent协议目前仍未定型，以后可能会改变
* 当前仅能与`btcpool`协同工作

## 架构

![架构图](docs/architecture.png)

## 编译安装

1. 从 https://go.dev/ 安装 golang

2. 从 https://git-scm.com/ 安装 git

3. 运行以下命令:
   ```bash
   git clone https://github.com/btccom/btcagent.git
   cd btcagent
   go build
   ```

4. 然后就能得到可执行文件`btcagent`（Windows中为`btcagent.exe`）。

## 下载

如果不想自行编译安装，也可以去这里下载编译好的可执行文件：

https://github.com/btccom/btcagent/releases

下载 `agent_conf.default.json`（配置文件模板）和适用于你系统的`btcagent-xxx-xxx`可执行文件，然后给`btcagent-xxx-xxx`执行权限（Linux/macOS需要）并重命名为`btcagent`。

给执行权限和重命名示例：
```bash
chmod +x btcagent-linux-x64
mv btcagent-linux-x64 btcagent
```

我该下载哪个可执行文件？
* 32位Windows系统：`btcagent-windows-x86.exe`
* 64位Windows系统：`btcagent-windows-x64.exe`
* 32位Linux系统：`btcagent-linux-x86`
* 64位Linux系统：`btcagent-linux-x64`
* 运行32位系统的树莓派：`btcagent-linux-arm`
* 运行64位系统的树莓派：`btcagent-linux-arm64`
* 英特尔CPU的macOS：`btcagent-macos-x64`
* M1芯片的macOS：`btcagent-macos-arm64`

## 运行

```bash
# 从模板创建配置文件
cp agent_conf.default.json agent_conf.json

# 然后按你的需要修改`agent_conf.json`文件

# 创建日志文件夹
mkdir log

# 启动BTCAgent
./btcagent -c agent_conf.json -l log -alsologtostderr
```

欲了解配置文件[agent_conf.json](agent_conf.default.json)中每个选项的作用，请看：[配置文件详情](docs/ConfigFileDetails-zhCN.md)。

## 作为 systemd 服务运行（Linux 开机自启动）

**仅适用于运行 systemd 的 Linux 发行版**

```bash
# 创建 systemd 服务文件
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

# 启动服务
sudo systemctl start btcagent

# 设置开机自启动
sudo systemctl enable btcagent

# 检查服务状态
sudo systemctl status btcagent

# 查看服务启动记录
sudo journalctl -u btcagent

# 查看日志
less log/*INFO
```

如果不再使用btcagent服务，可以这样删除：

```bash
# 停止服务
sudo systemctl stop btcagent

# 禁止开机自启动
sudo systemctl disable btcagent

# 删除服务
sudo rm /etc/systemd/system/btcagent.service
```
