# BTCAgent Config File Details

## All Options

```json
{
    "agent_type": "btc",
    "always_keep_downconn": false,
    "disconnect_when_lost_asicboost": true,
    "use_ip_as_worker_name": false,
    "ip_worker_name_format": "{1}x{2}x{3}x{4}",
    "submit_response_from_server": false,
    "agent_listen_ip": "0.0.0.0",
    "agent_listen_port": 3333,
    "pool_use_tls": false,
    "use_iocp": false,
    "fixed_worker_name": "",
    "pools": [
        ["us.ss.btc.com", 1800, "YourSubAccountName"],
        ["us.ss.btc.com", 443, "YourSubAccountName"],
        ["us.ss.btc.com", 3333, "YourSubAccountName"]
    ]
}
```

## Option Table

| Field | Name | Description |
| ----- | ---- | ----------- |
| agent_type | Agent Type | Reserved for the future, currently it can only be `"btc"`. It is recommended to omit this option. |
| always_keep_downconn | Send fake jobs when lost pool connection | Under normal circumstances, if BTCAgent suddenly lost all connections of mining pool servers, it will stop sending jobs to miners so that they can switch to their backup mining pools.<br><br>But if you experience an ISP failure, the miner will not be able to connect to backup pools. And it may suddenly stop computing. For some deployments, a sudden shutdown may cause damage to the miner or fail to return to normal after the network is recovered (because the temperature is too low). At this point, you can enable this option.<br><br>If you enable this option, BTCAgent will not stop sending jobs when disconnected from the mining pool, but will create some fake jobs and send them to your miners, which will keep them running continuously. When the BTCAgent reconnects to the mining pool, the fake job will be replaced by the real job.<br><br>But please note: fake jobs will not be submitted to the mining pool server (if submitted, server will only reject them), so they will not be paid. And if BTCAgent is a miner&apos;s preferred pool, enabling this option will also make it lose the opportunity to switch to its backup pool, because it will think that the preferred pool is always active. |
| disconnect_when_lost_asicboost | Automatically reconnect the miner to fix ASICBoost failure | Some miners with ASICBoost enabled will accidentally disable ASICBoost during operation. This will cause their hashrate to decrease or power consumption to increase.<br><br>Enabling this option can make BTCAgent automatically disconnect from such miners. Then the miner will automatically reconnect immediately and can usually resume ASICBoost again.<br><br>It is recommended to enable this option, as it usually has no side effects. Even if a miner does not support ASICBoost, no bad things will happen if this option is enabled. |
| use_ip_as_worker_name | Use miner's IP as its worker name | Enable this option to let BTCAgent use your miner&apos;s IP address as its  worker name. The name that filled in the miner&apos;s control panel will be  ignored. <br> <br>A typical IP address worker name is: &quot;192x168x1x23&quot;, which means the miner  whose IP address is 192.168.1.23. The format of the name can be set with `ip_worker_name_format`. |
| ip_worker_name_format | IP address worker name format | Set the format of the IP address worker name.<br><br>Available variables:<br>{1} represents the first number in the IP address.<br>{2} represents the second number in the IP address.<br>{3} represents the third number in the IP address.<br>{4} represents the 4th number in the IP address.<br><br>Examples:<br>{1}x{2}x{3}x{4}<br>If the IP address is &quot;192.168.1.23&quot;, the worker name is &quot;192x168x1x23&quot;.<br><br>{2}x{3}x{4}<br>If the IP address is &quot;192.168.1.23&quot;, the worker name is &quot;168x1x23&quot;.<br><br>{3}x{4}<br>If the IP address is &quot;192.168.1.23&quot;, the worker name is &quot;1x23&quot;. |
| submit_response_from_server | **[Advanced]**<br>Send the pool response to the miner | Send the real response from the mining pool server to the miner.<br><br>If this option is not enabled, BTCAgent will send a &quot;success&quot; response immediately upon receiving the miner&apos;s submission. This will keep the &quot;rejection rate&quot; in the miner&apos;s control panel always at 0.<br><br>If you want to see the real rejection rate in the miner control panel, you can enable this option. But this may increase network traffic and latency. |
| agent_listen_ip | BTCAgent listen IP | The listen IP of BTCAgent, miners should connect to your BTCAgent via this IP. It should be an IP address assigned to the computer running BTCAgent, or `0.0.0.0`. The `0.0.0.0` means "all possible IP addresses" and we recommend using it. |
| agent_listen_port | BTCAgent listen port | The listen port of BTCAgent, miners should connect to your BTCAgent via this port. If you run multiple BTCAgent processes on one computer, each process should use a different port.<br><br>The valid range of the port is 1 to 65535, and the recommended range is 2000 to 5000. Use of ports lower than 1024 requires root privileges, and ports higher than 5000 may be randomly occupied by other programs. |
| pool_use_tls | Use SSL/TLS encrypted connection to pool | Connect to the mining pool server encrypted with SSL/TLS to prevent network traffic from being monitored by the middleman.<br><br>Note: The address and port of the server that supports SSL/TLS encryption may be different from the normal server. If the server address and port you fill in does not support SSL/TLS encryption, enabling this option will cause BTCAgent to fail to connect to the server.<br><br>In addition, after enabling this option, the connection from your miners to this BTCAgent is still in plain text and will not be encrypted by SSL/TLS. So you don&apos;t need to change the miner settings. |
| use_iocp | **[Windows Only]**<br>**[Experimental]**<br>Use IOCP for network connection | Enable the Windows I/O Completion Port (IOCP) for BTCAgent.<br><br>This may increase the throughput of BTCAgent and allow more miners to connect to a single BTCAgent process at the same time.<br><br>But this option is highly experimental. Our tests show that it will increase the memory usage of BTCAgent, and BTCAgent may crash or be no response in some cases.<br><br>We do not recommend that you enable this option unless you do experience throughput issues and cannot be resolved by other methods.<br><br>If you enable this option, be sure to set up a backup mining pool for miners. |
| fixed_worker_name | **[Advanced]**<br>Use fixed worker name | Set the worker names of all miners to this value. It can simulate the traditional Stratum proxy, so that all miners connected to the BTCAgent are treated as a single miner in the mining pool.<br><br>Leave the value blank (`""`) or delete the option to disable this feature. |
| pools | Mining pool server host, port, sub-account | [<br>&nbsp;&nbsp;&nbsp;&nbsp;["pool-server-host-1", server-port1, "sub-account-1"],<br>&nbsp;&nbsp;&nbsp;&nbsp;["pool-server-host-2", server-port2, "sub-account-2"],<br>&nbsp;&nbsp;&nbsp;&nbsp;["pool-server-host-3", server-port3, "sub-account-3"]<br>] |
