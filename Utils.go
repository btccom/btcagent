package main

import (
	"bytes"
	"encoding/binary"
	"encoding/hex"
	"fmt"
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

// Uint64ToBin unit64 转二进制
func Uint64ToBin(num uint64) []byte {
	bytesBuffer := bytes.NewBuffer([]byte{})
	binary.Write(bytesBuffer, binary.BigEndian, num)
	return bytesBuffer.Bytes()
}

// Uint64ToHex unit64 转 hex
func Uint64ToHex(num uint64) string {
	bytesBuffer := bytes.NewBuffer([]byte{})
	binary.Write(bytesBuffer, binary.BigEndian, num)
	return hex.EncodeToString(bytesBuffer.Bytes())
}

// Uint32ToBin unit32 转二进制
func Uint32ToBin(num uint32) []byte {
	bytesBuffer := bytes.NewBuffer([]byte{})
	binary.Write(bytesBuffer, binary.BigEndian, num)
	return bytesBuffer.Bytes()
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

// Uint16ToHex uint16 转 hex
func Uint16ToHex(num uint16) string {
	bytesBuffer := bytes.NewBuffer([]byte{})
	binary.Write(bytesBuffer, binary.BigEndian, num)
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

	// The Ethereum address is 42 bytes and starting with "0x".
	// Example: 0x00d8c82Eb65124Ea3452CaC59B64aCC230AA3482
	if pos == 42 && fullNameStr[0] == '0' && (fullNameStr[1] == 'x' || fullNameStr[1] == 'X') {
		return fullNameStr[pos+1:]
	}

	return fullNameStr
}

// FilterWorkerName 过滤矿工名
func FilterWorkerName(workerName string) string {
	pattren := regexp.MustCompile(`[^a-zA-Z0-9,=/.\-_:|^]`)
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

func IsEnabled(option bool) string {
	if option {
		return "Enabled"
	}
	return "Disabled"
}

func HexAddPrefix(hexStr string) string {
	if len(hexStr) == 0 || (len(hexStr) >= 2 && hexStr[0] == '0' && (hexStr[1] == 'x' || hexStr[1] == 'X')) {
		return hexStr
	}
	return "0x" + hexStr
}

func HexRemovePrefix(hexStr string) string {
	// remove prefix "0x" or "0X"
	if len(hexStr) >= 2 && hexStr[0] == '0' && (hexStr[1] == 'x' || hexStr[1] == 'X') {
		hexStr = hexStr[2:]
	}
	return hexStr
}

func Hex2Bin(hexStr string) (bin []byte, err error) {
	hexStr = HexRemovePrefix(hexStr)
	bin, err = hex.DecodeString(hexStr)
	return
}

func Hex2Uint64(hexStr string) (result uint64, err error) {
	hexStr = HexRemovePrefix(hexStr)
	result, err = strconv.ParseUint(hexStr, 16, 64)
	return
}

// BinReverse 颠倒字节序列
func BinReverse(bin []byte) {
	i := 0
	j := len(bin) - 1
	for i < j {
		bin[i], bin[j] = bin[j], bin[i]
		i++
		j--
	}
}

// VersionMaskStr 获取version mask的字符串表示
func VersionMaskStr(mask uint32) string {
	return fmt.Sprintf("%08x", mask)
}
