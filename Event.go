package main

type EventType uint8

type EventApplicationExit struct{}

type EventInitFinished struct{}

type EventUpSessionReady struct {
	Slot    int
	Session *UpSession
}

type EventUpSessionInitFailed struct {
	Slot int
}

type EventAddStratumSession struct {
	Session *StratumSession
}

type EventConnBroken struct{}

type EventRecvExMessage struct {
	message *ExMessage
}

type EventRecvJSONRPC struct {
	rpcData   *JSONRPCLine
	jsonBytes []byte
}
