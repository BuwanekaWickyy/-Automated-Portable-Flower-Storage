#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <vector>
#include <time.h>

// --- I2C LCD and DHT Definitions ---
#define I2C_ADDR 0x3F
#define LCD_COLS 16
#define LCD_ROWS 2
#define DHTPIN 14
#define DHTTYPE DHT11

// --- Hardware Pin Definitions ---
#define TEMP_POT_PIN 32        // Analog pin for Temperature Threshold Potentiometer
#define HUMIDITY_POT_PIN 33    // Analog pin for Humidity Threshold Potentiometer
#define PUSH_BUTTON_PIN 15     // Pin for T/H display toggle button (using INPUT_PULLUP)
#define COOLER_IN1_PIN 26      // TEC/Cooler Motor Driver IN1
#define COOLER_IN2_PIN 27      // TEC/Cooler Motor Driver IN2
#define FAN_IN1_PIN 12         // Fan Motor Driver IN1
#define FAN_IN2_PIN 13         // Fan Motor Driver IN2

// --- CRITICAL ADDITION: Water Pump Pins ---
#define WATERPUMP_IN1_PIN 25   // Water Pump Motor Driver IN1 (Ensure this is a valid ESP32 GPIO)
#define WATERPUMP_IN2_PIN 4    // Water Pump Motor Driver IN2 (Ensure this is a valid ESP32 GPIO)

// --- WiFi Credentials ---
const char* ssid = "********";
const char* password = "*******";

// --- Global State Variables ---
String tecState = "Inactive";      // Possible values: "Cooling", "Heating", "Inactive"
String waterPumpState = "Off";     // Possible values: "On", "Off"

// --- Constants and Objects ---
LiquidCrystal_I2C lcd(I2C_ADDR, LCD_COLS, LCD_ROWS);
DHT dht(DHTPIN, DHTTYPE);
WebServer server(80);

// --- Control & Threshold Variables ---
bool displayThresholds = false;
int temperatureThreshold;
int humidityThreshold;

// --- Data Logging Structure and Vector ---
struct SensorData {
    float temperature;
    float humidity;
    unsigned long timestamp; // Unix time
};
std::vector<SensorData> sensorReadings;

// --- Water Pump Cycle Timing (in milliseconds) ---
unsigned long waterPumpCycleStartTime = 0;
const unsigned long waterPumpOnDuration = 2000;   // Pump on for 2 seconds
const unsigned long waterPumpOffDuration = 20000; // Pump off for 20 seconds
const unsigned long waterPumpCyclePeriod = waterPumpOnDuration + waterPumpOffDuration; // 22 seconds total cycle

// --- Function Prototypes ---
void readThresholds();
void storeSensorData();
void updateLCD();
void controlCoolerAndWaterPump();
void handleRoot();
void handleSensorDataJSON();

// =========================================================================
// FUNCTION DEFINITIONS
// =========================================================================

/**
 * @brief Reads the target temperature and humidity thresholds from potentiometers.
 */
void readThresholds() {
    // Read analog value (0-4095 for ESP32), scale to a percentage (0-100)
    temperatureThreshold = map(analogRead(TEMP_POT_PIN), 0, 4095, 0, 100);
    humidityThreshold = map(analogRead(HUMIDITY_POT_PIN), 0, 4095, 0, 100);
}

/**
 * @brief Reads sensor data and stores it in the vector, maintaining a size limit.
 */
void storeSensorData() {
    float temp = dht.readTemperature();
    float hum = dht.readHumidity();

    // Check if the sensor reading is valid before storing
    if (!isnan(temp) && !isnan(hum)) {
        if (sensorReadings.size() >= 100) {
            sensorReadings.erase(sensorReadings.begin());
        }
        SensorData newData = { temp, hum, time(nullptr) };
        sensorReadings.push_back(newData);
    }
}

/**
 * @brief Updates the LCD display with current readings or thresholds.
 */
void updateLCD() {
    lcd.clear();
    if (displayThresholds) {
        lcd.setCursor(0, 0);
        lcd.print("T Thresh: ");
        lcd.print(temperatureThreshold);
        lcd.print((char)223); // Degree symbol
        lcd.print("C");

        lcd.setCursor(0, 1);
        lcd.print("H Thresh: ");
        lcd.print(humidityThreshold);
        lcd.print("%");
    } else {
        if (!sensorReadings.empty()) {
            const SensorData& latestData = sensorReadings.back();
            lcd.setCursor(0, 0);
            lcd.print("Temp: ");
            lcd.print(latestData.temperature, 1);
            lcd.print((char)223);
            lcd.print("C");

            lcd.setCursor(0, 1);
            lcd.print("Hum: ");
            lcd.print(latestData.humidity, 1);
            lcd.print("%");
        } else {
            lcd.setCursor(0, 0);
            lcd.print("Reading Sensor...");
        }
    }
}

/**
 * @brief Controls the cooler/TEC, fan, and water pump based on sensor readings and thresholds.
 */
void controlCoolerAndWaterPump() {
    if (sensorReadings.empty()) return;

    const SensorData& latestData = sensorReadings.back();
    const float HYSTERESIS = 1.0; // 1.0 degree C dead band for TEC control

    // --- Cooler and Fan Control Logic (with Hysteresis) ---

    // Cooling: Current Temp > Threshold + Hysteresis/2
    if (latestData.temperature > (temperatureThreshold + HYSTERESIS / 2.0)) {
        digitalWrite(COOLER_IN1_PIN, HIGH);
        digitalWrite(COOLER_IN2_PIN, LOW);
        tecState = "Cooling";

        // Turn on the fan (assuming IN1 HIGH, IN2 LOW turns it on)
        digitalWrite(FAN_IN1_PIN, HIGH);
        digitalWrite(FAN_IN2_PIN, LOW);

    // Heating: Current Temp < Threshold - Hysteresis/2
    } else if (latestData.temperature < (temperatureThreshold - HYSTERESIS / 2.0)) {
        digitalWrite(COOLER_IN1_PIN, LOW);
        digitalWrite(COOLER_IN2_PIN, HIGH);
        tecState = "Heating";

        // Turn on the fan
        digitalWrite(FAN_IN1_PIN, HIGH);
        digitalWrite(FAN_IN2_PIN, LOW);

    // Inactive: Current Temp is within the Hysteresis dead band
    } else {
        digitalWrite(COOLER_IN1_PIN, LOW);
        digitalWrite(COOLER_IN2_PIN, LOW);
        tecState = "Inactive";

        // Turn off the fan
        digitalWrite(FAN_IN1_PIN, LOW);
        digitalWrite(FAN_IN2_PIN, LOW);
    }

    // --- Water Pump Control Logic (Cyclical for Humidification) ---
    unsigned long currentTime = millis();

    if (latestData.humidity < humidityThreshold) {
        // If the current cycle is over, reset the start time
        if (currentTime - waterPumpCycleStartTime >= waterPumpCyclePeriod) {
            waterPumpCycleStartTime = currentTime;
        }

        // Turn the water pump on for the first 'waterPumpOnDuration' of the cycle
        if (currentTime - waterPumpCycleStartTime < waterPumpOnDuration) {
            digitalWrite(WATERPUMP_IN1_PIN, HIGH); // <--- UNCOMMENTED/FIXED
            digitalWrite(WATERPUMP_IN2_PIN, LOW);  // <--- UNCOMMENTED/FIXED
            waterPumpState = "On";
        } else {
            // Keep the water pump off for the rest of the cycle
            digitalWrite(WATERPUMP_IN1_PIN, LOW);  // <--- UNCOMMENTED/FIXED
            digitalWrite(WATERPUMP_IN2_PIN, LOW);  // <--- UNCOMMENTED/FIXED
            waterPumpState = "Off";
        }
    } else {
        // Stop the water pump if the humidity is met or exceeded
        digitalWrite(WATERPUMP_IN1_PIN, LOW);  // <--- UNCOMMENTED/FIXED
        digitalWrite(WATERPUMP_IN2_PIN, LOW);  // <--- UNCOMMENTED/FIXED
        waterPumpState = "Off";

        // Reset the cycle start time to ensure a full delay before the next activation
        waterPumpCycleStartTime = currentTime;
    }
}

/**
 * @brief Handles the root web page request (serves the HTML/CSS/JS).
 */
void handleRoot() {
    server.send(200, "text/html", R"=====(
<!DOCTYPE html>
<html>
<head>
<link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/5.15.4/css/all.min.css" />
<meta charset='UTF-8'>
<title>Sensor Data</title>
<script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
<style>
body { text-align: center; background-color: #f0f8ff; font-family: Arial, sans-serif; }
h1 { background-color: #007BFF; color: white; padding: 15px; margin-bottom: 20px; }
.data-box { 
    margin: 10px auto; 
    width: 80%; 
    max-width: 600px; 
    padding: 15px; 
    border-radius: 8px;
    box-shadow: 0 4px 8px rgba(0,0,0,0.1);
    background-color: white;
}
p { font-size: 20px; margin: 8px 0; }
i { font-size: 24px; margin-right: 8px; }
#currentTemperature i { color: #ff4500; }
#currentHumidity i { color: #00bfff; }
#tempThreshold, #humidityThreshold { color: #444; font-size: 18px; }
#lastUpdated { font-size: 14px; color: #777; margin-top: 15px; }
#tecState, #waterPumpState {
    font-weight: bold;
    padding: 5px 10px;
    border-radius: 5px;
    display: inline-block;
    margin-top: 10px;
}
#chart-container {
    width: 80%;
    max-width: 800px;
    margin: 20px auto;
    background-color: white;
    padding: 20px;
    border-radius: 8px;
    box-shadow: 0 4px 8px rgba(0,0,0,0.1);
}
</style>
<script>
function fetchData() {
    const scrollPosition = window.scrollY; // Capture the current scroll position
    fetch('/sensorDataJSON')
    .then(response => response.json())
    .then(data => {
        document.getElementById('currentTemperature').innerHTML = '<i class="fas fa-thermometer-half"></i> Current Temperature: ' + data.latestTemperature + '°C';
        document.getElementById('currentHumidity').innerHTML = '<i class="fas fa-tint"></i> Current Humidity: ' + data.latestHumidity + '%';
        document.getElementById('tempThreshold').innerText = 'Temperature Threshold: ' + data.temperatureThreshold + '°C';
        document.getElementById('humidityThreshold').innerText = 'Humidity Threshold: ' + data.humidityThreshold + '%';
        document.getElementById('lastUpdated').innerText = 'Last Updated: ' + new Date(data.lastUpdated * 1000).toLocaleString();
        
        // TEC State Logic
        var tecStateElement = document.getElementById('tecState');
        tecStateElement.innerText = 'TEC State: ' + data.tecState;
        if(data.tecState === "Cooling") {
            tecStateElement.style.backgroundColor = '#1e90ff'; // Dodger Blue
            tecStateElement.style.color = 'white';
        } else if(data.tecState === "Heating") {
            tecStateElement.style.backgroundColor = '#dc143c'; // Crimson Red
            tecStateElement.style.color = 'white';
        } else {
            tecStateElement.style.backgroundColor = '#f0f0f0';
            tecStateElement.style.color = 'black';
        }

        // Water Pump State Logic
        var waterPumpStateElement = document.getElementById('waterPumpState');
        waterPumpStateElement.innerText = 'Water Pump State: ' + data.waterPumpState;
        if(data.waterPumpState === "On") {
            waterPumpStateElement.style.backgroundColor = '#32cd32'; // Lime Green
            waterPumpStateElement.style.color = 'white';
        } else {
            waterPumpStateElement.style.backgroundColor = '#87cefa'; // Light Sky Blue
            waterPumpStateElement.style.color = 'white';
        }

        renderChart(data.data);
        window.scrollTo(0, scrollPosition);
    });
}

function renderChart(sensorData) {
    const ctx = document.getElementById('sensorChart').getContext('2d');
    if (window.chart) {
        window.chart.destroy(); // Clear existing chart
    }

    // Prepare labels and datasets
    const labels = sensorData.map(data => new Date(data.timestamp * 1000).toLocaleTimeString());
    const temperatureData = sensorData.map(data => data.temperature);
    const humidityData = sensorData.map(data => data.humidity);

    window.chart = new Chart(ctx, {
        type: 'line',
        data: {
            labels: labels,
            datasets: [
                {
                    label: 'Temperature (°C)',
                    data: temperatureData,
                    borderColor: 'red',
                    backgroundColor: 'rgba(255, 99, 132, 0.2)',
                    fill: false,
                    yAxisID: 'y'
                }, 
                {
                    label: 'Humidity (%)',
                    data: humidityData,
                    borderColor: 'blue',
                    backgroundColor: 'rgba(54, 162, 235, 0.2)',
                    fill: false,
                    yAxisID: 'y1'
                }
            ]
        },
        options: {
            responsive: true,
            scales: {
                y: {
                    type: 'linear',
                    display: true,
                    position: 'left',
                    title: {
                        display: true,
                        text: 'Temperature (°C)'
                    }
                },
                y1: {
                    type: 'linear',
                    display: true,
                    position: 'right',
                    title: {
                        display: true,
                        text: 'Humidity (%)'
                    },
                    grid: {
                        drawOnChartArea: false, // Only draw grid lines for the first axis
                    }
                }
            }
        }
    });
}

document.addEventListener('DOMContentLoaded', fetchData);
setInterval(fetchData, 5000); // Fetch new data every 5 seconds
</script>
</head>
<body>
    <h1>Climate Control Monitor (ESP32)</h1>
    <div class="data-box">
        <p id="currentTemperature"></p>
        <p id="currentHumidity"></p>
        <p id="tempThreshold"></p>
        <p id="humidityThreshold"></p>
        <p>
            <span id="tecState">TEC State: --</span>
            <span id="waterPumpState">Water Pump State: --</span>
        </p>
        <p id="lastUpdated"></p>
    </div>
    <div id="chart-container">
        <h2>History Chart</h2>
        <canvas id="sensorChart"></canvas>
    </div>
</body>
</html>
)=====");
}

/**
 * @brief Handles the JSON data request for the web server.
 */
void handleSensorDataJSON() {
    String jsonResponse = "{";
    unsigned long lastUpdateTime = time(nullptr);

    if (!sensorReadings.empty()) {
        const SensorData& latestData = sensorReadings.back();
        jsonResponse += "\"latestTemperature\":" + String(latestData.temperature, 1) + ",";
        jsonResponse += "\"latestHumidity\":" + String(latestData.humidity, 1) + ",";
        jsonResponse += "\"temperatureThreshold\":" + String(temperatureThreshold) + ",";
        jsonResponse += "\"humidityThreshold\":" + String(humidityThreshold) + ",";
        jsonResponse += "\"lastUpdated\":" + String(lastUpdateTime) + ",";
        jsonResponse += "\"data\":[";
        for (size_t i = 0; i < sensorReadings.size(); ++i) {
            const SensorData& data = sensorReadings[i];
            jsonResponse += "{\"temperature\":" + String(data.temperature, 1) + ", \"humidity\":" + String(data.humidity, 1) + ", \"timestamp\":" + String(data.timestamp) + "}";
            if (i < sensorReadings.size() - 1) jsonResponse += ",";
        }
        jsonResponse += "]";
    } else {
        jsonResponse += "\"latestTemperature\": null,";
        jsonResponse += "\"latestHumidity\": null,";
        jsonResponse += "\"temperatureThreshold\":" + String(temperatureThreshold) + ",";
        jsonResponse += "\"humidityThreshold\":" + String(humidityThreshold) + ",";
        jsonResponse += "\"lastUpdated\":" + String(lastUpdateTime) + ",";
        jsonResponse += "\"data\":[]";
    }

    jsonResponse += ",\"tecState\":\"" + tecState + "\"";
    jsonResponse += ",\"waterPumpState\":\"" + waterPumpState + "\"";
    jsonResponse += "}";

    server.send(200, "application/json", jsonResponse);
    Serial.println("Data transferred to the webserver.");
}

// =========================================================================
// SETUP and LOOP
// =========================================================================

void setup() {
    Serial.begin(115200);

    // --- LCD & DHT Setup ---
    lcd.init();
    lcd.backlight();
    dht.begin();

    // --- Pin Mode Setup ---
    pinMode(TEMP_POT_PIN, INPUT);
    pinMode(HUMIDITY_POT_PIN, INPUT);
    pinMode(PUSH_BUTTON_PIN, INPUT_PULLUP);
    pinMode(COOLER_IN1_PIN, OUTPUT);
    pinMode(COOLER_IN2_PIN, OUTPUT);
    pinMode(FAN_IN1_PIN, OUTPUT);
    pinMode(FAN_IN2_PIN, OUTPUT);
    // CRITICAL FIX: Water Pump Pin Modes
    pinMode(WATERPUMP_IN1_PIN, OUTPUT);
    pinMode(WATERPUMP_IN2_PIN, OUTPUT);

    // --- WiFi Connection ---
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Connecting to");
    lcd.setCursor(0, 1);
    lcd.print(ssid);

    WiFi.begin(ssid, password);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    // --- Post-WiFi Setup ---
    lcd.clear();
    if (WiFi.status() == WL_CONNECTED) {
        lcd.setCursor(0, 0);
        lcd.print("Connected!");
        lcd.setCursor(0, 1);
        lcd.print(WiFi.localIP());
        Serial.println("\nConnected to WiFi");
        Serial.print("IP Address: ");
        Serial.println(WiFi.localIP());

        // mDNS Setup
        if (!MDNS.begin("esp32")) {
            Serial.println("Error starting mDNS");
        }

        // NTP Time Sync
        configTime(0, 0, "pool.ntp.org", "time.nist.gov");
        Serial.println("Waiting for NTP time sync: ");
        time_t nowSecs = time(nullptr);
        while (nowSecs < 24 * 3600) {
            delay(500);
            Serial.print(".");
            nowSecs = time(nullptr);
        }
        Serial.println("\nTime synchronized");

        // Web Server Setup
        server.on("/", handleRoot);
        server.on("/sensorDataJSON", HTTP_GET, handleSensorDataJSON);
        server.begin();
        Serial.println("HTTP server started");
    } else {
        lcd.setCursor(0, 0);
        lcd.print("WiFi Failed!");
        Serial.println("\nWiFi connection failed.");
    }
    delay(2000);
}

void loop() {
    server.handleClient();
    readThresholds();
    storeSensorData();
    controlCoolerAndWaterPump();

    // --- Button Debounce/Toggle Logic ---
    static bool lastButtonState = HIGH;
    static unsigned long lastDebounceTime = 0;
    const unsigned long debounceDelay = 50; // milliseconds

    bool currentButtonState = digitalRead(PUSH_BUTTON_PIN);

    if (currentButtonState != lastButtonState) {
        lastDebounceTime = millis();
    }

    if ((millis() - lastDebounceTime) > debounceDelay) {
        if (currentButtonState == LOW) { // Button pressed
            displayThresholds = !displayThresholds;
        }
    }

    lastButtonState = currentButtonState;

    // --- Update LCD and delay for next sensor read/control loop ---
    updateLCD();
    delay(2000); // 2 second delay between full control cycles/LCD update
}