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
	var configData ConfigData
	err := configData.LoadFromFile(*configFilePath)
	if err != nil {
		glog.Fatal("load config failed: ", err)
		return
	}

	configBytes, _ := json.Marshal(configData)
	glog.Info("config: ", string(configBytes))

	// for test only
	up := NewUpSession(configData.Pools[0].SubAccount, &configData)
	up.Run()

	// 运行代理
	manager := NewStratumSessionManager(&configData)
	manager.Run()
}
