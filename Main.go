package main

import (
	"encoding/json"
	"flag"

	"github.com/golang/glog"
)

func main() {
	// 解析命令行参数
	configFilePath := flag.String("c", "agent_conf.json", "Path of config file")
	logDir := flag.String("l", "", "Log directory")
	flag.Parse()

	if *logDir == "" || *logDir == "stderr" {
		flag.Lookup("logtostderr").Value.Set("true")
	} else {
		flag.Lookup("log_dir").Value.Set(*logDir)
	}

	// 读取配置文件
	var config Config
	err := config.LoadFromFile(*configFilePath)
	if err != nil {
		glog.Fatal("load config failed: ", err)
		return
	}

	configBytes, _ := json.Marshal(config)
	glog.Info("config: ", string(configBytes))

	// for test only
	up := NewUpSessionManager(config.Pools[0].SubAccount, &config)
	up.Run()

	// 运行代理
	manager := NewStratumSessionManager(&config)
	manager.Run()
}
