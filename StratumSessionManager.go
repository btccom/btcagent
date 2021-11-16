package main

import (
	"fmt"
	"net"

	"github.com/golang/glog"
)

type StratumSessionManager struct {
	config            *Config                      // 配置
	tcpListener       net.Listener                 // TCP监听对象
	sessionIDManager  *SessionIDManager            // 会话ID管理器
	upSessionManagers map[string]*UpSessionManager // map[子账户名]矿池会话管理器
	exitChannel       chan bool                    // 退出信号
	eventChannel      chan interface{}             // 事件循环
}

func NewStratumSessionManager(config *Config) (manager *StratumSessionManager) {
	manager = new(StratumSessionManager)
	manager.config = config
	manager.upSessionManagers = make(map[string]*UpSessionManager)
	manager.exitChannel = make(chan bool, 1)
	manager.eventChannel = make(chan interface{}, StratumSessionManagerChannelCache)
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

	// 启动事件循环
	go manager.handleEvent()

	// TCP监听
	listenAddr := fmt.Sprintf("%s:%d", manager.config.AgentListenIp, manager.config.AgentListenPort)
	glog.Info("startup is successful, listening: ", listenAddr)
	manager.tcpListener, err = net.Listen("tcp", listenAddr)
	if err != nil {
		glog.Fatal("listen failed: ", err)
		return
	}

	for {
		conn, err := manager.tcpListener.Accept()
		if err != nil {
			select {
			case <-manager.exitChannel:
				return
			default:
				glog.Warning("accept miner connection failed: ", err.Error())
				continue
			}
		}
		go manager.RunStratumSession(conn)
	}
}

func (manager *StratumSessionManager) Stop() {
	// 退出TCP监听
	manager.exitChannel <- true
	manager.tcpListener.Close()

	// 退出事件循环
	manager.SendEvent(EventExit{})
}

func (manager *StratumSessionManager) exit() {
	// 要求所有连接退出
	for _, up := range manager.upSessionManagers {
		up.SendEvent(EventExit{})
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
	session.Init()
	if session.stat != StatAuthorized {
		// 认证失败，放弃连接
		return
	}

	manager.SendEvent(EventAddStratumSession{session})
}

func (manager *StratumSessionManager) addStratumSession(e EventAddStratumSession) {
	upManager, ok := manager.upSessionManagers[e.Session.subAccountName]
	if !ok {
		upManager = NewUpSessionManager(e.Session.subAccountName, manager.config)
		go upManager.Run()
		manager.upSessionManagers[e.Session.subAccountName] = upManager
	}
	upManager.SendEvent(EventAddStratumSession{e.Session})
}

func (manager *StratumSessionManager) SendEvent(event interface{}) {
	manager.eventChannel <- event
}

func (manager *StratumSessionManager) handleEvent() {
	for {
		event := <-manager.eventChannel

		switch e := event.(type) {
		case EventAddStratumSession:
			manager.addStratumSession(e)
		case EventExit:
			manager.exit()
			return
		default:
			glog.Error("Unknown event: ", event)
		}
	}
}
