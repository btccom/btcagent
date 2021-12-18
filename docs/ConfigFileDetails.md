# BTCAgent Config File Details

[Chinese Version/中文版本](ConfigFileDetails-zhCN.md)

## All Options

```json
{
    "multi_user_mode": true,
    "agent_type": "btc",
    "always_keep_downconn": false,
    "disconnect_when_lost_asicboost": true,
    "use_ip_as_worker_name": false,
    "ip_worker_name_format": "{1}x{2}x{3}x{4}",
    "fixed_worker_name": "",
    "submit_response_from_server": false,
    "agent_listen_ip": "0.0.0.0",
    "agent_listen_port": 3333,
    "proxy": [],
    "use_proxy": true,
    "direct_connect_with_proxy": false,
    "direct_connect_after_proxy": true,
    "pool_use_tls": false,
    "pools": [
        ["us.ss.btc.com", 1800, "YourSubAccountName"],
        ["us.ss.btc.com", 443, "YourSubAccountName"],
        ["us.ss.btc.com", 3333, "YourSubAccountName"]
    ]
}
```

Note: The options `http_debug` and `advanced` are reserved for developers. They should not be adjusted unless you know what you are doing. No documentation is currently written for them.

You can delete the `http_debug` and `advanced` configuration sections from the configuration file without affecting the functionality of the program. However, arbitrarily adjusting these options may cause the program to not run normally.

## Option Table

| Field | Name | Description |
| ----- | ---- | ----------- |
| multi_user_mode | Multi-user mode | After enabling the multi-user mode, the sub-account name specified by the miner will be used. Otherwise, the sub-account name you specify here will be used.<br><br>For example:<br><br>If the multi-user mode is enabled, you connect a miner with worker name "aaa.bbb" to BTCAgent. On the pool web, you will see the miner "bbb" on your sub-account "aaa".<br><br>If the multi-user mode is disabled, and you fill in the sub-account name "ccc" in BTCAgent. If you connect a miner with worker name "aaa.bbb" to BTCAgent, you will see the miner "bbb" on your sub-account "ccc" on the pool web. The sub-account name specified by the miner ("aaa") will be ignored. |
| agent_type | Agent Type | Reserved for the future, currently it can only be `"btc"`. It is recommended to omit this option. |
| always_keep_downconn | Send fake jobs when lost pool connection | Under normal circumstances, if BTCAgent suddenly lost all connections of mining pool servers, it will stop sending jobs to miners so that they can switch to their backup mining pools.<br><br>But if you experience an ISP failure, the miner will not be able to connect to backup pools. And it may suddenly stop computing. For some deployments, a sudden shutdown may cause damage to the miner or fail to return to normal after the network is recovered (because the temperature is too low). At this point, you can enable this option.<br><br>If you enable this option, BTCAgent will not stop sending jobs when disconnected from the mining pool, but will create some fake jobs and send them to your miners, which will keep them running continuously. When the BTCAgent reconnects to the mining pool, the fake job will be replaced by the real job.<br><br>But please note: fake jobs will not be submitted to the mining pool server (if submitted, server will only reject them), so they will not be paid. And if BTCAgent is a miner&apos;s preferred pool, enabling this option will also make it lose the opportunity to switch to its backup pool, because it will think that the preferred pool is always active. |
| disconnect_when_lost_asicboost | Automatically reconnect the miner to fix ASICBoost failure | Some miners with ASICBoost enabled will accidentally disable ASICBoost during operation. This will cause their hashrate to decrease or power consumption to increase.<br><br>Enabling this option can make BTCAgent automatically disconnect from such miners. Then the miner will automatically reconnect immediately and can usually resume ASICBoost again.<br><br>It is recommended to enable this option, as it usually has no side effects. Even if a miner does not support ASICBoost, no bad things will happen if this option is enabled. |
| use_ip_as_worker_name | Use miner's IP as its worker name | Enable this option to let BTCAgent use your miner&apos;s IP address as its  worker name. The name that filled in the miner&apos;s control panel will be  ignored. <br> <br>A typical IP address worker name is: &quot;192x168x1x23&quot;, which means the miner  whose IP address is 192.168.1.23. The format of the name can be set with `ip_worker_name_format`. |
| ip_worker_name_format | IP address worker name format | Set the format of the IP address worker name.<br><br>Available variables:<br>{1} represents the first number in the IP address.<br>{2} represents the second number in the IP address.<br>{3} represents the third number in the IP address.<br>{4} represents the 4th number in the IP address.<br><br>Examples:<br>{1}x{2}x{3}x{4}<br>If the IP address is &quot;192.168.1.23&quot;, the worker name is &quot;192x168x1x23&quot;.<br><br>{2}x{3}x{4}<br>If the IP address is &quot;192.168.1.23&quot;, the worker name is &quot;168x1x23&quot;.<br><br>{3}x{4}<br>If the IP address is &quot;192.168.1.23&quot;, the worker name is &quot;1x23&quot;. |
| fixed_worker_name | **[Advanced]**<br>Use fixed worker name | Set the worker names of all miners to this value. It can simulate the traditional Stratum proxy, so that all miners connected to the BTCAgent are treated as a single miner in the mining pool.<br><br>Leave the value blank (`""`) or delete the option to disable this feature. |
| submit_response_from_server | **[Advanced]**<br>Send the pool response to the miner | Send the real response from the mining pool server to the miner.<br><br>If this option is not enabled, BTCAgent will send a &quot;success&quot; response immediately upon receiving the miner&apos;s submission. This will keep the &quot;rejection rate&quot; in the miner&apos;s control panel always at 0.<br><br>If you want to see the real rejection rate in the miner control panel, you can enable this option. But this may increase network traffic and latency. |
| agent_listen_ip | BTCAgent listen IP | The listen IP of BTCAgent, miners should connect to your BTCAgent via this IP. It should be an IP address assigned to the computer running BTCAgent, or `0.0.0.0`. The `0.0.0.0` means "all possible IP addresses" and we recommend using it. |
| agent_listen_port | BTCAgent listen port | The listen port of BTCAgent, miners should connect to your BTCAgent via this port. If you run multiple BTCAgent processes on one computer, each process should use a different port.<br><br>The valid range of the port is 1 to 65535, and the recommended range is 2000 to 5000. Use of ports lower than 1024 requires root privileges, and ports higher than 5000 may be randomly occupied by other programs. |
| proxy | Network proxy | The network proxy used when connecting to the mining pool.<br><br>String array, each string is a proxy, the fastest will be used.<br><br>See the "Use proxy" section below to understand the format of the proxy string. |
| use_proxy | Use network proxy | The switch of the network proxy, the default is `true` (use proxy if not empty), set to `false` to disable the network proxy. |
| direct_connect_with_proxy | Use direct connection if it is faster than all proxies | While connecting to the mining pool through proxies, it also tries to connect directly to the mining pool (not through any proxy). If the direct connection is faster than all proxies, it will be used. If it is not possible to connect directly to the mining pool or it's slower, the fastest proxy will be used. |
| direct_connect_after_proxy | Use direct connection after all proxies fail | If BTCAgent cannot connect to the mining pool through any proxy, it will try to connect to the mining pool directly (not through a proxy). This may help when proxy fails. Of course, you can also set up multiple proxies to reduce the possibility of failure. |
| pool_use_tls | Use SSL/TLS encrypted connection to pool | Connect to the mining pool server encrypted with SSL/TLS to prevent network traffic from being monitored by the middleman.<br><br>Note: The address and port of the server that supports SSL/TLS encryption may be different from the normal server. If the server address and port you fill in does not support SSL/TLS encryption, enabling this option will cause BTCAgent to fail to connect to the server.<br><br>In addition, after enabling this option, the connection from your miners to this BTCAgent is still in plain text and will not be encrypted by SSL/TLS. So you don&apos;t need to change the miner settings. |
| pools | Mining pool server host, port, sub-account | [<br>&nbsp;&nbsp;&nbsp;&nbsp;["pool-server-host-1", server-port1, "sub-account-1"],<br>&nbsp;&nbsp;&nbsp;&nbsp;["pool-server-host-2", server-port2, "sub-account-2"],<br>&nbsp;&nbsp;&nbsp;&nbsp;["pool-server-host-3", server-port3, "sub-account-3"]<br>] |

## Use proxy

In [agent_conf.json](../agent_conf.default.json#L12):

* socks5 proxy
   ```
   "proxy": [
      "socks5://127.0.0.1:1089"
   ],
   ```
* http proxy
   ```
   "proxy": [
      "http://127.0.0.1:8889"
   ],
   ```
* https proxy (http proxy with SSL/TLS)
   ```
   "proxy": [
      "https://127.0.0.1:4433"
   ],
   ```
* find proxy from system environment variables
   ```
   "proxy": [
      "system"
   ],
   ```
* disable proxy
   ```
   "proxy": [],
   ```
* Multiple proxies, choose the fastest one
   ```
   "proxy": [
      "socks5://127.0.0.1:1089",
      "socks5://192.168.1.1:1089",
      "http://127.0.0.1:8889"
   ],
   ```
* proxy that requires authentication
   ```
   "proxy": [
      "socks5://username:password@127.0.0.1:1089",
      "http://username:password@192.168.1.1:8889",
      "https://username:password@127.0.0.1:4433"
   ],
   ```
