package main

type EventType uint8

type EventExit struct{}

type EventInitFinished struct{}

type EventUpSessionReady struct {
	Slot    int
	Session *UpSession
}

type EventUpSessionInitFailed struct {
	Slot int
}

type EventSetUpSession struct {
	Session EventInterface
}

type EventAddStratumSession struct {
	Session *StratumSession
}

type EventConnBroken struct{}

type EventRecvExMessage struct {
	Message *ExMessage
}

type EventRecvJSONRPC struct {
	RPCData   *JSONRPCLine
	JSONBytes []byte
}

type EventSendBytes struct {
	Content []byte
}

type EventStratumSessionBroken struct {
	SessionID uint32
}

type EventUpSessionBroken struct {
	Slot int
}

type EventSubmitShare struct {
	ID      interface{}
	Message *ExMessageSubmitShare
}

type EventSubmitResponse struct {
	ID     interface{}
	Status StratumStatus
}

type EventUpdateMinerNum struct {
	Slot                     int
	DisconnectedMinerCounter int
}

type EventSendUpdateMinerNum struct{}

type EventStopUpSessionManager struct {
	SubAccount string
}

type EventUpdateFakeJob struct {
	FakeJob StratumJob
}

type EventTransferStratumSessions struct{}

type EventSendFakeNotify struct{}
