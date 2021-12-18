package main

import (
	"encoding/json"
	"io/ioutil"
	"time"

	"github.com/golang/glog"
)

type PoolInfo struct {
	Host       string
	Port       uint16
	SubAccount string
}

func (r *PoolInfo) UnmarshalJSON(p []byte) error {
	var tmp []json.RawMessage
	if err := json.Unmarshal(p, &tmp); err != nil {
		return err
	}
	if len(tmp) > 0 {
		if err := json.Unmarshal(tmp[0], &r.Host); err != nil {
			return err
		}
	}
	if len(tmp) > 1 {
		if err := json.Unmarshal(tmp[1], &r.Port); err != nil {
			return err
		}
	}
	if len(tmp) > 2 {
		if err := json.Unmarshal(tmp[2], &r.SubAccount); err != nil {
			return err
		}
	}
	return nil
}

func (r *PoolInfo) MarshalJSON() ([]byte, error) {
	return json.Marshal([]interface{}{r.Host, r.Port, r.SubAccount})
}

type Seconds uint32

func (s Seconds) Get() time.Duration {
	return time.Duration(s) * time.Second
}

type Config struct {
	MultiUserMode               bool       `json:"multi_user_mode"`
	AgentType                   string     `json:"agent_type"`
	AlwaysKeepDownconn          bool       `json:"always_keep_downconn"`
	DisconnectWhenLostAsicboost bool       `json:"disconnect_when_lost_asicboost"`
	UseIpAsWorkerName           bool       `json:"use_ip_as_worker_name"`
	IpWorkerNameFormat          string     `json:"ip_worker_name_format"`
	FixedWorkerName             string     `json:"fixed_worker_name"`
	SubmitResponseFromServer    bool       `json:"submit_response_from_server"`
	AgentListenIp               string     `json:"agent_listen_ip"`
	AgentListenPort             uint16     `json:"agent_listen_port"`
	Proxy                       []string   `json:"proxy"`
	UseProxy                    bool       `json:"use_proxy"`
	DirectConnectWithProxy      bool       `json:"direct_connect_with_proxy"`
	DirectConnectAfterProxy     bool       `json:"direct_connect_after_proxy"`
	PoolUseTls                  bool       `json:"pool_use_tls"`
	Pools                       []PoolInfo `json:"pools"`
	HTTPDebug                   struct {
		Enable bool   `json:"enable"`
		Listen string `json:"listen"`
	} `json:"http_debug"`
	Advanced struct {
		// 每个子账户的矿池连接数量
		PoolConnectionNumberPerSubAccount uint8 `json:"pool_connection_number_per_subaccount"`
		// 矿池连接超时时间
		PoolConnectionDialTimeoutSeconds Seconds `json:"pool_connection_dial_timeout_seconds"`
		// 矿池读取超时时间
		PoolConnectionReadTimeoutSeconds Seconds `json:"pool_connection_read_timeout_seconds"`
		// 假任务的发送周期（秒）
		FakeJobNotifyIntervalSeconds Seconds `json:"fake_job_notify_interval_seconds"`
		// 不进行 TLS 证书校验
		TLSSkipCertificateVerify bool `json:"tls_skip_certificate_verify"`

		// 消息队列大小
		MessageQueueSize struct {
			SessionManager     uint `json:"session_manager"`
			PoolSessionManager uint `json:"pool_session_manager"`
			PoolSession        uint `json:"pool_session"`
			MinerSession       uint `json:"miner_session"`
		} `json:"message_queue_size"`
	} `json:"advanced"`
}

// NewConfig 创建配置对象并设置默认值
func NewConfig() (config *Config) {
	config = new(Config)

	config.DisconnectWhenLostAsicboost = DownSessionDisconnectWhenLostAsicboost
	config.IpWorkerNameFormat = DefaultIpWorkerNameFormat
	config.UseProxy = true
	config.DirectConnectAfterProxy = true

	config.Advanced.PoolConnectionNumberPerSubAccount = UpSessionNumPerSubAccount
	config.Advanced.PoolConnectionDialTimeoutSeconds = UpSessionDialTimeoutSeconds
	config.Advanced.PoolConnectionReadTimeoutSeconds = UpSessionReadTimeoutSeconds
	config.Advanced.FakeJobNotifyIntervalSeconds = FakeJobNotifyIntervalSeconds
	config.Advanced.TLSSkipCertificateVerify = UpSessionTLSInsecureSkipVerify

	config.Advanced.MessageQueueSize.SessionManager = SessionManagerChannelCache
	config.Advanced.MessageQueueSize.PoolSessionManager = UpSessionManagerChannelCache
	config.Advanced.MessageQueueSize.PoolSession = UpSessionChannelCache
	config.Advanced.MessageQueueSize.MinerSession = DownSessionChannelCache

	return
}

// LoadFromFile 从文件载入配置
func (conf *Config) LoadFromFile(file string) (err error) {
	configJSON, err := ioutil.ReadFile(file)
	if err != nil {
		return
	}
	err = json.Unmarshal(configJSON, conf)
	return
}

func (conf *Config) Init() {
	if conf.MultiUserMode {
		glog.Info("[OPTION] Multi user mode: Enabled. Sub-accounts in config file will be ignored.")
	} else {
		glog.Info("[OPTION] Multi user mode: Disabled. Sub-accounts in config file will be used.")
	}

	glog.Info("[OPTION] Connect to pool server with SSL/TLS encryption: ", IsEnabled(conf.PoolUseTls))
	glog.Info("[OPTION] Always keep miner connections even if pool disconnected: ", IsEnabled(conf.AlwaysKeepDownconn))
	glog.Info("[OPTION] Disconnect if a miner lost its AsicBoost mid-way: ", IsEnabled(conf.DisconnectWhenLostAsicboost))

	if len(conf.FixedWorkerName) > 0 {
		glog.Info("[OPTION] Fixed worker name enabled, all worker name will be replaced to ", conf.FixedWorkerName, " on the server.")
	}

	if !conf.UseProxy && len(conf.Proxy) > 0 {
		conf.Proxy = []string{}
		glog.Info("[OPTION] Proxy disabled")
	}
	for i := range conf.Proxy {
		if conf.Proxy[i] == "system" {
			conf.Proxy[i] = GetProxyURLFromEnv()
		}
	}
	if len(conf.Proxy) > 0 {
		glog.Info("[OPTION] Connect to pool server with proxy ", conf.Proxy)
	}

	for i := range conf.Pools {
		pool := &conf.Pools[i]
		if conf.MultiUserMode {
			// 如果启用多用户模式，删除矿池设置中的子账户名
			pool.SubAccount = ""
			glog.Info("add pool: ", pool.Host, ":", pool.Port, ", multi user mode")
		} else {
			glog.Info("add pool: ", pool.Host, ":", pool.Port, ", sub-account: ", pool.SubAccount)
		}
	}
}
