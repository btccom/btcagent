package main

import "net"

type SessionFactory interface {
	NewUpSession(manager *UpSessionManager, poolIndex int, slot int) (up UpSession)
	NewFakeUpSession(manager *UpSessionManager) (up FakeUpSession)
	NewDownSession(manager *SessionManager, clientConn net.Conn, sessionID uint16) (down DownSession)
}
