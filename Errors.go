package main

import "errors"

// StratumError Stratum错误
type StratumError struct {
	// 错误号
	ErrNo int
	// 错误信息
	ErrMsg string
}

// NewStratumError 新建一个StratumError
func NewStratumError(errNo int, errMsg string) *StratumError {
	err := new(StratumError)
	err.ErrNo = errNo
	err.ErrMsg = errMsg

	return err
}

// Error 实现StratumError的Error()接口以便其被当做error类型使用
func (err *StratumError) Error() string {
	return err.ErrMsg
}

// ToJSONRPCArray 转换为JSONRPCArray
func (err *StratumError) ToJSONRPCArray(extData interface{}) JSONRPCArray {
	if err == nil {
		return nil
	}

	return JSONRPCArray{err.ErrNo, err.ErrMsg, extData}
}

var (
	// ErrBufIOReadTimeout 从bufio.Reader中读取数据时超时
	ErrBufIOReadTimeout = errors.New("bufIO read timeout")
	// ErrSessionIDFull SessionID已满（所有可用值均已分配）
	ErrSessionIDFull = errors.New("session id is full")
	// ErrSessionIDOccupied SessionID已被占用（恢复SessionID时）
	ErrSessionIDOccupied = errors.New("session id has been occupied")
	// ErrParseSubscribeResponseFailed 解析订阅响应失败
	ErrParseSubscribeResponseFailed = errors.New("parse subscribe response failed")
	// ErrSessionIDInconformity 返回的会话ID和当前保存的不匹配
	ErrSessionIDInconformity = errors.New("session id inconformity")
	// ErrAuthorizeFailed 认证失败
	ErrAuthorizeFailed = errors.New("authorize failed")
	// ErrTooMuchPendingAutoRegReq 太多等待中的自动注册请求
	ErrTooMuchPendingAutoRegReq = errors.New("too much pending auto reg request")
)

var (
	// StratumErrJobNotFound 任务不存在
	StratumErrJobNotFound = NewStratumError(21, "Job not found (=stale)")
	// StratumErrNeedAuthorized 需要认证
	StratumErrNeedAuthorized = NewStratumError(24, "Unauthorized worker")
	// StratumErrNeedSubscribed 需要订阅
	StratumErrNeedSubscribed = NewStratumError(25, "Not subscribed")
	// StratumErrIllegalParams 参数非法
	StratumErrIllegalParams = NewStratumError(27, "Illegal params")
	// StratumErrTooFewParams 参数太少
	StratumErrTooFewParams = NewStratumError(27, "Too few params")
	// StratumErrDuplicateSubscribed 重复订阅
	StratumErrDuplicateSubscribed = NewStratumError(102, "Duplicate Subscribed")
	// StratumErrWorkerNameMustBeString 矿工名必须是字符串
	StratumErrWorkerNameMustBeString = NewStratumError(104, "Worker Name Must be a String")
	// StratumErrSubAccountNameEmpty 子账户名为空
	StratumErrSubAccountNameEmpty = NewStratumError(105, "Sub-account Name Cannot be Empty")

	// StratumErrStratumServerNotFound 找不到对应币种的Stratum Server
	StratumErrStratumServerNotFound = NewStratumError(301, "Stratum Server Not Found")
	// StratumErrConnectStratumServerFailed 对应币种的Stratum Server连接失败
	StratumErrConnectStratumServerFailed = NewStratumError(302, "Connect Stratum Server Failed")

	// StratumErrUnknownChainType 未知区块链类型
	StratumErrUnknownChainType = NewStratumError(500, "Unknown Chain Type")
)

var (
	// ErrReadFailed IO读错误
	ErrReadFailed = errors.New("read failed")
	// ErrWriteFailed IO写错误
	ErrWriteFailed = errors.New("write failed")
	// ErrInvalidReader 非法Reader
	ErrInvalidReader = errors.New("invalid reader")
	// ErrInvalidWritter 非法Writter
	ErrInvalidWritter = errors.New("invalid writter")
	// ErrInvalidBuffer 非法Buffer
	ErrInvalidBuffer = errors.New("invalid buffer")
	// ErrConnectionClosed 连接已关闭
	ErrConnectionClosed = errors.New("connection closed")
)
