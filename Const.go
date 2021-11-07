package main

import "time"

// AuthorizeStat 认证状态
type AuthorizeStat uint8

const (
	StatConnecting AuthorizeStat = iota
	StatConnected
	StatSubScribed
	StatAuthorized
	StatDisconnected
)

const StratumSessionChannelCache = 128
const UpSessionChannelCache = 1024
const UpSessionManagerChannelCache = 128

const UpSessionDialTimeout = 15 * time.Second

const UpSessionUserAgent = "btccom-agent/2.0.0-mu"

// UpSessionNumPerSubAccount 每个子账户的矿池连接数量
const UpSessionNumPerSubAccount = 1

const (
	CapVersionRolling = "verrol" // ASICBoost version rolling
	CapSubmitResponse = "subres" // Send response of mining.submit
)
