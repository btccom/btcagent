package main

import (
	"errors"
	"fmt"
	"time"
)

type StratumJobBTC struct {
	JSONRPCRequest
}

func NewStratumJobBTC(json *JSONRPCLineBTC, sessionID uint32) (job *StratumJobBTC, err error) {
	/*
		Fields in order:
			[0] Job ID. This is included when miners submit a results so work can be matched with proper transactions.
			[1] Hash of previous block. Used to build the header.
			[2] Generation transaction (part 1). The miner inserts ExtraNonce1 and ExtraNonce2 after this section of the transaction data.
			[3] Generation transaction (part 2). The miner appends this after the first part of the transaction data and the two ExtraNonce values.
			[4] List of merkle branches. The generation transaction is hashed against the merkle branches to build the final merkle root.
			[5] Bitcoin block version. Used in the block header.
			[6] nBits. The encoded network difficulty. Used in the block header.
			[7] nTime. The current time. nTime rolling should be supported, but should not increase faster than actual time.
			[8] Clean Jobs. If true, miners should abort their current work and immediately use the new job. If false, they can still use the current job, but should move to the new one after exhausting the current nonce range.
	*/
	job = new(StratumJobBTC)
	job.ID = json.ID
	job.Method = json.Method
	job.Params = json.Params

	if len(job.Params) < 9 {
		err = fmt.Errorf("notify missing fields, should be 9 fields but only %d", len(job.Params))
		return
	}

	coinbase1, ok := job.Params[2].(string)
	if !ok {
		err = errors.New("wrong notify format, coinbase1 is not a string")
		return
	}

	job.Params[2] = coinbase1 + Uint32ToHex(sessionID)

	return
}

func (job *StratumJobBTC) ToNotifyLine(firstJob bool) (bytes []byte, err error) {
	if firstJob {
		job.Params[8] = true
	}

	return job.ToJSONBytesLine()
}

func IsFakeJobIDBTC(id string) bool {
	return len(id) < 1 || id[0] == 'f'
}

func (job *StratumJobBTC) ToNewFakeJob() {
	now := uint64(time.Now().Unix())

	// job id
	job.Params[0] = fmt.Sprintf("f%d", now%0xffff)

	coinbase1, _ := job.Params[2].(string)
	pos := len(coinbase1) - 8
	if pos < 0 {
		pos = 0
	}

	// coinbase1
	job.Params[2] = coinbase1[:pos] + Uint64ToHex(now)
}
