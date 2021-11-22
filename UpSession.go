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
	id string // 打印日志用的连接标识符

	manager *UpSessionManager
	config  *Config
	slot    int

	subAccount string
	poolIndex  int

	downSessions    map[uint16]*DownSession
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
	up.downSessions = make(map[uint16]*DownSession)
	up.stat = StatDisconnected
	up.eventChannel = make(chan interface{}, UpSessionChannelCache)
	up.submitIDs = make(map[uint16]SubmitID)

	if !up.config.MultiUserMode {
		up.subAccount = config.Pools[poolIndex].SubAccount
	}

	return
}

func (up *UpSession) connect() (err error) {
	pool := up.config.Pools[up.poolIndex]

	url := fmt.Sprintf("%s:%d", pool.Host, pool.Port)
	if up.config.PoolUseTls {
		up.id = fmt.Sprintf("pool#%d <%s> [tls://%s] ", up.slot, up.subAccount, url)
		glog.Info(up.id, "connect to pool server...")

		up.serverConn, err = tls.DialWithDialer(&net.Dialer{Timeout: UpSessionDialTimeout}, "tcp", url, UpSessionTLSConf)
	} else {
		up.id = fmt.Sprintf("pool#%d <%s> [%s] ", up.slot, up.subAccount, url)
		glog.Info(up.id, "connect to pool server...")

		up.serverConn, err = net.DialTimeout("tcp", url, UpSessionDialTimeout)
	}
	if err != nil {
		return
	}

	up.id += fmt.Sprintf("(%s) ", up.serverConn.RemoteAddr().String())

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
	// send agent.get_capabilities first
	var capsRequest JSONRPCRequest
	capsRequest.ID = "caps"
	capsRequest.Method = "agent.get_capabilities"
	if up.config.SubmitResponseFromServer {
		capsRequest.SetParams(JSONRPCArray{CapVersionRolling, CapSubmitResponse})
	} else {
		capsRequest.SetParams(JSONRPCArray{CapVersionRolling})
	}
	_, err = up.writeJSONRequest(&capsRequest)
	if err != nil {
		return
	}

	// send configure request
	var request JSONRPCRequest
	request.ID = "conf"
	request.Method = "mining.configure"
	request.SetParams(JSONRPCArray{"version-rolling"}, JSONRPCObj{"version-rolling.mask": "ffffffff", "version-rolling.min-bit-count": 0})
	_, err = up.writeJSONRequest(&request)
	if err != nil {
		return
	}

	// send subscribe request
	request.ID = "sub"
	request.Method = "mining.subscribe"
	request.SetParams(UpSessionUserAgent)
	_, err = up.writeJSONRequest(&request)
	if err != nil {
		return
	}

	// send authorize request
	request.ID = "auth"
	request.Method = "mining.authorize"
	request.SetParams(up.subAccount, "")
	_, err = up.writeJSONRequest(&request)
	if err != nil {
		return
	}

	// send agent.get_capabilities again
	// fix subres (submit_response_from_server)
	// Subres negotiation must be sent after authentication, or sserver will not send the response.
	capsRequest.ID = "caps_again"
	_, err = up.writeJSONRequest(&capsRequest)
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

	if up.config.AlwaysKeepDownconn {
		if up.lastJob != nil {
			up.manager.SendEvent(EventUpdateFakeJob{up.lastJob})
		}
		for _, down := range up.downSessions {
			go up.manager.SendEvent(EventAddDownSession{down})
		}
	} else {
		for _, down := range up.downSessions {
			go down.SendEvent(EventExit{})
		}
	}

	up.eventLoopRunning = false
	up.stat = StatDisconnected
	up.serverConn.Close()
}

func (up *UpSession) Init() {
	err := up.connect()
	if err != nil {
		glog.Error(up.id, "failed to connect to pool server: ", err.Error())
		return
	}

	go up.handleResponse()

	err = up.sendInitRequest()
	if err != nil {
		glog.Error(up.id, "failed to send request to pool server: ", err.Error())
		up.close()
		return
	}

	up.handleEvent()
}

func (up *UpSession) handleSetVersionMask(rpcData *JSONRPCLine, jsonBytes []byte) {
	if !up.serverCapVersionRolling {
		// server doesn't support version rolling via BTCAgent
		return
	}

	up.rpcSetVersionMask = jsonBytes

	if len(rpcData.Params) > 0 {
		versionMaskHex, ok := rpcData.Params[0].(string)
		if !ok {
			glog.Error(up.id, "version mask is not a string: ", string(jsonBytes))
			return
		}
		versionMask, err := strconv.ParseUint(versionMaskHex, 16, 32)
		if err != nil {
			glog.Error(up.id, "version mask is not a hex: ", string(jsonBytes))
			return
		}
		up.versionMask = uint32(versionMask)

		if glog.V(1) {
			glog.Info(up.id, "AsicBoost via BTCAgent enabled, allowed version mask: ", versionMaskHex)
		}
	}

	e := EventSendBytes{up.rpcSetVersionMask}
	for _, down := range up.downSessions {
		if down.versionMask != 0 {
			go down.SendEvent(e)
		}
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
		glog.Error(up.id, "subscribe result is not an array: ", string(jsonBytes))
		up.close()
		return
	}
	if len(result) < 3 {
		glog.Error(up.id, "subscribe result missing items: ", string(jsonBytes))
		up.close()
		return
	}
	sessionIDHex, ok := result[1].(string)
	if !ok {
		glog.Error(up.id, "session id is not a string: ", string(jsonBytes))
		up.close()
		return
	}
	sessionID, err := strconv.ParseUint(sessionIDHex, 16, 32)
	if err != nil {
		glog.Error(up.id, "session id is not a hex: ", string(jsonBytes))
		up.close()
		return
	}
	up.sessionID = uint32(sessionID)

	extraNonce2SizeFloat, ok := result[2].(float64)
	if !ok {
		glog.Error(up.id, "extra nonce 2 size is not an integer: ", string(jsonBytes))
		up.close()
		return
	}
	up.extraNonce2Size = int(extraNonce2SizeFloat)
	if up.extraNonce2Size != 8 {
		glog.Error(up.id, "BTCAgent is not compatible with this server, extra nonce 2 should be 8 bytes but only ", up.extraNonce2Size, " bytes")
		up.close()
		return
	}
	up.stat = StatSubScribed
}

func (up *UpSession) handleConfigureResponse(rpcData *JSONRPCLine, jsonBytes []byte) {
	// ignore
}

func (up *UpSession) handleGetCapsResponse(rpcData *JSONRPCLine, jsonBytes []byte) {
	result, ok := rpcData.Result.(map[string]interface{})
	if !ok {
		glog.Error(up.id, "get server capabilities failed, result is not an object: ", string(jsonBytes))
	}
	caps, ok := result["capabilities"]
	if !ok {
		glog.Error(up.id, "get server capabilities failed, missing field capabilities: ", string(jsonBytes))
	}
	capsArr, ok := caps.([]interface{})
	if !ok {
		glog.Error(up.id, "get server capabilities failed, capabilities is not an array: ", string(jsonBytes))
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
		glog.Warning(up.id, "[WARNING] pool server does not support ASICBoost")
	}
	if up.config.SubmitResponseFromServer {
		if up.serverCapSubmitResponse {
			if glog.V(1) {
				glog.Info(up.id, "pool server will send share response to BTCAgent")
			}
		} else {
			glog.Warning(up.id, "[WARNING] pool server does not support sendding share response to BTCAgent")
		}
	}
}

func (up *UpSession) handleAuthorizeResponse(rpcData *JSONRPCLine, jsonBytes []byte) {
	result, ok := rpcData.Result.(bool)
	if !ok || !result {
		glog.Error(up.id, "authorize failed: ", rpcData.Error)
		up.close()
		return
	}
	glog.Info(up.id, "authorize success, session id: ", up.sessionID)
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
			glog.Error(up.id, "failed to read pool server response: ", err.Error())
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
		glog.Error(up.id, "failed to read ex-message header from pool server: ", err.Error())
		up.connBroken()
		return
	}
	if message.Size < 4 {
		glog.Warning(up.id, "broken ex-message header from pool server: ", message.ExMessageHeader)
		up.connBroken()
		return
	}

	size := message.Size - 4 // len 包括 header 的长度 4 字节，所以减 4
	if size > 0 {
		message.Body = make([]byte, size)
		_, err = io.ReadFull(up.serverReader, message.Body)
		if err != nil {
			glog.Error(up.id, "failed to read ex-message body from pool server: ", err.Error())
			up.connBroken()
			return
		}
	}

	up.SendEvent(EventRecvExMessage{message})
	up.serverConn.SetReadDeadline(time.Now().Add(UpSessionReadTimeout))
}

func (up *UpSession) readLine() {
	jsonBytes, err := up.serverReader.ReadBytes('\n')
	if err != nil {
		glog.Error(up.id, "failed to read JSON line from pool server: ", err.Error())
		up.connBroken()
		return
	}

	rpcData, err := NewJSONRPCLine(jsonBytes)

	// ignore the json decode error
	if err != nil {
		glog.Info(up.id, "failed to decode JSON line from pool server: ", err.Error(), "; ", string(jsonBytes))
		return
	}

	up.SendEvent(EventRecvJSONRPC{rpcData, jsonBytes})
	up.serverConn.SetReadDeadline(time.Now().Add(UpSessionReadTimeout))
}

func (up *UpSession) Run() {
	up.handleEvent()
}

func (up *UpSession) SendEvent(event interface{}) {
	up.eventChannel <- event
}

func (up *UpSession) addDownSession(e EventAddDownSession) {
	up.downSessions[e.Session.sessionID] = e.Session
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
			glog.Warning(up.id, "failed to convert job to JSON: ", err.Error(), "; ", up.lastJob)
		}
	}
}

func (up *UpSession) registerWorker(down *DownSession) {
	msg := ExMessageRegisterWorker{down.sessionID, down.clientAgent, down.workerName}
	_, err := up.serverConn.Write(msg.Serialize())
	if err != nil {
		glog.Error(up.id, "failed to register worker to pool server: ", err.Error())
		up.close()
	}
}

func (up *UpSession) unregisterWorker(sessionID uint16) {
	msg := ExMessageUnregisterWorker{sessionID}
	_, err := up.serverConn.Write(msg.Serialize())
	if err != nil {
		glog.Error(up.id, "failed to unregister worker from pool server: ", err.Error())
		up.close()
	}
}

func (up *UpSession) handleMiningNotify(rpcData *JSONRPCLine, jsonBytes []byte) {
	job, err := NewStratumJob(rpcData, up.sessionID)
	if err != nil {
		glog.Warning(up.id, err.Error(), ": ", string(jsonBytes))
		return
	}

	bytes, err := job.ToNotifyLine(false)
	if err != nil {
		glog.Warning(up.id, "failed to convert job to JSON: ", err.Error(), "; ", string(jsonBytes))
		return
	}

	for _, down := range up.downSessions {
		go down.SendEvent(EventSendBytes{bytes})
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
			glog.Info(up.id, "[TODO] pool request: ", rpcData)
		}
		return
	}

	switch rpcData.ID {
	case "caps":
		up.handleGetCapsResponse(rpcData, jsonBytes)
	case "conf":
		up.handleConfigureResponse(rpcData, jsonBytes)
	case "sub":
		up.handleSubScribeResponse(rpcData, jsonBytes)
	case "auth":
		up.handleAuthorizeResponse(rpcData, jsonBytes)
	case "caps_again":
		// ignore
	default:
		glog.Info(up.id, "[TODO] pool response: ", rpcData)
	}
}

func (up *UpSession) handleSubmitShare(e EventSubmitShare) {
	if e.Message.IsFakeJob {
		up.sendSubmitResponse(e.Message.Base.SessionID, e.ID, STATUS_ACCEPT)
		return
	}

	data := e.Message.Serialize()
	_, err := up.serverConn.Write(data)

	if up.config.SubmitResponseFromServer && up.serverCapSubmitResponse {
		up.submitIDs[up.submitIndex] = SubmitID{e.ID, e.Message.Base.SessionID}
		up.submitIndex++
	} else {
		up.sendSubmitResponse(e.Message.Base.SessionID, e.ID, STATUS_ACCEPT)
	}

	if err != nil {
		glog.Error(up.id, "failed to submit share: ", err.Error())
		up.close()
		return
	}
}

func (up *UpSession) sendSubmitResponse(sessionID uint16, id interface{}, status StratumStatus) {
	down, ok := up.downSessions[sessionID]
	if !ok {
		// 客户端已断开，忽略
		if glog.V(3) {
			glog.Info(up.id, "cannot find down session: ", sessionID)
		}
		return
	}
	go down.SendEvent(EventSubmitResponse{id, status})
}

func (up *UpSession) handleExMessageSubmitResponse(ex *ExMessage) {
	if !up.config.SubmitResponseFromServer || !up.serverCapSubmitResponse {
		glog.Error(up.id, "unexpected ex-message CMD_SUBMIT_RESPONSE from pool server")
		return
	}

	var msg ExMessageSubmitResponse
	err := msg.Unserialize(ex.Body)
	if err != nil {
		glog.Error(up.id, "failed to decode ex-message CMD_SUBMIT_RESPONSE: ", err.Error(), "; ", ex)
		return
	}

	submitID, ok := up.submitIDs[msg.Index]
	if !ok {
		glog.Error(up.id, "cannot find submit id ", msg.Index, " in ex-message CMD_SUBMIT_RESPONSE: ", msg)
		return
	}
	delete(up.submitIDs, msg.Index)

	up.sendSubmitResponse(submitID.SessionID, submitID.ID, msg.Status)
}

func (up *UpSession) handleExMessageMiningSetDiff(ex *ExMessage) {
	var msg ExMessageMiningSetDiff
	err := msg.Unserialize(ex.Body)
	if err != nil {
		glog.Error(up.id, "failed to decode ex-message CMD_MINING_SET_DIFF: ", err.Error(), "; ", ex)
		return
	}

	diff := uint64(1) << msg.Base.DiffExp

	var request JSONRPCRequest
	request.Method = "mining.set_difficulty"
	request.SetParams(diff)
	bytes, err := request.ToJSONBytesLine()
	if err != nil {
		glog.Error(up.id, "failed to convert mining.set_difficulty request to JSON: ", err.Error(), "; ", request)
		return
	}

	e := EventSendBytes{bytes}
	for _, sessionID := range msg.SessionIDs {
		down := up.downSessions[sessionID]
		if down != nil {
			go down.SendEvent(e)
		} else {
			// 客户端已断开，忽略
			if glog.V(3) {
				glog.Info(up.id, "cannot find down session: ", sessionID)
			}
		}
	}
}

func (up *UpSession) recvExMessage(e EventRecvExMessage) {
	switch e.Message.Type {
	case CMD_SUBMIT_RESPONSE:
		up.handleExMessageSubmitResponse(e.Message)
	case CMD_MINING_SET_DIFF:
		up.handleExMessageMiningSetDiff(e.Message)
	default:
		glog.Error(up.id, "unknown ex-message: ", e.Message)
	}
}

func (up *UpSession) downSessionBroken(e EventDownSessionBroken) {
	delete(up.downSessions, e.SessionID)
	up.unregisterWorker(e.SessionID)

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
		case EventAddDownSession:
			up.addDownSession(e)
		case EventSubmitShare:
			up.handleSubmitShare(e)
		case EventDownSessionBroken:
			up.downSessionBroken(e)
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
			glog.Error(up.id, "unknown event: ", e)
		}
	}
}
