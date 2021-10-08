# BtcAgent

[Chinese Version/中文版本](README-zh_CN.md)

BtcAgent is a kind of stratum proxy which use [customize protocol](https://github.com/btccom/btcpool/blob/master/docs/AGENT.md) to communicate with the pool. It's very efficient and designed for huge mining farm. And you still can find each miner at the pool.

* With 10,000 miners:
  * Bandwith: less than 150kbps
  * Memory: less than 64MBytes
  * CPU: 1 Core

Support Platform:

* Linux / Unix system
* [Embedded System](README-EmbeddedSystem.md) like open-wrt / dd-wrt / PandoraBox
* [Windows](README-Windows.md) XP or later (testing)

Note:

* This is still a testbed and work in progress, all things could be changed.
* Now it's could only work with `btcpool`.

## Architecture

![Architecture](docs/architecture.png)

## Install

* OS: `Ubuntu 14.04 LTS, 64 Bits`
  * if you want build on Embedded System like open-wrt/dd-wrt/PandoraBox, please see: [build for Embedded System](README-EmbeddedSystem.md)

```
apt-get update
apt-get install -y build-essential cmake git

#
# install libevent
#
mkdir -p /root/source && cd /root/source
wget https://github.com/libevent/libevent/releases/download/release-2.1.9-beta/libevent-2.1.9-beta.tar.gz
tar zxvf libevent-2.1.9-beta.tar.gz
cd libevent-2.1.9-beta
./configure
make
make install

#
# install glog
#
mkdir -p /root/source && cd /root/source
wget https://github.com/google/glog/archive/v0.3.5.tar.gz
tar zxvf v0.3.5.tar.gz
cd glog-0.3.5
./configure && make && make install

#
# build agent
#
mkdir -p /work && cd /work
git clone https://github.com/btccom/btcagent.git
cd btcagent
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
cp ../src/agent/agent_conf.json .
mkdir -p log_btcagent
```

**config json file example**

```
{
    "agent_listen_ip": "0.0.0.0",
    "agent_listen_port": 3333,
    "pool_use_tls": false,
    "pools": [
        ["us.ss.btc.com", 1800, "testus"],
        ["us.ss.btc.com", 443, "testus"]
    ]
}
```

* `agent_listen_ip`: Agent's listen IP address.

* `agent_listen_port`: Agent's listen port, miners will connect to this port.

* `pool_use_tls`: If you want to connect to a pool server via SSL/TLS encryption, you can change `false` to `true`.

   Note: If this option is `true` and a pool server does not support SSL/TLS, you cannot connect to it.

* `pools`: pools settings which Agent will connect. You can put serval pool's settings here.

  * `["<stratum_server_host>", <stratum_server_port>, "<pool_username>"]`

**start / stop**

```
cd /work/btcagent/build
#
# start
#
./agent -c agent_conf.json -l log_btcagent

#
# stop: `kill` the process pid or use `Control+C`
#
kill `pgrep 'agent'`
```

**recommand to use `supervisor` to manage it**

```
$ apt-get install -y supervisor
$ cp /work/btcagent/install/agent.conf /etc/supervisor/conf.d/
$ supervisorctl
supervisor> reread
agent: available
supervisor> update
agent: added process group
supervisor> status
agent                            RUNNING    pid 21296, uptime 0:00:09
supervisor> exit
```

**listen on multi port**

One agent only could listen to one port, if need to listen more than one port you need setup multi agent.

```
cd /work/btcagent/build

# mkdir log dir
mkdir log_btcagent_3334

# copy conf json for another port: 3334
cp agent_conf.json agent_conf_3334.json
```

`agent_conf_3334.json` looks like:

```
{
    "agent_listen_ip": "0.0.0.0",
    "agent_listen_port": 3334,
    "pool_use_tls": false,
    "pools": [
        ["us.ss.btc.com", 1800, "testus3334"]
    ]
}
```

```
# start
cd /work/btcagent/build
./agent -c agent_conf_3334.json -l log_btcagent_3334
```

if you use `supervisor`, you need another conf:

`vim /etc/supervisor/conf.d/agent3334.conf`

```
[program:agent3334]
directory=/work/btcagent/build
command=/work/btcagent/build/agent -c /work/btcagent/build/agent_conf_3334.json -l /work/btcagent/build/log_btcagent_3334
autostart=true
autorestart=true
startsecs=3
startretries=100

redirect_stderr=true
stdout_logfile_backups=5
stdout_logfile=/work/btcagent/build/log_btcagent_3334/agent_stdout.log
```

`vim /etc/supervisor/supervisord.conf` change or insert `minfds` in section `[supervisord]`:

```
[supervisord]
minfds=65535
```

restart supervisor: `service supervisor restart`.

than update supervisor:

```
$ supervisorctl
supervisor> reread
...
supervisor> update
...
supervisor> status
...
supervisor> exit
```

---

If you get `Too many open files` error, it means you need to increase the system file limits. Usually the default value is 1024 so you can't connect more than 1024 miners at one agent.

if you are using Ubuntu, `vim /etc/security/limits.conf`, add these lines:

```
root soft nofile 65535
root hard nofile 65535
* soft nofile 65535
* hard nofile 65535
```

You need to logout shell than login again. Check the value, should as below:

```
$ ulimit -Sn
65535
$ ulimit -Hn
65535
```
