package main

type StratumStatus uint32

const (
	// make ACCEPT and SOLVED be two singular value
	// so code bug is unlikely to make false ACCEPT shares

	// share reached the job target (but may not reached the network target)
	STATUS_ACCEPT StratumStatus = 1798084231 // bin(01101011 00101100 10010110 10000111)

	// share reached the job target but the job is stale
	// if uncle block is allowed in the chain share can be accept as this
	// status
	STATUS_ACCEPT_STALE StratumStatus = 950395421 // bin(00111000 10100101 11100010 00011101)

	// share reached the network target
	STATUS_SOLVED StratumStatus = 1422486894 // bin(‭01010100 11001001 01101101 01101110‬)

	// share reached the network target but the job is stale
	// if uncle block is allowed in the chain share can be accept as this
	// status
	STATUS_SOLVED_STALE StratumStatus = 1713984938 // bin(01100110 00101001 01010101 10101010)

	// share reached the network target but the correctness is not verified
	STATUS_SOLVED_PRELIMINARY StratumStatus = 1835617709 // // bin(01101101 01101001 01001101 10101101)

	STATUS_REJECT_NO_REASON StratumStatus = 0

	STATUS_JOB_NOT_FOUND_OR_STALE StratumStatus = 21
	STATUS_DUPLICATE_SHARE        StratumStatus = 22
	STATUS_LOW_DIFFICULTY         StratumStatus = 23
	STATUS_UNAUTHORIZED           StratumStatus = 24
	STATUS_NOT_SUBSCRIBED         StratumStatus = 25

	STATUS_ILLEGAL_METHOD   StratumStatus = 26
	STATUS_ILLEGAL_PARARMS  StratumStatus = 27
	STATUS_IP_BANNED        StratumStatus = 28
	STATUS_INVALID_USERNAME StratumStatus = 29
	STATUS_INTERNAL_ERROR   StratumStatus = 30
	STATUS_TIME_TOO_OLD     StratumStatus = 31
	STATUS_TIME_TOO_NEW     StratumStatus = 32
	STATUS_ILLEGAL_VERMASK  StratumStatus = 33

	STATUS_INVALID_SOLUTION   StratumStatus = 34
	STATUS_WRONG_NONCE_PREFIX StratumStatus = 35

	STATUS_JOB_NOT_FOUND        StratumStatus = 36
	STATUS_STALE_SHARE          StratumStatus = 37
	STATUS_NICEHASH_UNSUPPORTED StratumStatus = 38

	STATUS_CLIENT_IS_NOT_SWITCHER StratumStatus = 400

	STATUS_UNKNOWN StratumStatus = 2147483647 // bin(01111111 11111111 11111111 11111111)
)

func (status StratumStatus) IsAccepted() bool {
	return (status == STATUS_ACCEPT) || (status == STATUS_ACCEPT_STALE) ||
		(status == STATUS_SOLVED) || (status == STATUS_SOLVED_STALE)
}

func (status StratumStatus) IsAcceptedStale() bool {
	return (status == STATUS_ACCEPT_STALE) || (status == STATUS_SOLVED_STALE)
}

func (status StratumStatus) IsRejectedStale() bool {
	return (status == STATUS_JOB_NOT_FOUND_OR_STALE) || (status == STATUS_STALE_SHARE)
}

func (status StratumStatus) IsAnyStale() bool {
	return status.IsAcceptedStale() || status.IsRejectedStale()
}

func (status StratumStatus) IsSolved() bool {
	return (status == STATUS_SOLVED) || (status == STATUS_SOLVED_STALE) ||
		(status == STATUS_SOLVED_PRELIMINARY)
}

func (status StratumStatus) ToString() string {
	switch status {
	case STATUS_ACCEPT:
		return "Share accepted"
	case STATUS_ACCEPT_STALE:
		return "Share accepted (stale)"
	case STATUS_SOLVED:
		return "Share accepted and solved"
	case STATUS_SOLVED_STALE:
		return "Share accepted and solved (stale)"
	case STATUS_REJECT_NO_REASON:
		return "Share rejected"

	case STATUS_JOB_NOT_FOUND_OR_STALE:
		return "Job not found (=stale)"
	case STATUS_DUPLICATE_SHARE:
		return "Duplicate share"
	case STATUS_LOW_DIFFICULTY:
		return "Low difficulty"
	case STATUS_UNAUTHORIZED:
		return "Unauthorized worker"
	case STATUS_NOT_SUBSCRIBED:
		return "Not subscribed"

	case STATUS_ILLEGAL_METHOD:
		return "Illegal method"
	case STATUS_ILLEGAL_PARARMS:
		return "Illegal params"
	case STATUS_IP_BANNED:
		return "Ip banned"
	case STATUS_INVALID_USERNAME:
		return "Invalid username"
	case STATUS_INTERNAL_ERROR:
		return "Internal error"
	case STATUS_TIME_TOO_OLD:
		return "Time too old"
	case STATUS_TIME_TOO_NEW:
		return "Time too new"
	case STATUS_ILLEGAL_VERMASK:
		return "Invalid version mask"

	case STATUS_INVALID_SOLUTION:
		return "Invalid Solution"
	case STATUS_WRONG_NONCE_PREFIX:
		return "Wrong Nonce Prefix"

	case STATUS_JOB_NOT_FOUND:
		return "Job not found"
	case STATUS_STALE_SHARE:
		return "Stale share"
	case STATUS_NICEHASH_UNSUPPORTED:
		return "Nichhash is not supported"

	case STATUS_CLIENT_IS_NOT_SWITCHER:
		return "Client is not a stratum switcher"

	case STATUS_UNKNOWN:
		return "Unknown"
	default:
		return "Unknown"
	}
}

func (status StratumStatus) ToStratumError() *StratumError {
	return NewStratumError(int(status), status.ToString())
}

func (status StratumStatus) ToJSONRPCArray(extData interface{}) JSONRPCArray {
	return JSONRPCArray{int(status), status.ToString(), extData}
}
