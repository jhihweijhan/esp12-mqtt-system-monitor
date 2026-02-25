package main

import (
	"encoding/json"
	"fmt"
	"math"
	"os"
	"os/exec"
	"strconv"
	"strings"
	"time"

	mqtt "github.com/eclipse/paho.mqtt.golang"
	"github.com/shirou/gopsutil/v4/cpu"
	"github.com/shirou/gopsutil/v4/disk"
	"github.com/shirou/gopsutil/v4/host"
	"github.com/shirou/gopsutil/v4/mem"
	"github.com/shirou/gopsutil/v4/net"
)

type rateState struct {
	netRx uint64
	netTx uint64
	dr    uint64
	dw    uint64
	ts    time.Time
}

type rateSampler struct {
	state rateState
}

func envString(name, fallback string) string {
	v := os.Getenv(name)
	if v == "" {
		return fallback
	}
	return v
}

func envInt(name string, fallback int) int {
	v := os.Getenv(name)
	if v == "" {
		return fallback
	}
	n, err := strconv.Atoi(v)
	if err != nil {
		return fallback
	}
	return n
}

func envFloat(name string, fallback float64) float64 {
	v := os.Getenv(name)
	if v == "" {
		return fallback
	}
	f, err := strconv.ParseFloat(v, 64)
	if err != nil {
		return fallback
	}
	return f
}

func (s *rateSampler) readState() rateState {
	nio, _ := net.IOCounters(false)
	dio, _ := disk.IOCounters()
	st := rateState{ts: time.Now()}
	if len(nio) > 0 {
		st.netRx = nio[0].BytesRecv
		st.netTx = nio[0].BytesSent
	}
	for _, d := range dio {
		st.dr += d.ReadBytes
		st.dw += d.WriteBytes
	}
	return st
}

func (s *rateSampler) sample() (int64, int64, int64, int64) {
	now := s.readState()
	if s.state.ts.IsZero() {
		s.state = now
		return 0, 0, 0, 0
	}
	elapsed := now.ts.Sub(s.state.ts).Seconds()
	if elapsed <= 0 {
		elapsed = 1
	}

	rxKbps := int64(math.Max(float64(now.netRx-s.state.netRx)/elapsed/1024.0, 0))
	txKbps := int64(math.Max(float64(now.netTx-s.state.netTx)/elapsed/1024.0, 0))
	rKBps := int64(math.Max(float64(now.dr-s.state.dr)/elapsed/1024.0, 0))
	wKBps := int64(math.Max(float64(now.dw-s.state.dw)/elapsed/1024.0, 0))
	s.state = now
	return rxKbps, txKbps, rKBps, wKBps
}

func readGPUTelemetry() (usage, temp, memPct, hotspot, memTemp float64) {
	cmd := exec.Command("nvidia-smi", "--query-gpu=utilization.gpu,temperature.gpu,memory.used,memory.total", "--format=csv,noheader,nounits")
	out, err := cmd.Output()
	if err != nil {
		return 0, 0, 0, 0, 0
	}
	line := strings.TrimSpace(strings.Split(string(out), "\n")[0])
	parts := strings.Split(line, ",")
	if len(parts) < 4 {
		return 0, 0, 0, 0, 0
	}

	parse := func(s string) float64 {
		v, err := strconv.ParseFloat(strings.TrimSpace(s), 64)
		if err != nil {
			return 0
		}
		return v
	}

	usage = parse(parts[0])
	temp = parse(parts[1])
	used := parse(parts[2])
	total := parse(parts[3])
	if total > 0 {
		memPct = (used / total) * 100
	}
	return usage, temp, memPct, 0, 0
}

func buildPayload(hostname string, sampler *rateSampler) Payload {
	cpuPct, _ := cpu.Percent(0, false)
	vm, _ := mem.VirtualMemory()
	rxKbps, txKbps, rKBps, wKBps := sampler.sample()
	gpuPct, gpuTemp, gpuMemPct, gpuHotspot, gpuMemTemp := readGPUTelemetry()

	payload := Payload{
		Version: 2,
		TS:      nowMillis(),
		Host:    hostname,
		NET:     [2]int64{rxKbps, txKbps},
		Disk:    [2]int64{rKBps, wKBps},
		GPU:     [5]float64{gpuPct, gpuTemp, gpuMemPct, gpuHotspot, gpuMemTemp},
	}

	if len(cpuPct) > 0 {
		payload.CPU = [2]float64{math.Round(cpuPct[0]*10) / 10, 0}
	}

	if vm != nil {
		payload.RAM = [3]float64{
			math.Round(vm.UsedPercent*10) / 10,
			float64(vm.Used / 1024 / 1024),
			float64(vm.Total / 1024 / 1024),
		}
	}

	return payload
}

func main() {
	hostname := envString("SENDER_HOSTNAME", "")
	if hostname == "" {
		h, err := host.Hostname()
		if err == nil {
			hostname = h
		}
	}
	if hostname == "" {
		hostname = "unknown"
	}

	intervalSec := envFloat("SEND_INTERVAL_SEC", 1.0)
	mqttHost := envString("MQTT_HOST", "127.0.0.1")
	mqttPort := envInt("MQTT_PORT", 1883)
	mqttUser := envString("MQTT_USER", "")
	mqttPass := envString("MQTT_PASS", "")
	qos := byte(envInt("MQTT_QOS", 0))

	opts := mqtt.NewClientOptions()
	opts.AddBroker(fmt.Sprintf("tcp://%s:%d", mqttHost, mqttPort))
	opts.SetClientID("sender-v2-" + hostname + "-go")
	opts.SetAutoReconnect(true)
	if mqttUser != "" {
		opts.SetUsername(mqttUser)
		opts.SetPassword(mqttPass)
	}

	client := mqtt.NewClient(opts)
	token := client.Connect()
	token.Wait()
	if token.Error() != nil {
		panic(token.Error())
	}

	topic := topicForHost(hostname)
	fmt.Printf("Sender v2 (Go) started: host=%s topic=%s interval=%.1fs\n", hostname, topic, intervalSec)

	sampler := &rateSampler{}
	ticker := time.NewTicker(time.Duration(intervalSec * float64(time.Second)))
	defer ticker.Stop()

	for range ticker.C {
		payload := buildPayload(hostname, sampler)
		body, err := json.Marshal(payload)
		if err != nil {
			fmt.Printf("marshal failed: %v\n", err)
			continue
		}

		pub := client.Publish(topic, qos, false, body)
		pub.Wait()
		if pub.Error() != nil {
			fmt.Printf("publish failed: %v\n", pub.Error())
		}
	}
}
