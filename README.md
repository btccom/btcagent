# BtcAgent

[Chinese Version/中文版本](https://github.com/btccom/btcagent/blob/master/README-zh_CN.md)

BtcAgent is a kind of stratum proxy which use [customize protocol](https://github.com/btccom/btcpool/blob/master/docs/AGENT.md) to communicate with the pool. It's very efficient and designed for huge mining farm. And you still can find each miner at the pool.

* With 10,000 miners:
  * Bandwith: less than 150kbps
  * Memory: less than 64MBytes
  * CPU: 1 Core

It's so efficient and we are going to build it at a ARM platform like Open-WRT. So people could use WiFi-Route as the stratum proxy, it's very cool and easy to manange.


Note:

* This is still a testbed and work in progress, all things could be changed.
* Now it's could only work with `btcpool`.

## Architecture

![Architecture](docs/architecture.png)

## Install

* OS: `Ubuntu 14.04 LTS, 64 Bits`

```
apt-get update
apt-get install -y build-essential cmake git
apt-get install -y libconfig++-dev

#
# install libevent
#
mkdir -p /root/source && cd /root/source
wget https://github.com/libevent/libevent/releases/download/release-2.0.22-stable/libevent-2.0.22-stable.tar.gz
tar zxvf libevent-2.0.22-stable.tar.gz
cd libevent-2.0.22-stable
./configure
make
make install

#
# install glog
#
mkdir -p /root/source && cd /root/source
wget https://github.com/google/glog/archive/v0.3.4.tar.gz
tar zxvf v0.3.4.tar.gz
cd glog-0.3.4
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
    "pools": [
        ["us.ss.btc.com", 1800, "kevin"],
        ["us.ss.btc.com", 1800, "kevin"]
    ]
}
```

* `agent_listen_ip`: Agent's listen IP address.
* `agent_listen_port`: Agent's listen port, miners will connect to this port.
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
    "pools": [
        ["us.ss.btc.com", 1800, "kevin3334"]
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
