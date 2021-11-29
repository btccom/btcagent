package main

import (
	"fmt"
	"net"
	"net/url"
	"os"
	"strings"
	"time"

	"github.com/btccom/connectproxy"
	"golang.org/x/net/proxy"
)

type Dialer interface {
	// Dial connects to the given address.
	Dial(network, addr string) (net.Conn, error)
}

func GetProxyURLFromEnv() (url string) {
	url = os.Getenv("ALL_PROXY")
	if len(url) < 1 {
		url = os.Getenv("all_proxy")
	}
	if len(url) < 1 {
		url = os.Getenv("HTTPS_PROXY")
	}
	if len(url) < 1 {
		url = os.Getenv("https_proxy")
	}
	if len(url) < 1 {
		url = os.Getenv("HTTP_PROXY")
	}
	if len(url) < 1 {
		url = os.Getenv("http_proxy")
	}
	return
}

func RegularProxyURL(url string) string {
	if len(url) < 1 {
		return url
	}
	url = strings.TrimSpace(url)
	pos := strings.Index(url, "://")

	var protocol, address string
	if pos < 0 {
		protocol = "http"
		address = url
	} else {
		protocol = strings.ToLower(url[:pos])
		address = url[pos+3:]
	}

	switch protocol {
	case "":
		protocol = "http"
	case "socks4":
		fallthrough
	case "socks4a":
		fallthrough
	case "socks5":
		protocol = "socks"
	}
	return fmt.Sprintf("%s://%s", protocol, address)
}

func GetProxyDialer(proxyURL string, timeout time.Duration, insecureSkipVerify bool) (dailer Dialer, err error) {
	proxyURL = RegularProxyURL(proxyURL)
	u, err := url.Parse(proxyURL)
	if err != nil {
		return
	}

	if u.Scheme == "socks" {
		var auth proxy.Auth
		auth.User = u.User.Username()
		auth.Password, _ = u.User.Password()
		dailer, err = proxy.SOCKS5("tcp", u.Host, &auth, &net.Dialer{
			Timeout: timeout,
		})
		return
	}

	if u.Scheme == "http" || u.Scheme == "https" {
		dailer, err = connectproxy.NewWithConfig(
			u,
			&net.Dialer{
				Timeout: timeout,
			},
			&connectproxy.Config{
				InsecureSkipVerify: insecureSkipVerify,
				DialTimeout:        timeout,
			},
		)
		return
	}

	if len(proxyURL) > 0 {
		err = fmt.Errorf("unknown proxy scheme '%s'", u.Scheme)
	}
	return
}
