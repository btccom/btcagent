package main

import (
	"bufio"
	"crypto/tls"
	"encoding/binary"
	"fmt"
	"io"
	"net"
	"strconv"
	"time"

	"github.com/golang/glog"
)

type SubmitID struct {
	ID        interface{}
	SessionID uint16
}

type UpSession struct {
	manager *UpSessionManager
	config  *Config
	slot    int

	subAccount string
	poolIndex  int

	stratumSessions map[uint32]*StratumSession
	serverConn      net.Conn
	serverReader    *bufio.Reader
	readLoopRunning bool

	stat            AuthorizeStat
	sessionID       uint32
	versionMask     uint32
	extraNonce2Size int

	serverCapVersionRolling bool
	serverCapSubmitResponse bool

	eventLoopRunning bool
	eventChannel     chan interface{}

	lastJob           *StratumJob
	rpcSetVersionMask []byte
	rpcSetDifficulty  []byte

	submitIDs   map[uint16]SubmitID
	submitIndex uint16

	// 用于统计断开连接的矿机数，并同步给 UpSessionManager
	disconnectedMinerCounter int
}

func NewUpSession(manager *UpSessionManager, config *Config, subAccount string, poolIndex int, slot int) (up *UpSession) {
	up = new(UpSession)
	up.manager = manager
	up.config = config
	up.slot = slot
	up.subAccount = subAccount
	up.poolIndex = poolIndex
	up.stratumSessions = make(map[uint32]*StratumSession)
	up.stat = StatDisconnected
	up.eventChannel = make(chan interface{}, UpSessionChannelCache)
	up.submitIDs = make(map[uint16]SubmitID)
	return
}

func (up *UpSession) connect() (err error) {
	pool := up.config.Pools[up.poolIndex]

	url := fmt.Sprintf("%s:%d", pool.Host, pool.Port)
	if up.config.PoolUseTls {
		up.serverConn, err = tls.DialWithDialer(&net.Dialer{Timeout: UpSessionDialTimeout}, "tcp", url, UpSessionTLSConf)
	} else {
		up.serverConn, err = net.DialTimeout("tcp", url, UpSessionDialTimeout)
	}
	if err != nil {
		return
	}

	up.serverReader = bufio.NewReader(up.serverConn)
	up.stat = StatConnected
	return
}

func (up *UpSession) writeJSONRequest(jsonData *JSONRPCRequest) (int, error) {
	bytes, err := jsonData.ToJSONBytesLine()
	if err != nil {
		return 0, err
	}
	return up.serverConn.Write(bytes)
}

func (up *UpSession) sendInitRequest() (err error) {
	var request JSONRPCRequest

	request.ID = "sub"
	request.Method = "mining.subscribe"
	request.SetParams(UpSessionUserAgent)
	up.writeJSONRequest(&request)
	if err != nil {
		return
	}

	request.ID = "conf"
	request.Method = "mining.configure"
	request.SetParams(JSONRPCArray{"version-rolling"}, JSONRPCObj{"version-rolling.mask": "ffffffff", "version-rolling.min-bit-count": 0})
	up.writeJSONRequest(&request)
	if err != nil {
		return
	}

	// send agent.get_capabilities first
	request.ID = "caps"
	request.Method = "agent.get_capabilities"
	if up.config.SubmitResponseFromServer {
		request.SetParams(JSONRPCArray{CapVersionRolling, CapSubmitResponse})
	} else {
		request.SetParams(JSONRPCArray{CapVersionRolling})
	}
	up.writeJSONRequest(&request)
	if err != nil {
		return
	}
	capsRequestAgain := request

	request.ID = "auth"
	request.Method = "mining.authorize"
	request.SetParams(up.subAccount, "")
	up.writeJSONRequest(&request)
	if err != nil {
		return
	}

	// send agent.get_capabilities again
	// fix subres (submit_response_from_server)
	// Subres negotiation must be sent after authentication, or sserver will not send the response.
	up.writeJSONRequest(&capsRequestAgain)
	if err != nil {
		return
	}

	return
}

func (up *UpSession) exit() {
	up.stat = StatExit
	up.close()
}

func (up *UpSession) close() {
	if up.stat == StatAuthorized {
		up.manager.SendEvent(EventUpSessionBroken{up.slot})
	}

	for _, session := range up.stratumSessions {
		go session.SendEvent(EventExit{})
	}

	up.eventLoopRunning = false
	up.stat = StatDisconnected
	up.serverConn.Close()
}

func (up *UpSession) IP() string {
	if up.stat == StatDisconnected {
		pool := up.config.Pools[up.poolIndex]
		return fmt.Sprintf("%s:%d", pool.Host, pool.Port)
	}
	return up.serverConn.RemoteAddr().String()
}

func (up *UpSession) Init() {
	err := up.connect()
	if err != nil {
		glog.Error("connect failed, server: ", up.IP(), ", error: ", err.Error())
		return
	}

	go up.handleResponse()

	err = up.sendInitRequest()
	if err != nil {
		glog.Error("write JSON request failed, server: ", up.IP(), ", error: ", err.Error())
		up.close()
		return
	}

	up.handleEvent()
}

func (up *UpSession) handleSetVersionMask(rpcData *JSONRPCLine, jsonBytes []byte) {
	up.rpcSetVersionMask = jsonBytes

	if len(rpcData.Params) > 0 {
		versionMaskHex, ok := rpcData.Params[0].(string)
		if !ok {
			glog.Error("version mask is not a string, server: ", up.IP(), ", response: ", string(jsonBytes))
			return
		}
		versionMask, err := strconv.ParseUint(versionMaskHex, 16, 32)
		if err != nil {
			glog.Error("version mask is not a hex, server: ", up.IP(), ", response: ", string(jsonBytes))
			return
		}
		up.versionMask = uint32(versionMask)
		glog.Info("version mask update, server: ", up.IP(), ", version mask: ", versionMaskHex)
	}

	e := EventSendBytes{up.rpcSetVersionMask}
	for _, session := range up.stratumSessions {
		go session.SendEvent(e)
	}
}

func (up *UpSession) handleSetDifficulty(rpcData *JSONRPCLine, jsonBytes []byte) {
	if up.rpcSetDifficulty == nil {
		up.rpcSetDifficulty = jsonBytes
	}
}

func (up *UpSession) handleSubScribeResponse(rpcData *JSONRPCLine, jsonBytes []byte) {
	result, ok := rpcData.Result.([]interface{})
	if !ok {
		glog.Error("subscribe result is not an array, server: ", up.IP(), ", response: ", string(jsonBytes))
		up.close()
		return
	}
	if len(result) < 3 {
		glog.Error("subscribe result missing items, server: ", up.IP(), ", response: ", string(jsonBytes))
		up.close()
		return
	}
	sessionIDHex, ok := result[1].(string)
	if !ok {
		glog.Error("session id is not a string, server: ", up.IP(), ", response: ", string(jsonBytes))
		up.close()
		return
	}
	sessionID, err := strconv.ParseUint(sessionIDHex, 16, 32)
	if err != nil {
		glog.Error("session id is not a hex, server: ", up.IP(), ", response: ", string(jsonBytes))
		up.close()
		return
	}
	up.sessionID = uint32(sessionID)

	extraNonce2SizeFloat, ok := result[2].(float64)
	if !ok {
		glog.Error("extra nonce 2 size is not an integer, server: ", up.IP(), ", response: ", string(jsonBytes))
		up.close()
		return
	}
	up.extraNonce2Size = int(extraNonce2SizeFloat)
	if up.extraNonce2Size < 6 {
		glog.Error("BTCAgent is not compatible with this server: ", up.IP(), ", extra nonce 2 is too short (only ", up.extraNonce2Size, " bytes), should be at least 6 bytes")
		up.close()
		return
	}
	up.stat = StatSubScribed
}

func (up *UpSession) handleConfigureResponse(rpcData *JSONRPCLine, jsonBytes []byte) {
	//glog.Info("TODO: finish handleConfigureResponse, ", string(jsonBytes))
	// ignore
}

func (up *UpSession) handleGetCapsResponse(rpcData *JSONRPCLine, jsonBytes []byte) {
	result, ok := rpcData.Result.(map[string]interface{})
	if !ok {
		glog.Error("get server capabilities failed, result is not an object, server: ", up.IP(), ", response: ", string(jsonBytes))
	}
	caps, ok := result["capabilities"]
	if !ok {
		glog.Error("get server capabilities failed, missing field capabilities, server: ", up.IP(), ", response: ", string(jsonBytes))
	}
	capsArr, ok := caps.([]interface{})
	if !ok {
		glog.Error("get server capabilities failed, capabilities is not an array, server: ", up.IP(), ", response: ", string(jsonBytes))
	}
	for _, capability := range capsArr {
		switch capability {
		case CapVersionRolling:
			up.serverCapVersionRolling = true
		case CapSubmitResponse:
			up.serverCapSubmitResponse = true
		}
	}
	if !up.serverCapVersionRolling {
		glog.Warning("[WARNING] pool server ", up.IP(), " does not support ASICBoost")
	}
	if up.config.SubmitResponseFromServer && !up.serverCapSubmitResponse {
		glog.Warning("[WARNING] pool server does not support sendding response to BTCAgent")
	}
}

func (up *UpSession) handleAuthorizeResponse(rpcData *JSONRPCLine, jsonBytes []byte) {
	result, ok := rpcData.Result.(bool)
	if !ok || !result {
		glog.Error("authorize failed, server: ", up.IP(), ", sub-account: ", up.subAccount, ", error: ", rpcData.Error)
		up.close()
		return
	}
	glog.Info("authorize success, server: ", up.IP(), ", sub-account: ", up.subAccount)
	up.stat = StatAuthorized
	// 让 Init() 函数返回
	up.eventLoopRunning = false
}

func (up *UpSession) connBroken() {
	up.readLoopRunning = false
	up.SendEvent(EventConnBroken{})
}

func (up *UpSession) handleResponse() {
	up.readLoopRunning = true
	for up.readLoopRunning {
		magicNum, err := up.serverReader.Peek(1)
		if err != nil {
			glog.Error("peek failed, server: ", up.IP(), ", error: ", err.Error())
			up.connBroken()
			return
		}
		if magicNum[0] == ExMessageMagicNumber {
			up.readExMessage()
		} else {
			up.readLine()
		}
	}
}

func (up *UpSession) readExMessage() {
	// ex-message:
	//   magic_number	uint8_t		magic number for Ex-Message, always 0x7F
	//   type/cmd		uint8_t		message type
	//   length			uint16_t	message length (include header self)
	//   message_body	uint8_t[]	message body
	message := new(ExMessage)
	err := binary.Read(up.serverReader, binary.LittleEndian, &message.ExMessageHeader)
	if err != nil {
		glog.Error("read ex-message failed, server: ", up.IP(), ", error: ", err.Error())
		up.connBroken()
		return
	}
	if message.Size < 4 {
		glog.Warning("Broken ex-message header from server: ", up.IP(), ", content: ", message.ExMessageHeader)
		up.connBroken()
		return
	}

	size := message.Size - 4 // len 包括 header 的长度 4 字节，所以减 4
	if size > 0 {
		message.Body = make([]byte, size)
		_, err = io.ReadFull(up.serverReader, message.Body)
		if err != nil {
			glog.Error("read ex-message failed, server: ", up.IP(), ", error: ", err.Error())
			up.connBroken()
			return
		}
	}

	up.SendEvent(EventRecvExMessage{message})
}

func (up *UpSession) readLine() {
	jsonBytes, err := up.serverReader.ReadBytes('\n')
	if err != nil {
		glog.Error("read line failed, server: ", up.IP(), ", error: ", err.Error())
		up.connBroken()
		return
	}

	rpcData, err := NewJSONRPCLine(jsonBytes)

	// ignore the json decode error
	if err != nil {
		glog.Info("JSON decode failed, server: ", up.IP(), err.Error(), string(jsonBytes))
		return
	}

	up.SendEvent(EventRecvJSONRPC{rpcData, jsonBytes})
}

func (up *UpSession) Run() {
	up.handleEvent()
}

func (up *UpSession) SendEvent(event interface{}) {
	up.eventChannel <- event
}

func (up *UpSession) addStratumSession(e EventAddStratumSession) {
	up.stratumSessions[e.Session.sessionID] = e.Session
	up.registerWorker(e.Session)

	if up.rpcSetVersionMask != nil && e.Session.versionMask != 0 {
		e.Session.SendEvent(EventSendBytes{up.rpcSetVersionMask})
	}

	if up.rpcSetDifficulty != nil {
		e.Session.SendEvent(EventSendBytes{up.rpcSetDifficulty})
	}

	if up.lastJob != nil {
		bytes, err := up.lastJob.ToNotifyLine(true)
		if err == nil {
			e.Session.SendEvent(EventSendBytes{bytes})
		} else {
			glog.Warning("create notify bytes failed, ", err.Error(), ", struct: ", up.lastJob)
		}
	}
}

func (up *UpSession) registerWorker(session *StratumSession) {
	msg := ExMessageRegisterWorker{uint16(session.sessionID), session.clientAgent, session.workerName}
	_, err := up.serverConn.Write(msg.Serialize())
	if err != nil {
		glog.Error("register worker to server failed, server: ", up.IP(), ", error: ", err.Error())
		up.close()
	}
}

func (up *UpSession) handleMiningNotify(rpcData *JSONRPCLine, jsonBytes []byte) {
	job, err := NewStratumJob(rpcData, up.sessionID)
	if err != nil {
		glog.Warning(err.Error(), ": ", string(jsonBytes))
		return
	}

	bytes, err := job.ToNotifyLine(false)
	if err != nil {
		glog.Warning("create notify bytes failed, ", err.Error(), ", content: ", string(jsonBytes))
		return
	}

	for _, session := range up.stratumSessions {
		go session.SendEvent(EventSendBytes{bytes})
	}

	up.lastJob = job
}

func (up *UpSession) recvJSONRPC(e EventRecvJSONRPC) {
	rpcData := e.RPCData
	jsonBytes := e.JSONBytes

	if len(rpcData.Method) > 0 {
		switch rpcData.Method {
		case "mining.set_version_mask":
			up.handleSetVersionMask(rpcData, jsonBytes)
		case "mining.set_difficulty":
			up.handleSetDifficulty(rpcData, jsonBytes)
		case "mining.notify":
			up.handleMiningNotify(rpcData, jsonBytes)
		default:
			glog.Info("[TODO] pool request: ", rpcData)
		}
		return
	}

	switch rpcData.ID {
	case "sub":
		up.handleSubScribeResponse(rpcData, jsonBytes)
	case "conf":
		up.handleConfigureResponse(rpcData, jsonBytes)
	case "caps":
		up.handleGetCapsResponse(rpcData, jsonBytes)
	case "auth":
		up.handleAuthorizeResponse(rpcData, jsonBytes)
	default:
		glog.Info("[TODO] pool response: ", rpcData)
	}
}

func (up *UpSession) handleSubmitShare(e EventSubmitShare) {
	if e.Message.IsFakeJob {
		up.sendSubmitResponse(uint32(e.Message.Base.SessionID), e.ID, STATUS_ACCEPT)
		return
	}

	data := e.Message.Serialize()
	_, err := up.serverConn.Write(data)

	if up.config.SubmitResponseFromServer && up.serverCapSubmitResponse {
		up.submitIDs[up.submitIndex] = SubmitID{e.ID, e.Message.Base.SessionID}
		up.submitIndex++
	} else {
		up.sendSubmitResponse(uint32(e.Message.Base.SessionID), e.ID, STATUS_ACCEPT)
	}

	if err != nil {
		glog.Error("submit share failed, server: ", up.IP(), ", error: ", err.Error())
		up.close()
		return
	}
}

func (up *UpSession) sendSubmitResponse(sessionID uint32, id interface{}, status StratumStatus) {
	session, ok := up.stratumSessions[sessionID]
	if !ok {
		// 客户端已断开，忽略
		glog.Info("cannot find session ", sessionID)
		return
	}
	go session.SendEvent(EventSubmitResponse{id, status})
}

func (up *UpSession) handleExMessageSubmitResponse(ex *ExMessage) {
	if !up.config.SubmitResponseFromServer || !up.serverCapSubmitResponse {
		glog.Error("unexpected ex-message CMD_SUBMIT_RESPONSE, server: ", up.IP())
		return
	}

	var msg ExMessageSubmitResponse
	err := msg.Unserialize(ex.Body)
	if err != nil {
		glog.Error("decode ex-message failed, server: ", up.IP(), ", error: ", err.Error(), ", ex-message: ", ex)
		return
	}

	submitID, ok := up.submitIDs[msg.Index]
	if !ok {
		glog.Error("cannot find submit id ", msg.Index, " in ex-message CMD_SUBMIT_RESPONSE, server: ", up.IP(), ", ex-message: ", msg)
		return
	}
	delete(up.submitIDs, msg.Index)

	up.sendSubmitResponse(uint32(submitID.SessionID), submitID.ID, msg.Status)
}

func (up *UpSession) recvExMessage(e EventRecvExMessage) {
	switch e.Message.Type {
	case CMD_SUBMIT_RESPONSE:
		up.handleExMessageSubmitResponse(e.Message)
	default:
		glog.Error("Unknown ex-message: ", e.Message)
	}
}

func (up *UpSession) stratumSessionBroken(e EventStratumSessionBroken) {
	delete(up.stratumSessions, e.SessionID)

	if up.disconnectedMinerCounter == 0 {
		go func() {
			time.Sleep(1 * time.Second)
			up.SendEvent(EventSendUpdateMinerNum{})
		}()
	}
	up.disconnectedMinerCounter++
}

func (up *UpSession) sendUpdateMinerNum() {
	go up.manager.SendEvent(EventUpdateMinerNum{up.slot, up.disconnectedMinerCounter})
	up.disconnectedMinerCounter = 0
}

func (up *UpSession) handleEvent() {
	up.eventLoopRunning = true
	for up.eventLoopRunning {
		event := <-up.eventChannel

		switch e := event.(type) {
		case EventAddStratumSession:
			up.addStratumSession(e)
		case EventSubmitShare:
			up.handleSubmitShare(e)
		case EventStratumSessionBroken:
			up.stratumSessionBroken(e)
		case EventSendUpdateMinerNum:
			up.sendUpdateMinerNum()
		case EventRecvJSONRPC:
			up.recvJSONRPC(e)
		case EventRecvExMessage:
			up.recvExMessage(e)
		case EventConnBroken:
			up.close()
		case EventExit:
			up.exit()
		default:
			glog.Error("Unknown event: ", e)
		}
	}
}
