package main

import "net"

type SessionFactoryETH struct {
}

func (factory *SessionFactoryETH) NewUpSession(manager *UpSessionManager, poolIndex int, slot int) (up UpSession) {
	return NewUpSessionETH(manager, poolIndex, slot)
}

func (factory *SessionFactoryETH) NewFakeUpSession(manager *UpSessionManager) (up FakeUpSession) {
	return NewFakeUpSessionETH(manager)
}

func (factory *SessionFactoryETH) NewDownSession(manager *SessionManager, clientConn net.Conn, sessionID uint16) (down DownSession) {
	return NewDownSessionETH(manager, clientConn, sessionID)
}
