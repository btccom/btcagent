package main

import (
	"bufio"
	"crypto/tls"
	"encoding/binary"
	"encoding/hex"
	"errors"
	"fmt"
	"io"
	"net"
	"strconv"
	"time"

	"github.com/golang/glog"
)

type UpSessionETH struct {
	id string // 打印日志用的连接标识符

	manager *UpSessionManager
	config  *Config
	slot    int

	subAccount string
	poolIndex  int

	downSessions    map[uint16]*DownSessionETH
	serverConn      net.Conn
	serverReader    *bufio.Reader
	readLoopRunning bool

	stat      AuthorizeStat
	sessionID uint32

	serverCapSubmitResponse bool

	eventLoopRunning bool
	eventChannel     chan interface{}

	lastJob     *StratumJobETH
	defaultDiff uint64

	submitIDs   map[uint16]SubmitID
	submitIndex uint16

	// 用于统计断开连接的矿机数，并同步给 UpSessionManager
	disconnectedMinerCounter int
}

func NewUpSessionETH(manager *UpSessionManager, poolIndex int, slot int) (up *UpSessionETH) {
	up = new(UpSessionETH)
	up.manager = manager
	up.config = manager.config
	up.slot = slot
	up.subAccount = manager.subAccount
	up.poolIndex = poolIndex
	up.downSessions = make(map[uint16]*DownSessionETH)
	up.stat = StatDisconnected
	up.eventChannel = make(chan interface{}, manager.config.Advanced.MessageQueueSize.PoolSession)
	up.submitIDs = make(map[uint16]SubmitID)

	if !up.config.MultiUserMode {
		up.subAccount = manager.config.Pools[poolIndex].SubAccount
	}

	return
}

func (up *UpSessionETH) Stat() AuthorizeStat {
	return up.stat
}

func (up *UpSessionETH) connect() {
	pool := up.config.Pools[up.poolIndex]
	url := fmt.Sprintf("%s:%d", pool.Host, pool.Port)

	if up.config.PoolUseTls {
		up.id = fmt.Sprintf("pool#%d <%s> [tls://%s] ", up.slot, up.subAccount, url)
	} else {
		up.id = fmt.Sprintf("pool#%d <%s> [%s] ", up.slot, up.subAccount, url)
	}

	// Try to connect to all proxies and find the fastest one
	counter := len(up.config.Proxy)
	for i := 0; i < counter; i++ {
		go up.tryConnect(pool.Host, url, up.config.Proxy[i])
	}
	if up.config.DirectConnectWithProxy {
		counter++
		go up.tryConnect(pool.Host, url, "")
	}

	// 接收连接事件
	for i := 0; i < counter; i++ {
		event := <-up.eventChannel
		switch e := event.(type) {
		case EventUpSessionConnection:
			up.upSessionConnection(e)
			if up.stat == StatConnected {
				return
			}
		default:
			glog.Error(up.id, "unknown event: ", e)
		}
	}

	// 无需尝试直连
	if counter > 0 && !up.config.DirectConnectAfterProxy {
		return
	}

	// 尝试直连
	go up.tryConnect(pool.Host, url, "")
	event := <-up.eventChannel
	switch e := event.(type) {
	case EventUpSessionConnection:
		up.upSessionConnection(e)
	default:
		glog.Error(up.id, "unknown event: ", e)
	}
}

func (up *UpSessionETH) upSessionConnection(e EventUpSessionConnection) {
	if e.Error != nil {
		if len(e.ProxyURL) > 0 {
			glog.Warning(up.id, "proxy [", e.ProxyURL, "] failed: ", e.Error.Error())
		} else {
			glog.Warning(up.id, "direct connection failed: ", e.Error.Error())
		}

		if e.Conn != nil {
			e.Conn.Close()
		}
		return
	}

	up.serverConn = e.Conn
	up.serverReader = e.Reader
	up.stat = StatConnected
	up.id += fmt.Sprintf("(%s) ", up.serverConn.RemoteAddr().String())

	if len(e.ProxyURL) > 0 {
		glog.Info(up.id, "successfully connected with proxy [", e.ProxyURL, "]")
	} else {
		glog.Info(up.id, "successfully connected directly")
	}
}

func (up *UpSessionETH) tryConnect(poolHost, poolURL, proxyURL string) {
	timeout := up.config.Advanced.PoolConnectionDialTimeoutSeconds.Get()
	insecureSkipVerify := up.config.Advanced.TLSSkipCertificateVerify

	var err error
	var dialer Dialer
	var conn net.Conn
	var reader *bufio.Reader

	if len(proxyURL) > 0 {
		glog.Info(up.id, "connect to pool server with proxy [", proxyURL, "]...")
		dialer, err = GetProxyDialer(proxyURL, timeout, insecureSkipVerify)
	} else {
		glog.Info(up.id, "connect to pool server directly...")
		dialer = &net.Dialer{Timeout: timeout}
	}

	if err == nil {
		conn, err = dialer.Dial("tcp", poolURL)
		if err == nil {
			if up.config.PoolUseTls {
				conn = tls.Client(conn, &tls.Config{
					ServerName:         poolHost,
					InsecureSkipVerify: insecureSkipVerify,
				})
			}
			reader, err = up.testConnection(conn)
		}
	}

	up.SendEvent(EventUpSessionConnection{proxyURL, conn, reader, err})
}

func (up *UpSessionETH) testConnection(conn net.Conn) (reader *bufio.Reader, err error) {
	ch := make(chan error, 1)
	reader = bufio.NewReader(conn)

	go func() {
		capsRequest := up.getAgentGetCapsRequest("conn_test")
		bytes, e := capsRequest.ToJSONBytesLine()
		if e == nil {
			if glog.V(10) {
				glog.Info(up.id, "testConnection send: ", string(bytes))
			}
			conn.SetWriteDeadline(up.getIODeadLine())
			_, e = conn.Write(bytes)
			if e == nil {
				conn.SetReadDeadline(up.getIODeadLine())
				bytes, e = reader.ReadBytes('\n')
				if glog.V(9) {
					glog.Info(up.id, "testConnection recv: ", string(bytes))
				}
			}
		}
		ch <- e
	}()

	select {
	case <-time.After(up.config.Advanced.PoolConnectionDialTimeoutSeconds.Get()):
		err = errors.New("connection timeout")
		conn.Close()
	case err = <-ch:
	}

	return
}

func (up *UpSessionETH) writeJSONRequest(jsonData *JSONRPCRequest) (int, error) {
	bytes, err := jsonData.ToJSONBytesLine()
	if err != nil {
		return 0, err
	}
	if glog.V(10) {
		glog.Info(up.id, "writeJSONRequest: ", string(bytes))
	}
	return up.writeBytes(bytes)
}

func (up *UpSessionETH) writeExMessage(msg SerializableExMessage) (int, error) {
	bytes := msg.Serialize()
	if glog.V(10) && len(bytes) > 1 {
		glog.Info(up.id, "writeExMessage: ", bytes[1], msg, " ", hex.EncodeToString(bytes))
	}
	return up.writeBytes(bytes)
}

func (up *UpSessionETH) writeBytes(bytes []byte) (int, error) {
	up.setWriteDeadline()
	return up.serverConn.Write(bytes)
}

func (up *UpSessionETH) getAgentGetCapsRequest(id string) (req JSONRPCRequest) {
	req.ID = id
	req.Method = "agent.get_capabilities"
	if up.config.SubmitResponseFromServer {
		req.SetParams(JSONRPCArray{CapSubmitResponse})
	} else {
		req.SetParams(JSONRPCArray{})
	}
	return
}

func (up *UpSessionETH) sendInitRequest() (err error) {
	// send agent.get_capabilities first
	capsRequest := up.getAgentGetCapsRequest("caps")
	_, err = up.writeJSONRequest(&capsRequest)
	if err != nil {
		return
	}

	var request JSONRPCRequest

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

func (up *UpSessionETH) exit() {
	up.stat = StatExit
	up.close()
}

func (up *UpSessionETH) close() {
	if up.stat == StatAuthorized {
		up.manager.SendEvent(EventUpSessionBroken{up.slot})
	}

	if up.stat != StatExit && up.config.AlwaysKeepDownconn {
		if up.lastJob != nil {
			up.manager.SendEvent(EventUpdateFakeJobETH{up.lastJob})
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

func (up *UpSessionETH) Init() {
	up.connect()
	if up.stat != StatConnected {
		if len(up.config.Proxy) > 0 && (up.config.DirectConnectWithProxy || up.config.DirectConnectAfterProxy) {
			glog.Error(up.id, "all connections both proxy and direct failed")
		} else if len(up.config.Proxy) > 1 {
			glog.Error(up.id, "all proxy connections failed")
		}
		return
	}

	go up.handleResponse()

	err := up.sendInitRequest()
	if err != nil {
		glog.Error(up.id, "failed to send request to pool server: ", err.Error())
		up.close()
		return
	}

	up.handleEvent()
}

func (up *UpSessionETH) handleSetDifficulty(rpcData *JSONRPCLineETH, jsonBytes []byte) {
	if up.defaultDiff == 0 {
		if len(rpcData.Params) < 1 {
			glog.Error(up.id, "missing difficulty in mining.set_difficulty message: ", string(jsonBytes))
			return
		}
		diff, ok := rpcData.Params[0].(float64)
		if !ok {
			glog.Error(up.id, "difficulty in mining.set_difficulty message is not a number: ", string(jsonBytes))
			return
		}
		// nicehash_diff = btcpool_diff / pow(2, 32)
		up.defaultDiff = uint64(diff * 4294967296.0)
		if glog.V(5) {
			glog.Info(up.id, "mining.set_difficulty: ", diff, " -> ", up.defaultDiff)
		}

		e := EventSetDifficulty{up.defaultDiff}
		for _, down := range up.downSessions {
			go down.SendEvent(e)
		}
	}
}

func (up *UpSessionETH) handleSubScribeResponse(rpcData *JSONRPCLineETH, jsonBytes []byte) {
	result, ok := rpcData.Result.([]interface{})
	if !ok {
		glog.Error(up.id, "subscribe result is not an array: ", string(jsonBytes))
		up.close()
		return
	}
	if len(result) < 2 {
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
	up.stat = StatSubScribed
}

func (up *UpSessionETH) handleGetCapsResponse(rpcData *JSONRPCLineETH, jsonBytes []byte) {
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
		case CapSubmitResponse:
			up.serverCapSubmitResponse = true
		}
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

func (up *UpSessionETH) handleAuthorizeResponse(rpcData *JSONRPCLineETH, jsonBytes []byte) {
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

func (up *UpSessionETH) connBroken() {
	up.readLoopRunning = false
	up.SendEvent(EventConnBroken{})
}

func (up *UpSessionETH) getIODeadLine() time.Time {
	var timeout Seconds
	if up.stat == StatAuthorized {
		timeout = up.config.Advanced.PoolConnectionReadTimeoutSeconds
	} else {
		timeout = up.config.Advanced.PoolConnectionDialTimeoutSeconds
	}
	return time.Now().Add(timeout.Get())
}

func (up *UpSessionETH) setReadDeadline() {
	up.serverConn.SetReadDeadline(up.getIODeadLine())
}

func (up *UpSessionETH) setWriteDeadline() {
	up.serverConn.SetWriteDeadline(up.getIODeadLine())
}

func (up *UpSessionETH) handleResponse() {
	up.readLoopRunning = true
	for up.readLoopRunning {
		up.setReadDeadline()
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

func (up *UpSessionETH) readExMessage() {
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

	if glog.V(9) {
		glog.Info(up.id, "readExMessage: ", message.ExMessageHeader.Type, " ", hex.EncodeToString(message.Body))
	}
	up.SendEvent(EventRecvExMessage{message})
}

func (up *UpSessionETH) readLine() {
	jsonBytes, err := up.serverReader.ReadBytes('\n')
	if err != nil {
		glog.Error(up.id, "failed to read JSON line from pool server: ", err.Error())
		up.connBroken()
		return
	}
	if glog.V(9) {
		glog.Info(up.id, "readLine: ", string(jsonBytes))
	}

	rpcData, err := NewJSONRPCLineETH(jsonBytes)

	// ignore the json decode error
	if err != nil {
		glog.Info(up.id, "failed to decode JSON line from pool server: ", err.Error(), "; ", string(jsonBytes))
		return
	}

	up.SendEvent(EventRecvJSONRPCETH{rpcData, jsonBytes})
}

func (up *UpSessionETH) Run() {
	up.handleEvent()
}

func (up *UpSessionETH) SendEvent(event interface{}) {
	up.eventChannel <- event
}

func (up *UpSessionETH) addDownSession(e EventAddDownSession) {
	down := e.Session.(*DownSessionETH)
	up.downSessions[down.sessionID] = down
	up.registerWorker(down)

	if up.defaultDiff != 0 {
		down.SendEvent(EventSetDifficulty{up.defaultDiff})
	}
}

func (up *UpSessionETH) registerWorker(down *DownSessionETH) {
	msg := ExMessageRegisterWorker{down.sessionID, down.clientAgent, down.workerName}
	_, err := up.writeExMessage(&msg)
	if err != nil {
		glog.Error(up.id, "failed to register worker to pool server: ", err.Error())
		up.close()
	}
}

func (up *UpSessionETH) unregisterWorker(sessionID uint16) {
	msg := ExMessageUnregisterWorker{sessionID}
	_, err := up.writeExMessage(&msg)
	if err != nil {
		glog.Error(up.id, "failed to unregister worker from pool server: ", err.Error())
		up.close()
	}
}

func (up *UpSessionETH) handleMiningNotify(rpcData *JSONRPCLineETH, jsonBytes []byte) {
	job, err := NewStratumJobETH(rpcData, up.sessionID)
	if err != nil {
		glog.Warning(up.id, err.Error(), ": ", string(jsonBytes))
		return
	}

	e := EventStratumJobETH{job}
	for _, down := range up.downSessions {
		go down.SendEvent(e)
	}

	up.lastJob = job
}

func (up *UpSessionETH) recvJSONRPC(e EventRecvJSONRPCETH) {
	rpcData := e.RPCData
	jsonBytes := e.JSONBytes

	if len(rpcData.Method) > 0 {
		switch rpcData.Method {
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
	case "sub":
		up.handleSubScribeResponse(rpcData, jsonBytes)
	case "auth":
		up.handleAuthorizeResponse(rpcData, jsonBytes)
	case "caps_again":
		// ignore
	case "conn_test":
		// ignore
	default:
		glog.Info(up.id, "[TODO] pool response: ", rpcData)
	}
}

func (up *UpSessionETH) handleSubmitShare(e EventSubmitShareETH) {
	if e.Message.IsFakeJob {
		up.sendSubmitResponse(e.Message.SessionID, e.ID, STATUS_ACCEPT)
		return
	}

	_, err := up.writeExMessage(e.Message)

	if up.config.SubmitResponseFromServer && up.serverCapSubmitResponse {
		up.submitIDs[up.submitIndex] = SubmitID{e.ID, e.Message.SessionID}
		up.submitIndex++
	} else {
		up.sendSubmitResponse(e.Message.SessionID, e.ID, STATUS_ACCEPT)
	}

	if err != nil {
		glog.Error(up.id, "failed to submit share: ", err.Error())
		up.close()
		return
	}
}

func (up *UpSessionETH) sendSubmitResponse(sessionID uint16, id interface{}, status StratumStatus) {
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

func (up *UpSessionETH) handleExMessageSubmitResponse(ex *ExMessage) {
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

func (up *UpSessionETH) handleExMessageMiningSetDiff(ex *ExMessage) {
	var msg ExMessageMiningSetDiff
	err := msg.Unserialize(ex.Body)
	if err != nil {
		glog.Error(up.id, "failed to decode ex-message CMD_MINING_SET_DIFF: ", err.Error(), "; ", ex)
		return
	}

	diff := uint64(1) << msg.Base.DiffExp

	e := EventSetDifficulty{diff}
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

func (up *UpSessionETH) handleExMessageSetExtraNonce(ex *ExMessage) {
	var msg ExMessageSetExtranonce
	err := msg.Unserialize(ex.Body)
	if err != nil {
		glog.Error(up.id, "failed to decode ex-message CMD_SET_EXTRA_NONCE: ", err.Error(), "; ", ex)
		return
	}

	// 矿池服务器满了，无法连接新矿机
	if msg.ExtraNonce == EthereumInvalidExtraNonce {
		glog.Error(up.id, "pool server is full, try to reconnect")
		up.close()
		return
	}

	down := up.downSessions[msg.SessionID]
	if down != nil {
		down.SendEvent(EventSetExtraNonce{msg.ExtraNonce})
		if up.lastJob != nil {
			down.SendEvent(EventStratumJobETH{up.lastJob})
		}
	} else {
		// 客户端已断开，忽略
		if glog.V(3) {
			glog.Info(up.id, "cannot find down session: ", msg.SessionID)
		}
	}
}

func (up *UpSessionETH) recvExMessage(e EventRecvExMessage) {
	switch e.Message.Type {
	case CMD_SUBMIT_RESPONSE:
		up.handleExMessageSubmitResponse(e.Message)
	case CMD_MINING_SET_DIFF:
		up.handleExMessageMiningSetDiff(e.Message)
	case CMD_SET_EXTRA_NONCE:
		up.handleExMessageSetExtraNonce(e.Message)
	default:
		glog.Error(up.id, "unknown ex-message: ", e.Message)
	}
}

func (up *UpSessionETH) downSessionBroken(e EventDownSessionBroken) {
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

func (up *UpSessionETH) sendUpdateMinerNum() {
	go up.manager.SendEvent(EventUpdateMinerNum{up.slot, up.disconnectedMinerCounter})
	up.disconnectedMinerCounter = 0
}

func (up *UpSessionETH) outdatedUpSessionConnection(e EventUpSessionConnection) {
	// up.connect() 方法有自己的事件循环来接收连接，
	// 所以到达这里的连接都是多余的，可以直接关闭。
	if e.Conn != nil {
		e.Conn.Close()
	}
}

func (up *UpSessionETH) handleEvent() {
	up.eventLoopRunning = true
	for up.eventLoopRunning {
		event := <-up.eventChannel

		switch e := event.(type) {
		case EventAddDownSession:
			up.addDownSession(e)
		case EventSubmitShareETH:
			up.handleSubmitShare(e)
		case EventDownSessionBroken:
			up.downSessionBroken(e)
		case EventSendUpdateMinerNum:
			up.sendUpdateMinerNum()
		case EventRecvJSONRPCETH:
			up.recvJSONRPC(e)
		case EventRecvExMessage:
			up.recvExMessage(e)
		case EventConnBroken:
			up.close()
		case EventUpSessionConnection:
			up.outdatedUpSessionConnection(e)
		case EventExit:
			up.exit()
		default:
			glog.Error(up.id, "unknown event: ", e)
		}
	}
}
