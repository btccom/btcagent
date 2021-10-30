package main

import (
	"time"

	"github.com/golang/glog"
)

type UpSessionInfo struct {
	minerNum  uint
	ready     bool
	upSession *UpSession
}

type UpSessionManager struct {
	subAccount string
	config     *Config

	upSessions []UpSessionInfo

	eventChannel chan interface{}
}

func NewUpSessionManager(subAccount string, config *Config) (manager *UpSessionManager) {
	manager = new(UpSessionManager)
	manager.subAccount = subAccount
	manager.config = config

	upSessions := [UpSessionNumPerSubAccount]UpSessionInfo{}
	manager.upSessions = upSessions[:]

	manager.eventChannel = make(chan interface{})
	return
}

func (manager *UpSessionManager) Run() {
	for i := range manager.upSessions {
		go manager.connect(i)
	}

	manager.handleEvent()
}

func (manager *UpSessionManager) connect(slot int) {
	for i := range manager.config.Pools {
		session := NewUpSession(manager, manager.config, manager.subAccount, i)
		session.Init()

		if session.stat == StatAuthorized {
			go session.Run()
			manager.SendEvent(EventUpSessionReady{slot, session})
			return
		}
	}
	manager.SendEvent(EventUpSessionInitFailed{slot})
}

func (manager *UpSessionManager) SendEvent(event interface{}) {
	manager.eventChannel <- event
}

func (manager *UpSessionManager) AddStratumSession(session *StratumSession) {
	manager.SendEvent(EventAddStratumSession{session})
}

func (manager *UpSessionManager) addStratumSession(e EventAddStratumSession) {
	var selected *UpSessionInfo

	// 寻找连接数最少的服务器
	for i := range manager.upSessions {
		info := &manager.upSessions[i]
		if info.ready && (selected == nil || info.minerNum < selected.minerNum) {
			selected = info
		}
	}

	// 服务器均未就绪，3秒后重试
	if selected == nil {
		glog.Warning("Cannot find a ready pool connection for miner ", e.Session.ID(), "! Retry in 3 seconds.")
		go func() {
			time.Sleep(3 * time.Second)
			manager.SendEvent(e)
		}()
		return
	}

	selected.upSession.AddStratumSession(e.Session)
}

func (manager *UpSessionManager) upSessionReady(e EventUpSessionReady) {
	info := &manager.upSessions[e.Slot]
	info.upSession = e.Session
	info.ready = true
}

func (manager *UpSessionManager) upSessionInitFailed(e EventUpSessionInitFailed) {
	glog.Error("Failed to connect to all ", len(manager.config.Pools), " pool servers, please check your configuration! Retry in 5 seconds.")
	go func() {
		time.Sleep(5 * time.Second)
		manager.connect(e.Slot)
	}()
}

func (manager *UpSessionManager) handleEvent() {
	for {
		event := <-manager.eventChannel

		switch e := event.(type) {
		case EventUpSessionReady:
			manager.upSessionReady(e)
		case EventUpSessionInitFailed:
			manager.upSessionInitFailed(e)
		case EventAddStratumSession:
			manager.addStratumSession(e)
		case EventApplicationExit:
		default:
			glog.Error("Unknown event: ", event)
		}
	}
}
