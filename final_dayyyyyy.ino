#include <Wire.h> 
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DHT.h> 

// --- CÀI ĐẶT CẢM BIẾN DHT ---
#define DHTPIN 15       
#define DHTTYPE DHT22   
DHT dht(DHTPIN, DHTTYPE);

// --- CÀI ĐẶT LCD VÀ CÁC CHÂN KHÁC ---
LiquidCrystal_I2C lcd(0x27, 16, 2);

const int mq4Pin = 34;
const int buzzerPin = 18; // Module Active Low (HIGH=Tắt, LOW=Bật)

const int ledGreen = 25;
const int ledYellow = 26;
const int ledRed = 27;

// --- CÀI ĐẶT WIFI ---
const char* ap_ssid = "ESP32-Metan-LeHoangAnhNguyet";     
const char* ap_password = "12345678";        

WebServer server(80);
bool systemOn = true;

// --- CÁC BIẾN TRẠNG THÁI ---
int mq4Value = 0;
float t = 0.0; 
float h = 0.0; 

String currentStatusText = ""; 
String currentStatusLcd = "";  
String currentStatusClass = "";
bool lastBuzzerState = HIGH;

// Thời gian không đồng bộ (non-blocking)
unsigned long lastSensorReadTime = 0;
const long sensorReadInterval = 1000; 

unsigned long lastBuzzerBeep = 0;
const long buzzerBeepInterval = 200; 

unsigned long lastLcdToggle = 0;
const long lcdToggleInterval = 2000; 
bool showGasScreen = true; 

const int mq4MaxGaugeValue = 2500;

// === HÀM CẬP NHẬT TRẠNG THÁI ===
void updateSystemStatus() {
  if (mq4Value < 500) {
    currentStatusText = "An toàn";       
    currentStatusLcd = "An toan";        
    currentStatusClass = "safe";
  } else if (mq4Value >= 500 && mq4Value <= 1500) {
    currentStatusText = "Cảnh báo";      
    currentStatusLcd = "Canh bao";       
    currentStatusClass = "warning";
  } else {
    currentStatusText = "NGUY HIỂM!";    
    currentStatusLcd = "NGUY HIEM!";     
    currentStatusClass = "danger";
  }
}

// === HÀM HIỂN THỊ LCD LUÂN PHIÊN (TRÁNH NHẤP NHÁY) ===
void updateLCD() {
  char line0[17];
  char line1[17];

  if (!systemOn) {
    snprintf(line0, sizeof(line0), "%-16s", "He thong: OFF");
    snprintf(line1, sizeof(line1), "%-16s", " (Da ngat)");
    
    lcd.setCursor(0, 0);
    lcd.print(line0);
    lcd.setCursor(0, 1);
    lcd.print(line1);
    return;
  }

  if (showGasScreen) {
    snprintf(line0, sizeof(line0), "Khi Metan: %-5d", mq4Value);
    snprintf(line1, sizeof(line1), "%-16s", currentStatusLcd.c_str());
  } else {
    char tempStr[8];
    char humStr[8];
    
    if (isnan(t)) strcpy(tempStr, "--");
    else snprintf(tempStr, sizeof(tempStr), "%.1f", t);
    
    if (isnan(h)) strcpy(humStr, "--");
    else snprintf(humStr, sizeof(humStr), "%.1f", h);
    
    snprintf(line0, sizeof(line0), "Nhiet do: %-4sC", tempStr);
    snprintf(line1, sizeof(line1), "Do am:    %-4s%%", humStr);
  }

  lcd.setCursor(0, 0);
  lcd.print(line0);
  lcd.setCursor(0, 1);
  lcd.print(line1);
}

// === HÀM TRẢ DỮ LIỆU JSON CHO WEB ===
String getStatusJson() {
  char jsonBuffer[256];
  
  const char* status_text = systemOn ? currentStatusText.c_str() : "(Hệ thống tắt)";
  const char* status_class = systemOn ? currentStatusClass.c_str() : "off";
  
  char val_str[8];
  if (systemOn) snprintf(val_str, sizeof(val_str), "%d", mq4Value);
  else strcpy(val_str, "---");
  
  char temp_str[8];
  if (systemOn && !isnan(t)) snprintf(temp_str, sizeof(temp_str), "%.1f", t);
  else strcpy(temp_str, "---");
  
  char hum_str[8];
  if (systemOn && !isnan(h)) snprintf(hum_str, sizeof(hum_str), "%.1f", h);
  else strcpy(hum_str, "---");

  snprintf(jsonBuffer, sizeof(jsonBuffer),
           "{\"value\":\"%s\",\"text\":\"%s\",\"class\":\"%s\",\"temp\":\"%s\",\"hum\":\"%s\",\"on\":%s}",
           val_str, status_text, status_class, temp_str, hum_str, systemOn ? "true" : "false");

  return String(jsonBuffer);
}

// === GIAO DIỆN WEB (CẢI TIẾN CÒI WEB ĐỒNG BỘ CỨNG) ===
const char webPageHtml[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Hệ thống Giám sát PRO</title>
<style>
  :root {
    --bg-color: #f4f7f6; --card-color: #ffffff;
    --text-main: #333; --text-sub: #777;
    --safe: #2ecc71; --warning: #f1c40f; --danger: #e74c3c;
    --off: #bdc3c7; --info: #3498db;
    --btn-start: #3498db; --btn-stop: #e74c3c;
    --btn-mute: #9b59b6;
  }
  body {
    font-family: 'Segoe UI', sans-serif; background-color: var(--bg-color);
    margin: 0; display: flex; justify-content: center; align-items: center; min-height: 100vh;
  }
  .main-card {
    background-color: var(--card-color); border-radius: 15px;
    box-shadow: 0 10px 30px rgba(0,0,0,0.1); padding: 30px;
    width: 90%; max-width: 500px; text-align: center;
  }
  h1 { margin-top: 0; font-size: 1.5rem; color: var(--text-main); }
  p.sub-title { color: var(--text-sub); margin-top: -10px; font-size: 0.9rem; }

  /* Gauge Metan */
  .gauge-container { margin: 20px 0; position: relative; }
  .gauge {
    position: relative; width: 150px; height: 150px; border-radius: 50%;
    background: conic-gradient(var(--off) 0%, var(--bg-color) 0%);
    margin: 0 auto; transition: background 0.3s ease;
  }
  .gauge-cover {
    position: absolute; top: 12px; left: 12px; right: 12px; bottom: 12px;
    background-color: var(--card-color); border-radius: 50%;
    display: flex; justify-content: center; align-items: center; flex-direction: column;
    box-shadow: inset 0px 2px 5px rgba(0,0,0,0.05);
  }
  .gauge-cover .val { font-size: 2rem; font-weight: bold; color: var(--text-main); line-height: 1;}
  .gauge-cover .lbl { font-size: 0.8rem; color: var(--text-sub); margin-top: 5px;}

  /* Grid Nhiệt độ - Độ ẩm */
  .env-grid {
    display: grid; grid-template-columns: 1fr 1fr; gap: 15px; margin-bottom: 20px;
  }
  .env-box {
    background: rgba(52, 152, 219, 0.1); border: 1px solid rgba(52, 152, 219, 0.3);
    border-radius: 8px; padding: 15px 10px; transition: all 0.3s ease;
  }
  .env-box .lbl { font-size: 0.9rem; color: var(--text-sub); display: block; margin-bottom: 5px; text-transform: uppercase; font-weight: 600;}
  .env-box .val { font-size: 1.5rem; font-weight: bold; color: var(--info); }
  .env-box.off { background: rgba(189,195,199,0.1); border-color: rgba(189,195,199,0.3); }
  .env-box.off .val { color: var(--off); }

  /* Status Box */
  .status-box {
    border-radius: 8px; padding: 15px; margin: 15px 0; font-size: 1.1rem;
    font-weight: 600; text-transform: uppercase; transition: all 0.3s ease;
  }
  .safe { background-color: rgba(46, 204, 113, 0.15); color: var(--safe); border: 1px solid var(--safe); }
  .warning { background-color: rgba(241, 196, 15, 0.15); color: var(--warning); border: 1px solid var(--warning); }
  .danger { background-color: rgba(231, 76, 60, 0.15); color: var(--danger); border: 1px solid var(--danger); }
  .off { background-color: rgba(189, 195, 199, 0.15); color: var(--off); border: 1px solid var(--off); }

  /* Controls */
  .control-area { display: flex; justify-content: center; gap: 10px; margin-top: 20px; flex-wrap: wrap; }
  .btn {
    border: none; border-radius: 5px; padding: 12px 20px; font-size: 0.95rem;
    font-weight: 600; cursor: pointer; transition: all 0.2s ease; display: flex; align-items: center; gap: 8px;
  }
  .btn:active { transform: translateY(2px); }
  .btn:hover { box-shadow: 0 4px 12px rgba(0,0,0,0.15); }
  .btn-start { background-color: var(--btn-start); color: #fff; }
  .btn-stop { background-color: var(--btn-stop); color: #fff; }
  .btn-mute { background-color: var(--btn-mute); color: #fff; }
  .btn-mute.muted { background-color: var(--off); }
  .btn:disabled { opacity: 0.5; cursor: not-allowed; }
</style>
</head>
<body>
  <div class="main-card">
    <h1>Giám sát Môi trường</h1>
    <p class="sub-title">Khí Metan - Nhiệt độ - Độ ẩm</p>

    <div class="gauge-container">
      <div id="gauge" class="gauge">
        <div class="gauge-cover">
          <span id="value" class="val">---</span>
          <span class="lbl">Khí Metan</span>
        </div>
      </div>
    </div>

    <div class="env-grid">
      <div id="temp-box" class="env-box off">
        <span class="lbl">Nhiệt độ</span>
        <span id="temp" class="val">--</span><span style="color:var(--text-sub); font-size:0.9rem"> °C</span>
      </div>
      <div id="hum-box" class="env-box off">
        <span class="lbl">Độ ẩm</span>
        <span id="hum" class="val">--</span><span style="color:var(--text-sub); font-size:0.9rem"> %</span>
      </div>
    </div>

    <div id="status-box" class="status-box off">
      <span id="status-text">...</span>
    </div>

    <div class="control-area">
      <button id="btn-start" class="btn btn-start" onclick="controlSystem('/start')" disabled>Bật</button>
      <button id="btn-stop" class="btn btn-stop" onclick="controlSystem('/stop')">Ngắt</button>
      <button id="btn-mute" class="btn btn-mute" onclick="toggleMute()">🔊 Còi Web</button>
    </div>
  </div>

  <script>
    const gauge = document.getElementById('gauge');
    const valueSpan = document.getElementById('value');
    const tempSpan = document.getElementById('temp');
    const humSpan = document.getElementById('hum');
    const tempBox = document.getElementById('temp-box');
    const humBox = document.getElementById('hum-box');
    const statusBox = document.getElementById('status-box');
    const statusTextSpan = document.getElementById('status-text');
    const btnStart = document.getElementById('btn-start');
    const btnStop = document.getElementById('btn-stop');
    const btnMute = document.getElementById('btn-mute');

    const maxGaugeValue = 2500;
    
    // --- BỘ TỔNG HỢP ÂM THANH ĐỒNG BỘ PHẦN CỨNG ---
    let audioCtx = null;
    let osc = null;
    let gainNode = null;
    let warningTimer = null;
    let isMuted = false;
    let currentAlarmState = "none"; // Các trạng thái: "none", "warning", "danger"

    function initAudio() {
      if (!audioCtx) {
        audioCtx = new (window.AudioContext || window.webkitAudioContext)();
        
        // Khởi tạo một nguồn dao động và một node quản lý âm lượng dùng chung
        osc = audioCtx.createOscillator();
        gainNode = audioCtx.createGain();
        
        osc.type = 'sine';
        osc.frequency.setValueAtTime(600, audioCtx.currentTime); // Mặc định 600Hz
        gainNode.gain.setValueAtTime(0, audioCtx.currentTime);   // Ban đầu im lặng
        
        osc.connect(gainNode);
        gainNode.connect(audioCtx.destination);
        osc.start(); // Chạy ngầm oscillator
      }
    }

    function toggleMute() {
      initAudio();
      if (audioCtx && audioCtx.state === 'suspended') {
        audioCtx.resume();
      }
      isMuted = !isMuted;
      if (isMuted) {
        btnMute.innerText = "🔇 Tắt còi";
        btnMute.classList.add('muted');
        muteAudio();
      } else {
        btnMute.innerText = "🔊 Còi Web";
        btnMute.classList.remove('muted');
        unmuteAudio();
      }
    }

    function muteAudio() {
      if (gainNode && audioCtx) {
        gainNode.gain.setValueAtTime(0, audioCtx.currentTime);
      }
      if (warningTimer) {
        clearInterval(warningTimer);
        warningTimer = null;
      }
    }

    function unmuteAudio() {
      applyAlarmState(currentAlarmState);
    }

    // Thiết lập trạng thái phát còi
    function applyAlarmState(state) {
      currentAlarmState = state;
      if (isMuted || !audioCtx) return;

      if (audioCtx.state === 'suspended') {
        audioCtx.resume();
      }

      // Xóa bộ nhấp nháy bíp bíp cũ nếu có
      if (warningTimer) {
        clearInterval(warningTimer);
        warningTimer = null;
      }

      if (state === "none") {
        // An toàn hoặc tắt: Tắt âm lượng
        gainNode.gain.setValueAtTime(0, audioCtx.currentTime);
      } 
      else if (state === "warning") {
        // Cảnh báo: Bíp bíp bíp nhịp 200ms (khớp hoàn toàn với phần cứng)
        osc.frequency.setValueAtTime(600, audioCtx.currentTime); // Tần số cảnh báo 600Hz
        let beepOn = false;
        
        warningTimer = setInterval(() => {
          if (isMuted) return;
          beepOn = !beepOn;
          // Ramp nhanh để tránh tiếng nổ click của loa
          gainNode.gain.setTargetAtTime(beepOn ? 0.2 : 0, audioCtx.currentTime, 0.01);
        }, 200); 
      } 
      else if (state === "danger") {
        // Nguy hiểm: Kêu liên tục không ngừng
        osc.frequency.setValueAtTime(1000, audioCtx.currentTime); // Tần số nguy hiểm 1000Hz (chói tai hơn)
        gainNode.gain.setTargetAtTime(0.2, audioCtx.currentTime, 0.01);
      }
    }

    function handleAlarmSound(statusClass, systemOn) {
      if (!systemOn || statusClass === 'safe' || statusClass === 'off') {
        if (currentAlarmState !== "none") {
          applyAlarmState("none");
        }
      } else if (statusClass === 'warning') {
        if (currentAlarmState !== "warning") {
          applyAlarmState("warning");
        }
      } else if (statusClass === 'danger') {
        if (currentAlarmState !== "danger") {
          applyAlarmState("danger");
        }
      }
    }

    // Tự động kích hoạt khi người dùng click bất kỳ chỗ nào trên màn hình lần đầu
    document.addEventListener('click', () => {
      initAudio();
      if (audioCtx && audioCtx.state === 'suspended') {
        audioCtx.resume();
      }
    }, { once: true });

    function updatePage() {
      fetch('/data')
        .then(res => res.json())
        .then(data => {
          // Metan Gauge
          valueSpan.innerText = data.value;
          let percent = 0;
          if (data.on && !isNaN(parseInt(data.value))) {
              percent = Math.min((parseInt(data.value) / maxGaugeValue) * 100, 100);
          }
          let color = 'var(--off)';
          if (data.on) {
              if (data.class === 'safe') color = 'var(--safe)';
              else if (data.class === 'warning') color = 'var(--warning)';
              else if (data.class === 'danger') color = 'var(--danger)';
          }
          gauge.style.background = `conic-gradient(${color} ${percent}%, var(--bg-color) ${percent}%)`;

          // Temp & Hum
          tempSpan.innerText = data.temp;
          humSpan.innerText = data.hum;
          if(data.on) {
            tempBox.classList.remove('off'); humBox.classList.remove('off');
          } else {
            tempBox.classList.add('off'); humBox.classList.add('off');
          }

          // Status
          statusTextSpan.innerText = data.text;
          statusBox.className = `status-box ${data.class}`;

          // Cập nhật trạng thái Còi Web
          handleAlarmSound(data.class, data.on);

          // Buttons
          btnStart.disabled = data.on;
          btnStop.disabled = !data.on;
        })
        .catch(err => {
          statusTextSpan.innerText = "LỖI KẾT NỐI";
          statusBox.className = "status-box off";
          gauge.style.background = `conic-gradient(var(--off) 0%, var(--bg-color) 0%)`;
          valueSpan.innerText = '---'; tempSpan.innerText = '--'; humSpan.innerText = '--';
          tempBox.classList.add('off'); humBox.classList.add('off');
          applyAlarmState("none");
        });
    }

    function controlSystem(endpoint) {
        btnStart.disabled = true; btnStop.disabled = true;
        fetch(endpoint).then(res => {
            if (res.ok) setTimeout(updatePage, 100); 
            else { alert('Lỗi: ' + endpoint); updatePage(); }
        });
    }

    updatePage(); 
    setInterval(updatePage, 1000); 
  </script>
</body>
</html>
)rawliteral";

// === KHỞI TẠO ===
void setup() {
  Serial.begin(115200);
  pinMode(buzzerPin, OUTPUT);
  pinMode(ledGreen, OUTPUT);
  pinMode(ledYellow, OUTPUT);
  pinMode(ledRed, OUTPUT);
  
  digitalWrite(buzzerPin, HIGH); // Tắt còi mặc định

  dht.begin();

  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("He thong: ON    ");
  lcd.setCursor(0, 1);
  lcd.print("                ");
  delay(1000); 
  
  WiFi.softAP(ap_ssid, ap_password);
  Serial.print("WiFi AP da tao: ");
  Serial.println(ap_ssid);

  mq4Value = analogRead(mq4Pin);
  t = dht.readTemperature();
  h = dht.readHumidity();
  updateSystemStatus();
  updateLCD();

  // --- ROUTES WEB ---
  server.on("/data", []() {
    server.send(200, "application/json", getStatusJson());
  });

  server.on("/start", []() {
    systemOn = true;
    updateLCD(); 
    server.send(200, "text/plain", "System started");
  });

  server.on("/stop", []() {
    systemOn = false;
    digitalWrite(buzzerPin, HIGH); 
    digitalWrite(ledGreen, LOW);
    digitalWrite(ledYellow, LOW);
    digitalWrite(ledRed, LOW);
    lastBuzzerState = HIGH; 
    updateLCD(); 
    server.send(200, "text/plain", "System stopped");
  });

  server.on("/", []() {
    server.send_P(200, "text/html", webPageHtml);
  });

  server.begin();
}

// === VÒNG LẶP CHÍNH ===
void loop() {
  server.handleClient();
  unsigned long currentMillis = millis();

  // 1. Đọc cảm biến mỗi giây
  if (currentMillis - lastSensorReadTime >= sensorReadInterval) {
    lastSensorReadTime = currentMillis;
    
    mq4Value = analogRead(mq4Pin);
    
    float newT = dht.readTemperature();
    float newH = dht.readHumidity();
    if (!isnan(newT)) t = newT;
    if (!isnan(newH)) h = newH;

    updateSystemStatus(); 
    
    if (systemOn) {
      updateLCD();
    }

    Serial.printf("Metan: %d | Nhiet do: %.1fC | Do am: %.1f%%\n", mq4Value, t, h);
  }

  // 2. Luân phiên màn hình LCD mỗi 2 giây
  if (currentMillis - lastLcdToggle >= lcdToggleInterval) {
    lastLcdToggle = currentMillis;
    showGasScreen = !showGasScreen; 
    if (systemOn) {
        updateLCD();
    }
  }

  // 3. Điều khiển thiết bị vật lý
  if (systemOn) {
    if (currentStatusClass == "safe") {
      digitalWrite(ledGreen, HIGH);
      digitalWrite(ledYellow, LOW);
      digitalWrite(ledRed, LOW);
      digitalWrite(buzzerPin, HIGH); 
      lastBuzzerState = HIGH; 
    } 
    else if (currentStatusClass == "warning") {
      digitalWrite(ledGreen, LOW);
      digitalWrite(ledYellow, HIGH);
      digitalWrite(ledRed, LOW);
      
      if (currentMillis - lastBuzzerBeep >= buzzerBeepInterval) {
        lastBuzzerBeep = currentMillis;
        lastBuzzerState = !lastBuzzerState;
        digitalWrite(buzzerPin, lastBuzzerState);
      }
    } 
    else if (currentStatusClass == "danger") {
      digitalWrite(ledGreen, LOW);
      digitalWrite(ledYellow, LOW);
      digitalWrite(ledRed, HIGH);
      digitalWrite(buzzerPin, LOW); 
      lastBuzzerState = LOW;
    }
  } else {
    digitalWrite(ledGreen, LOW);
    digitalWrite(ledYellow, LOW);
    digitalWrite(ledRed, LOW);
    digitalWrite(buzzerPin, HIGH); 
  }
}