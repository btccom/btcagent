package main

import (
	"encoding/json"
	"io/ioutil"
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

type Config struct {
	MultiUserMode               bool       `json:"multi_user_mode"`
	AgentType                   string     `json:"agent_type"`
	AlwaysKeepDownconn          bool       `json:"always_keep_downconn"`
	DisconnectWhenLostAsicboost bool       `json:"disconnect_when_lost_asicboost"`
	UseIpAsWorkerName           bool       `json:"use_ip_as_worker_name"`
	IpWorkerNameFormat          string     `json:"ip_worker_name_format"`
	SubmitResponseFromServer    bool       `json:"submit_response_from_server"`
	AgentListenIp               string     `json:"agent_listen_ip"`
	AgentListenPort             uint16     `json:"agent_listen_port"`
	PoolUseTls                  bool       `json:"pool_use_tls"`
	UseIocp                     bool       `json:"use_iocp"`
	FixedWorkerName             string     `json:"fixed_worker_name"`
	Pools                       []PoolInfo `json:"pools"`
	HTTPDebug                   struct {
		Enable bool   `json:"enable"`
		Listen string `json:"listen"`
	} `json:"http_debug"`
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
	// 如果启用多用户模式，删除矿池设置中的子账户名
	if conf.MultiUserMode {
		for i := range conf.Pools {
			conf.Pools[i].SubAccount = ""
		}
	}
}
