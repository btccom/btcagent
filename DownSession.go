package main

import (
	"bufio"
	"fmt"
	"net"
	"strconv"
	"strings"

	"github.com/golang/glog"
)

type DownSession struct {
	manager   *SessionManager // 会话管理器
	upSession EventInterface  // 所属的服务器会话

	sessionID       uint16        // 会话ID
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

// NewDownSession 创建一个新的 Stratum 会话
func NewDownSession(manager *SessionManager, clientConn net.Conn, sessionID uint16) (down *DownSession) {
	down = new(DownSession)
	down.manager = manager
	down.sessionID = sessionID
	down.clientConn = clientConn
	down.clientReader = bufio.NewReader(clientConn)
	down.stat = StatConnected
	down.eventChannel = make(chan interface{}, DownSessionChannelCache)

	glog.Info("miner connected, sessionId: ", sessionID, ", IP: ", down.IP())
	return
}

func (down *DownSession) Init() {
	go down.handleRequest()
	down.handleEvent()
}

func (down *DownSession) Run() {
	down.handleEvent()
}

func (down *DownSession) close() {
	if down.upSession != nil && down.stat != StatExit {
		go down.upSession.SendEvent(EventDownSessionBroken{down.sessionID})
	}

	down.eventLoopRunning = false
	down.stat = StatDisconnected
	down.clientConn.Close()

	// release down id
	down.manager.sessionIDManager.FreeSessionID(down.sessionID)
}

func (down *DownSession) IP() string {
	return down.clientConn.RemoteAddr().String()
}

func (down *DownSession) ID() string {
	if down.stat == StatAuthorized {
		return fmt.Sprintf("%s@%s", down.fullName, down.IP())
	}
	return down.IP()
}

func (down *DownSession) writeJSONResponse(jsonData *JSONRPCResponse) (int, error) {
	bytes, err := jsonData.ToJSONBytesLine()
	if err != nil {
		return 0, err
	}
	return down.clientConn.Write(bytes)
}

func (down *DownSession) stratumHandleRequest(request *JSONRPCLine, requestJSON []byte) (result interface{}, err *StratumError) {
	switch request.Method {
	case "mining.subscribe":
		if down.stat != StatConnected {
			err = StratumErrDuplicateSubscribed
			return
		}
		result, err = down.parseSubscribeRequest(request)
		if err == nil {
			down.stat = StatSubScribed
		}
		return

	case "mining.authorize":
		if down.stat != StatSubScribed {
			err = StratumErrNeedSubscribed
			return
		}
		result, err = down.parseAuthorizeRequest(request)
		if err == nil {
			down.stat = StatAuthorized
			// 让 Init() 函数返回
			down.eventLoopRunning = false

			glog.Info("miner authorized, session id: ", down.sessionID, ", IP: ", down.IP(), ", worker name: ", down.fullName)
		}
		return

	case "mining.configure":
		result, err = down.parseConfigureRequest(request)
		return

	case "mining.submit":
		result, err = down.parseMiningSubmit(request)
		if err != nil {
			glog.Warning("stratum error, IP: ", down.IP(), ", worker: ", down.fullName, ", error: ", err, ", submit: ", string(requestJSON))
		}
		return

	default:
		// ignore unimplemented methods
		glog.Warning("unknown request, IP: ", down.IP(), ", request: ", string(requestJSON))
		return
	}
}

func (down *DownSession) parseMiningSubmit(request *JSONRPCLine) (result interface{}, err *StratumError) {
	if down.stat != StatAuthorized {
		err = StratumErrNeedAuthorized

		// there must be something wrong, send reconnect command
		down.sendReconnectRequest()
		return
	}

	if down.upSession == nil {
		err = StratumErrJobNotFound
		return
	}

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

	if IsFakeJobID(jobIDStr) {
		msg.IsFakeJob = true
	} else {
		jobID, convErr := strconv.ParseUint(jobIDStr, 10, 8)
		if convErr != nil {
			err = StratumErrIllegalParams
			return
		}
		msg.Base.JobID = uint8(jobID)
	}

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

	// down id
	msg.Base.SessionID = uint16(down.sessionID)

	go down.upSession.SendEvent(EventSubmitShare{request.ID, &msg})

	// 如果 AsicBoost 丢失，就发送重连请求
	if down.manager.config.DisconnectWhenLostAsicboost {
		if hasVersionMask {
			down.versionRollingShareCounter++
		} else if down.versionRollingShareCounter > 100 {
			glog.Warning("AsicBoost disabled mid-way, send client.reconnect. worker: ", down.ID(), ", version rolling shares: ", down.versionRollingShareCounter)

			// send reconnect request to miner
			down.sendReconnectRequest()
		}
	}
	return
}

func (down *DownSession) sendReconnectRequest() {
	var reconnect JSONRPCRequest
	reconnect.Method = "client.reconnect"
	reconnect.Params = JSONRPCArray{}
	bytes, err := reconnect.ToJSONBytesLine()
	if err != nil {
		glog.Error("convert client.reconnect request to JSON failed, request: ", reconnect, ", error: ", err.Error())
		return
	}
	go down.SendEvent(EventSendBytes{bytes})
}

func (down *DownSession) parseSubscribeRequest(request *JSONRPCLine) (result interface{}, err *StratumError) {

	if len(request.Params) >= 1 {
		down.clientAgent, _ = request.Params[0].(string)
	}

	sessionIDString := Uint32ToHex(uint32(down.sessionID))

	result = JSONRPCArray{JSONRPCArray{JSONRPCArray{"mining.set_difficulty", sessionIDString}, JSONRPCArray{"mining.notify", sessionIDString}}, sessionIDString, 4}
	return
}

func (down *DownSession) parseAuthorizeRequest(request *JSONRPCLine) (result interface{}, err *StratumError) {
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
	down.fullName = FilterWorkerName(fullWorkerName)

	// 截取“.”之前的做为子账户名，“.”及之后的做矿机名
	pos := strings.IndexByte(down.fullName, '.')
	if pos >= 0 {
		down.subAccountName = down.fullName[:pos]
		down.workerName = down.fullName[pos+1:]
	} else {
		down.subAccountName = down.fullName
		down.workerName = ""
	}

	if len(down.manager.config.FixedWorkerName) > 0 {
		down.workerName = down.manager.config.FixedWorkerName
		down.fullName = down.subAccountName + "." + down.workerName
	} else if down.manager.config.UseIpAsWorkerName {
		down.workerName = IPAsWorkerName(down.manager.config.IpWorkerNameFormat, down.clientConn.RemoteAddr().String())
		down.fullName = down.subAccountName + "." + down.workerName
	}

	if down.manager.config.MultiUserMode {
		if len(down.subAccountName) < 1 {
			err = StratumErrSubAccountNameEmpty
			return
		}
	} else {
		down.subAccountName = ""
	}

	if down.workerName == "" {
		down.workerName = down.fullName
		if down.workerName == "" {
			down.workerName = DefaultWorkerName
			down.fullName = down.subAccountName + "." + down.workerName
		}
	}

	// 获取矿机名成功
	result = true
	err = nil
	return
}

func (down *DownSession) parseConfigureRequest(request *JSONRPCLine) (result interface{}, err *StratumError) {
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
					down.versionMask = uint32(versionMask)
				}
			}
		}
	}

	if down.versionMask != 0 {
		// 这里响应的是虚假的版本掩码。在连接服务器后将通过 mining.set_version_mask
		// 更新为真实的版本掩码。
		result = JSONRPCObj{
			"version-rolling":      true,
			"version-rolling.mask": down.versionMaskStr()}
		return
	}

	// 未知配置内容，不响应
	return
}

func (down *DownSession) versionMaskStr() string {
	return fmt.Sprintf("%08x", down.versionMask)
}

func (down *DownSession) setUpSession(e EventSetUpSession) {
	down.upSession = e.Session
	down.upSession.SendEvent(EventAddDownSession{down})
}

func (down *DownSession) handleRequest() {
	down.readLoopRunning = true

	for down.readLoopRunning {
		jsonBytes, err := down.clientReader.ReadBytes('\n')

		if err != nil {
			glog.Error("read line failed, IP: ", down.IP(), ", error: ", err.Error())
			down.connBroken()
			return
		}

		rpcData, err := NewJSONRPCLine(jsonBytes)

		// ignore the json decode error
		if err != nil {
			glog.Warning("JSON decode failed, IP: ", down.IP(), err.Error(), string(jsonBytes))
		}

		down.SendEvent(EventRecvJSONRPC{rpcData, jsonBytes})
	}
}

func (down *DownSession) recvJSONRPC(e EventRecvJSONRPC) {
	// stat will be changed in stratumHandleRequest
	result, stratumErr := down.stratumHandleRequest(e.RPCData, e.JSONBytes)

	// 两个均为空说明没有想要返回的响应
	if result != nil || stratumErr != nil {
		var response JSONRPCResponse
		response.ID = e.RPCData.ID
		response.Result = result
		response.Error = stratumErr.ToJSONRPCArray(nil)

		_, err := down.writeJSONResponse(&response)

		if err != nil {
			glog.Error("write JSON response failed, IP: ", down.IP(), ", error: ", err.Error())
			down.close()
			return
		}
	}
}

func (down *DownSession) SendEvent(event interface{}) {
	down.eventChannel <- event
}

func (down *DownSession) connBroken() {
	down.readLoopRunning = false
	down.SendEvent(EventConnBroken{})
}

func (down *DownSession) sendBytes(e EventSendBytes) {
	_, err := down.clientConn.Write(e.Content)
	if err != nil {
		glog.Error("write bytes failed, IP: ", down.IP(), ", error: ", err.Error())
		down.close()
	}
}

func (down *DownSession) submitResponse(e EventSubmitResponse) {
	var response JSONRPCResponse
	response.ID = e.ID
	if e.Status.IsAccepted() {
		response.Result = true
	} else {
		response.Error = e.Status.ToJSONRPCArray(nil)
	}

	_, err := down.writeJSONResponse(&response)
	if err != nil {
		glog.Error("write submit response failed, IP: ", down.IP(), ", error: ", err.Error())
		down.close()
	}
}

func (down *DownSession) exit() {
	down.stat = StatExit
	down.close()
}

func (down *DownSession) handleEvent() {
	down.eventLoopRunning = true
	for down.eventLoopRunning {
		event := <-down.eventChannel

		switch e := event.(type) {
		case EventSetUpSession:
			down.setUpSession(e)
		case EventRecvJSONRPC:
			down.recvJSONRPC(e)
		case EventSendBytes:
			down.sendBytes(e)
		case EventSubmitResponse:
			down.submitResponse(e)
		case EventConnBroken:
			down.close()
		case EventExit:
			down.exit()
		default:
			glog.Error("Unknown event: ", e)
		}
	}
}
