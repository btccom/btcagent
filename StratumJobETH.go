package main

import (
	"encoding/hex"
	"fmt"
	"time"

	"github.com/holiman/uint256"
)

type StratumJobETH struct {
	JobID     []byte
	SeedHash  []byte
	IsClean   bool
	IsFakeJob bool
	PoWHeader ETHPoWHeader
}

type JSONRPCJobETH struct {
	ID     interface{}   `json:"id"`
	Method string        `json:"method"`
	Params []interface{} `json:"params"`
	Height uint64        `json:"height"`
}

type JSONRPC2JobETH struct {
	ID      int           `json:"id"`
	JSONRPC string        `json:"jsonrpc"`
	Result  []interface{} `json:"result"`
	Height  uint64        `json:"height"`
}

func NewStratumJobETH(json *JSONRPCLineETH, sessionID uint32) (job *StratumJobETH, err error) {
	/*
		Fields in order:
		["params"]
			[0] job id
			[1] seed hash
			[2] header hash
			[3] is clean
		["header"] block header hex
		["basefee"] (optional) base fee for EIP-1559
	*/
	job = new(StratumJobETH)

	if len(json.Header) < 1 {
		err = fmt.Errorf("notify missing field header")
		return
	}
	header, err := Hex2Bin(json.Header)
	if err != nil {
		err = fmt.Errorf("failed to decode header: %s", err.Error())
		return
	}

	baseFee, err := Hex2Bin(json.BaseFee)
	if err != nil {
		err = fmt.Errorf("failed to decode base fee: %s", err.Error())
		return
	}

	if len(json.Params) < 4 {
		err = fmt.Errorf("notify missing fields, should be 4 fields but only %d", len(json.Params))
		return
	}

	jobID, ok := json.Params[0].(string)
	if !ok {
		err = fmt.Errorf("job id is not a string")
		return
	}
	job.JobID, err = Hex2Bin(jobID)
	if err != nil {
		err = fmt.Errorf("failed to decode job id: %s", err.Error())
		return
	}
	BinReverse(job.JobID) // btcpool使用小端字节序

	seedHash, ok := json.Params[1].(string)
	if !ok {
		err = fmt.Errorf("seed hash is not a string")
		return
	}
	job.SeedHash, err = Hex2Bin(seedHash)
	if err != nil {
		err = fmt.Errorf("failed to decode seed hash: %s", err.Error())
		return
	}

	header = append(header, 0, 0, 0, 0) // append extra nonce
	header = append(header, baseFee...) // append base fee
	err = job.PoWHeader.Decode(header)
	if err != nil {
		err = fmt.Errorf("failed to decode block header: %s", err.Error())
		return
	}

	// succeeded
	return
}

func (job *StratumJobETH) PoWHash(extraNonce uint32) string {
	if job.IsFakeJob {
		return hex.EncodeToString(job.JobID)
	}
	return hex.EncodeToString(job.PoWHeader.Hash(extraNonce).Bytes())
}

func IsFakeJobIDETH(id string) bool {
	id = HexRemovePrefix(id)
	return len(id) == len(FakeJobIDETHPrefix)+16 && id[:len(FakeJobIDETHPrefix)] == FakeJobIDETHPrefix
}

func (job *StratumJobETH) ToNewFakeJob() {
	job.IsFakeJob = true

	now := uint64(time.Now().Unix())
	job.PoWHeader.SetTime(now)

	// fake job id
	job.JobID = FakeJobIDETHPrefixBin
	job.JobID = append(job.JobID, Uint64ToBin(now)...)
}

func (job *StratumJobETH) Height() uint64 {
	return job.PoWHeader.Number.Uint64()
}

func DiffToTargetETH(diff uint64) (target string) {
	var result uint256.Int
	result.SetUint64(diff)
	result.Div(&EthereumPoolDiff1, &result)
	bin := result.Bytes32()
	target = hex.EncodeToString(bin[:])
	return
}
