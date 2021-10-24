package main

import (
	"fmt"
	"net"

	"github.com/golang/glog"
)

type StratumSessionManager struct {
	// 配置
	configData *ConfigData
	// TCP监听对象
	tcpListener net.Listener
	// 会话ID管理器
	sessionIDManager *SessionIDManager
}

func NewStratumSessionManager(configData *ConfigData) (manager *StratumSessionManager) {
	manager = new(StratumSessionManager)
	manager.configData = configData
	return
}

func (manager *StratumSessionManager) Run() {
	var err error

	// 初始化会话管理器
	manager.sessionIDManager, err = NewSessionIDManager(0xfffe)
	if err != nil {
		glog.Fatal("NewSessionIDManager failed: ", err)
		return
	}

	// TCP监听
	listenAddr := fmt.Sprintf("%s:%d", manager.configData.AgentListenIp, manager.configData.AgentListenPort)
	glog.Info("startup is successful, listening: ", listenAddr)
	manager.tcpListener, err = net.Listen("tcp", listenAddr)
	if err != nil {
		glog.Fatal("listen failed: ", err)
		return
	}

	for {
		conn, err := manager.tcpListener.Accept()
		if err != nil {
			continue
		}
		go manager.RunStratumSession(conn)
	}
}

func (manager *StratumSessionManager) RunStratumSession(conn net.Conn) {
	// 产生 sessionID （Extranonce1）
	sessionID, err := manager.sessionIDManager.AllocSessionID()

	if err != nil {
		glog.Warning("session id allocation failed: ", err)
		conn.Close()
		return
	}

	session := NewStratumSession(manager, conn, sessionID)
	session.Run()
}
