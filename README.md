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
