package main

import (
	"bytes"
	"encoding/binary"
)

// ex-message的magic number
const ExMessageMagicNumber uint8 = 0x7F

// types
const (
	CMD_REGISTER_WORKER            uint8 = 0x01 // Agent -> Pool
	CMD_SUBMIT_SHARE               uint8 = 0x02 // Agent -> Pool,  mining.submit(...)
	CMD_SUBMIT_SHARE_WITH_TIME     uint8 = 0x03 // Agent -> Pool,  mining.submit(..., nTime)
	CMD_UNREGISTER_WORKER          uint8 = 0x04 // Agent -> Pool
	CMD_MINING_SET_DIFF            uint8 = 0x05 // Pool  -> Agent, mining.set_difficulty(diff)
	CMD_SUBMIT_RESPONSE            uint8 = 0x10 // Pool  -> Agent, response of the submit (optional)
	CMD_SUBMIT_SHARE_WITH_VER      uint8 = 0x12 // Agent -> Pool,  mining.submit(..., nVersionMask)
	CMD_SUBMIT_SHARE_WITH_TIME_VER uint8 = 0x13 // Agent -> Pool,  mining.submit(..., nTime, nVersionMask)
	CMD_GET_NONCE_PREFIX           uint8 = 0x21 // Agent -> Pool,  ask the pool to allocate nonce prefix (Ethereum)
	CMD_SET_NONCE_PREFIX           uint8 = 0x22 // Pool  -> Agent, pool nonce prefix allocation result (Ethereum)
)

type SerializableExMessage interface {
	Serialize() []byte
}

type UnserializableExMessage interface {
	Unserialize(data []byte) (err error)
}

type ExMessageHeader struct {
	MagicNumber uint8
	Type        uint8
	Size        uint16
}

type ExMessage struct {
	ExMessageHeader
	Body []byte
}

type ExMessageRegisterWorker struct {
	SessionID   uint16
	ClientAgent string
	WorkerName  string
}

func (msg *ExMessageRegisterWorker) Serialize() []byte {
	header := ExMessageHeader{
		ExMessageMagicNumber,
		CMD_REGISTER_WORKER,
		uint16(4 + 2 + len(msg.ClientAgent) + 1 + len(msg.WorkerName) + 1)}

	buf := new(bytes.Buffer)

	binary.Write(buf, binary.LittleEndian, &header)
	binary.Write(buf, binary.LittleEndian, msg.SessionID)
	buf.WriteString(msg.ClientAgent)
	buf.WriteByte(0)
	buf.WriteString(msg.WorkerName)
	buf.WriteByte(0)

	return buf.Bytes()
}

type ExMessageUnregisterWorker struct {
	SessionID uint16
}

func (msg *ExMessageUnregisterWorker) Serialize() []byte {
	header := ExMessageHeader{
		ExMessageMagicNumber,
		CMD_UNREGISTER_WORKER,
		uint16(4 + 2)}

	buf := new(bytes.Buffer)

	binary.Write(buf, binary.LittleEndian, &header)
	binary.Write(buf, binary.LittleEndian, msg.SessionID)

	return buf.Bytes()
}

type ExMessageSubmitShare struct {
	Base struct {
		JobID       uint8
		SessionID   uint16
		ExtraNonce2 uint32
		Nonce       uint32
	}

	Time        uint32
	VersionMask uint32

	IsFakeJob bool
}

func (msg *ExMessageSubmitShare) Serialize() []byte {
	var header ExMessageHeader
	header.MagicNumber = ExMessageMagicNumber

	if msg.Time == 0 {
		if msg.VersionMask == 0 {
			header.Type = CMD_SUBMIT_SHARE
			header.Size = 4 + 1 + 2 + 4 + 4
		} else {
			header.Type = CMD_SUBMIT_SHARE_WITH_VER
			header.Size = 4 + 1 + 2 + 4 + 4 + 4
		}
	} else {
		if msg.VersionMask == 0 {
			header.Type = CMD_SUBMIT_SHARE_WITH_TIME
			header.Size = 4 + 1 + 2 + 4 + 4 + 4
		} else {
			header.Type = CMD_SUBMIT_SHARE_WITH_TIME_VER
			header.Size = 4 + 1 + 2 + 4 + 4 + 4 + 4
		}
	}

	buf := new(bytes.Buffer)

	binary.Write(buf, binary.LittleEndian, &header)
	binary.Write(buf, binary.LittleEndian, &msg.Base)
	if msg.Time != 0 {
		binary.Write(buf, binary.LittleEndian, msg.Time)
	}
	if msg.VersionMask != 0 {
		binary.Write(buf, binary.LittleEndian, msg.VersionMask)
	}

	return buf.Bytes()
}

type ExMessageMiningSetDiff struct {
	Base struct {
		DiffExp uint8
		Count   uint16
	}
	SessionIDs []uint16
}

func (msg *ExMessageMiningSetDiff) Unserialize(data []byte) (err error) {
	buf := bytes.NewReader(data)

	err = binary.Read(buf, binary.LittleEndian, &msg.Base)
	if err != nil || msg.Base.Count == 0 {
		return
	}

	msg.SessionIDs = make([]uint16, msg.Base.Count)
	err = binary.Read(buf, binary.LittleEndian, msg.SessionIDs)
	return
}

type ExMessageSubmitResponse struct {
	Index  uint16
	Status StratumStatus
}

func (msg *ExMessageSubmitResponse) Unserialize(data []byte) (err error) {
	buf := bytes.NewReader(data)
	err = binary.Read(buf, binary.LittleEndian, msg)
	return
}
