package main

import "time"

type Payload struct {
	Version int       `json:"v"`
	TS      int64     `json:"ts"`
	Host    string    `json:"h"`
	CPU     [2]float64 `json:"cpu"`
	RAM     [3]float64 `json:"ram"`
	GPU     [5]float64 `json:"gpu"`
	NET     [2]int64  `json:"net"`
	Disk    [2]int64  `json:"disk"`
}

func topicForHost(host string) string {
	return "sys/agents/" + host + "/metrics/v2"
}

func nowMillis() int64 {
	return time.Now().UnixMilli()
}
