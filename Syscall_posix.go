//go:build !windows

package main

import (
	"syscall"

	"github.com/golang/glog"
)

func IncreaseFDLimit() {
	var rlm syscall.Rlimit

	// Try to increase the soft limit
	syscall.Getrlimit(syscall.RLIMIT_NOFILE, &rlm)
	if rlm.Cur < 65535 && rlm.Cur < rlm.Max {
		rlm.Cur = rlm.Max
		syscall.Setrlimit(syscall.RLIMIT_NOFILE, &rlm)
	}

	// Try to increase the hard limit
	syscall.Getrlimit(syscall.RLIMIT_NOFILE, &rlm)
	if rlm.Cur < 65535 || rlm.Max < 65535 {
		rlm.Cur = 65535
		rlm.Max = 65535
		syscall.Setrlimit(syscall.RLIMIT_NOFILE, &rlm)
	}

	// checking
	syscall.Getrlimit(syscall.RLIMIT_NOFILE, &rlm)
	glog.Info("[OPTION] File descriptor limits: ", rlm.Cur, " / ", rlm.Max)
	if rlm.Max < 5000 {
		glog.Error("[OPTION] File descriptor hard limit is too small: ", rlm.Max, "! The problem may be solved by executing the following command before launching BTCAgent: ulimit -Hn 65535")
	}
	if rlm.Cur < 5000 {
		glog.Error("[OPTION] File descriptor soft limit is too small: ", rlm.Cur, "! The problem may be solved by executing the following command before launching BTCAgent: ulimit -Sn 65535")
	}
}
