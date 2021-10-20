package main

import (
	"encoding/json"
)

// JSONRPCRequest JSON RPC 请求的数据结构
type JSONRPCRequest struct {
	ID     interface{}   `json:"id"`
	Method string        `json:"method"`
	Params []interface{} `json:"params"`

	// Worker: ETHProxy from ethminer may contains this field
	Worker string `json:"worker,omitempty"`
}

// JSONRPCResponse JSON RPC 响应的数据结构
type JSONRPCResponse struct {
	ID     interface{} `json:"id"`
	Result interface{} `json:"result"`
	Error  interface{} `json:"error"`
}

// JSONRPC2Error error object of json-rpc 2.0
type JSONRPC2Error struct {
	Code    int         `json:"code"`
	Message string      `json:"message"`
	Data    interface{} `json:"data,omitempty"`
}

// NewJSONRPC2Error create json-rpc 2.0 error object from json-1.0 error object
func NewJSONRPC2Error(v1Err interface{}) (err *JSONRPC2Error) {
	if v1Err == nil {
		return nil
	}

	errArr, ok := v1Err.(JSONRPCArray)
	if !ok {
		return nil
	}

	err = new(JSONRPC2Error)
	if len(errArr) >= 1 {
		code, ok := errArr[0].(int)
		if ok {
			err.Code = code
		}
	}
	if len(errArr) >= 2 {
		message, ok := errArr[1].(string)
		if ok {
			err.Message = message
		}
	}
	if len(errArr) >= 3 {
		err.Data = errArr[2]
	}
	return
}

// JSONRPC2Response response message of json-rpc 2.0
type JSONRPC2Response struct {
	ID      interface{}    `json:"id"`
	JSONRPC string         `json:"jsonrpc"`
	Result  interface{}    `json:"result,omitempty"`
	Error   *JSONRPC2Error `json:"error,omitempty"`
}

// JSONRPCArray JSON RPC 数组
type JSONRPCArray []interface{}

// JSONRPCObj JSON RPC 对象
type JSONRPCObj map[string]interface{}

// NewJSONRPCRequest 解析 JSON RPC 请求字符串并创建 JSONRPCRequest 对象
func NewJSONRPCRequest(rpcJSON []byte) (*JSONRPCRequest, error) {
	rpcData := new(JSONRPCRequest)

	err := json.Unmarshal(rpcJSON, &rpcData)

	return rpcData, err
}

// AddParam 向 JSONRPCRequest 对象添加一个或多个参数
func (rpcData *JSONRPCRequest) AddParam(param ...interface{}) {
	rpcData.Params = append(rpcData.Params, param...)
}

// SetParam 设置 JSONRPCRequest 对象的参数
// 传递给 SetParam 的参数列表将按顺序复制到 JSONRPCRequest.Params 中
func (rpcData *JSONRPCRequest) SetParam(param ...interface{}) {
	rpcData.Params = param
}

// ToJSONBytes 将 JSONRPCRequest 对象转换为 JSON 字节序列
func (rpcData *JSONRPCRequest) ToJSONBytes() ([]byte, error) {
	return json.Marshal(rpcData)
}

// NewJSONRPCResponse 解析 JSON RPC 响应字符串并创建 JSONRPCResponse 对象
func NewJSONRPCResponse(rpcJSON []byte) (*JSONRPCResponse, error) {
	rpcData := new(JSONRPCResponse)

	err := json.Unmarshal(rpcJSON, &rpcData)

	return rpcData, err
}

// SetResult 设置 JSONRPCResponse 对象的返回结果
func (rpcData *JSONRPCResponse) SetResult(result interface{}) {
	rpcData.Result = result
}

// ToJSONBytes 将 JSONRPCResponse 对象转换为 JSON 字节序列
func (rpcData *JSONRPCResponse) ToJSONBytes(version int) ([]byte, error) {
	if version == 1 {
		return json.Marshal(rpcData)
	}

	rpc2Data := JSONRPC2Response{rpcData.ID, "2.0", rpcData.Result, NewJSONRPC2Error(rpcData.Error)}
	return json.Marshal(rpc2Data)
}
