package main

import (
	"crypto/tls"
	"time"
)

// AuthorizeStat 认证状态
type AuthorizeStat uint8

const (
	StatConnected AuthorizeStat = iota
	StatSubScribed
	StatAuthorized
	StatDisconnected
	StatExit
)

const DownSessionChannelCache = 64
const UpSessionChannelCache = 512
const UpSessionManagerChannelCache = 64
const SessionManagerChannelCache = 64

const UpSessionDialTimeout = 15 * time.Second
const UpSessionReadTimeout = 60 * time.Second

const UpSessionUserAgent = "btccom-agent/2.0.0-mu"
const DefaultWorkerName = "__default__"

// UpSessionNumPerSubAccount 每个子账户的矿池连接数量
const UpSessionNumPerSubAccount = 5

const (
	CapVersionRolling = "verrol" // ASICBoost version rolling
	CapSubmitResponse = "subres" // Send response of mining.submit
)

var UpSessionTLSConf = &tls.Config{
	InsecureSkipVerify: true}

const FakeJobNotifyInterval = 30 * time.Second
