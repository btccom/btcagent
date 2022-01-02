package main

import (
	"fmt"
	"time"
)

type StratumJobETH struct {
	JobID    []byte
	SeedHash []byte
	Header   []byte
	BaseFee  []byte
	IsClean  bool
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
	job.Header, err = Hex2Bin(json.Header)
	if err != nil {
		err = fmt.Errorf("failed to decode header: %s", err.Error())
		return
	}

	job.BaseFee, err = Hex2Bin(json.BaseFee)
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

	// succeeded
	return
}

func (job *StratumJobETH) ToNotifyLine(firstJob bool) (bytes []byte, err error) {
	// TODO: finish it
	return
}

func IsFakeJobIDETH(id string) bool {
	id = HexRemovePrefix(id)
	return len(id) == len(FakeJobIDETHPrefix)+16 && id[:len(FakeJobIDETHPrefix)] == FakeJobIDETHPrefix
}

func (job *StratumJobETH) ToNewFakeJob() {
	now := uint64(time.Now().Unix())
	job.Header = Uint64ToBin(now)

	// fake job id
	job.JobID = FakeJobIDETHPrefixBin
	job.JobID = append(job.JobID, job.Header...)
}
