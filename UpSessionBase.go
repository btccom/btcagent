package main

type SubmitID struct {
	ID        interface{}
	SessionID uint16
}

type UpSession interface {
	Stat() AuthorizeStat
	Init()
	Run()
	SendEvent(event interface{})
}
