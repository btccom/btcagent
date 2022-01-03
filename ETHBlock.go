package main

import (
	"math/big"

	"github.com/ethereum/go-ethereum/common"
	"github.com/ethereum/go-ethereum/core/types"
	"github.com/ethereum/go-ethereum/rlp"
	"github.com/golang/glog"
	"golang.org/x/crypto/sha3"
)

type ETHPoWHeader struct {
	ParentHash  common.Hash
	UncleHash   common.Hash
	Coinbase    common.Address
	Root        common.Hash
	TxHash      common.Hash
	ReceiptHash common.Hash
	Bloom       types.Bloom
	Difficulty  *big.Int
	Number      *big.Int
	GasLimit    uint64
	GasUsed     uint64
	Time        uint64
	Extra       []byte
	BaseFee     *big.Int `rlp:"optional"`
}

func (header *ETHPoWHeader) Decode(bin []byte) error {
	return rlp.DecodeBytes(bin, header)
}

func (header *ETHPoWHeader) Encode() ([]byte, error) {
	return rlp.EncodeToBytes(header)
}

func (header *ETHPoWHeader) Hash(extraNonce uint32) (hash common.Hash) {
	hasher := sha3.NewLegacyKeccak256()

	enc := []interface{}{
		header.ParentHash,
		header.UncleHash,
		header.Coinbase,
		header.Root,
		header.TxHash,
		header.ReceiptHash,
		header.Bloom,
		header.Difficulty,
		header.Number,
		header.GasLimit,
		header.GasUsed,
		header.Time,
		header.GetExtraWithNonce(extraNonce),
	}

	if header.BaseFee != nil {
		enc = append(enc, header.BaseFee)
	}

	rlp.Encode(hasher, enc)
	hasher.Sum(hash[:0])

	return hash
}

func (header *ETHPoWHeader) GetExtraWithNonce(extraNonce uint32) (data []byte) {
	nonce := Uint32ToBin(extraNonce)
	pos := len(header.Extra) - len(nonce)
	if pos < 0 {
		glog.Error("ETHPoWHeader.SetExtraNonce: Extra too small: ", header)
		return
	}
	data = make([]byte, len(header.Extra))
	copy(data[:pos], header.Extra[:pos])
	copy(data[pos:], nonce[:])
	return
}

func (header *ETHPoWHeader) SetTime(time uint64) {
	header.Time = time
}
