package main

type EventInterface interface {
	SendEvent(e interface{})
}
