#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>  // Thư viện để làm việc với thời gian

// WiFi credentials
const char* ssid = "nam";
const char* password = "12345678";

// MQTT server credentials
#define MQTT_SERVER " 192.168.180.172"
#define MQTT_PORT 1883


// MQTT topics
#define MQTT_CONTROL_TOPIC "ESP32/Control"       // Topic để điều khiển bật/tắt hệ thống
#define MQTT_NOTIFICATION_TOPIC "ESP32/Notification" // Topic gửi thông báo phát hiện chuyển động
#define MQTT_DURATION_TOPIC "ESP32/Duration"     // Topic để cài đặt thời gian còi kêu

// Pin definitions
#define SENSORPIN 26  // Chân cảm biến chuyển động
#define RELAYPIN 18   // Chân relay điều khiển còi

WiFiClient espClient;
PubSubClient client(espClient);  // MQTT client

// Biến để lưu trạng thái hệ thống và thời gian còi kêu
bool systemEnabled = true;  // Mặc định tắt hệ thống
int buzzerDuration = 5;      // Thời gian còi kêu mặc định là 5 giây
String deviceId = "TB01";  // ID của thiết bị, có thể thay đổi tùy theo yêu cầu
const long gmtOffset_sec = 7 * 3600;  // GMT+7
const int daylightOffset_sec = 0;    // Không có giờ mùa hè ở Việt Nam
const char* ntpServer = "pool.ntp.org"; // Máy chủ NTP mặc định

// Function to connect to WiFi
void setup_Wifi() {
    Serial.print("Đang kết nối tới ");
    Serial.println(ssid);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.print(".");
    }
    Serial.println("\nWiFi đã kết nối");
    Serial.print("Địa chỉ IP: ");
    Serial.println(WiFi.localIP());
}

// Function to connect to the MQTT broker
void connect_to_broker() {
    while (!client.connected()) {
        Serial.print("Đang kết nối tới MQTT broker...");
        String clientId = "ESP32Client";
        if (client.connect(clientId.c_str())) {
            Serial.println("Đã kết nối tới MQTT broker");
            client.subscribe(MQTT_CONTROL_TOPIC);  // Đăng ký topic để nhận lệnh bật/tắt
            client.subscribe(MQTT_DURATION_TOPIC); // Đăng ký topic để nhận thời gian còi kêu
        } else {
            Serial.print("Kết nối thất bại, rc=");
            Serial.print(client.state());
            Serial.println(" thử lại sau 2 giây");
            delay(2000);
        }
    }
}

// Function to publish a notification with details, timestamp, and deviceId
void send_motion_notification() {
  // Tạo chuỗi JSON
  DynamicJsonDocument doc(1024);
  doc["details"] = "Motion detected!";
  doc["timestamp"] = get_timestamp();
  doc["deviceId"] = deviceId;

  // Serialize JSON to string
  String jsonString;
  serializeJson(doc, jsonString);

  // Gửi dữ liệu qua MQTT
  client.publish(MQTT_NOTIFICATION_TOPIC, jsonString.c_str());
  Serial.println("Đã gửi thông báo: ");
  Serial.println(jsonString);
}

// Function to get current timestamp as a formatted string (yyyy-MM-dd HH:mm:ss)
String get_timestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Lỗi khi lấy thời gian từ NTP");
    return "";
  }

  // Định dạng thời gian thành chuỗi "yyyy-MM-dd HH:mm:ss"
  char timestamp[20];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(timestamp);
}

// Callback function to handle incoming MQTT messages
void callback(char* topic, byte* payload, unsigned int length) {
    Serial.print("Nhận tin nhắn từ broker: ");
    Serial.print("Topic: ");
    Serial.println(topic);
    Serial.print("Message: ");
    Serial.write(payload, length);
    Serial.println();

    String message = "";
    for (int i = 0; i < length; i++) {
      message += (char)payload[i];
    }

    // Xử lý thông tin điều khiển bật/tắt hệ thống
    if (String(topic) == MQTT_CONTROL_TOPIC) {
        if (message == "ON") {
            systemEnabled = true;
            Serial.println("Hệ thống đã được bật.");
        } else if (message == "OFF") {
            systemEnabled = false;
            Serial.println("Hệ thống đã được tắt.");
            digitalWrite(RELAYPIN, LOW);  // Đảm bảo còi tắt ngay khi tắt hệ thống
        }
    }
    
    // Xử lý cài đặt thời gian còi kêu từ MQTT
    else if (String(topic) == MQTT_DURATION_TOPIC) {
        String message = "";
        for (int i = 0; i < length; i++) {
          message += (char)payload[i];
        }

        int duration = message.toInt(); // Chuyển chuỗi thành số nguyên
        
        if (duration >= 1 && duration <= 5) {
            buzzerDuration = duration;   // Cập nhật thời gian còi kêu
            Serial.print("Thời gian còi kêu được cài đặt: ");
            Serial.print(buzzerDuration);
            Serial.println(" giây.");
        } else {
            Serial.println("Thời gian không hợp lệ! Vui lòng nhập từ 1 đến 5.");
        }
    }
}

// Setup function
void setup() {
    Serial.begin(115200);
    pinMode(SENSORPIN, INPUT);    // Cảm biến chuyển động
    pinMode(RELAYPIN, OUTPUT);    // Relay điều khiển còi
    digitalWrite(RELAYPIN, LOW);  // Ban đầu tắt relay

    // Kết nối WiFi
    setup_Wifi();
    
    // Thiết lập thời gian NTP
   // Cấu hình múi giờ và kết nối NTP
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

    // Cài đặt MQTT server và callback function
    client.setServer(MQTT_SERVER, MQTT_PORT);
    client.setCallback(callback);
}

// Loop function
void loop() {
    // Kết nối lại MQTT nếu cần
    if (!client.connected()) {
        connect_to_broker();
    }
    client.loop();

    // Kiểm tra trạng thái cảm biến nếu hệ thống được bật
    if (systemEnabled) {
        int sensorState = digitalRead(SENSORPIN);
        
        // Nếu phát hiện chuyển động thì còi sẽ kêu
        if (sensorState == HIGH) { // Chế độ H (HIGH)
            Serial.println("Phát hiện chuyển động! Kích hoạt còi...");

            // Gửi thông báo với chi tiết, thời gian, và deviceId
            send_motion_notification();

            digitalWrite(RELAYPIN, HIGH);                 // Bật còi
            delay(buzzerDuration * 1000);                 // Còi kêu theo thời gian đã cài đặt
            digitalWrite(RELAYPIN, LOW);                  // Tắt còi sau thời gian đã cài đặt
            Serial.println("Còi đã tắt sau thời gian cài đặt.");
        }
    } else {
        // Tắt relay khi hệ thống không được kích hoạt
        digitalWrite(RELAYPIN, LOW);
    }

    delay(1000);  // Đợi 1 giây trước khi kiểm tra lại
}
