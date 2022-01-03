package main

type JobIDPairETH struct {
	PowHash string
	JobID   []byte
}

type JobIDQueueETH struct {
	Queue []JobIDPairETH
	Size  int
	Pos   int
}

func NewJobqueueETH(size int) (queue *JobIDQueueETH) {
	queue = new(JobIDQueueETH)
	queue.Queue = make([]JobIDPairETH, size)
	queue.Size = size
	queue.Pos = 0
	return
}

func (queue *JobIDQueueETH) Add(powHash string, jobID []byte) {
	queue.Queue[queue.Pos] = JobIDPairETH{powHash, jobID}
	queue.Pos++
	if queue.Pos >= queue.Size {
		queue.Pos = 0
	}
}

func (queue *JobIDQueueETH) Find(powHash string) []byte {
	for i := queue.Pos - 1; i >= 0; i-- {
		if queue.Queue[i].PowHash == powHash {
			return queue.Queue[i].JobID
		}
	}
	for i := queue.Size - 1; i >= queue.Pos; i-- {
		if queue.Queue[i].PowHash == powHash {
			return queue.Queue[i].JobID
		}
	}
	return nil
}
