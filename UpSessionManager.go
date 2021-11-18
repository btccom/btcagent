package main

import (
	"time"

	"github.com/golang/glog"
)

type UpSessionInfo struct {
	minerNum  int
	ready     bool
	upSession *UpSession
}

type UpSessionManager struct {
	subAccount string
	config     *Config
	parent     *StratumSessionManager

	upSessions    []UpSessionInfo
	fakeUpSession *FakeUpSession

	eventChannel chan interface{}

	initSuccess        bool
	initFailureCounter int
}

func NewUpSessionManager(subAccount string, config *Config, parent *StratumSessionManager) (manager *UpSessionManager) {
	manager = new(UpSessionManager)
	manager.subAccount = subAccount
	manager.config = config
	manager.parent = parent

	upSessions := [UpSessionNumPerSubAccount]UpSessionInfo{}
	manager.upSessions = upSessions[:]
	manager.fakeUpSession = NewFakeUpSession(manager)

	manager.eventChannel = make(chan interface{}, UpSessionManagerChannelCache)
	return
}

func (manager *UpSessionManager) Run() {
	go manager.fakeUpSession.Run()

	for i := range manager.upSessions {
		go manager.connect(i)
	}

	manager.handleEvent()
}

func (manager *UpSessionManager) connect(slot int) {
	for i := range manager.config.Pools {
		session := NewUpSession(manager, manager.config, manager.subAccount, i, slot)
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

func (manager *UpSessionManager) addStratumSession(e EventAddStratumSession) {
	var selected *UpSessionInfo

	// 寻找连接数最少的服务器
	for i := range manager.upSessions {
		info := &manager.upSessions[i]
		if info.ready && (selected == nil || info.minerNum < selected.minerNum) {
			selected = info
		}
	}

	if selected != nil {
		selected.minerNum++
		e.Session.SendEvent(EventSetUpSession{selected.upSession})
		return
	}

	// 服务器均未就绪，把矿机托管给 FakeUpSession
	e.Session.SendEvent(EventSetUpSession{manager.fakeUpSession})
}

func (manager *UpSessionManager) upSessionReady(e EventUpSessionReady) {
	manager.initSuccess = true

	info := &manager.upSessions[e.Slot]
	info.upSession = e.Session
	info.ready = true

	// 从 FakeUpSession 拿回矿机
	manager.fakeUpSession.SendEvent(EventTransferStratumSessions{})
}

func (manager *UpSessionManager) upSessionInitFailed(e EventUpSessionInitFailed) {
	if manager.initSuccess {
		glog.Error("Failed to connect to all ", len(manager.config.Pools), " pool servers, please check your configuration! Retry in 5 seconds.")
		go func() {
			time.Sleep(5 * time.Second)
			manager.connect(e.Slot)
		}()
		return
	}

	manager.initFailureCounter++

	if manager.initFailureCounter >= len(manager.upSessions) {
		glog.Error("Too many connection failure to pool, please check your sub-account or pool configurations! Sub-account: ", manager.subAccount, ", pools: ", manager.config.Pools)

		manager.parent.SendEvent(EventStopUpSessionManager{manager.subAccount})
		return
	}
}

func (manager *UpSessionManager) upSessionBroken(e EventUpSessionBroken) {
	info := &manager.upSessions[e.Slot]
	info.ready = false
	info.minerNum = 0

	go manager.connect(e.Slot)
}

func (manager *UpSessionManager) updateMinerNum(e EventUpdateMinerNum) {
	manager.upSessions[e.Slot].minerNum -= e.DisconnectedMinerCounter
	glog.Info("miner num update, slot: ", e.Slot, ", miners: ", manager.upSessions[e.Slot].minerNum)
}

func (manager *UpSessionManager) exit() {
	manager.fakeUpSession.SendEvent(EventExit{})

	for _, up := range manager.upSessions {
		if up.ready {
			up.upSession.SendEvent(EventExit{})
		}
	}
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
		case EventUpSessionBroken:
			manager.upSessionBroken(e)
		case EventUpdateMinerNum:
			manager.updateMinerNum(e)
		case EventExit:
			manager.exit()
			return
		default:
			glog.Error("Unknown event: ", event)
		}
	}
}
