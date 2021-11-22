package main

import (
	"encoding/json"
	"flag"
	"net/http"
	_ "net/http/pprof"
	"os"
	"os/signal"
	"sync"
	"syscall"

	"github.com/golang/glog"
)

func main() {
	var waitGroup sync.WaitGroup

	// 解析命令行参数
	configFilePath := flag.String("c", "agent_conf.json", "Path of config file")
	logDir := flag.String("l", "", "Log directory")
	flag.Parse()

	if *logDir == "" || *logDir == "stderr" {
		flag.Lookup("logtostderr").Value.Set("true")
	} else {
		flag.Lookup("log_dir").Value.Set(*logDir)
	}

	// 增大文件描述符上限
	IncreaseFDLimit()

	// 读取配置文件
	var config Config
	err := config.LoadFromFile(*configFilePath)
	if err != nil {
		glog.Fatal("load config failed: ", err)
		return
	}
	config.Init()

	configBytes, _ := json.Marshal(config)
	glog.Info("config: ", string(configBytes))

	if config.HTTPDebug.Enable {
		glog.Info("HTTP debug enabled: ", config.HTTPDebug.Listen)
		waitGroup.Add(1)
		go func() {
			defer waitGroup.Done()
			err := http.ListenAndServe(config.HTTPDebug.Listen, nil)
			if err != nil {
				glog.Error("launch http debug service failed: ", err.Error())
			}
		}()
	}

	manager := NewSessionManager(&config)

	c := make(chan os.Signal)
	signal.Notify(c, os.Interrupt, syscall.SIGTERM)
	go func() {
		<-c
		glog.Info("exiting...")
		manager.Stop()
	}()

	// 运行代理
	manager.Run()

	// 等待所有服务结束
	waitGroup.Done()
}
