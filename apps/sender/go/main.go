package main

import (
	"encoding/json"
	"fmt"
	"math"
	"os"
	"os/exec"
	"path/filepath"
	"regexp"
	"sort"
	"strconv"
	"strings"
	"time"

	mqtt "github.com/eclipse/paho.mqtt.golang"
	"github.com/shirou/gopsutil/v4/cpu"
	"github.com/shirou/gopsutil/v4/disk"
	"github.com/shirou/gopsutil/v4/mem"
	"github.com/shirou/gopsutil/v4/net"
)

var (
	metricNumberRe = regexp.MustCompile(`-?\d+(?:\.\d+)?`)
	cardNameRe     = regexp.MustCompile(`^card\d+$`)
	drmClassRoot   = "/sys/class/drm"
)

type gpuTelemetry struct {
	usage   float64
	temp    float64
	memPct  float64
	hotspot float64
	memTemp float64
}

type metricValue struct {
	value float64
	ok    bool
}

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
	nvidiaCmd := exec.Command(
		"nvidia-smi",
		"--query-gpu=utilization.gpu,temperature.gpu,memory.used,memory.total",
		"--format=csv,noheader,nounits",
	)
	if out, err := nvidiaCmd.Output(); err == nil {
		if parsed, ok := parseNvidiaTelemetry(string(out)); ok {
			return parsed.usage, parsed.temp, parsed.memPct, parsed.hotspot, parsed.memTemp
		}
	}

	rocmCmd := exec.Command("rocm-smi", "--showuse", "--showtemp", "--showmemuse", "--json")
	if out, err := rocmCmd.Output(); err == nil {
		if parsed, ok := parseRocmTelemetry(strings.TrimSpace(string(out))); ok {
			return parsed.usage, parsed.temp, parsed.memPct, parsed.hotspot, parsed.memTemp
		}
	}

	if parsed, ok := readGPUTelemetryFromSysfs(drmClassRoot); ok {
		return parsed.usage, parsed.temp, parsed.memPct, parsed.hotspot, parsed.memTemp
	}

	return 0, 0, 0, 0, 0
}

func parseNvidiaTelemetry(raw string) (gpuTelemetry, bool) {
	raw = strings.TrimSpace(raw)
	if raw == "" {
		return gpuTelemetry{}, false
	}

	line := strings.TrimSpace(strings.Split(raw, "\n")[0])
	parts := strings.Split(line, ",")
	if len(parts) < 4 {
		return gpuTelemetry{}, false
	}

	usage, ok := parseMetricFloat(parts[0])
	if !ok {
		return gpuTelemetry{}, false
	}
	temp, ok := parseMetricFloat(parts[1])
	if !ok {
		return gpuTelemetry{}, false
	}
	used, ok := parseMetricFloat(parts[2])
	if !ok {
		return gpuTelemetry{}, false
	}
	total, ok := parseMetricFloat(parts[3])
	if !ok {
		return gpuTelemetry{}, false
	}

	memPct := 0.0
	if total > 0 {
		memPct = used / total * 100.0
	}

	return gpuTelemetry{usage: usage, temp: temp, memPct: memPct}, true
}

func parseRocmTelemetry(raw string) (gpuTelemetry, bool) {
	if strings.TrimSpace(raw) == "" {
		return gpuTelemetry{}, false
	}

	var parsed map[string]any
	if err := json.Unmarshal([]byte(raw), &parsed); err != nil {
		return gpuTelemetry{}, false
	}
	if len(parsed) == 0 {
		return gpuTelemetry{}, false
	}

	var firstCard map[string]any
	for key, val := range parsed {
		obj, ok := val.(map[string]any)
		if !ok {
			continue
		}
		if strings.HasPrefix(strings.ToLower(key), "card") {
			firstCard = obj
			break
		}
	}
	if firstCard == nil {
		return gpuTelemetry{}, false
	}

	usage := readMetricByKeys(firstCard, []string{"gpu"}, []string{"use", "busy", "util"})
	edgeTemp := readMetricByKeys(firstCard, []string{"temp"}, []string{"edge"})
	junctionTemp := readMetricByKeys(firstCard, []string{"temp"}, []string{"junction", "hotspot", "hot"})
	memTemp := readMetricByKeys(firstCard, []string{"temp"}, []string{"memory", "mem"})
	memPct := readMetricByKeys(firstCard, []string{"vram"}, []string{"%", "alloc", "use", "used"})

	if !memPct.ok {
		memPct = readMetricByKeys(firstCard, []string{"memory"}, []string{"vram", "alloc", "use", "used", "%"})
	}

	if !usage.ok && !edgeTemp.ok && !junctionTemp.ok && !memPct.ok {
		return gpuTelemetry{}, false
	}

	temp := 0.0
	if edgeTemp.ok {
		temp = edgeTemp.value
	} else if junctionTemp.ok {
		temp = junctionTemp.value
	}

	hotspot := 0.0
	if junctionTemp.ok {
		hotspot = junctionTemp.value
	}

	memoryTemp := 0.0
	if memTemp.ok {
		memoryTemp = memTemp.value
	}

	return gpuTelemetry{
		usage:   valueOrZero(usage),
		temp:    temp,
		memPct:  valueOrZero(memPct),
		hotspot: hotspot,
		memTemp: memoryTemp,
	}, true
}

func readGPUTelemetryFromSysfs(root string) (gpuTelemetry, bool) {
	entries, err := os.ReadDir(root)
	if err != nil {
		return gpuTelemetry{}, false
	}
	sort.Slice(entries, func(i, j int) bool { return entries[i].Name() < entries[j].Name() })

	var best gpuTelemetry
	hasBest := false

	for _, entry := range entries {
		if !entry.IsDir() {
			continue
		}
		if !cardNameRe.MatchString(entry.Name()) {
			continue
		}
		cardPath := filepath.Join(root, entry.Name())
		if parsed, ok := readGPUTelemetryFromSysfsCard(cardPath); ok {
			if !hasBest || isTelemetryBetter(parsed, best) {
				best = parsed
				hasBest = true
			}
		}
	}

	if hasBest {
		return best, true
	}

	return gpuTelemetry{}, false
}

func readGPUTelemetryFromSysfsCard(cardPath string) (gpuTelemetry, bool) {
	devicePath := filepath.Join(cardPath, "device")
	if _, err := os.Stat(devicePath); err != nil {
		return gpuTelemetry{}, false
	}

	usage := readFirstMetricFromFiles(
		filepath.Join(devicePath, "gpu_busy_percent"),
		filepath.Join(devicePath, "gt_busy_percent"),
	)

	memPct := readVramPercent(devicePath)
	if !memPct.ok {
		memPct = readFirstMetricFromFiles(filepath.Join(devicePath, "mem_busy_percent"))
	}

	edgeTemp, junctionTemp, memTemp := readSysfsTemps(devicePath)

	if !usage.ok && !memPct.ok && !edgeTemp.ok && !junctionTemp.ok && !memTemp.ok {
		return gpuTelemetry{}, false
	}

	temp := 0.0
	if edgeTemp.ok {
		temp = edgeTemp.value
	} else if junctionTemp.ok {
		temp = junctionTemp.value
	}

	hotspot := 0.0
	if junctionTemp.ok {
		hotspot = junctionTemp.value
	}

	memoryTemp := 0.0
	if memTemp.ok {
		memoryTemp = memTemp.value
	}

	return gpuTelemetry{
		usage:   valueOrZero(usage),
		temp:    temp,
		memPct:  valueOrZero(memPct),
		hotspot: hotspot,
		memTemp: memoryTemp,
	}, true
}

func readVramPercent(devicePath string) metricValue {
	used := readMetricFromFile(filepath.Join(devicePath, "mem_info_vram_used"))
	total := readMetricFromFile(filepath.Join(devicePath, "mem_info_vram_total"))
	if !used.ok || !total.ok || total.value <= 0 {
		return metricValue{}
	}
	return metricValue{value: used.value / total.value * 100.0, ok: true}
}

func readSysfsTemps(devicePath string) (edgeTemp, junctionTemp, memTemp metricValue) {
	hwmonRoot := filepath.Join(devicePath, "hwmon")
	hwmons, err := os.ReadDir(hwmonRoot)
	if err != nil {
		return metricValue{}, metricValue{}, metricValue{}
	}
	sort.Slice(hwmons, func(i, j int) bool { return hwmons[i].Name() < hwmons[j].Name() })

	for _, hw := range hwmons {
		if !hw.IsDir() {
			continue
		}

		pattern := filepath.Join(hwmonRoot, hw.Name(), "temp*_input")
		inputs, err := filepath.Glob(pattern)
		if err != nil {
			continue
		}
		sort.Strings(inputs)

		for _, inputPath := range inputs {
			raw := readMetricFromFile(inputPath)
			if !raw.ok {
				continue
			}
			tempC := normalizeTemp(raw.value)

			labelPath := strings.TrimSuffix(inputPath, "_input") + "_label"
			labelBytes, _ := os.ReadFile(labelPath)
			label := strings.ToLower(strings.TrimSpace(string(labelBytes)))

			switch {
			case strings.Contains(label, "junction") || strings.Contains(label, "hotspot") || label == "hot":
				if !junctionTemp.ok {
					junctionTemp = metricValue{value: tempC, ok: true}
				}
			case strings.Contains(label, "edge"):
				if !edgeTemp.ok {
					edgeTemp = metricValue{value: tempC, ok: true}
				}
			case strings.Contains(label, "mem"):
				if !memTemp.ok {
					memTemp = metricValue{value: tempC, ok: true}
				}
			default:
				if !edgeTemp.ok {
					edgeTemp = metricValue{value: tempC, ok: true}
				} else if !junctionTemp.ok {
					junctionTemp = metricValue{value: tempC, ok: true}
				} else if !memTemp.ok {
					memTemp = metricValue{value: tempC, ok: true}
				}
			}
		}
	}

	return edgeTemp, junctionTemp, memTemp
}

func readFirstMetricFromFiles(paths ...string) metricValue {
	for _, path := range paths {
		parsed := readMetricFromFile(path)
		if parsed.ok {
			return parsed
		}
	}
	return metricValue{}
}

func readMetricFromFile(path string) metricValue {
	body, err := os.ReadFile(path)
	if err != nil {
		return metricValue{}
	}
	parsed, ok := parseMetricFloat(string(body))
	return metricValue{value: parsed, ok: ok}
}

func normalizeTemp(raw float64) float64 {
	if math.Abs(raw) >= 200.0 {
		return raw / 1000.0
	}
	return raw
}

func readMetricByKeys(metrics map[string]any, includeAll []string, includeAny []string) metricValue {
	for key, value := range metrics {
		keyLower := strings.ToLower(key)
		if !containsAllTokens(keyLower, includeAll) {
			continue
		}
		if len(includeAny) > 0 && !containsAnyToken(keyLower, includeAny) {
			continue
		}
		parsed, ok := parseMetricValue(value)
		if ok {
			return metricValue{value: parsed, ok: true}
		}
	}
	return metricValue{}
}

func containsAllTokens(text string, tokens []string) bool {
	for _, token := range tokens {
		if !strings.Contains(text, token) {
			return false
		}
	}
	return true
}

func containsAnyToken(text string, tokens []string) bool {
	for _, token := range tokens {
		if strings.Contains(text, token) {
			return true
		}
	}
	return false
}

func parseMetricValue(value any) (float64, bool) {
	switch typed := value.(type) {
	case float64:
		return typed, true
	case float32:
		return float64(typed), true
	case int:
		return float64(typed), true
	case int64:
		return float64(typed), true
	case int32:
		return float64(typed), true
	case uint:
		return float64(typed), true
	case uint64:
		return float64(typed), true
	case uint32:
		return float64(typed), true
	case string:
		return parseMetricFloat(typed)
	default:
		return parseMetricFloat(fmt.Sprintf("%v", value))
	}
}

func parseMetricFloat(raw string) (float64, bool) {
	match := metricNumberRe.FindString(raw)
	if match == "" {
		return 0, false
	}
	parsed, err := strconv.ParseFloat(match, 64)
	if err != nil {
		return 0, false
	}
	return parsed, true
}

func valueOrZero(metric metricValue) float64 {
	if !metric.ok {
		return 0
	}
	return metric.value
}

func isTelemetryBetter(candidate gpuTelemetry, current gpuTelemetry) bool {
	cRank := telemetryRank(candidate)
	kRank := telemetryRank(current)

	if cRank.usageActive != kRank.usageActive {
		return cRank.usageActive && !kRank.usageActive
	}
	if cRank.usage != kRank.usage {
		return cRank.usage > kRank.usage
	}
	if cRank.memActive != kRank.memActive {
		return cRank.memActive && !kRank.memActive
	}
	if cRank.mem != kRank.mem {
		return cRank.mem > kRank.mem
	}
	if cRank.hotspot != kRank.hotspot {
		return cRank.hotspot > kRank.hotspot
	}
	if cRank.temp != kRank.temp {
		return cRank.temp > kRank.temp
	}
	return cRank.memTemp > kRank.memTemp
}

type gpuTelemetryRank struct {
	usageActive bool
	usage       float64
	memActive   bool
	mem         float64
	hotspot     float64
	temp        float64
	memTemp     float64
}

func telemetryRank(t gpuTelemetry) gpuTelemetryRank {
	return gpuTelemetryRank{
		usageActive: t.usage > 0,
		usage:       t.usage,
		memActive:   t.memPct > 0,
		mem:         t.memPct,
		hotspot:     t.hotspot,
		temp:        t.temp,
		memTemp:     t.memTemp,
	}
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
		h, err := os.Hostname()
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
