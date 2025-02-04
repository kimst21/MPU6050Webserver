#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Arduino_JSON.h>
#include "SPIFFS.h"

// WiFi 접속 정보 설정 (사용자가 자신의 네트워크 정보를 입력해야 함)
const char* ssid = "Your_SSID";      // WiFi SSID
const char* password = "Your_Password";  // WiFi 비밀번호

// 웹 서버 및 이벤트 소스 생성
AsyncWebServer server(80);  // 포트 80에서 웹 서버 실행
AsyncEventSource events("/events");  // Server-Sent Events(SSE) 엔드포인트 설정

// 센서 데이터를 저장할 JSON 객체 생성
JSONVar readings;

// millis() 기반으로 센서 데이터 전송을 위한 시간 관리 변수
unsigned long previous_time = 0;  
unsigned long previous_time_temp = 0;
unsigned long previous_time_acceleration = 0;

// 각 센서 데이터 전송 간격 (밀리초)
unsigned long gyro_delay  = 10;       // 자이로스코프 데이터 전송 간격 (10ms)
unsigned long temperature_delay  = 1000;  // 온도 데이터 전송 간격 (1초)
unsigned long accelerometer_delay  = 200;  // 가속도 데이터 전송 간격 (200ms)

// MPU6050 센서 객체 생성
Adafruit_MPU6050 mpu;
sensors_event_t a, g, temp; // 센서 이벤트 객체 (가속도, 자이로스코프, 온도)

// 센서 데이터 저장 변수
float rotationX, rotationY, rotationZ; // 회전 각도 저장
float accelerationX, accelerationY, accelerationZ; // 가속도 저장
float temperature; // 온도 저장

// 자이로스코프 오차 보정값 (노이즈 제거 목적)
float rotationX_error = 0.05;
float rotationY_error = 0.02;
float rotationZ_error = 0.01;

// 자이로스코프 데이터 수집 및 JSON 변환 함수
String getGyroscopeReadings(){
  mpu.getEvent(&a, &g, &temp); // MPU6050 센서에서 데이터를 읽음

  // X축 회전 값이 보정 오차 이상이면 누적
  float rotationX_temporary = g.gyro.x;
  if(abs(rotationX_temporary) > rotationX_error) {
    rotationX += rotationX_temporary * 0.01; // 적분 계산
  }

  // Y축 회전 값이 보정 오차 이상이면 누적
  float rotationY_temporary = g.gyro.y;
  if(abs(rotationY_temporary) > rotationY_error) {
    rotationY += rotationY_temporary * 0.01;
  }

  // Z축 회전 값이 보정 오차 이상이면 누적
  float rotationZ_temporary = g.gyro.z;
  if(abs(rotationZ_temporary) > rotationZ_error) {
    rotationZ += rotationZ_temporary * 0.01;
  }

  // JSON 객체에 값 저장
  readings["rotationX"] = String(rotationX);
  readings["rotationY"] = String(rotationY);
  readings["rotationZ"] = String(rotationZ);

  return JSON.stringify(readings); // JSON 형식의 문자열 반환
}

// 가속도 데이터 수집 및 JSON 변환 함수
String getAccelerationReadings() {
  mpu.getEvent(&a, &g, &temp); // MPU6050 센서에서 데이터를 읽음

  accelerationX = a.acceleration.x;
  accelerationY = a.acceleration.y;
  accelerationZ = a.acceleration.z;

  // JSON 객체에 값 저장
  readings["accelerationX"] = String(accelerationX);
  readings["accelerationY"] = String(accelerationY);
  readings["accelerationZ"] = String(accelerationZ);

  return JSON.stringify(readings); // JSON 형식의 문자열 반환
}

// 온도 데이터 수집 함수
String getTemperatureReadings(){
  mpu.getEvent(&a, &g, &temp); // MPU6050 센서에서 데이터를 읽음
  temperature = temp.temperature;
  return String(temperature); // 온도를 문자열로 반환
}

void setup() {
  Serial.begin(115200); // 시리얼 모니터 시작 (디버깅용)

  // WiFi 연결 설정
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) { // WiFi가 연결될 때까지 대기
    Serial.print(".");
    delay(1000);
  }
  Serial.println("\nConnected to WiFi");
  Serial.println(WiFi.localIP()); // 연결된 IP 주소 출력

  // SPIFFS 파일 시스템 마운트 (웹 페이지 제공을 위함)
  if (!SPIFFS.begin()) {
    Serial.println("An error has occurred while mounting SPIFFS");
  } else {
    Serial.println("SPIFFS mounted successfully");
  }

  // MPU6050 센서 초기화
  if (!mpu.begin()) {
    Serial.println("MPU6050 is not properly connected. Check circuit!");
    while (1) {
      delay(10); // 센서가 연결되지 않으면 무한 대기
    }
  }
  Serial.println("MPU6050 Found");

  // 웹 페이지 요청 처리 ("/" 요청 시 index.html 제공)
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/index.html", "text/html");
  });

  // 정적 파일 제공 (JS, CSS 등)
  server.serveStatic("/", SPIFFS, "/");

  // 회전 데이터 초기화 API
  server.on("/reset", HTTP_GET, [](AsyncWebServerRequest *request){
    rotationX = 0;
    rotationY = 0;
    rotationZ = 0;
    request->send(200, "text/plain", "OK");
  });

  // 개별 축 초기화 API
  server.on("/resetX", HTTP_GET, [](AsyncWebServerRequest *request){
    rotationX = 0;
    request->send(200, "text/plain", "OK");
  });

  server.on("/resetY", HTTP_GET, [](AsyncWebServerRequest *request){
    rotationY = 0;
    request->send(200, "text/plain", "OK");
  });

  server.on("/resetZ", HTTP_GET, [](AsyncWebServerRequest *request){
    rotationZ = 0;
    request->send(200, "text/plain", "OK");
  });

  // Server-Sent Events (SSE) 설정
  events.onConnect([](AsyncEventSourceClient *client){
    if(client->lastId()){
      Serial.printf("Client reconnected! Last message ID that it got is: %u\n", client->lastId());
    }
    client->send("hello!", NULL, millis(), 10000); // 클라이언트 연결 확인
  });

  server.addHandler(&events); // SSE 이벤트 추가
  server.begin(); // 웹 서버 시작
}

void loop() {
  // 10ms 간격으로 자이로 데이터 전송
  if ((millis() - previous_time) > gyro_delay) {
    events.send(getGyroscopeReadings().c_str(), "gyro_readings", millis());
    previous_time = millis();
  }

  // 200ms 간격으로 가속도 데이터 전송
  if ((millis() - previous_time_acceleration) > accelerometer_delay) {
    events.send(getAccelerationReadings().c_str(), "accelerometer_readings", millis());
    previous_time_acceleration = millis();
  }

  // 1초 간격으로 온도 데이터 전송
  if ((millis() - previous_time_temp) > temperature_delay) {
    events.send(getTemperatureReadings().c_str(), "temperature_reading", millis());
    previous_time_temp = millis();
  }
}
