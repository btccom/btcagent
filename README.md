# BTCAgent

## Build

1. Install golang from https://go.dev/

2. Install git from https://git-scm.com/

3. Run the following commands:
   ```
   git clone -b golang https://github.com/btccom/btcagent.git
   cd btcagent
   go build
   ```

4. You will get the executable file `btcagent`.

## Run

```
# Modify `agent_conf.json` according to your needs
cp agent_conf.default.json agent_conf.json

# log directory
mkdir log

./btcagent -c agent_conf.json -l log -alsologtostderr
```

## Use proxy

In [agent_conf.json](agent_conf.default.json):

* socks5 proxy
   ```
   "proxy": "socks5://127.0.0.1:1089",
   ```
* http proxy
   ```
   "proxy": "http://127.0.0.1:8089",
   ```
* https proxy (http proxy with SSL/TLS)
   ```
   "proxy": "http://127.0.0.1:4433",
   ```
* find proxy from system environment variables
   ```
   "proxy": "system",
   ```
* disable proxy
   ```
   "proxy": "",
   ```
