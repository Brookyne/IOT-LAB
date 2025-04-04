#define LED_PIN 48
#define SDA_PIN GPIO_NUM_11
#define SCL_PIN GPIO_NUM_12

#include <WiFi.h>
#include <Arduino_MQTT_Client.h>
#include <ThingsBoard.h>
#include "DHT20.h"
#include "Wire.h"
#include <ArduinoOTA.h>
#include <scheduler.h>  
constexpr char WIFI_SSID[] = "Anonymous";
constexpr char WIFI_PASSWORD[] = "Nguyen2004";

// constexpr char WIFI_SSID[] = "ACLAB-IOT";
// constexpr char WIFI_PASSWORD[] = "12345678";

constexpr char TOKEN[] = "1h4pxb8834heq7f7er4f";

constexpr char THINGSBOARD_SERVER[] = "app.coreiot.io";
constexpr char DEVICE_PROFILE[] = "Temperature Sensor";
constexpr uint16_t THINGSBOARD_PORT = 1883U;

constexpr uint32_t MAX_MESSAGE_SIZE = 1024U;
constexpr uint32_t SERIAL_DEBUG_BAUD = 115200U;

constexpr char BLINKING_INTERVAL_ATTR[] = "blinkingInterval";
constexpr char LED_MODE_ATTR[] = "ledMode";
constexpr char LED_STATE_ATTR[] = "ledState";

volatile bool attributesChanged = false;
volatile int ledMode = 0;
volatile bool ledState = false;

constexpr uint16_t BLINKING_INTERVAL_MS_MIN = 10U;
constexpr uint16_t BLINKING_INTERVAL_MS_MAX = 60000U;
volatile uint16_t blinkingInterval = 1000U;

uint32_t previousStateChange;

constexpr int16_t telemetrySendInterval = 10000U;
uint32_t previousDataSend;

constexpr std::array<const char *, 2U> SHARED_ATTRIBUTES_LIST = {
  LED_STATE_ATTR,
  BLINKING_INTERVAL_ATTR
};

WiFiClient wifiClient;
Arduino_MQTT_Client mqttClient(wifiClient);
ThingsBoard tb(mqttClient, MAX_MESSAGE_SIZE);

DHT20 dht20;

RPC_Response setLedSwitchState(const RPC_Data &data) {
    Serial.println("Received Switch state");
    bool newState = data;
    Serial.print("Switch state change: ");
    Serial.println(newState);
    digitalWrite(LED_PIN, newState);
    attributesChanged = true;
    return RPC_Response("setLedSwitchValue", newState);
}

const std::array<RPC_Callback, 1U> callbacks = {
  RPC_Callback{ "setLedSwitchValue", setLedSwitchState }
};

void processSharedAttributes(const Shared_Attribute_Data &data) {
  for (auto it = data.begin(); it != data.end(); ++it) {
    if (strcmp(it->key().c_str(), BLINKING_INTERVAL_ATTR) == 0) {
      const uint16_t new_interval = it->value().as<uint16_t>();
      if (new_interval >= BLINKING_INTERVAL_MS_MIN && new_interval <= BLINKING_INTERVAL_MS_MAX) {
        blinkingInterval = new_interval;
        Serial.print("Blinking interval is set to: ");
        Serial.println(new_interval);
      }
    } else if (strcmp(it->key().c_str(), LED_STATE_ATTR) == 0) {
      ledState = it->value().as<bool>();
      digitalWrite(LED_PIN, ledState);
      Serial.print("LED state is set to: ");
      Serial.println(ledState);
    }
  }
  attributesChanged = true;
}

const Shared_Attribute_Callback attributes_callback(&processSharedAttributes, SHARED_ATTRIBUTES_LIST.cbegin(), SHARED_ATTRIBUTES_LIST.cend());
const Attribute_Request_Callback attribute_shared_request_callback(&processSharedAttributes, SHARED_ATTRIBUTES_LIST.cbegin(), SHARED_ATTRIBUTES_LIST.cend());
void task1(){
  Serial.println("Hello Task 1");
}

void task2(){
  Serial.println("Hello Task 2");
}
void InitWiFi() {
  Serial.println("Connecting to AP ...");
  // Attempting to establish a connection to the given WiFi network
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    // Delay 500ms until a connection has been successfully established
    delay(500);
    Serial.print(".");
  }
  Serial.println("Connected to AP");
}

const bool reconnect() {
  // Check to ensure we aren't connected yet
  const wl_status_t status = WiFi.status();
  if (status == WL_CONNECTED) {
    return true;
  }
  // If we aren't establish a new connection to the given WiFi network
  InitWiFi();
  return true;
}
void TB_LoopTask() {
  tb.loop();  // Cần gọi thường xuyên để xử lý RPC
}
void wifi_reconnect(void *pvParameters) {
  while(1) {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi not connected, reconnecting...");
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

      while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
      }
      Serial.println("WiFi reconnected");
    }
    Serial.println("WiFi reconnect task runed...");
    vTaskDelay(10000);
  }
}

void tb_reconnect(void *pvParameters) {
  while(1) {
    if (!tb.connected()) {
      Serial.print("Connecting to: ");
      Serial.print(THINGSBOARD_SERVER);
      Serial.print(" with token ");
      Serial.println(TOKEN);
      if (!tb.connect(THINGSBOARD_SERVER, TOKEN, THINGSBOARD_PORT)) {
        Serial.println("Failed to connect");
        return;
      }
  
      tb.sendAttributeData("macAddress", WiFi.macAddress().c_str());
  
      Serial.println("Subscribing for RPC...");
      if (!tb.RPC_Subscribe(callbacks.cbegin(), callbacks.cend())) {
        Serial.println("Failed to subscribe for RPC");
        return;
      }
  
      if (!tb.Shared_Attributes_Subscribe(attributes_callback)) {
        Serial.println("Failed to subscribe for shared attribute updates");
        return;
      }
  
      Serial.println("Subscribe done");
  
      if (!tb.Shared_Attributes_Request(attribute_shared_request_callback)) {
        Serial.println("Failed to request for shared attributes");
        return;
      }
    }
    Serial.println("Thingsboard reconnect task runed...");
    vTaskDelay(10000);
  }
}
void Button_LED(void * pvParameters) {
  while(1) {
    if (attributesChanged) {
      attributesChanged = false;
      tb.sendAttributeData(LED_STATE_ATTR, digitalRead(LED_PIN));
    }
    // Serial.println("Button_LED task runed...");
    vTaskDelay(10);
    tb.loop();
  }
}
void Send_TelemetryData(void * pvParameters) {
  while(1) {
    dht20.read();
    
    float temperature = dht20.getTemperature();
    float humidity = dht20.getHumidity();

    if (isnan(temperature) || isnan(humidity)) {
      Serial.println("Failed to read from DHT20 sensor!");
    } else {
      Serial.print("Temperature: ");
      Serial.print(temperature);
      Serial.print(" °C, Humidity: ");
      Serial.print(humidity);
      Serial.println(" %");

      tb.sendTelemetryData("temperature", temperature);
      tb.sendTelemetryData("humidity", humidity);
    }

    tb.sendAttributeData("rssi", WiFi.RSSI());
    tb.sendAttributeData("channel", WiFi.channel());
    tb.sendAttributeData("bssid", WiFi.BSSIDstr().c_str());
    tb.sendAttributeData("localIp", WiFi.localIP().toString().c_str());
    tb.sendAttributeData("ssid", WiFi.SSID().c_str());
  
    vTaskDelay(5000);
    tb.loop();
  }
}
void setup() {
  Serial.begin(SERIAL_DEBUG_BAUD);
  pinMode(LED_PIN, OUTPUT);
  delay(1000);
  InitWiFi();

  Wire.begin(SDA_PIN, SCL_PIN);
  dht20.begin();
  
  Serial.println("hello world");
  // SCH_Init();
  // SCH_Add_Task(task1, 200, 2000);
  // SCH_Add_Task(task2, 100, 5000);

  xTaskCreate(wifi_reconnect, "wifi_reconnect", 4096, NULL, 1, NULL);
  xTaskCreate(tb_reconnect, "tb_reconnect", 4096, NULL, 1, NULL);
  xTaskCreate(Button_LED, "Button_LED", 2048, NULL, 1, NULL);
  xTaskCreate(Send_TelemetryData, "Send_TelemetryData", 4096, NULL, 2, NULL);
  SCH_Add_Task(TB_LoopTask, 200, 2000);
}

void loop() {
  // SCH_Dispatch_Tasks();
}