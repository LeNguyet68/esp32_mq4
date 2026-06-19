#include <Wire.h> 
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DHT.h> // Thư viện cảm biến Nhiệt độ/Độ ẩm

// --- CÀI ĐẶT CẢM BIẾN DHT ---
#define DHTPIN 15       // Đã đổi sang chân 15
#define DHTTYPE DHT22   // Đã đổi sang loại cảm biến DHT22
DHT dht(DHTPIN, DHTTYPE);

// --- CÀI ĐẶT LCD VÀ CÁC CHÂN KHÁC ---
LiquidCrystal_I2C lcd(0x27, 16, 2);

int mq4Pin = 34;
int buzzerPin = 18; // Giả định Module Active Low (HIGH=Tắt, LOW=Bật)

int ledGreen = 25;
int ledYellow = 26;
int ledRed = 27;

// --- CÀI ĐẶT WIFI ---
const char* ap_ssid = "ESP32-Metan-Pro";     
const char* ap_password = "12345678";        

WebServer server(80);
bool systemOn = true;

// --- CÁC BIẾN TRẠNG THÁI ---
int mq4Value = 0;
float t = 0.0; // Nhiệt độ
float h = 0.0; // Độ ẩm

String currentStatusText = ""; 
String currentStatusLcd = "";  
String currentStatusClass = "";
bool lastBuzzerState = HIGH;

// Thời gian không đồng bộ (non-blocking)
unsigned long lastSensorReadTime = 0;
const long sensorReadInterval = 1000; // Đọc cảm biến mỗi 1 giây

unsigned long lastBuzzerBeep = 0;
const long buzzerBeepInterval = 200; 

// Biến cho việc luân phiên màn hình LCD
unsigned long lastLcdToggle = 0;
const long lcdToggleInterval = 2000; // Đổi màn hình mỗi 2 giây
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

// === HÀM HIỂN THỊ LCD LUÂN PHIÊN ===
void updateLCD() {
  if (!systemOn) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("He thong: OFF");
    lcd.setCursor(0, 1);
    lcd.print(" (Da ngat)");
    return;
  }

  // Nếu hệ thống đang bật, luân phiên hiển thị 2 màn hình
  if (showGasScreen) {
    // Màn hình 1: Khí gas
    lcd.setCursor(0, 0);
    lcd.print("Khi Metan:      "); 
    lcd.setCursor(11, 0);
    lcd.print(mq4Value);
    lcd.print("   "); // Xóa rác
    
    lcd.setCursor(0, 1);
    lcd.print("                ");
    lcd.setCursor(0, 1);
    lcd.print(currentStatusLcd);
  } else {
    // Màn hình 2: Nhiệt độ & Độ ẩm
    lcd.setCursor(0, 0);
    lcd.print("Nhiet do: ");
    if(isnan(t)) lcd.print("--"); else lcd.print(t, 1);
    lcd.print("C  ");

    lcd.setCursor(0, 1);
    lcd.print("Do am:    ");
    if(isnan(h)) lcd.print("--"); else lcd.print(h, 1);
    lcd.print("%  ");
  }
}

// === HÀM TRẢ DỮ LIỆU JSON CHO WEB ===
String getStatusJson() {
  String status_text = systemOn ? currentStatusText : "(Hệ thống tắt)";
  String status_class = systemOn ? currentStatusClass : "off";
  String value_text = systemOn ? String(mq4Value) : "---";
  
  // Xử lý NaN nếu cảm biến DHT chưa cắm hoặc lỗi
  String temp_text = (systemOn && !isnan(t)) ? String(t, 1) : "---";
  String hum_text = (systemOn && !isnan(h)) ? String(h, 1) : "---";

  return "{\"value\":\"" + value_text + "\",\"text\":\"" + status_text + "\",\"class\":\"" + status_class + "\",\"temp\":\"" + temp_text + "\",\"hum\":\"" + hum_text + "\",\"on\":" + String(systemOn) + "}";
}

// === GIAO DIỆN WEB (HTML/CSS/JS) ===
const char* webPageHtml = R"rawliteral(
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
  .control-area { display: flex; justify-content: center; gap: 15px; margin-top: 20px; }
  .btn {
    border: none; border-radius: 5px; padding: 12px 24px; font-size: 1rem;
    font-weight: 600; cursor: pointer; transition: all 0.2s ease; display: flex; align-items: center; gap: 8px;
  }
  .btn:active { transform: translateY(2px); }
  .btn:hover { box-shadow: 0 4px 12px rgba(0,0,0,0.15); }
  .btn-start { background-color: var(--btn-start); color: #fff; }
  .btn-stop { background-color: var(--btn-stop); color: #fff; }
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

    const maxGaugeValue = 2500;

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

  // Khởi động DHT
  dht.begin();

  // Khởi động LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("He thong: ON");
  delay(1000); 
  
  // Phát WiFi
  WiFi.softAP(ap_ssid, ap_password);
  Serial.println("WiFi AP da tao: ESP32-Metan-Pro");

  // Đọc dữ liệu lần đầu
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
    digitalWrite(buzzerPin, HIGH); // Tắt còi
    digitalWrite(ledGreen, LOW);
    digitalWrite(ledYellow, LOW);
    digitalWrite(ledRed, LOW);
    updateLCD(); 
    server.send(200, "text/plain", "System stopped");
  });

  server.on("/", []() {
    server.send(200, "text/html", String(webPageHtml));
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
    
    // Đọc DHT (có thể mất khoảng 250ms tùy module)
    float newT = dht.readTemperature();
    float newH = dht.readHumidity();
    if (!isnan(newT)) t = newT;
    if (!isnan(newH)) h = newH;

    updateSystemStatus(); 
  }

  // 2. Luân phiên màn hình LCD mỗi 2 giây
  if (currentMillis - lastLcdToggle >= lcdToggleInterval) {
    lastLcdToggle = currentMillis;
    showGasScreen = !showGasScreen; // Đảo trạng thái hiển thị
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
      digitalWrite(buzzerPin, HIGH); // Tắt còi
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
      digitalWrite(buzzerPin, LOW); // Liên tục
    }
  } else {
    // Nếu tắt
    digitalWrite(ledGreen, LOW);
    digitalWrite(ledYellow, LOW);
    digitalWrite(ledRed, LOW);
    digitalWrite(buzzerPin, HIGH); // Tắt còi
  }
}