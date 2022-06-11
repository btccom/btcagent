package main

import (
	"fmt"
	"time"

	"github.com/golang/glog"
)

type UpSessionInfo struct {
	minerNum  int
	ready     bool
	full      bool
	upSession UpSession
}

type FakeUpSessionInfo struct {
	minerNum  int
	upSession FakeUpSession
}

type UpSessionManager struct {
	id string // 打印日志用的连接标识符

	subAccount string
	config     *Config
	parent     *SessionManager

	upSessions    []UpSessionInfo
	fakeUpSession FakeUpSessionInfo

	eventChannel chan interface{}

	initSuccess        bool
	initFailureCounter int

	printingMinerNum bool
}

func NewUpSessionManager(subAccount string, config *Config, parent *SessionManager) (manager *UpSessionManager) {
	manager = new(UpSessionManager)
	manager.subAccount = subAccount
	manager.config = config
	manager.parent = parent

	upSessions := make([]UpSessionInfo, manager.config.Advanced.PoolConnectionNumberPerSubAccount)
	manager.upSessions = upSessions[:]
	manager.fakeUpSession.upSession = manager.config.sessionFactory.NewFakeUpSession(manager)

	manager.eventChannel = make(chan interface{}, manager.config.Advanced.MessageQueueSize.PoolSessionManager)

	if manager.config.MultiUserMode {
		manager.id = fmt.Sprintf("<%s> ", manager.subAccount)
	}
	return
}

func (manager *UpSessionManager) Run() {
	go manager.fakeUpSession.upSession.Run()

	for i := range manager.upSessions {
		go manager.connect(i)
	}

	manager.handleEvent()
}

func (manager *UpSessionManager) connect(slot int) {
	for i := range manager.config.Pools {
		up := manager.config.sessionFactory.NewUpSession(manager, i, slot)
		up.Init()

		if up.Stat() == StatAuthorized {
			go up.Run()
			manager.SendEvent(EventUpSessionReady{slot, up})
			return
		}
	}
	manager.SendEvent(EventUpSessionInitFailed{slot})
}

func (manager *UpSessionManager) SendEvent(event interface{}) {
	manager.eventChannel <- event
}

func (manager *UpSessionManager) addDownSession(e EventAddDownSession) {
	defer manager.tryPrintMinerNum()

	var selected *UpSessionInfo

	// 寻找连接数最少的服务器
	for i := range manager.upSessions {
		info := &manager.upSessions[i]
		if info.ready && !info.full && (selected == nil || info.minerNum < selected.minerNum) {
			selected = info
		}
	}

	if selected != nil {
		selected.minerNum++
		e.Session.SendEvent(EventSetUpSession{selected.upSession})
		return
	}

	// 服务器均未就绪，若已启用 AlwaysKeepDownconn，就把矿机托管给 FakeUpSession
	if manager.config.AlwaysKeepDownconn {
		manager.fakeUpSession.minerNum++
		e.Session.SendEvent(EventSetUpSession{manager.fakeUpSession.upSession})
		return
	}

	// 未启用 AlwaysKeepDownconn，直接断开连接，防止矿机认为 BTCAgent 连接活跃
	e.Session.SendEvent(EventPoolNotReady{})
}

func (manager *UpSessionManager) upSessionReady(e EventUpSessionReady) {
	defer manager.tryPrintMinerNum()

	manager.initSuccess = true

	info := &manager.upSessions[e.Slot]
	info.upSession = e.Session
	info.ready = true

	// 从 FakeUpSession 拿回矿机
	manager.fakeUpSession.upSession.SendEvent(EventTransferDownSessions{})
}

func (manager *UpSessionManager) upSessionInitFailed(e EventUpSessionInitFailed) {
	if manager.initSuccess {
		glog.Error(manager.id, "Failed to connect to all ", len(manager.config.Pools), " pool servers, please check your configuration! Retry in 5 seconds.")
		go func() {
			time.Sleep(5 * time.Second)
			manager.connect(e.Slot)
		}()
		return
	}

	manager.initFailureCounter++

	if manager.initFailureCounter >= len(manager.upSessions) {
		glog.Error(manager.id, "Too many connection failure to pool, please check your sub-account or pool configurations! Sub-account: ", manager.subAccount, ", pools: ", manager.config.Pools)

		manager.parent.SendEvent(EventStopUpSessionManager{manager.subAccount})
		return
	}
}

func (manager *UpSessionManager) upSessionBroken(e EventUpSessionBroken) {
	defer manager.tryPrintMinerNum()

	info := &manager.upSessions[e.Slot]
	info.ready = false
	info.minerNum = 0

	go manager.connect(e.Slot)
}

func (manager *UpSessionManager) upSessionFull(e EventUpSessionFull) {
	info := &manager.upSessions[e.Slot]
	info.full = true
}

func (manager *UpSessionManager) updateMinerNum(e EventUpdateMinerNum) {
	defer manager.tryPrintMinerNum()

	up := &manager.upSessions[e.Slot]
	up.minerNum -= e.DisconnectedMinerCounter

	if glog.V(3) {
		glog.Info(manager.id, "miner num update, slot: ", e.Slot, ", miners: ", up.minerNum)
	}

	if manager.config.MultiUserMode {
		minerNum := 0
		for i := range manager.upSessions {
			minerNum += manager.upSessions[i].minerNum
		}
		if minerNum < 1 {
			glog.Info(manager.id, "no miners on sub-account ", manager.subAccount, ", close pool connections")
			manager.parent.SendEvent(EventStopUpSessionManager{manager.subAccount})
		}
	}

	if up.full && up.minerNum == 0 {
		// 重连矿池服务器已满的空闲连接
		go func() {
			glog.Info(manager.id, "reconnect full pool session #", e.Slot)
			up.upSession.SendEvent(EventExit{})
			manager.SendEvent(EventUpSessionBroken{e.Slot})
		}()
	}
}

func (manager *UpSessionManager) updateFakeMinerNum(e EventUpdateFakeMinerNum) {
	defer manager.tryPrintMinerNum()

	manager.fakeUpSession.minerNum -= e.DisconnectedMinerCounter
}

func (manager *UpSessionManager) updateFakeJob(e interface{}) {
	manager.fakeUpSession.upSession.SendEvent(e)
}

func (manager *UpSessionManager) exit() {
	manager.fakeUpSession.upSession.SendEvent(EventExit{})

	for _, up := range manager.upSessions {
		if up.ready {
			up.upSession.SendEvent(EventExit{})
		}
	}
}

func (manager *UpSessionManager) tryPrintMinerNum() {
	if manager.printingMinerNum {
		return
	}
	manager.printingMinerNum = true
	go func() {
		time.Sleep(5 * time.Second)
		manager.SendEvent(EventPrintMinerNum{})
	}()
}

func (manager *UpSessionManager) printMinerNum() {
	pools := 0
	miners := manager.fakeUpSession.minerNum
	for _, info := range manager.upSessions {
		miners += info.minerNum
		if info.ready {
			pools++
		}
	}
	glog.Info(manager.id, "connection number changed, pool servers: ", pools, ", miners: ", miners)
	manager.printingMinerNum = false
}

func (manager *UpSessionManager) handleEvent() {
	for {
		event := <-manager.eventChannel

		switch e := event.(type) {
		case EventUpSessionReady:
			manager.upSessionReady(e)
		case EventUpSessionInitFailed:
			manager.upSessionInitFailed(e)
		case EventAddDownSession:
			manager.addDownSession(e)
		case EventUpSessionBroken:
			manager.upSessionBroken(e)
		case EventUpSessionFull:
			manager.upSessionFull(e)
		case EventUpdateMinerNum:
			manager.updateMinerNum(e)
		case EventUpdateFakeMinerNum:
			manager.updateFakeMinerNum(e)
		case EventUpdateFakeJobBTC:
			manager.updateFakeJob(e)
		case EventUpdateFakeJobETH:
			manager.updateFakeJob(e)
		case EventPrintMinerNum:
			manager.printMinerNum()
		case EventExit:
			manager.exit()
			return
		default:
			glog.Error("[UpSessionManager] unknown event: ", event)
		}
	}
}
