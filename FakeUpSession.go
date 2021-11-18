package main

import (
	"time"

	"github.com/golang/glog"
)

type FakeUpSession struct {
	manager      *UpSessionManager
	downSessions map[uint32]*DownSession
	eventChannel chan interface{}

	fakeJob     *StratumJob
	exitChannel chan bool
}

func NewFakeUpSession(manager *UpSessionManager) (up *FakeUpSession) {
	up = new(FakeUpSession)
	up.manager = manager
	up.downSessions = make(map[uint32]*DownSession)
	up.eventChannel = make(chan interface{}, UpSessionChannelCache)
	up.exitChannel = make(chan bool, 1)
	return
}

func (up *FakeUpSession) Run() {
	if up.manager.config.AlwaysKeepDownconn {
		go up.fakeNotifyTicker()
	}

	up.handleEvent()
}

func (up *FakeUpSession) SendEvent(event interface{}) {
	up.eventChannel <- event
}

func (up *FakeUpSession) addDownSession(e EventAddDownSession) {
	up.downSessions[e.Session.sessionID] = e.Session

	if up.manager.config.AlwaysKeepDownconn && up.fakeJob != nil {
		up.fakeJob.ToNewFakeJob()
		bytes, err := up.fakeJob.ToNotifyLine(true)
		if err == nil {
			e.Session.SendEvent(EventSendBytes{bytes})
		} else {
			glog.Warning("create notify bytes failed, ", err.Error(), ", fake job: ", up.fakeJob)
		}
	}
}

func (up *FakeUpSession) transferDownSessions() {
	for _, down := range up.downSessions {
		up.manager.SendEvent(EventAddDownSession{down})
	}
	// 清空map
	up.downSessions = make(map[uint32]*DownSession)
}

func (up *FakeUpSession) exit() {
	if up.manager.config.AlwaysKeepDownconn {
		up.exitChannel <- true
	}

	for _, down := range up.downSessions {
		go down.SendEvent(EventExit{})
	}
}

func (up *FakeUpSession) sendSubmitResponse(sessionID uint32, id interface{}, status StratumStatus) {
	down, ok := up.downSessions[sessionID]
	if !ok {
		// 客户端已断开，忽略
		glog.Info("cannot find down session ", sessionID)
		return
	}
	go down.SendEvent(EventSubmitResponse{id, status})
}

func (up *FakeUpSession) handleSubmitShare(e EventSubmitShare) {
	up.sendSubmitResponse(uint32(e.Message.Base.SessionID), e.ID, STATUS_ACCEPT)
}

func (up *FakeUpSession) downSessionBroken(e EventDownSessionBroken) {
	delete(up.downSessions, e.SessionID)
}

func (up *FakeUpSession) updateFakeJob(e EventUpdateFakeJob) {
	up.fakeJob = &e.FakeJob
}

func (up *FakeUpSession) fakeNotifyTicker() {
	ticker := time.NewTicker(FakeJobNotifyInterval)
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

func (up *FakeUpSession) sendFakeNotify() {
	if up.fakeJob == nil || len(up.downSessions) < 1 {
		return
	}

	up.fakeJob.ToNewFakeJob()

	bytes, err := up.fakeJob.ToNotifyLine(false)
	if err != nil {
		glog.Warning("create notify bytes failed, ", err.Error(), ", fake job: ", up.fakeJob)
		return
	}

	for _, down := range up.downSessions {
		go down.SendEvent(EventSendBytes{bytes})
	}
}

func (up *FakeUpSession) handleEvent() {
	for {
		event := <-up.eventChannel

		switch e := event.(type) {
		case EventAddDownSession:
			up.addDownSession(e)
		case EventSubmitShare:
			up.handleSubmitShare(e)
		case EventDownSessionBroken:
			up.downSessionBroken(e)
		case EventTransferDownSessions:
			up.transferDownSessions()
		case EventUpdateFakeJob:
			up.updateFakeJob(e)
		case EventSendFakeNotify:
			up.sendFakeNotify()
		case EventExit:
			up.exit()
			return

		default:
			glog.Error("Unknown event: ", e)
		}
	}
}
