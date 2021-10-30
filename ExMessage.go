package main

type ExMessageHeader struct {
	MagicNumber uint8
	Type        uint8
	Size        uint16
}

type ExMessage struct {
	ExMessageHeader
	Body []byte
}
