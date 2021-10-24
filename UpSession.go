package main

import (
	"bufio"
	"fmt"
	"net"

	"github.com/golang/glog"
)

type UpSession struct {
	subAccountName string
	configData     *ConfigData
	serverConn     net.Conn
	serverReader   *bufio.Reader
	stat           AuthorizeStat
	versionMask    uint32

	poolIndex int
}

func NewUpSession(subAccountName string, configData *ConfigData) (up *UpSession) {
	up = new(UpSession)
	up.subAccountName = subAccountName
	up.configData = configData
	up.poolIndex = 0
	up.stat = StatDisconnected
	up.versionMask = 0
	return
}

func (up *UpSession) Connect() (err error) {
	up.stat = StatConnecting

	pool := up.configData.Pools[up.poolIndex]
	up.poolIndex++

	url := fmt.Sprintf("%s:%d", pool.Host, pool.Port)
	up.serverConn, err = net.DialTimeout("tcp", url, UpSessionDialTimeout)
	if err != nil {
		up.stat = StatDisconnected
		return
	}

	up.serverReader = bufio.NewReader(up.serverConn)
	up.stat = StatConnected
	return
}

func (up *UpSession) writeJSONRequestToServer(jsonData *JSONRPCRequest) (int, error) {
	if up.stat == StatConnecting || up.stat == StatDisconnected {
		return 0, ErrConnectionClosed
	}

	bytes, err := jsonData.ToJSONBytesLine()
	if err != nil {
		return 0, err
	}

	return up.serverConn.Write(bytes)
}

func (up *UpSession) SendRequest() (err error) {
	var request JSONRPCRequest

	request.ID = "sub"
	request.Method = "mining.subscribe"
	request.SetParam(UpSessionUserAgent)
	up.writeJSONRequestToServer(&request)
	if err != nil {
		return
	}

	request.ID = "conf"
	request.Method = "mining.configure"
	request.SetParam(JSONRPCArray{"version-rolling"}, JSONRPCObj{"version-rolling.mask": "ffffffff", "version-rolling.min-bit-count": 0})
	up.writeJSONRequestToServer(&request)
	if err != nil {
		return
	}

	request.ID = "caps"
	request.Method = "agent.get_capabilities"
	request.SetParam(JSONRPCArray{"verrol" /*version rolling*/})
	up.writeJSONRequestToServer(&request)
	if err != nil {
		return
	}

	request.ID = "auth"
	request.Method = "mining.authorize"
	request.SetParam(up.subAccountName, "")
	up.writeJSONRequestToServer(&request)
	if err != nil {
		return
	}

	return
}

func (up *UpSession) close() {
	up.stat = StatDisconnected
	up.serverConn.Close()
}

func (up *UpSession) IP() string {
	return up.serverConn.RemoteAddr().String()
}

func (up *UpSession) handleResponse() {
	magicNum, err := up.serverReader.Peek(1)
	if err != nil {
		glog.Error("peek failed, server: ", up.IP(), ", error: ", err.Error())
		up.close()
		return
	}
	if magicNum[0] == ExMessageMagicNumber {
		up.readExMessage()
	} else {
		up.readLine()
	}
}

func (up *UpSession) readExMessage() {
	glog.Info("readExMessage: stub")
}

func (up *UpSession) readLine() {
	jsonBytes, err := up.serverReader.ReadBytes('\n')
	if err != nil {
		glog.Error("read line failed, server: ", up.IP(), ", error: ", err.Error())
		up.close()
		return
	}

	rpcData, err := NewJSONRPCLine(jsonBytes)

	// ignore the json decode error
	if err != nil {
		glog.Info("JSON decode failed, server: ", up.IP(), err.Error(), string(jsonBytes))
		return
	}

	glog.Info("readLine: ", rpcData)
}

func (up *UpSession) Run() {
	err := up.Connect()
	if err != nil {
		glog.Error("connect failed, server: ", up.IP(), ", error: ", err.Error())
		return
	}

	err = up.SendRequest()
	if err != nil {
		glog.Error("write JSON request failed, server: ", up.IP(), ", error: ", err.Error())
		up.close()
		return
	}

	for up.stat != StatAuthorized && up.stat != StatDisconnected && up.stat != StatConnecting {
		up.handleResponse()
	}
}
