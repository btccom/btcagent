package main

import (
	"bufio"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"net"
	"strings"

	"github.com/golang/glog"
)

type DownSessionETH struct {
	id string // 打印日志用的连接标识符

	manager   *SessionManager // 会话管理器
	upSession EventInterface  // 所属的服务器会话

	sessionID       uint16        // 会话ID
	clientConn      net.Conn      // 到矿机的TCP连接
	clientReader    *bufio.Reader // 读取矿机发送的内容
	readLoopRunning bool          // TCP读循环是否在运行

	jobDiff       uint64         // 挖矿任务难度
	extraNonce    uint32         // 由矿池指定
	hasExtraNonce bool           // 是否获得了ExtraNonce
	isFirstJob    bool           // 是否为第一个任务
	jobIDQueue    *JobIDQueueETH // job id 队列
	ethGetWorkID  interface{}    // 矿机eth_getWork请求的id

	stat       AuthorizeStat   // 认证状态
	protocol   StratumProtocol // 挖矿协议
	rpcVersion int             // JSON-RPC版本

	clientAgent    string // 挖矿软件名称
	fullName       string // 完整的矿工名
	subAccountName string // 子账户名部分
	workerName     string // 矿机名部分

	eventLoopRunning bool             // 消息循环是否在运行
	eventChannel     chan interface{} // 消息通道
}

// NewDownSessionETH 创建一个新的 Stratum 会话
func NewDownSessionETH(manager *SessionManager, clientConn net.Conn, sessionID uint16) (down *DownSessionETH) {
	down = new(DownSessionETH)
	down.manager = manager
	down.sessionID = sessionID
	down.clientConn = clientConn
	down.clientReader = bufio.NewReader(clientConn)
	down.stat = StatConnected
	down.eventChannel = make(chan interface{}, manager.config.Advanced.MessageQueueSize.MinerSession)
	down.jobIDQueue = NewJobqueueETH(EthereumJobIDQueueSize)
	down.ethGetWorkID = 0

	down.id = fmt.Sprintf("miner#%d (%s) ", down.sessionID, down.clientConn.RemoteAddr())

	glog.Info(down.id, "miner connected")
	return
}

func (down *DownSessionETH) SessionID() uint16 {
	return down.sessionID
}

func (down *DownSessionETH) SubAccountName() string {
	return down.subAccountName
}

func (down *DownSessionETH) Stat() AuthorizeStat {
	return down.stat
}

func (down *DownSessionETH) Init() {
	go down.handleRequest()
	down.handleEvent()
}

func (down *DownSessionETH) Run() {
	down.handleEvent()
}

func (down *DownSessionETH) close() {
	if down.upSession != nil && down.stat != StatExit {
		go down.upSession.SendEvent(EventDownSessionBroken{down.sessionID})
	}

	down.eventLoopRunning = false
	down.stat = StatDisconnected
	down.clientConn.Close()

	// release down id
	down.manager.sessionIDManager.FreeSessionID(down.sessionID)
}

func (down *DownSessionETH) writeJSONRequest(jsonData *JSONRPCRequest) (int, error) {
	bytes, err := jsonData.ToJSONBytesLineWithVersion(down.rpcVersion)
	if err != nil {
		return 0, err
	}
	if glog.V(10) {
		glog.Info(down.id, "writeJSONRequest: ", string(bytes))
	}
	return down.clientConn.Write(bytes)
}

func (down *DownSessionETH) writeJSONResponse(jsonData *JSONRPCResponse) (int, error) {
	bytes, err := jsonData.ToJSONBytesLineWithVersion(down.rpcVersion)
	if err != nil {
		return 0, err
	}
	if glog.V(12) {
		glog.Info(down.id, "writeJSONResponse: ", string(bytes))
	}
	return down.clientConn.Write(bytes)
}

func (down *DownSessionETH) stratumHandleRequest(request *JSONRPCLineETH, requestJSON []byte) (result interface{}, err *StratumError) {
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

	case "eth_submitLogin":
		down.protocol = ProtocolETHProxy
		down.rpcVersion = 2
		down.stat = StatSubScribed
		fallthrough
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

			down.id += fmt.Sprintf("<%s> ", down.fullName)

			glog.Info(down.id, "miner authorized")
		}
		return

	case "eth_submitWork":
		fallthrough
	case "mining.submit":
		result, err = down.parseMiningSubmit(request)
		if err != nil {
			glog.Warning(down.id, "stratum error: ", err, "; ", string(requestJSON))
		}
		return

	case "mining.extranonce.subscribe":
		fallthrough
	case "eth_submitHashrate":
		result = true
		return

	case "eth_getWork":
		result, err = down.parseEthGetWork(request)
		return

	// ignore unimplemented methods
	case "mining.multi_version":
		fallthrough
	case "mining.suggest_difficulty":
		// If no response, the miner may wait indefinitely
		err = StratumErrIllegalParams
		return

	default:
		glog.Warning(down.id, "unknown request: ", string(requestJSON))

		// If no response, the miner may wait indefinitely
		err = StratumErrIllegalParams
		return
	}
}

func (down *DownSessionETH) parseEthGetWork(request *JSONRPCLineETH) (result interface{}, err *StratumError) {
	down.ethGetWorkID = request.ID
	return
}

func (down *DownSessionETH) parseMiningSubmit(request *JSONRPCLineETH) (result interface{}, err *StratumError) {
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

	var powHash, mixHash, nonce string

	switch down.protocol {
	case ProtocolLegacyStratum:
		if len(request.Params) < 5 {
			err = StratumErrTooFewParams
			return
		}
		powHash, _ = request.Params[3].(string)
		if len(powHash) < 1 {
			powHash, _ = request.Params[1].(string)
		}
		nonce, _ = request.Params[2].(string)
		mixHash, _ = request.Params[4].(string)
	case ProtocolETHProxy:
		if len(request.Params) < 3 {
			err = StratumErrTooFewParams
			return
		}
		nonce, _ = request.Params[0].(string)
		powHash, _ = request.Params[1].(string)
		mixHash, _ = request.Params[2].(string)
	case ProtocolEthereumStratum:
		if len(request.Params) < 3 {
			err = StratumErrTooFewParams
			return
		}
		powHash, _ = request.Params[1].(string)
		nonce, _ = request.Params[2].(string)
	}

	var msg ExMessageSubmitShareETH
	msg.SessionID = down.sessionID

	powHash = HexRemovePrefix(powHash)
	if IsFakeJobIDETH(powHash) {
		msg.IsFakeJob = true
	}

	msg.JobID = down.jobIDQueue.Find(powHash)
	if msg.JobID == nil {
		err = StratumErrJobNotFound
		return
	}

	if len(nonce) < 1 {
		err = StratumErrIllegalParams
		return
	}
	var convErr error
	msg.Nonce, convErr = Hex2Uint64(nonce)
	if convErr != nil {
		err = StratumErrIllegalParams
		return
	}

	if len(mixHash) > 0 {
		msg.MixHash, _ = Hex2Bin(mixHash)
		BinReverse(msg.MixHash) // btcpool使用小端字节序
	}

	go down.upSession.SendEvent(EventSubmitShareETH{request.ID, &msg})
	return
}

func (down *DownSessionETH) sendReconnectRequest() {
	var reconnect JSONRPCRequest
	reconnect.Method = "client.reconnect"
	reconnect.Params = JSONRPCArray{}
	bytes, err := reconnect.ToJSONBytesLineWithVersion(down.rpcVersion)
	if err != nil {
		glog.Error(down.id, "failed to convert client.reconnect request to JSON: ", err.Error(), "; ", reconnect)
		return
	}
	go down.SendEvent(EventSendBytes{bytes})
}

func (down *DownSessionETH) parseSubscribeRequest(request *JSONRPCLineETH) (result interface{}, err *StratumError) {
	down.protocol = ProtocolLegacyStratum
	down.rpcVersion = 1
	result = true

	if len(request.Params) >= 1 {
		down.clientAgent, _ = request.Params[0].(string)
	}

	if len(request.Params) >= 2 {
		// message example: {"id":1,"method":"mining.subscribe","params":["ethminer 0.15.0rc1","EthereumStratum/1.0.0"]}
		protocol, ok := request.Params[1].(string)

		// 判断是否为"EthereumStratum/xxx"
		if ok && strings.HasPrefix(strings.ToLower(protocol), EthereumStratumPrefix) {
			down.protocol = ProtocolEthereumStratum
			sessionIDString := Uint16ToHex(down.sessionID)
			// message example: {"id":1,"jsonrpc":"2.0","result":[["mining.notify","0001","EthereumStratum/1.0.0"],"0001"],"error":null}
			// Add a padding nonce prefix. BTCAgent doesn't need a nonce prefix, but gminer has problems when it's missing.
			result = JSONRPCArray{JSONRPCArray{"mining.notify", sessionIDString, EthereumStratumVersion}, "00"}
		}
	}

	return
}

func (down *DownSessionETH) parseAuthorizeRequest(request *JSONRPCLineETH) (result interface{}, err *StratumError) {
	if len(request.Params) < 1 {
		err = StratumErrTooFewParams
		return
	}

	fullWorkerName, ok := request.Params[0].(string)
	if !ok {
		err = StratumErrWorkerNameMustBeString
		return
	}
	if len(request.Worker) > 0 {
		fullWorkerName = fullWorkerName + "." + request.Worker
	}

	// 矿工名
	down.fullName = FilterWorkerName(StripEthAddrFromFullName(fullWorkerName))

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

func (down *DownSessionETH) setUpSession(e EventSetUpSession) {
	down.hasExtraNonce = false
	down.isFirstJob = true
	down.upSession = e.Session
	down.upSession.SendEvent(EventAddDownSession{down})
}

func (down *DownSessionETH) handleRequest() {
	down.readLoopRunning = true

	for down.readLoopRunning {
		jsonBytes, err := down.clientReader.ReadBytes('\n')
		if err != nil {
			glog.Error(down.id, "failed to read request from miner: ", err.Error())
			down.connBroken()
			return
		}
		if glog.V(11) {
			glog.Info(down.id, "handleRequest: ", string(jsonBytes), ", len=", len(jsonBytes), ", hex=", hex.EncodeToString(jsonBytes))
		}

		rpcData, err := NewJSONRPCLineETH(jsonBytes)

		// ignore the json decode error
		if err != nil {
			glog.Warning(down.id, "failed to decode JSON from miner: ", err.Error(), "; ", string(jsonBytes), ", len=", len(jsonBytes), ", hex=", hex.EncodeToString(jsonBytes))
			continue
		}

		down.SendEvent(EventRecvJSONRPCETH{rpcData, jsonBytes})
	}
}

func (down *DownSessionETH) recvJSONRPC(e EventRecvJSONRPCETH) {
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
			glog.Error(down.id, "failed to send response to miner: ", err.Error())
			down.close()
			return
		}
	}
}

func (down *DownSessionETH) sendJob(e EventStratumJobETH) {
	// 还没有获得 extra nonce，不应该给矿机发送任务
	if !down.hasExtraNonce {
		return
	}

	powHash := e.Job.PoWHash(down.extraNonce)
	seedHash := hex.EncodeToString(e.Job.SeedHash)
	target := DiffToTargetETH(down.jobDiff)
	height := e.Job.Height()

	isClean := down.isFirstJob || e.Job.IsClean
	down.isFirstJob = false

	down.jobIDQueue.Add(powHash, e.Job.JobID)

	var job interface{}
	switch down.protocol {
	case ProtocolLegacyStratum:
		job = JSONRPCJobETH{nil, "mining.notify", JSONRPCArray{
			powHash,
			powHash,
			seedHash,
			target,
			isClean,
		}, height}
	case ProtocolETHProxy:
		job = JSONRPC2JobETH{down.ethGetWorkID, "2.0", JSONRPCArray{
			HexAddPrefix(powHash),
			HexAddPrefix(seedHash),
			HexAddPrefix(target),
		}, height}
		down.ethGetWorkID = 0
	case ProtocolEthereumStratum:
		job = JSONRPCJobETH{nil, "mining.notify", JSONRPCArray{
			powHash,
			seedHash,
			powHash,
			isClean,
		}, height}
	}

	jsonBytes, err := json.Marshal(job)
	if err != nil {
		glog.Error(down.id, "failed to convert mining job to JSON: ", err, ": ", job)
	}
	jsonBytes = append(jsonBytes, '\n')

	if glog.V(12) {
		glog.Info(down.id, "sendJob: ", string(jsonBytes))
	}
	_, err = down.clientConn.Write(jsonBytes)
	if err != nil {
		glog.Error(down.id, "failed to send job to miner: ", err.Error())
		down.close()
	}
}

func (down *DownSessionETH) SendEvent(event interface{}) {
	down.eventChannel <- event
}

func (down *DownSessionETH) connBroken() {
	down.readLoopRunning = false
	down.SendEvent(EventConnBroken{})
}

func (down *DownSessionETH) sendBytes(e EventSendBytes) {
	if glog.V(12) {
		glog.Info(down.id, "sendBytes: ", string(e.Content))
	}
	_, err := down.clientConn.Write(e.Content)
	if err != nil {
		glog.Error(down.id, "failed to send notify to miner: ", err.Error())
		down.close()
	}
}

func (down *DownSessionETH) submitResponse(e EventSubmitResponse) {
	var response JSONRPCResponse
	response.ID = e.ID
	if e.Status.IsAccepted() {
		response.Result = true
	} else {
		response.Error = e.Status.ToJSONRPCArray(nil)
	}

	_, err := down.writeJSONResponse(&response)
	if err != nil {
		glog.Error(down.id, "failed to send share response to miner: ", err.Error())
		down.close()
	}
}

func (down *DownSessionETH) setDifficulty(e EventSetDifficulty) {
	if down.protocol == ProtocolEthereumStratum && down.jobDiff != e.Difficulty {
		diff := float64(e.Difficulty) / 4294967296.0

		var request JSONRPCRequest
		request.Method = "mining.set_difficulty"
		request.Params = JSONRPCArray{diff}

		_, err := down.writeJSONRequest(&request)
		if err != nil {
			glog.Error(down.id, "failed to send difficulty to miner: ", err.Error())
			down.close()
		}
		return
	}

	down.jobDiff = e.Difficulty
}

func (down *DownSessionETH) setExtraNonce(e EventSetExtraNonce) {
	if e.ExtraNonce == EthereumInvalidExtraNonce {
		glog.Error(down.id, "pool server is full and cannot allocate an extra nonce")
		down.close()
		return
	}

	down.extraNonce = e.ExtraNonce
	down.hasExtraNonce = true
}

func (down *DownSessionETH) exit() {
	down.stat = StatExit
	down.close()
}

func (down *DownSessionETH) poolNotReady() {
	glog.Warning(down.id, "pool connection not ready")
	down.exit()
}

func (down *DownSessionETH) handleEvent() {
	down.eventLoopRunning = true
	for down.eventLoopRunning {
		event := <-down.eventChannel

		switch e := event.(type) {
		case EventSetUpSession:
			down.setUpSession(e)
		case EventRecvJSONRPCETH:
			down.recvJSONRPC(e)
		case EventStratumJobETH:
			down.sendJob(e)
		case EventSendBytes:
			down.sendBytes(e)
		case EventSubmitResponse:
			down.submitResponse(e)
		case EventSetDifficulty:
			down.setDifficulty(e)
		case EventSetExtraNonce:
			down.setExtraNonce(e)
		case EventConnBroken:
			down.close()
		case EventExit:
			down.exit()
		case EventPoolNotReady:
			down.poolNotReady()
		default:
			glog.Error(down.id, "unknown event: ", e)
		}
	}
}
