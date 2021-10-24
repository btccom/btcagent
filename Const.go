package main

// AuthorizeStat 认证状态
type AuthorizeStat uint8

const (
	StatConnecting AuthorizeStat = iota
	StatConnected
	StatSubScribed
	StatAuthorized
	StatDisconnected
)
