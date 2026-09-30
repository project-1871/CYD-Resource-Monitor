#pragma once
#include <ArduinoJson.h>
#include "ui.h"
#include "mouth.h"

// ── Metric ids (also settings-tile order) ──────────
enum { M_CPU = 0, M_GPU, M_RAM, M_TEMP, M_DISK, M_NET, M_COUNT };
const char *metricName[M_COUNT] = {"CPU", "GPU", "RAM", "TEMP", "DISK", "NET"};
uint16_t metricCol(int m) {
  const uint16_t c[M_COUNT] = {th.cpu, th.gpu, th.ram, th.temp, th.disk, th.net};
  return c[m];
}

struct Metrics {
  // cpu
  float cpu = 0, cpuTemp = 0, cpuFreq = 0;
  int   cores = 0;
  // ram (GB)
  float ram = 0, ramUsed = 0, ramTotal = 0;
  // gpu (first = preferred; agent sorts discrete first)
  float gpu = 0, gpuTemp = 0, vramUsed = 0, vramTotal = 0;
  char  gpuName[26] = "";
  bool  gpuDiscrete = false;
  int   gpuCount = 0;
  // disk
  float disk = 0, diskR = 0, diskW = 0;
  // net (MB/s)
  float netDl = 0, netUl = 0;
  // host
  char  host[22] = "";
  char  os[8] = "";
  // availability
  bool  hasCpuTemp = false, hasGpuLoad = false, hasGpuTemp = false;
  bool  valid = false;
  uint32_t lastRx = 0;
};
Metrics pc;

// ── History rings (2 Hz × 120 = last 60 s) ─────────
#define HIST_N 120
float hist[M_COUNT][HIST_N] = {};
int   histHead = 0, histCnt = 0;

float metricValue(int m) {
  switch (m) {
    case M_CPU:  return pc.cpu;
    case M_GPU:  return pc.gpu;
    case M_RAM:  return pc.ram;
    case M_TEMP: return max(pc.cpuTemp, pc.gpuTemp);
    case M_DISK: return pc.disk;
    case M_NET:  return pc.netDl;
    default:     return 0;
  }
}

void pushHistory() {
  for (int m = 0; m < M_COUNT; m++) hist[m][histHead] = metricValue(m);
  histHead = (histHead + 1) % HIST_N;
  if (histCnt < HIST_N) histCnt++;
}

float histMax(int m) {
  float mx = 0;
  for (int i = 0; i < histCnt; i++) mx = max(mx, hist[m][i]);
  return mx;
}

// ── Quick-launch keys (labels come from the agent's keys.json) ──
#define N_KEYS 6
char keyLabel[N_KEYS][14] = {"STEAM", "BROWSER", "EMAIL", "DISCORD", "YOUTUBE", "FILES"};
char keyIcon[N_KEYS][12]  = {"steam", "web", "email", "discord", "youtube", "folder"};

// ── Boombox: what the PC's media player is doing ──
// {"np":{ok,t,a,st,pos,len,vol,pl}} once a second; {"tl":{s,c,n,i:[...]}} when
// the song changes (a window of the playlist starting at index s; c = current).
#define TL_MAX 8
struct NowPlaying {
  bool ok = false;
  char title[48] = "", artist[40] = "", player[12] = "";
  int  state = 0;            // 0 stopped, 1 playing, 2 paused
  float pos = 0, len = 0;
  int  vol = -1;
  uint32_t rx = 0;
  int  tlStart = 0, tlCur = -1, tlTotal = 0, tlN = 0;
  char tl[TL_MAX][40];
};
NowPlaying np;

bool boomMsg(JsonDocument &doc) {
  if (JsonObject o = doc["np"]) {
    np.ok = o["ok"] | 0;
    np.vol = o["vol"] | np.vol;
    if (np.ok) {
      strlcpy(np.title,  o["t"]  | "", sizeof(np.title));
      strlcpy(np.artist, o["a"]  | "", sizeof(np.artist));
      strlcpy(np.player, o["pl"] | "", sizeof(np.player));
      np.state = o["st"] | 0;
      np.pos = o["pos"] | 0.0f;
      np.len = o["len"] | 0.0f;
    } else {
      np.tlN = 0; np.tlCur = -1;
    }
    np.rx = millis();
    return true;
  }
  if (JsonObject o = doc["tl"]) {
    np.tlStart = o["s"] | 0;
    np.tlCur   = o["c"] | -1;
    np.tlTotal = o["n"] | 0;
    JsonArray it = o["i"];
    np.tlN = min((int)it.size(), TL_MAX);
    for (int i = 0; i < np.tlN; i++) strlcpy(np.tl[i], it[i] | "", sizeof(np.tl[i]));
    return true;
  }
  return false;
}

// ── Radio (Radio Atlas plugin, via the agent) ──
// {"rd":{ok,on,p,st,c,t,v,i,n,b}} every 1.5 s; {"rl":{l,cur,i:[names]}} when the list changes.
#define RL_MAX 20
struct RadioNow {
  bool ok = false, on = false, paused = false;
  char station[42] = "", country[32] = "", title[62] = "", busy[40] = "";
  int  vol = 0, pos = -1, count = 0;
  uint32_t rx = 0;
  char listLabel[12] = "";
  int  listCur = -1, listN = 0;
  char list[RL_MAX][32];
};
RadioNow rad;

bool radioMsg(JsonDocument &doc) {
  if (JsonObject o = doc["rd"]) {
    rad.ok = o["ok"] | 0;
    rad.on = o["on"] | 0;
    rad.paused = o["p"] | 0;
    strlcpy(rad.station, o["st"] | "", sizeof(rad.station));
    strlcpy(rad.country, o["c"]  | "", sizeof(rad.country));
    strlcpy(rad.title,   o["t"]  | "", sizeof(rad.title));
    strlcpy(rad.busy,    o["b"]  | "", sizeof(rad.busy));
    rad.vol = o["v"] | 0;
    rad.pos = o["i"] | -1;
    rad.count = o["n"] | 0;
    rad.rx = millis();
    return true;
  }
  if (JsonObject o = doc["rl"]) {
    strlcpy(rad.listLabel, o["l"] | "", sizeof(rad.listLabel));
    rad.listCur = o["cur"] | -1;
    JsonArray it = o["i"];
    rad.listN = min((int)it.size(), RL_MAX);
    for (int i = 0; i < rad.listN; i++) strlcpy(rad.list[i], it[i] | "", sizeof(rad.list[i]));
    return true;
  }
  return false;
}

// ── Hello / goodbye banner (agent sends {"msg":"hello"|"bye"}) ──
enum { MSG_NONE = 0, MSG_HELLO, MSG_BYE };
int      msgKind = MSG_NONE;
uint32_t msgAt   = 0;
#define HELLO_MS 3500   // goodbye stays up until data comes back

// ── Serial JSON ingest ─────────────────────────────
void parseLine(char *line) {
  static JsonDocument doc;
  if (deserializeJson(doc, line)) return;
  if (mouthMsg(doc)) return;
  if (boomMsg(doc)) return;
  if (radioMsg(doc)) return;
  if (const char *msg = doc["msg"]) {
    msgKind = strcmp(msg, "bye") ? MSG_HELLO : MSG_BYE;
    msgAt   = millis();
    if (msgKind == MSG_BYE) pc.valid = false;
    return;
  }
  if (!doc["cpu"].is<JsonObject>()) return;

  pc.cpu     = doc["cpu"]["load"]  | 0.0f;
  pc.cpuFreq = doc["cpu"]["freq"]  | 0.0f;
  pc.cores   = doc["cpu"]["cores"] | 0;
  pc.hasCpuTemp = doc["cpu"]["temp"].is<float>();
  pc.cpuTemp    = doc["cpu"]["temp"] | 0.0f;

  pc.ram      = doc["ram"]["pct"]   | 0.0f;
  pc.ramUsed  = doc["ram"]["used"]  | 0.0f;
  pc.ramTotal = doc["ram"]["total"] | 0.0f;

  JsonArray gpus = doc["gpus"];
  pc.gpuCount = gpus.size();
  if (pc.gpuCount > 0) {
    JsonObject g = gpus[0];
    strlcpy(pc.gpuName, g["name"] | "GPU", sizeof(pc.gpuName));
    pc.gpuDiscrete = g["discrete"] | false;
    pc.hasGpuLoad  = g["load"].is<float>();
    pc.gpu         = g["load"] | 0.0f;
    pc.hasGpuTemp  = g["temp"].is<float>();
    pc.gpuTemp     = g["temp"] | 0.0f;
    pc.vramUsed    = g["vram_used"]  | 0.0f;
    pc.vramTotal   = g["vram_total"] | 0.0f;
  } else {
    pc.hasGpuLoad = pc.hasGpuTemp = false;
    pc.gpu = pc.gpuTemp = 0;
  }

  pc.disk  = doc["disk"]["pct"] | 0.0f;
  pc.diskR = doc["disk"]["r"]   | 0.0f;
  pc.diskW = doc["disk"]["w"]   | 0.0f;

  pc.netDl = doc["net"]["dl"] | 0.0f;
  pc.netUl = doc["net"]["ul"] | 0.0f;

  JsonArray keys = doc["keys"];
  for (int i = 0; i < N_KEYS && i < (int)keys.size(); i++)
    strlcpy(keyLabel[i], keys[i] | "", sizeof(keyLabel[i]));
  JsonArray icons = doc["icons"];
  for (int i = 0; i < N_KEYS && i < (int)icons.size(); i++)
    strlcpy(keyIcon[i], icons[i] | "", sizeof(keyIcon[i]));

  strlcpy(pc.host, doc["host"]["name"] | "PC", sizeof(pc.host));
  strlcpy(pc.os,   doc["host"]["os"]   | "?",  sizeof(pc.os));

  if (msgKind == MSG_BYE) msgKind = MSG_NONE;
  pc.valid  = true;
  pc.lastRx = millis();
  pushHistory();
}

void serialPoll() {
  static char buf[1600];
  static int  len = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      buf[len] = 0;
      if (len > 2) parseLine(buf);
      len = 0;
    } else if (len < (int)sizeof(buf) - 1) {
      buf[len++] = c;
    }
  }
  if (pc.valid && millis() - pc.lastRx > DATA_TIMEOUT_MS) pc.valid = false;
}

// ── Settings (which tiles on the dashboard) ────────
uint8_t tilesMask = 0x3F;   // all six on by default
bool    rgbOn = true;       // animated RGB strip under the header

void loadSettings() {
  prefs.begin("resmon", true);
  tilesMask = prefs.getUChar("tiles", 0x3F);
  themeIdx  = prefs.getUChar("theme", 0) % N_THEMES;
  rgbOn     = prefs.getBool("rgb", true);
  prefs.end();
  if ((tilesMask & 0x3F) == 0) tilesMask = 0x3F;
}
void saveSettings() {
  prefs.begin("resmon", false);
  prefs.putUChar("tiles", tilesMask);
  prefs.putUChar("theme", themeIdx);
  prefs.putBool("rgb", rgbOn);
  prefs.end();
}
