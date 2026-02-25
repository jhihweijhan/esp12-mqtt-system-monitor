package main

import "testing"

func TestTopicForHost(t *testing.T) {
	got := topicForHost("desk")
	want := "sys/agents/desk/metrics/v2"
	if got != want {
		t.Fatalf("topicForHost mismatch: got %s want %s", got, want)
	}
}
