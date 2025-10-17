Automated Portable Flower Storage
-------------------------------------

A complete embedded system using the ESP32 to monitor and regulate temperature and humidity in an environment (e.g., a terrarium, grow box, or small room). It utilizes a TEC (Peltier cooler/heater) for temperature control, a cyclically controlled water pump for humidification, an I2C LCD for local feedback, and a built-in Web Server for remote monitoring and data visualization.


Features
---------

•Real-time Sensing: Uses a DHT11 sensor to read Temperature and Humidity.

•Analog Thresholds: Uses Potentiometers to set target thresholds for both temperature and humidity.

•Temperature Regulation: Controls a TEC (Peltier module) in Cooling or Heating mode to maintain the temperature setpoint, utilizing Hysteresis for stable control.

•Humidity Control: Implements a cyclical water pump routine (e.g., 2 seconds ON, 20 seconds OFF) that activates only when the humidity drops below the set threshold.

•Local Display: A 16x2 I2C LCD displays current sensor readings or the configured thresholds, toggled by a push button.

•Remote Web Server: Hosts a simple web page displaying current status, thresholds, and a Chart.js plot of historical sensor data, updated via AJAX.


Hardware Requirements
----------------------
•ESP32 Development Board - Any standard ESP32 model (e.g., NodeMCU-32S, ESP32 DevKitC)
•DHT11/DHT22 Sensor - DHT11 is used in the code, but DHT22 offers better accuracy.
•16x2 I2C LCD Display - Address is set to 0x3F (often 0x27—check your module)
•Potentiometers 210k 
•Push Button - For toggling the LCD display mode.
•H-Bridge Motor Driver - (e.g., L298N, TB6612FNG) to drive the TEC/Cooler and Water Pump.
•TEC Module1(Peltier Plate) for cooling and heating.
•Water Pump & Driver - Small submersible pump for humidification.

Wiring Diagram (Pinout)
-----------------------
The code uses the following GPIO pin assignments:


Function		GPIO Pin	Code Define		Notes

I2C SDA			21		(Wire library default)	Connects to LCD SDA pin.
I2C SCL			22		(Wire library default)	Connects to LCD SCL pin.
DHT Data		14		DHTPIN			Connect with a pull-up resistor.
Temp Threshold		32		TEMP_POT_PIN		Analog input (VP).
Humidity Threshold	33		HUMIDITY_POT_PIN	Analog input (VN).
Display Toggle		15		PUSH_BUTTON_PIN		Digital input
TEC/Cooler IN1		26		COOLER_IN1_PIN		TEC direction/Cooling.
TEC/Cooler IN2		27		COOLER_IN2_PIN		TEC direction/Heating.
Water Pump IN1		13		WATERPUMP_IN1_PIN	Pump ON (direction 1).
Water Pump IN2		12		WATERPUMP_IN2_PIN	Pump OFF 



Arduino IDE Setup
-----------------

1. Libraries
You must install the following libraries in your Arduino IDE via the Library Manager:

LiquidCrystal I2C: (by Frank de Brabander)

DHT sensor library (by Adafruit)

Adafruit Unified Sensor (dependency for DHT)

(ESP32 core is built-in)

2. Configuration
•WiFi Credentials: Update your network settings at the top of the code:

const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

•I2C LCD Address: If your LCD doesn't work, change the I2C address:

#define I2C_ADDR            0x3F // Change to 0x27 if needed

•Hysteresis: Adjust the temperature control dead-band for stability in controlCoolerAndWaterPump():

const float HYSTERESIS = 1.0; // 1.0 degree C dead band


3. Upload and Access
Select the correct Board (ESP32 Dev Module) and Port.

Upload the code.

Monitor the Serial output to find the ESP32's IP Address after it connects to WiFi.

Open a web browser on your phone or PC on the same network and navigate to the IP Address (e.g., http://192.168.1.123). You can also use the mDNS name: http://esp32.local.

@BuwanekaWickramasinghe














