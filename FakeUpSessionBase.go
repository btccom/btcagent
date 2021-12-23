package main

type FakeUpSession interface {
	Run()
	SendEvent(event interface{})
}
