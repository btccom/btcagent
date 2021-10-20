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
	ErrBufIOReadTimeout = errors.New("BufIO Read Timeout")
	// ErrSessionIDFull SessionID已满（所有可用值均已分配）
	ErrSessionIDFull = errors.New("Session ID is Full")
	// ErrSessionIDOccupied SessionID已被占用（恢复SessionID时）
	ErrSessionIDOccupied = errors.New("Session ID has been occupied")
	// ErrParseSubscribeResponseFailed 解析订阅响应失败
	ErrParseSubscribeResponseFailed = errors.New("Parse Subscribe Response Failed")
	// ErrSessionIDInconformity 返回的会话ID和当前保存的不匹配
	ErrSessionIDInconformity = errors.New("Session ID Inconformity")
	// ErrAuthorizeFailed 认证失败
	ErrAuthorizeFailed = errors.New("Authorize Failed")
	// ErrTooMuchPendingAutoRegReq 太多等待中的自动注册请求
	ErrTooMuchPendingAutoRegReq = errors.New("Too much pending auto reg request")
)

var (
	// StratumErrNeedSubscribed 需要订阅
	StratumErrNeedSubscribed = NewStratumError(101, "Need Subscribed")
	// StratumErrDuplicateSubscribed 重复订阅
	StratumErrDuplicateSubscribed = NewStratumError(102, "Duplicate Subscribed")
	// StratumErrTooFewParams 参数太少
	StratumErrTooFewParams = NewStratumError(103, "Too Few Params")
	// StratumErrWorkerNameMustBeString 矿工名必须是字符串
	StratumErrWorkerNameMustBeString = NewStratumError(104, "Worker Name Must be a String")
	// StratumErrWorkerNameStartWrong 矿工名开头错误
	StratumErrWorkerNameStartWrong = NewStratumError(105, "Sub-account Name Cannot be Empty")

	// StratumErrStratumServerNotFound 找不到对应币种的Stratum Server
	StratumErrStratumServerNotFound = NewStratumError(301, "Stratum Server Not Found")
	// StratumErrConnectStratumServerFailed 对应币种的Stratum Server连接失败
	StratumErrConnectStratumServerFailed = NewStratumError(302, "Connect Stratum Server Failed")

	// StratumErrUnknownChainType 未知区块链类型
	StratumErrUnknownChainType = NewStratumError(500, "Unknown Chain Type")
)

var (
	// ErrReadFailed IO读错误
	ErrReadFailed = errors.New("Read Failed")
	// ErrWriteFailed IO写错误
	ErrWriteFailed = errors.New("Write Failed")
	// ErrInvalidReader 非法Reader
	ErrInvalidReader = errors.New("Invalid Reader")
	// ErrInvalidWritter 非法Writter
	ErrInvalidWritter = errors.New("Invalid Writter")
	// ErrInvalidBuffer 非法Buffer
	ErrInvalidBuffer = errors.New("Invalid Buffer")
)
