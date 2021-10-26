package main

import "sync"

type UpSessionInfo struct {
	downSessionNum uint
	upSession      *UpSession
}

type UpSessionManager struct {
	subAccount string
	configData *ConfigData

	upSessions []UpSessionInfo
}

func NewUpSessionManager(subAccount string, configData *ConfigData) (manager *UpSessionManager) {
	manager = new(UpSessionManager)
	manager.subAccount = subAccount
	manager.configData = configData

	upSessions := [UpSessionNumPerSubAccount]UpSessionInfo{}
	manager.upSessions = upSessions[:]
	return
}

func (manager *UpSessionManager) Run() {
	var wg sync.WaitGroup
	wg.Add(len(manager.upSessions))
	for i := range manager.upSessions {
		go manager.connect(&manager.upSessions[i], wg)
	}
	wg.Wait()
}

func (manager *UpSessionManager) connect(info *UpSessionInfo, wg sync.WaitGroup) {
	defer wg.Done()

	for i := range manager.configData.Pools {
		info.upSession = NewUpSession(manager.subAccount, i, manager.configData)
		info.upSession.Run()

		if info.upSession.stat == StatAuthorized {
			break
		}
	}
}
