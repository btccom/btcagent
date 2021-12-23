package main

type DownSession interface {
	SessionID() uint16
	SubAccountName() string
	Stat() AuthorizeStat
	Init()
	Run()
	SendEvent(event interface{})
}
