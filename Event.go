package main

type EventType uint8

type EventApplicationExit struct{}

type EventUpStreamReady struct {
	Slot int
}

type EventAddStratumSession struct {
	Session *StratumSession
}
