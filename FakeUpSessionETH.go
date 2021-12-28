package main

import (
	"time"

	"github.com/golang/glog"
)

type FakeUpSessionETH struct {
	manager      *UpSessionManager
	downSessions map[uint16]DownSession
	eventChannel chan interface{}

	fakeJob     *StratumJobETH
	exitChannel chan bool

	// 用于统计断开连接的矿机数，并同步给 UpSessionManager
	disconnectedMinerCounter int
}

func NewFakeUpSessionETH(manager *UpSessionManager) (up *FakeUpSessionETH) {
	up = new(FakeUpSessionETH)
	up.manager = manager
	up.downSessions = make(map[uint16]DownSession)
	up.eventChannel = make(chan interface{}, manager.config.Advanced.MessageQueueSize.PoolSession)
	up.exitChannel = make(chan bool, 1)
	return
}

func (up *FakeUpSessionETH) Run() {
	if up.manager.config.AlwaysKeepDownconn {
		go up.fakeNotifyTicker()
	}

	up.handleEvent()
}

func (up *FakeUpSessionETH) SendEvent(event interface{}) {
	up.eventChannel <- event
}

func (up *FakeUpSessionETH) addDownSession(e EventAddDownSession) {
	up.downSessions[e.Session.SessionID()] = e.Session

	if up.manager.config.AlwaysKeepDownconn && up.fakeJob != nil {
		up.fakeJob.ToNewFakeJob()
		bytes, err := up.fakeJob.ToNotifyLine(true)
		if err == nil {
			e.Session.SendEvent(EventSendBytes{bytes})
		} else {
			glog.Warning("[fake-pool-connection] failed to convert fake job to JSON:", err.Error(), "; ", up.fakeJob)
		}
	}
}

func (up *FakeUpSessionETH) transferDownSessions() {
	for _, down := range up.downSessions {
		go up.manager.SendEvent(EventAddDownSession{down})
	}
	// 与 UpSessionManager 同步矿机数量
	go up.manager.SendEvent(EventUpdateFakeMinerNum{len(up.downSessions)})
	// 清空map
	up.downSessions = make(map[uint16]DownSession)
}

func (up *FakeUpSessionETH) exit() {
	if up.manager.config.AlwaysKeepDownconn {
		up.exitChannel <- true
	}

	for _, down := range up.downSessions {
		go down.SendEvent(EventExit{})
	}
}

func (up *FakeUpSessionETH) sendSubmitResponse(sessionID uint16, id interface{}, status StratumStatus) {
	down, ok := up.downSessions[sessionID]
	if !ok {
		// 客户端已断开，忽略
		if glog.V(3) {
			glog.Info("[fake-pool-connection] cannot find down session: ", sessionID)
		}
		return
	}
	go down.SendEvent(EventSubmitResponse{id, status})
}

func (up *FakeUpSessionETH) handleSubmitShare(e EventSubmitShare) {
	up.sendSubmitResponse(e.Message.Base.SessionID, e.ID, STATUS_ACCEPT)
}

func (up *FakeUpSessionETH) downSessionBroken(e EventDownSessionBroken) {
	delete(up.downSessions, e.SessionID)

	if up.disconnectedMinerCounter == 0 {
		go func() {
			time.Sleep(1 * time.Second)
			up.SendEvent(EventSendUpdateMinerNum{})
		}()
	}
	up.disconnectedMinerCounter++
}

func (up *FakeUpSessionETH) sendUpdateMinerNum() {
	go up.manager.SendEvent(EventUpdateFakeMinerNum{up.disconnectedMinerCounter})
	up.disconnectedMinerCounter = 0
}

func (up *FakeUpSessionETH) updateFakeJob(e EventUpdateFakeJobETH) {
	up.fakeJob = e.FakeJob
}

func (up *FakeUpSessionETH) fakeNotifyTicker() {
	ticker := time.NewTicker(up.manager.config.Advanced.FakeJobNotifyIntervalSeconds.Get())
	defer ticker.Stop()

	for {
		select {
		case <-up.exitChannel:
			return
		case <-ticker.C:
			up.SendEvent(EventSendFakeNotify{})
		}
	}
}

func (up *FakeUpSessionETH) sendFakeNotify() {
	if up.fakeJob == nil || len(up.downSessions) < 1 {
		return
	}

	up.fakeJob.ToNewFakeJob()

	bytes, err := up.fakeJob.ToNotifyLine(false)
	if err != nil {
		glog.Warning("[fake-pool-connection] failed to convert fake job to JSON:", err.Error(), "; ", up.fakeJob)
		return
	}

	for _, down := range up.downSessions {
		go down.SendEvent(EventSendBytes{bytes})
	}
}

func (up *FakeUpSessionETH) handleEvent() {
	for {
		event := <-up.eventChannel

		switch e := event.(type) {
		case EventAddDownSession:
			up.addDownSession(e)
		case EventSubmitShare:
			up.handleSubmitShare(e)
		case EventDownSessionBroken:
			up.downSessionBroken(e)
		case EventSendUpdateMinerNum:
			up.sendUpdateMinerNum()
		case EventTransferDownSessions:
			up.transferDownSessions()
		case EventUpdateFakeJobETH:
			up.updateFakeJob(e)
		case EventSendFakeNotify:
			up.sendFakeNotify()
		case EventExit:
			up.exit()
			return

		default:
			glog.Error("[fake-pool-connection] unknown event: ", e)
		}
	}
}
