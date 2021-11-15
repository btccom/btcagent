package main

import (
	"bytes"
	"encoding/binary"
	"encoding/hex"
	"io"
	"net"
	"regexp"
	"strconv"
	"strings"
)

// IP2Long IP转整数
// 来自 <https://www.socketloop.com/tutorials/golang-convert-ip-address-string-to-long-unsigned-32-bit-integer>
func IP2Long(ip string) uint32 {
	var long uint32
	binary.Read(bytes.NewBuffer(net.ParseIP(ip).To4()), binary.BigEndian, &long)
	return long
}

// Long2IP 整数转IP
// 来自 <https://www.socketloop.com/tutorials/golang-convert-ip-address-string-to-long-unsigned-32-bit-integer>
func Long2IP(ipLong uint32) string {
	ipInt := int64(ipLong)

	// need to do two bit shifting and “0xff” masking
	b0 := strconv.FormatInt((ipInt>>24)&0xff, 10)
	b1 := strconv.FormatInt((ipInt>>16)&0xff, 10)
	b2 := strconv.FormatInt((ipInt>>8)&0xff, 10)
	b3 := strconv.FormatInt((ipInt & 0xff), 10)
	return b0 + "." + b1 + "." + b2 + "." + b3
}

// Uint32ToHex unit32 转 hex
func Uint32ToHex(num uint32) string {
	bytesBuffer := bytes.NewBuffer([]byte{})
	binary.Write(bytesBuffer, binary.BigEndian, num)
	return hex.EncodeToString(bytesBuffer.Bytes())
}

// Uint32ToHexLE unit32 转 hex (小端字节序)
func Uint32ToHexLE(num uint32) string {
	bytesBuffer := bytes.NewBuffer([]byte{})
	binary.Write(bytesBuffer, binary.LittleEndian, num)
	return hex.EncodeToString(bytesBuffer.Bytes())
}

// SubString 字符串截取
// <http://outofmemory.cn/code-snippet/1365/Go-language-jiequ-string-function>
func SubString(str string, start, length int) string {
	rs := []rune(str)
	rl := len(rs)
	end := 0

	if start < 0 {
		start = rl - 1 + start
	}
	end = start + length

	if start > end {
		start, end = end, start
	}

	if start < 0 {
		start = 0
	}
	if start > rl {
		start = rl
	}
	if end < 0 {
		end = 0
	}
	if end > rl {
		end = rl
	}

	return string(rs[start:end])
}

// IOCopyBuffer 在写入失败时还能拿到Buffer的IO拷贝函数
func IOCopyBuffer(dst io.Writer, src io.Reader, buf []byte) (bufferLen int, err error) {
	if buf == nil {
		err = ErrInvalidBuffer
		return
	}
	if src == nil {
		err = ErrInvalidReader
		return
	}
	if dst == nil {
		err = ErrInvalidWritter
		return
	}
	for {
		nr, er := src.Read(buf)
		bufferLen = nr
		if nr > 0 {
			nw, ew := dst.Write(buf[0:nr])
			if ew != nil {
				err = ErrWriteFailed
				break
			}
			if nr != nw {
				err = ErrWriteFailed
				break
			}
		}
		if er != nil {
			err = ErrReadFailed
			break
		}
	}
	return
}

// StripEthAddrFromFullName 从矿机名中去除不必要的以太坊钱包地址
func StripEthAddrFromFullName(fullNameStr string) string {
	pos := strings.Index(fullNameStr, ".")

	// The Ethereum address is 42 bytes and starting with "0x" as normal
	// Example: 0x00d8c82Eb65124Ea3452CaC59B64aCC230AA3482
	if pos == 42 && fullNameStr[0] == '0' && (fullNameStr[1] == 'x' || fullNameStr[1] == 'X') {
		return fullNameStr[pos+1:]
	}

	return fullNameStr
}

// FilterWorkerName 过滤矿工名
func FilterWorkerName(workerName string) string {
	pattren := regexp.MustCompile("[^a-zA-Z0-9._:|^/-]")
	return pattren.ReplaceAllString(workerName, "")
}

func IPAsWorkerName(format string, ip string) string {
	if len(ip) < 1 {
		return ip
	}

	addr, err := net.ResolveTCPAddr("tcp", ip)
	if err != nil {
		return ip
	}

	len := len(addr.IP)
	if len < 4 {
		return ip
	}

	var base int
	if ip[0] == '[' {
		base = 16
	} else {
		base = 10
	}

	format = strings.ReplaceAll(format, "{1}", strconv.FormatUint(uint64(addr.IP[len-4]), base))
	format = strings.ReplaceAll(format, "{2}", strconv.FormatUint(uint64(addr.IP[len-3]), base))
	format = strings.ReplaceAll(format, "{3}", strconv.FormatUint(uint64(addr.IP[len-2]), base))
	format = strings.ReplaceAll(format, "{4}", strconv.FormatUint(uint64(addr.IP[len-1]), base))
	return format
}
