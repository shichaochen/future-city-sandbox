// 未来城市沙盘 · ESP32 控制程序
// 板型：ESP32 Dev Module（经典 ESP32），Arduino Core 2.0.17
// 依赖库：FastLED（Arduino 库管理器搜索 FastLED 安装）
// 接线总览（详见 WIRING.md）：
//   WS2812B 灯带数据线 -> GPIO 18（串联 330Ω 电阻），灯带独立 5V 供电并与 ESP32 共地
//   雾化器继电器 -> GPIO 26（高电平触发；若是低电平触发模块，把 RELAY_ON 改成 LOW）
//   风力发电机 MOSFET -> GPIO 27（PWM 调速）
// 灯带分段（同一条 80 灯灯带的逻辑分段）：
//   1段：0-59 号灯（60 灯，轨道灯带 / 列车流水灯）
//   2段：60-64 号灯（5 灯，霓虹呼吸灯）
//   3段：65-69 号灯（5 灯，霓虹呼吸灯）
//   4段：70-74 号灯（5 灯，霓虹呼吸灯）
//   5段：75-79 号灯（5 灯，霓虹呼吸灯）
// 手机连接：连入 WiFi “FutureCity”，密码 future123，浏览器打开 192.168.4.1

#include <WiFi.h>
#include <WebServer.h>
#include <FastLED.h>

#define LED_PIN 18
#define NUM_LEDS 80
#define HUM_PIN 26
#define WIND_PIN 27
#define RELAY_ON HIGH

const char* AP_SSID = "FutureCity";
const char* AP_PASS = "future123";

CRGB leds[NUM_LEDS];
WebServer server(80);

// ---------- 状态 ----------
bool master = true;        // 系统总控
int seg1Mode = 2;          // 1段模式：0关闭 1常亮 2流水（列车） 3彩虹
int trainSpeed = 5;        // 列车速度 1-10
bool segOn[4] = {true, true, true, true};   // 2-5段开关
uint8_t segHue[4] = {160, 224, 128, 192};   // 2-5段霓虹颜色（蓝/品红/青/紫）
bool hum = false;          // 雾化器
bool wind = false;         // 风力发电机
int windSpeed = 50;        // 风机转速 0-100
int demoMode = 1;          // 演示模式：0手动 1未来夜景 2列车飞驰 3梦幻霓虹

// ---------- 动画状态 ----------
int trainPos = 0;
unsigned long lastTrainMove = 0;
unsigned long lastFrame = 0;

int segStart(int n) { return 60 + (n - 2) * 5; }  // n = 2..5

void applyMode(int m) {
  demoMode = m;
  if (m == 1) {  // 未来夜景
    seg1Mode = 2; trainSpeed = 3;
    segOn[0] = segOn[1] = segOn[2] = segOn[3] = true;
    segHue[0] = 160; segHue[1] = 224; segHue[2] = 128; segHue[3] = 192;
    hum = true; wind = true; windSpeed = 40;
  } else if (m == 2) {  // 列车飞驰
    seg1Mode = 2; trainSpeed = 9;
    segOn[0] = segOn[1] = segOn[2] = segOn[3] = true;
    segHue[0] = 160; segHue[1] = 224; segHue[2] = 128; segHue[3] = 192;
    hum = true; wind = true; windSpeed = 90;
  } else if (m == 3) {  // 梦幻霓虹
    seg1Mode = 3;
    segOn[0] = segOn[1] = segOn[2] = segOn[3] = true;
    hum = true; wind = true; windSpeed = 60;
  }
}

// 呼吸灯亮度因子：40-255 正弦变化
uint8_t breathFactor(bool fast) {
  uint32_t period = fast ? 1200 : 3000;
  uint32_t ph = millis() % period;
  float s = (sin(2 * PI * (float)ph / (float)period) + 1.0f) / 2.0f;
  return 40 + (uint8_t)(s * 215);
}

void render() {
  if (!master) {
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    return;
  }
  // ---- 1段（60灯）----
  if (seg1Mode == 0) {
    fill_solid(leds, 60, CRGB::Black);
  } else if (seg1Mode == 1) {
    fill_solid(leds, 60, CRGB(255, 180, 100));  // 暖白常亮
  } else if (seg1Mode == 2) {
    // 列车流水灯：亮点带拖尾，模拟列车飞驰
    unsigned long interval = 110 - trainSpeed * 10;  // 速度1-10 -> 100-10ms
    if (millis() - lastTrainMove >= interval) {
      lastTrainMove = millis();
      trainPos = (trainPos + 1) % 60;
    }
    fill_solid(leds, 60, CRGB::Black);
    for (int k = 0; k < 9; k++) {
      int idx = (trainPos - k + 60) % 60;
      leds[idx] = CHSV(160, 180, 255 - k * 28);
    }
    leds[trainPos] = CRGB(255, 255, 255);  // 车头白光
  } else if (seg1Mode == 3) {
    uint8_t hue = (uint8_t)(millis() / 60);
    fill_rainbow(leds, 60, hue, 4);
  }
  // ---- 2-5段（每段5灯，霓虹呼吸灯）----
  bool fast = (demoMode == 2);
  for (int j = 0; j < 4; j++) {
    int start = segStart(j + 2);
    if (!segOn[j]) {
      fill_solid(&leds[start], 5, CRGB::Black);
    } else {
      uint8_t hue = segHue[j];
      if (demoMode == 3) hue = (uint8_t)(millis() / 60 + j * 40);  // 梦幻霓虹：颜色流动
      CRGB c = CHSV(hue, 255, breathFactor(fast));
      fill_solid(&leds[start], 5, c);
    }
  }
}

void applyOutputs() {
  digitalWrite(HUM_PIN, (master && hum) ? RELAY_ON : !RELAY_ON);
  int duty = (master && wind) ? windSpeed * 255 / 100 : 0;
  ledcWrite(0, duty);
}

String stateJson() {
  String s = "{";
  s += "\"master\":" + String(master ? 1 : 0);
  s += ",\"seg1\":" + String(seg1Mode);
  s += ",\"speed\":" + String(trainSpeed);
  s += ",\"segOn\":[" + String(segOn[0] ? 1 : 0) + "," + String(segOn[1] ? 1 : 0) + "," + String(segOn[2] ? 1 : 0) + "," + String(segOn[3] ? 1 : 0) + "]";
  s += ",\"hues\":[" + String(segHue[0]) + "," + String(segHue[1]) + "," + String(segHue[2]) + "," + String(segHue[3]) + "]";
  s += ",\"hum\":" + String(hum ? 1 : 0);
  s += ",\"wind\":" + String(wind ? 1 : 0);
  s += ",\"windSpeed\":" + String(windSpeed);
  s += ",\"mode\":" + String(demoMode);
  s += "}";
  return s;
}

void handleApi() {
  String cmd = server.arg("cmd");
  int v = server.arg("v").toInt();
  int n = server.arg("n").toInt();
  if (cmd != "master" && !master) master = true;  // 点任何控制先自动开总控
  if (cmd == "master") {
    master = (v == 1);
    if (master) applyMode(1);
  } else if (cmd == "train") {
    seg1Mode = (v == 1) ? 2 : 1; demoMode = 0;
  } else if (cmd == "speed") {
    trainSpeed = constrain(v, 1, 10); demoMode = 0;
  } else if (cmd == "seg1") {
    seg1Mode = constrain(v, 0, 3); demoMode = 0;
  } else if (cmd == "seg") {
    if (n >= 2 && n <= 5) segOn[n - 2] = (v == 1); demoMode = 0;
  } else if (cmd == "color") {
    if (n >= 2 && n <= 5) segHue[n - 2] = (uint8_t)constrain(v, 0, 255); demoMode = 0;
  } else if (cmd == "hum") {
    hum = (v == 1); demoMode = 0;
  } else if (cmd == "wind") {
    wind = (v == 1); demoMode = 0;
  } else if (cmd == "windspeed") {
    windSpeed = constrain(v, 0, 100); demoMode = 0;
  } else if (cmd == "mode") {
    if (v >= 1 && v <= 3) applyMode(v);
  }
  server.send(200, "text/plain", "OK");
}

const char PAGE[] = R"HTML(<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>未来城市沙盘</title>
<style>
body{margin:0;padding:16px;background:linear-gradient(160deg,#050914,#0b1230 60%,#1a0b2e);color:#eaf2ff;font-family:system-ui,-apple-system,"PingFang SC","Microsoft YaHei",sans-serif}
h1{text-align:center;font-size:26px;margin:8px 0 2px;background:linear-gradient(90deg,#00e5ff,#8b5cff,#ff4fd8);-webkit-background-clip:text;background-clip:text;color:transparent}
.sub{text-align:center;color:#8fa3c8;margin:0 0 16px;font-size:13px}
.card{max-width:520px;margin:0 auto 14px;background:rgba(255,255,255,.07);border:1px solid rgba(255,255,255,.13);border-radius:20px;padding:16px}
.card h2{font-size:17px;margin:0 0 12px}
.center{text-align:center}
.row{display:flex;gap:10px;flex-wrap:wrap;align-items:center}
.btn{flex:1;min-height:48px;border:0;border-radius:14px;background:rgba(255,255,255,.12);color:#eaf2ff;font-size:16px;cursor:pointer}
.btn.on{background:linear-gradient(90deg,#00e5ff,#7c4dff);box-shadow:0 0 18px rgba(0,229,255,.55);color:#fff}
.big-power{width:96px;height:96px;border-radius:50%;border:0;font-size:42px;background:rgba(255,255,255,.12);color:#8fa3c8;cursor:pointer}
.big-power.on{background:radial-gradient(circle,#00e5ff,#7c4dff);color:#fff;box-shadow:0 0 30px rgba(0,229,255,.7)}
.segcard{background:rgba(255,255,255,.05);border-radius:14px;padding:12px;margin-bottom:10px}
.dot{width:30px;height:30px;border-radius:50%;border:2px solid rgba(255,255,255,.6);cursor:pointer;padding:0}
.dot.on{border-color:#fff;box-shadow:0 0 10px #fff}
input[type=range]{width:100%;accent-color:#00e5ff}
.mode{flex:1;min-height:64px;border:0;border-radius:16px;color:#fff;font-size:16px;cursor:pointer}
.mode.on{outline:3px solid #fff}
.m1{background:linear-gradient(135deg,#0b2a6b,#00b4d8)}
.m2{background:linear-gradient(135deg,#7c2d12,#ff6a00)}
.m3{background:linear-gradient(135deg,#7c4dff,#ff4fd8)}
label{color:#c7d5f0;font-size:14px}
.foot{text-align:center;color:#5f7398;font-size:12px;max-width:520px;margin:0 auto}
</style>
</head>
<body>
<h1>🌃 未来城市沙盘</h1>
<p class="sub">ESP32 · WiFi 控制 · 未来城市夜景</p>
<div class="card center">
<button id="masterBtn" class="big-power" onclick="api('cmd=master&v='+(state.master?0:1))">⏻</button>
<p id="masterTxt">系统总控：开启</p>
</div>
<div class="card">
<h2>🚄 列车</h2>
<div class="row">
<button id="trainGo" class="btn" onclick="api('cmd=train&v=1')">🚄 列车启动</button>
<button id="trainStop" class="btn" onclick="api('cmd=train&v=0')">⏹ 列车停止</button>
</div>
<p><label>🚀 列车速度 <span id="speedVal">5</span></label></p>
<input type="range" id="speed" min="1" max="10" value="5" onchange="api('cmd=speed&v='+this.value)">
</div>
<div class="card">
<h2>💡 轨道灯带 · 1段（60灯）</h2>
<div class="row">
<button class="btn seg1btn" data-v="0" onclick="api('cmd=seg1&v=0')">关闭</button>
<button class="btn seg1btn" data-v="1" onclick="api('cmd=seg1&v=1')">常亮</button>
<button class="btn seg1btn" data-v="2" onclick="api('cmd=seg1&v=2')">流水灯</button>
<button class="btn seg1btn" data-v="3" onclick="api('cmd=seg1&v=3')">彩虹</button>
</div>
</div>
<div class="card">
<h2>✨ 霓虹灯带 · 2–5段（每段5灯呼吸灯）</h2>
<div id="segs"></div>
</div>
<div class="card">
<h2>💧 雾化器</h2>
<button id="humBtn" class="btn" style="width:100%" onclick="api('cmd=hum&v='+(state.hum?0:1))">💧 雾化器</button>
</div>
<div class="card">
<h2>🌀 风力发电机</h2>
<div class="row">
<button id="windBtn" class="btn" onclick="api('cmd=wind&v='+(state.wind?0:1))">🌀 风机旋转</button>
</div>
<p><label>转速 <span id="windVal">50</span>%</label></p>
<input type="range" id="windspeed" min="0" max="100" value="50" onchange="api('cmd=windspeed&v='+this.value)">
</div>
<div class="card">
<h2>🎬 演示模式</h2>
<div class="row">
<button id="mode1" class="mode m1" onclick="api('cmd=mode&v=1')">🌃<br>未来夜景</button>
<button id="mode2" class="mode m2" onclick="api('cmd=mode&v=2')">🚀<br>列车飞驰</button>
<button id="mode3" class="mode m3" onclick="api('cmd=mode&v=3')">🌈<br>梦幻霓虹</button>
</div>
<p style="color:#8fa3c8;font-size:13px">未来夜景：慢速列车 + 霓虹呼吸 + 雾 + 慢速风机；列车飞驰：高速流水灯 + 快呼吸 + 雾 + 高速风机；梦幻霓虹：全段彩虹流动 + 雾 + 中速风机。</p>
</div>
<p class="foot">手机连入 WiFi “FutureCity”（密码 future123），浏览器打开 192.168.4.1 即可控制。</p>
<script>
var state = {master:1, seg1:2, speed:5, segOn:[1,1,1,1], hues:[160,224,128,192], hum:0, wind:0, windSpeed:50, mode:1};
var COLORS = [[0,'#ff2d2d'],[32,'#ff9a1f'],[64,'#ffe600'],[96,'#2dff5e'],[128,'#00e5ff'],[160,'#2d6bff'],[192,'#8b5cff'],[224,'#ff4fd8']];
function buildSegs(){
  var box = document.getElementById('segs');
  var html = '';
  for (var n = 2; n <= 5; n++){
    html += '<div class="segcard"><div class="row"><span>💡 ' + n + '段</span><button class="btn" id="segbtn' + n + '" onclick="api(\'cmd=seg&n=' + n + '&v=\' + (state.segOn[' + (n-2) + '] ? 0 : 1))">开关</button></div><div class="row" style="margin-top:10px">';
    for (var i = 0; i < COLORS.length; i++){
      html += '<button class="dot" data-n="' + n + '" data-h="' + COLORS[i][0] + '" style="background:' + COLORS[i][1] + '" onclick="api(\'cmd=color&n=' + n + '&v=' + COLORS[i][0] + '\')"></button>';
    }
    html += '</div></div>';
  }
  box.innerHTML = html;
}
function api(q){
  fetch('/api?' + q).then(function(){ refresh(); }).catch(function(){});
}
function refresh(){
  fetch('/state').then(function(r){ return r.json(); }).then(function(s){
    state = s;
    var mb = document.getElementById('masterBtn');
    mb.className = 'big-power' + (state.master ? ' on' : '');
    document.getElementById('masterTxt').textContent = '系统总控：' + (state.master ? '开启' : '关闭');
    document.getElementById('trainGo').className = 'btn' + (state.seg1 === 2 ? ' on' : '');
    document.getElementById('trainStop').className = 'btn' + (state.seg1 !== 2 ? ' on' : '');
    document.getElementById('speedVal').textContent = state.speed;
    if (document.activeElement.id !== 'speed') document.getElementById('speed').value = state.speed;
    var b1 = document.querySelectorAll('.seg1btn');
    for (var i = 0; i < b1.length; i++) b1[i].className = 'btn seg1btn' + (parseInt(b1[i].getAttribute('data-v')) === state.seg1 ? ' on' : '');
    for (var n = 2; n <= 5; n++){
      var b = document.getElementById('segbtn' + n);
      if (b){ b.className = 'btn' + (state.segOn[n-2] ? ' on' : ''); b.textContent = state.segOn[n-2] ? '已开' : '已关'; }
    }
    var dots = document.querySelectorAll('.dot');
    for (var j = 0; j < dots.length; j++){
      var dn = parseInt(dots[j].getAttribute('data-n'));
      var dh = parseInt(dots[j].getAttribute('data-h'));
      dots[j].className = 'dot' + (state.hues[dn-2] === dh ? ' on' : '');
    }
    var hb = document.getElementById('humBtn');
    hb.className = 'btn' + (state.hum ? ' on' : ''); hb.textContent = '💧 雾化器：' + (state.hum ? '开启' : '关闭');
    var wb = document.getElementById('windBtn');
    wb.className = 'btn' + (state.wind ? ' on' : ''); wb.textContent = '🌀 风机旋转：' + (state.wind ? '开启' : '关闭');
    document.getElementById('windVal').textContent = state.windSpeed;
    if (document.activeElement.id !== 'windspeed') document.getElementById('windspeed').value = state.windSpeed;
    for (var m = 1; m <= 3; m++){
      var el = document.getElementById('mode' + m);
      el.className = el.className.replace(' on', '') + (state.mode === m ? ' on' : '');
    }
  }).catch(function(){});
}
buildSegs();
refresh();
setInterval(refresh, 3000);
</script>
</body>
</html>
)HTML";

void handleRoot() { server.send(200, "text/html", PAGE); }
void handleState() { server.send(200, "application/json", stateJson()); }

void setup() {
  Serial.begin(115200);
  pinMode(HUM_PIN, OUTPUT);
  digitalWrite(HUM_PIN, !RELAY_ON);
  ledcSetup(0, 5000, 8);
  ledcAttachPin(WIND_PIN, 0);
  ledcWrite(0, 0);
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(180);
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
  server.on("/", handleRoot);
  server.on("/state", handleState);
  server.on("/api", handleApi);
  server.begin();
  applyMode(1);  // 上电默认：未来夜景
}

void loop() {
  server.handleClient();
  if (millis() - lastFrame >= 20) {
    lastFrame = millis();
    render();
    FastLED.show();
  }
  applyOutputs();
}
