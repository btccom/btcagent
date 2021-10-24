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
	manager      *StratumSessionManager // 会话管理器
	sessionID    uint32                 // 会话ID
	clientConn   net.Conn               // 到矿机的TCP连接
	clientReader *bufio.Reader          // 读取矿机发送的内容
	stat         AuthorizeStat          // 认证状态

	userAgent      string // 矿机User-Agent
	fullWorkerName string // 完整的矿工名
	subAccountName string // 子账户名部分
	minerName      string // 矿机名部分

	versionMask uint32 // 比特币版本掩码(用于AsicBoost)
}

// NewStratumSession 创建一个新的 Stratum 会话
func NewStratumSession(manager *StratumSessionManager, clientConn net.Conn, sessionID uint32) (session *StratumSession) {
	session = new(StratumSession)
	session.manager = manager
	session.sessionID = sessionID
	session.clientConn = clientConn
	session.clientReader = bufio.NewReader(clientConn)
	session.stat = StatConnected

	glog.Info("miner connected, sessionId: ", sessionID, ", IP: ", session.IP())
	return
}

func (session *StratumSession) Run() {
	for session.stat != StatAuthorized && session.stat != StatDisconnected {
		session.handleRequest()
	}
}

func (session *StratumSession) close() {
	session.stat = StatDisconnected
	session.clientConn.Close()
}

func (session *StratumSession) IP() string {
	return session.clientConn.RemoteAddr().String()
}

func (session *StratumSession) handleRequest() {
	requestJSON, err := session.clientReader.ReadBytes('\n')

	if err != nil {
		glog.Error("read line failed, IP: ", session.IP(), ", error: ", err.Error())
		session.close()
		return
	}

	request, err := NewJSONRPCRequest(requestJSON)

	// ignore the json decode error
	if err != nil {
		glog.Info("JSON decode failed, IP: ", session.IP(), err.Error(), string(requestJSON))
		return
	}

	// stat will be changed in stratumHandleRequest
	result, stratumErr := session.stratumHandleRequest(request, requestJSON)

	// 两个均为空说明没有想要返回的响应
	if result != nil || stratumErr != nil {
		var response JSONRPCResponse
		response.ID = request.ID
		response.Result = result
		response.Error = stratumErr.ToJSONRPCArray(nil)

		_, err = session.writeJSONResponseToClient(&response)

		if err != nil {
			glog.Error("write JSON response failed, IP: ", session.IP(), ", error: ", err.Error())
			session.close()
			return
		}
	}
}

func (session *StratumSession) writeJSONResponseToClient(jsonData *JSONRPCResponse) (int, error) {
	if session.stat == StatDisconnected {
		return 0, nil
	}

	bytes, err := jsonData.ToJSONBytesLine()
	if err != nil {
		return 0, err
	}

	return session.clientConn.Write(bytes)
}

func (session *StratumSession) stratumHandleRequest(request *JSONRPCRequest, requestJSON []byte) (result interface{}, err *StratumError) {
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
			glog.Info("miner authorized, session id: ", session.sessionID, ", IP: ", session.IP(), ", worker name: ", session.fullWorkerName)
		}
		return

	case "mining.configure":
		result, err = session.parseConfigureRequest(request)
		return

	default:
		// ignore unimplemented methods
		glog.Warning("unknown request, IP: ", session.IP(), ", request: ", string(requestJSON))
		return
	}
}

func (session *StratumSession) parseSubscribeRequest(request *JSONRPCRequest) (result interface{}, err *StratumError) {

	if len(request.Params) >= 1 {
		session.userAgent, _ = request.Params[0].(string)
	}

	sessionIDString := Uint32ToHex(session.sessionID)[2:8]

	result = JSONRPCArray{JSONRPCArray{JSONRPCArray{"mining.set_difficulty", sessionIDString}, JSONRPCArray{"mining.notify", sessionIDString}}, sessionIDString, 4}
	return

}

func (session *StratumSession) parseAuthorizeRequest(request *JSONRPCRequest) (result interface{}, err *StratumError) {
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
	session.fullWorkerName = FilterWorkerName(fullWorkerName)

	if strings.Contains(session.fullWorkerName, ".") {
		// 截取“.”之前的做为子账户名，“.”及之后的做矿机名
		pos := strings.Index(session.fullWorkerName, ".")
		session.subAccountName = session.fullWorkerName[:pos]
		session.minerName = session.fullWorkerName[pos+1:]
	} else {
		session.subAccountName = session.fullWorkerName
		session.minerName = ""
	}

	if len(session.subAccountName) < 1 {
		err = StratumErrWorkerNameStartWrong
		return
	}

	// 获取矿机名成功，但此处不需要返回内容给矿机
	// 连接服务器后会将服务器发送的响应返回给矿机
	result = nil
	err = nil
	return
}

func (session *StratumSession) parseConfigureRequest(request *JSONRPCRequest) (result interface{}, err *StratumError) {
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
		if versionMaskI, ok := options["version-rolling.mask"]; ok {
			if versionMaskStr, ok := versionMaskI.(string); ok {
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
