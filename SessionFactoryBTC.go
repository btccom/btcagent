package main

import "net"

type SessionFactoryBTC struct {
}

func (factory *SessionFactoryBTC) NewUpSession(manager *UpSessionManager, poolIndex int, slot int) (up UpSession) {
	return NewUpSessionBTC(manager, poolIndex, slot)
}

func (factory *SessionFactoryBTC) NewFakeUpSession(manager *UpSessionManager) (up FakeUpSession) {
	return NewFakeUpSessionBTC(manager)
}

func (factory *SessionFactoryBTC) NewDownSession(manager *SessionManager, clientConn net.Conn, sessionID uint16) (down DownSession) {
	return NewDownSessionBTC(manager, clientConn, sessionID)
}
