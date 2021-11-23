package main

// AuthorizeStat 认证状态
type AuthorizeStat uint8

const (
	StatConnected AuthorizeStat = iota
	StatSubScribed
	StatAuthorized
	StatDisconnected
	StatExit
)

const DownSessionChannelCache uint = 64
const UpSessionChannelCache uint = 512
const UpSessionManagerChannelCache uint = 64
const SessionManagerChannelCache uint = 64

const UpSessionDialTimeoutSeconds Seconds = 15
const UpSessionReadTimeoutSeconds Seconds = 60

const UpSessionUserAgent = "btccom-agent/2.0.0-mu"
const DefaultWorkerName = "__default__"
const DefaultIpWorkerNameFormat = "{1}x{2}x{3}x{4}"

// UpSessionNumPerSubAccount 每个子账户的矿池连接数量
const UpSessionNumPerSubAccount uint8 = 5

const (
	CapVersionRolling = "verrol" // ASICBoost version rolling
	CapSubmitResponse = "subres" // Send response of mining.submit
)

const DownSessionDisconnectWhenLostAsicboost = true
const UpSessionTLSInsecureSkipVerify = true

const FakeJobNotifyIntervalSeconds Seconds = 30
