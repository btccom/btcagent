package main

type EventType uint8

type EventApplicationExit struct{}

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
