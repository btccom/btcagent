package main

import (
	"bufio"
	"net"
)

type EventType uint8

type EventExit struct{}

type EventInitFinished struct{}

type EventUpSessionReady struct {
	Slot    int
	Session UpSession
}

type EventUpSessionInitFailed struct {
	Slot int
}

type EventSetUpSession struct {
	Session EventInterface
}

type EventAddDownSession struct {
	Session DownSession
}

type EventConnBroken struct{}

type EventRecvExMessage struct {
	Message *ExMessage
}

type EventRecvJSONRPCBTC struct {
	RPCData   *JSONRPCLineBTC
	JSONBytes []byte
}

type EventRecvJSONRPCETH struct {
	RPCData   *JSONRPCLineETH
	JSONBytes []byte
}

type EventSendBytes struct {
	Content []byte
}

type EventDownSessionBroken struct {
	SessionID uint16
}

type EventUpSessionBroken struct {
	Slot int
}

type EventSubmitShareBTC struct {
	ID      interface{}
	Message *ExMessageSubmitShareBTC
}

type EventSubmitShareETH struct {
	ID      interface{}
	Message *ExMessageSubmitShareETH
}

type EventSubmitResponse struct {
	ID     interface{}
	Status StratumStatus
}

type EventUpdateMinerNum struct {
	Slot                     int
	DisconnectedMinerCounter int
}

type EventUpdateFakeMinerNum struct {
	DisconnectedMinerCounter int
}

type EventSendUpdateMinerNum struct{}

type EventPrintMinerNum struct{}

type EventStopUpSessionManager struct {
	SubAccount string
}

type EventUpdateFakeJobBTC struct {
	FakeJob *StratumJobBTC
}

type EventUpdateFakeJobETH struct {
	FakeJob *StratumJobETH
}

type EventTransferDownSessions struct{}

type EventSendFakeNotify struct{}

type EventUpSessionConnection struct {
	ProxyURL string
	Conn     net.Conn
	Reader   *bufio.Reader
	Error    error
}

type EventSetDifficulty struct {
	Difficulty uint64
}

type EventSetExtraNonce struct {
	ExtraNonce uint32
}

type EventStratumJobETH struct {
	Job *StratumJobETH
}
