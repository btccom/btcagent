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
	return
}

func (manager *UpSessionManager) Run() {
	for i := range manager.upSessions {
		go manager.connect(i)
	}

	go manager.handleEvent()
}

func (manager *UpSessionManager) connect(slot int) {
	info := &manager.upSessions[slot]
	for i := range manager.config.Pools {
		info.upSession = NewUpSession(manager.subAccount, i, manager.config)
		info.upSession.Run()

		if info.upSession.stat == StatAuthorized {
			manager.SendEvent(EventUpStreamReady{slot})
			break
		}
	}
}

func (manager *UpSessionManager) SendEvent(event interface{}) {
	manager.eventChannel <- event
}

func (manager *UpSessionManager) AddStratumSession(session *StratumSession) {
	manager.SendEvent(EventAddStratumSession{session})
}

func (manager *UpSessionManager) upStreamReady(slot int) {
	manager.upSessions[slot].ready = true
}

func (manager *UpSessionManager) addStratumSession(session *StratumSession) {
	var selected *UpSessionInfo

	// 寻找连接数最少的服务器
	for i := range manager.upSessions {
		info := &manager.upSessions[i]
		if info.ready && (selected == nil || info.minerNum < selected.minerNum) {
			selected = info
		}
	}

	// 服务器均未就绪，1秒后重试
	if selected == nil {
		go func() {
			time.Sleep(1 * time.Second)
			manager.SendEvent(EventAddStratumSession{session})
		}()
		return
	}

}

func (manager *UpSessionManager) handleEvent() {
	for {
		event := <-manager.eventChannel

		switch e := event.(type) {
		case EventUpStreamReady:
			manager.upStreamReady(e.Slot)
		case EventAddStratumSession:
			manager.addStratumSession(e.Session)
		case EventApplicationExit:
		default:
			glog.Error("Unknown event: ", event)
		}
	}
}
