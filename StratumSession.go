package main

import (
	"bufio"
	"fmt"
	"net"
	"strconv"
	"strings"

	"github.com/golang/glog"
)

type StratumSession struct {
	manager   *StratumSessionManager // 会话管理器
	upSession *UpSession             // 所属的服务器会话

	sessionID       uint32        // 会话ID
	clientConn      net.Conn      // 到矿机的TCP连接
	clientReader    *bufio.Reader // 读取矿机发送的内容
	readLoopRunning bool          // TCP读循环是否在运行
	stat            AuthorizeStat // 认证状态

	clientAgent    string // 挖矿软件名称
	fullName       string // 完整的矿工名
	subAccountName string // 子账户名部分
	workerName     string // 矿机名部分
	versionMask    uint32 // 比特币版本掩码(用于AsicBoost)

	eventLoopRunning bool             // 消息循环是否在运行
	eventChannel     chan interface{} // 消息通道

	versionRollingShareCounter uint64 // ASICBoost share 提交数量
}

// NewStratumSession 创建一个新的 Stratum 会话
func NewStratumSession(manager *StratumSessionManager, clientConn net.Conn, sessionID uint32) (session *StratumSession) {
	session = new(StratumSession)
	session.manager = manager
	session.sessionID = sessionID
	session.clientConn = clientConn
	session.clientReader = bufio.NewReader(clientConn)
	session.stat = StatConnected
	session.eventChannel = make(chan interface{}, StratumSessionChannelCache)

	glog.Info("miner connected, sessionId: ", sessionID, ", IP: ", session.IP())
	return
}

func (session *StratumSession) Init() {
	go session.handleRequest()
	session.handleEvent()
}

func (session *StratumSession) Run() {
	session.handleEvent()
}

func (session *StratumSession) close() {
	if session.upSession != nil && session.stat != StatExit {
		go session.upSession.SendEvent(EventStratumSessionBroken{session.sessionID})
	}

	session.eventLoopRunning = false
	session.stat = StatDisconnected
	session.clientConn.Close()

	// release session id
	session.manager.sessionIDManager.FreeSessionID(session.sessionID)
}

func (session *StratumSession) IP() string {
	return session.clientConn.RemoteAddr().String()
}

func (session *StratumSession) ID() string {
	if session.stat == StatAuthorized {
		return fmt.Sprintf("%s@%s", session.fullName, session.IP())
	}
	return session.IP()
}

func (session *StratumSession) writeJSONResponse(jsonData *JSONRPCResponse) (int, error) {
	bytes, err := jsonData.ToJSONBytesLine()
	if err != nil {
		return 0, err
	}
	return session.clientConn.Write(bytes)
}

func (session *StratumSession) stratumHandleRequest(request *JSONRPCLine, requestJSON []byte) (result interface{}, err *StratumError) {
	switch request.Method {
	case "mining.subscribe":
		if session.stat != StatConnected {
			err = StratumErrDuplicateSubscribed
			return
		}
		result, err = session.parseSubscribeRequest(request)
		if err == nil {
			session.stat = StatSubScribed
		}
		return

	case "mining.authorize":
		if session.stat != StatSubScribed {
			err = StratumErrNeedSubscribed
			return
		}
		result, err = session.parseAuthorizeRequest(request)
		if err == nil {
			session.stat = StatAuthorized
			// 让 Init() 函数返回
			session.eventLoopRunning = false

			glog.Info("miner authorized, session id: ", session.sessionID, ", IP: ", session.IP(), ", worker name: ", session.fullName)
		}
		return

	case "mining.configure":
		result, err = session.parseConfigureRequest(request)
		return

	case "mining.submit":
		result, err = session.parseMiningSubmit(request)
		if err != nil {
			glog.Warning("stratum error, IP: ", session.IP(), ", worker: ", session.fullName, ", error: ", err, ", submit: ", string(requestJSON))
		}
		return

	default:
		// ignore unimplemented methods
		glog.Warning("unknown request, IP: ", session.IP(), ", request: ", string(requestJSON))
		return
	}
}

func (session *StratumSession) parseMiningSubmit(request *JSONRPCLine) (result interface{}, err *StratumError) {
	// params:
	// [0] Worker Name
	// [1] Job ID
	// [2] ExtraNonce2
	// [3] Time
	// [4] Nonce
	// [5] Version Mask

	if len(request.Params) < 5 {
		err = StratumErrTooFewParams
		return
	}

	var msg ExMessageSubmitShare

	// [1] Job ID
	jobIDStr, ok := request.Params[1].(string)
	if !ok {
		err = StratumErrIllegalParams
		return
	}
	jobID, convErr := strconv.ParseUint(jobIDStr, 10, 8)
	if convErr != nil {
		err = StratumErrIllegalParams
		return
	}
	msg.Base.JobID = uint8(jobID)

	// [2] ExtraNonce2
	extraNonce2Hex, ok := request.Params[2].(string)
	if !ok {
		err = StratumErrIllegalParams
		return
	}
	extraNonce, convErr := strconv.ParseUint(extraNonce2Hex, 16, 32)
	if convErr != nil {
		err = StratumErrIllegalParams
		return
	}
	msg.Base.ExtraNonce2 = uint32(extraNonce)

	// [3] Time
	timeHex, ok := request.Params[3].(string)
	if !ok {
		err = StratumErrIllegalParams
		return
	}
	time, convErr := strconv.ParseUint(timeHex, 16, 32)
	if convErr != nil {
		err = StratumErrIllegalParams
		return
	}
	msg.Time = uint32(time)

	// [4] Nonce
	nonceHex, ok := request.Params[4].(string)
	if !ok {
		err = StratumErrIllegalParams
		return
	}
	nonce, convErr := strconv.ParseUint(nonceHex, 16, 32)
	if convErr != nil {
		err = StratumErrIllegalParams
		return
	}
	msg.Base.Nonce = uint32(nonce)

	// [5] Version Mask
	hasVersionMask := false
	if len(request.Params) >= 6 {
		versionMaskHex, ok := request.Params[5].(string)
		if !ok {
			err = StratumErrIllegalParams
			return
		}
		versionMask, convErr := strconv.ParseUint(versionMaskHex, 16, 32)
		if convErr != nil {
			err = StratumErrIllegalParams
			return
		}
		msg.VersionMask = uint32(versionMask)
		hasVersionMask = true
	}

	// session id
	msg.Base.SessionID = uint16(session.sessionID)

	var e EventSubmitShare
	e.ID = request.ID
	e.Message = &msg
	go session.upSession.SendEvent(e)

	// 如果 AsicBoost 丢失，就发送重连请求
	if session.manager.config.DisconnectWhenLostAsicboost {
		if hasVersionMask {
			session.versionRollingShareCounter++
		} else if session.versionRollingShareCounter > 100 {
			glog.Warning("AsicBoost disabled mid-way, send client.reconnect. worker: ", session.ID(), ", version rolling shares: ", session.versionRollingShareCounter)

			// send reconnect request to miner
			sendErr := session.sendReconnectRequest()
			if sendErr != nil {
				glog.Error("write reconnect request failed, IP: ", session.IP(), ", error: ", err.Error())
				session.close()
				return
			}
		}
	}
	return
}

func (session *StratumSession) sendReconnectRequest() (err error) {
	var reconnect JSONRPCRequest
	reconnect.Method = "client.reconnect"
	bytes, err := reconnect.ToJSONBytesLine()
	if err != nil {
		return
	}
	_, err = session.clientConn.Write(bytes)
	return
}

func (session *StratumSession) parseSubscribeRequest(request *JSONRPCLine) (result interface{}, err *StratumError) {

	if len(request.Params) >= 1 {
		session.clientAgent, _ = request.Params[0].(string)
	}

	sessionIDString := Uint32ToHex(session.sessionID)

	result = JSONRPCArray{JSONRPCArray{JSONRPCArray{"mining.set_difficulty", sessionIDString}, JSONRPCArray{"mining.notify", sessionIDString}}, sessionIDString, 4}
	return
}

func (session *StratumSession) parseAuthorizeRequest(request *JSONRPCLine) (result interface{}, err *StratumError) {
	if len(request.Params) < 1 {
		err = StratumErrTooFewParams
		return
	}

	fullWorkerName, ok := request.Params[0].(string)

	if !ok {
		err = StratumErrWorkerNameMustBeString
		return
	}

	// 矿工名
	session.fullName = FilterWorkerName(fullWorkerName)

	// 截取“.”之前的做为子账户名，“.”及之后的做矿机名
	pos := strings.IndexByte(session.fullName, '.')
	if pos >= 0 {
		session.subAccountName = session.fullName[:pos]
		session.workerName = session.fullName[pos+1:]
	} else {
		session.subAccountName = session.fullName
		session.workerName = ""
	}

	if len(session.manager.config.FixedWorkerName) > 0 {
		session.workerName = session.manager.config.FixedWorkerName
		session.fullName = session.subAccountName + "." + session.workerName
	} else if session.manager.config.UseIpAsWorkerName {
		session.workerName = IPAsWorkerName(session.manager.config.IpWorkerNameFormat, session.clientConn.RemoteAddr().String())
		session.fullName = session.subAccountName + "." + session.workerName
	}

	if len(session.subAccountName) < 1 {
		err = StratumErrSubAccountNameEmpty
		return
	}

	// 获取矿机名成功
	result = true
	err = nil
	return
}

func (session *StratumSession) parseConfigureRequest(request *JSONRPCLine) (result interface{}, err *StratumError) {
	// request:
	//		{"id":3,"method":"mining.configure","params":[["version-rolling"],{"version-rolling.mask":"1fffe000","version-rolling.min-bit-count":2}]}
	// response:
	//		{"id":3,"result":{"version-rolling":true,"version-rolling.mask":"1fffe000"},"error":null}
	//		{"id":null,"method":"mining.set_version_mask","params":["1fffe000"]}

	if len(request.Params) < 2 {
		err = StratumErrTooFewParams
		return
	}

	if options, ok := request.Params[1].(map[string]interface{}); ok {
		if obj, ok := options["version-rolling.mask"]; ok {
			if versionMaskStr, ok := obj.(string); ok {
				versionMask, err := strconv.ParseUint(versionMaskStr, 16, 32)
				if err == nil {
					session.versionMask = uint32(versionMask)
				}
			}
		}
	}

	if session.versionMask != 0 {
		// 这里响应的是虚假的版本掩码。在连接服务器后将通过 mining.set_version_mask
		// 更新为真实的版本掩码。
		result = JSONRPCObj{
			"version-rolling":      true,
			"version-rolling.mask": session.versionMaskStr()}
		return
	}

	// 未知配置内容，不响应
	return
}

func (session *StratumSession) versionMaskStr() string {
	return fmt.Sprintf("%08x", session.versionMask)
}

func (session *StratumSession) SetUpSession(upSession *UpSession) {
	session.upSession = upSession
}

func (session *StratumSession) handleRequest() {
	session.readLoopRunning = true

	for session.readLoopRunning {
		jsonBytes, err := session.clientReader.ReadBytes('\n')

		if err != nil {
			glog.Error("read line failed, IP: ", session.IP(), ", error: ", err.Error())
			session.connBroken()
			return
		}

		rpcData, err := NewJSONRPCLine(jsonBytes)

		// ignore the json decode error
		if err != nil {
			glog.Warning("JSON decode failed, IP: ", session.IP(), err.Error(), string(jsonBytes))
		}

		session.SendEvent(EventRecvJSONRPC{rpcData, jsonBytes})
	}
}

func (session *StratumSession) recvJSONRPC(e EventRecvJSONRPC) {
	// stat will be changed in stratumHandleRequest
	result, stratumErr := session.stratumHandleRequest(e.RPCData, e.JSONBytes)

	// 两个均为空说明没有想要返回的响应
	if result != nil || stratumErr != nil {
		var response JSONRPCResponse
		response.ID = e.RPCData.ID
		response.Result = result
		response.Error = stratumErr.ToJSONRPCArray(nil)

		_, err := session.writeJSONResponse(&response)

		if err != nil {
			glog.Error("write JSON response failed, IP: ", session.IP(), ", error: ", err.Error())
			session.close()
			return
		}
	}
}

func (session *StratumSession) SendEvent(event interface{}) {
	session.eventChannel <- event
}

func (session *StratumSession) connBroken() {
	session.readLoopRunning = false
	session.SendEvent(EventConnBroken{})
}

func (session *StratumSession) sendBytes(e EventSendBytes) {
	_, err := session.clientConn.Write(e.Content)
	if err != nil {
		glog.Error("write bytes failed, IP: ", session.IP(), ", error: ", err.Error())
		session.close()
	}
}

func (session *StratumSession) submitResponse(e EventSubmitResponse) {
	var response JSONRPCResponse
	response.ID = e.ID
	if e.Status.IsAccepted() {
		response.Result = true
	} else {
		response.Error = e.Status.ToJSONRPCArray(nil)
	}

	_, err := session.writeJSONResponse(&response)
	if err != nil {
		glog.Error("write submit response failed, IP: ", session.IP(), ", error: ", err.Error())
		session.close()
	}
}

func (session *StratumSession) exit() {
	session.stat = StatExit
	session.sendReconnectRequest()
	session.close()
}

func (session *StratumSession) handleEvent() {
	session.eventLoopRunning = true
	for session.eventLoopRunning {
		event := <-session.eventChannel

		switch e := event.(type) {
		case EventRecvJSONRPC:
			session.recvJSONRPC(e)
		case EventSendBytes:
			session.sendBytes(e)
		case EventSubmitResponse:
			session.submitResponse(e)
		case EventConnBroken:
			session.close()
		case EventExit:
			session.exit()
		default:
			glog.Error("Unknown event: ", e)
		}
	}
}
