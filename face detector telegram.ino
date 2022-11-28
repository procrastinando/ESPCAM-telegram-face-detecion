/*
ESP32-CAM Face detection (offline)
Author : ChungYi Fu (Kaohsiung, Taiwan)  2021-6-29 21:30
https://www.facebook.com/francefu
*/

const char* ssid     = "wifi";   //your network SSID
const char* password = "password";   //your network password

String myToken = "xxxx:xxxxxxxx";   // Create your bot and get the token -> https://telegram.me/fatherbot
String myChatId = "649792299";        // Get chat_id -> https://telegram.me/userinfobot
boolean sendHelp = false;

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "soc/soc.h"             //用於電源不穩不重開機 
#include "soc/rtc_cntl_reg.h"    //用於電源不穩不重開機 
#include "esp_camera.h"          //視訊函式
#include "fd_forward.h"          //人臉偵測函式
#include <ArduinoJson.h>         //解析json格式函式

//ESP32-CAM 安信可模組腳位設定
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

//https://github.com/espressif/esp-dl/blob/master/face_detection/README.md
box_array_t *net_boxes = NULL;

WiFiClientSecure client_tcp;
long message_id_last = 0;

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);  //關閉電源不穩就重開機的設定
    
  Serial.begin(115200);
  Serial.setDebugOutput(true);  //開啟診斷輸出
  Serial.println();

  //視訊組態設定  https://github.com/espressif/esp32-camera/blob/master/driver/include/esp_camera.h
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  
  //
  // WARNING!!! Make sure that you have either selected ESP32 Wrover Module,
  //            or another board which has PSRAM enabled
  //  
  //init with high specs to pre-allocate larger buffers
  if(psramFound()){
    config.frame_size = FRAMESIZE_SXGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_XGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  //視訊初始化
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    ESP.restart();
  }

  //可自訂視訊框架預設大小(解析度大小)
  sensor_t * s = esp_camera_sensor_get();
  // initial sensors are flipped vertically and colors are a bit saturated
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1); // flip it back
    s->set_brightness(s, 1); // up the brightness just a bit
    s->set_saturation(s, -2); // lower the saturation
  }
  // drop down frame size for higher initial frame rate
  s->set_framesize(s, FRAMESIZE_QVGA);    //解析度 UXGA(1600x1200), SXGA(1280x1024), XGA(1024x768), SVGA(800x600), VGA(640x480), CIF(400x296), QVGA(320x240), HQVGA(240x176), QQVGA(160x120), QXGA(2048x1564 for OV3660)

  //s->set_vflip(s, 1);  //垂直翻轉
  //s->set_hmirror(s, 1);  //水平鏡像         

  //閃光燈(GPIO4)
  ledcAttachPin(4, 4);  
  ledcSetup(4, 5000, 8);
  
  WiFi.mode(WIFI_AP_STA);  //其他模式 WiFi.mode(WIFI_AP); WiFi.mode(WIFI_STA);

  //指定Client端靜態IP
  //WiFi.config(IPAddress(192, 168, 201, 100), IPAddress(192, 168, 201, 2), IPAddress(255, 255, 255, 0));

  for (int i=0;i<2;i++) {
    WiFi.begin(ssid, password);    //執行網路連線
  
    delay(1000);
    Serial.println("");
    Serial.print("Connecting to ");
    Serial.println(ssid);
    
    long int StartTime=millis();
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        if ((StartTime+5000) < millis()) break;    //等待10秒連線
    } 
  
    if (WiFi.status() == WL_CONNECTED) {    //若連線成功
      Serial.println("");
      Serial.println("STAIP address: ");
      Serial.println(WiFi.localIP());
      Serial.println("");
  
      for (int i=0;i<5;i++) {   //若連上WIFI設定閃光燈快速閃爍
        ledcWrite(4,10);
        delay(200);
        ledcWrite(4,0);
        delay(200);    
      }
      break;
    }
  } 

  if (WiFi.status() != WL_CONNECTED) {    //若連線失敗
    for (int i=0;i<2;i++) {    //若連不上WIFI設定閃光燈慢速閃爍
      ledcWrite(4,10);
      delay(1000);
      ledcWrite(4,0);
      delay(1000);    
    }
    ESP.restart();
  } 

  //設定閃光燈為低電位
  pinMode(4, OUTPUT);
  digitalWrite(4, LOW); 

  //sendMessage2Telegram(myToken, myChatId, "Person detected!");
  //sendCapturedImage2Telegram(myToken, myChatId);
  getTelegramMessage(myToken);
}

void loop() {

}

void executeCommand(String text) {
  if (!text||text=="") return;
    
  // 自訂指令
  if (text=="help"||text=="/help"||text=="/start") {
    String command = "/help Command list\n/capture Get still\n/on Turn on the flash\n/off Turn off the flash\n/restart Restart the board";

    //鍵盤按鈕
    //One row
    //String keyboard = "{\"keyboard\":[[{\"text\":\"/on\"},{\"text\":\"/off\"},{\"text\":\"/capture\"},{\"text\":\"/restart\"}]],\"one_time_keyboard\":false}";
    //Two rows
    String keyboard = "{\"keyboard\":[[{\"text\":\"/on\"},{\"text\":\"/off\"}], [{\"text\":\"/capture\"},{\"text\":\"/restart\"}]],\"one_time_keyboard\":false}";
    
    sendMessage2Telegram(myToken, myChatId, command, keyboard);  //傳送功能清單
  } else if (text=="/capture") {  //取得視訊截圖
    sendCapturedImage2Telegram(myToken, myChatId);
  } else if (text=="/on") {  //開啟閃光燈
    ledcAttachPin(4, 3);
    ledcSetup(3, 5000, 8);
    ledcWrite(3,10);
    sendMessage2Telegram(myToken, myChatId, "Turn on the flash", "");
  } else if (text=="/off") {  //關閉閃光燈
    ledcAttachPin(4, 3);
    ledcSetup(3, 5000, 8);
    ledcWrite(3,0);
    sendMessage2Telegram(myToken, myChatId, "Turn off the flash", "");
  } else if (text=="/restart") {  //重啟電源
    sendMessage2Telegram(myToken, myChatId, "Restart the board", "");
    ESP.restart();
  } else if (text=="null") {   //不可刪除此條件 Server sends the response unexpectedly. Don't delete the code.
    client_tcp.stop();
    getTelegramMessage(myToken);
  } else
    sendMessage2Telegram(myToken, myChatId, "Command is not defined", "");
}

void getTelegramMessage(String token) {
  const char* myDomain = "api.telegram.org";
  String getAll="", getBody = ""; 
  JsonObject obj;
  DynamicJsonDocument doc(1024);
  String result;
  long update_id;
  String message;
  long message_id;
  String text;  

  client_tcp.setInsecure();   //run version 1.0.5 or above
  if (message_id_last == 0) Serial.println("Connect to " + String(myDomain));
  if (client_tcp.connect(myDomain, 443)) {
    if (message_id_last == 0) Serial.println("Connection successful");

    while (client_tcp.connected()) { 

      dl_matrix3du_t *image_matrix = NULL;
      camera_fb_t * fb = NULL;
      fb = esp_camera_fb_get();
      if (!fb) {
          Serial.println("Camera capture failed");
      } else {
          image_matrix = dl_matrix3du_alloc(1, fb->width, fb->height, 3);  //分配內部記憶體
          if (!image_matrix) {
              Serial.println("dl_matrix3du_alloc failed");
          } else {
              //臉部偵測參數設定  https://github.com/espressif/esp-face/blob/master/face_detection/README.md
              static mtmn_config_t mtmn_config = {0};
              mtmn_config.type = NORMAL;
              mtmn_config.min_face = 70;
              mtmn_config.pyramid = 0.9;
              mtmn_config.pyramid_times = 4;
              mtmn_config.p_threshold.score = 0.3;
              mtmn_config.p_threshold.nms = 0.7;
              mtmn_config.p_threshold.candidate_number = 20;
              mtmn_config.r_threshold.score = 0.5;
              mtmn_config.r_threshold.nms = 0.5;
              mtmn_config.r_threshold.candidate_number = 10;
              mtmn_config.o_threshold.score = 0.5;
              mtmn_config.o_threshold.nms = 0.5;
              mtmn_config.o_threshold.candidate_number = 1;
              
              fmt2rgb888(fb->buf, fb->len, fb->format, image_matrix->item);  //影像格式轉換RGB格式
              net_boxes = face_detect(image_matrix, &mtmn_config);  //執行人臉偵測取得臉框數據
              if (net_boxes){
                Serial.println("faces = " + String(net_boxes->len));  //偵測到的人臉數
                sendCapturedImage2Telegram(myToken, myChatId);
                Serial.println();
                for (int i = 0; i < net_boxes->len; i++){  //列舉人臉位置與大小
                    Serial.println("index = " + String(i));
                    int x = (int)net_boxes->box[i].box_p[0];
                    Serial.println("x = " + String(x));
                    int y = (int)net_boxes->box[i].box_p[1];
                    Serial.println("y = " + String(y));
                    int w = (int)net_boxes->box[i].box_p[2] - x + 1;
                    Serial.println("width = " + String(w));
                    int h = (int)net_boxes->box[i].box_p[3] - y + 1;
                    Serial.println("height = " + String(h));
                    Serial.println();
                } 
                dl_lib_free(net_boxes->score);
                dl_lib_free(net_boxes->box);
                dl_lib_free(net_boxes->landmark);
                dl_lib_free(net_boxes);                                
                net_boxes = NULL;
              }
              else {
                Serial.println("No Face");    //未偵測到的人臉
                Serial.println();
              }
              dl_matrix3du_free(image_matrix);
          }
          esp_camera_fb_return(fb);
      }

      /*
        //新增PIR感測器
        //If you want to add PIR sensor in your project, please put the code here.
        pinMode(pinPIR, INPUT_PULLUP);
        if (digitalRead(pinPIR)==1) {
          sendCapturedImage2Telegram();
        }        
      */
      
      getAll = "";
      getBody = "";

      String request = "limit=1&offset=-1&allowed_updates=message";
      client_tcp.println("POST /bot"+token+"/getUpdates HTTP/1.1");
      client_tcp.println("Host: " + String(myDomain));
      client_tcp.println("Content-Length: " + String(request.length()));
      client_tcp.println("Content-Type: application/x-www-form-urlencoded");
      client_tcp.println("Connection: keep-alive");
      client_tcp.println();
      client_tcp.print(request);
      
      int waitTime = 5000;   // timeout 5 seconds
      long startTime = millis();
      boolean state = false;
      
      while ((startTime + waitTime) > millis()) {
        //Serial.print(".");
        delay(100);      
        while (client_tcp.available()) {
            char c = client_tcp.read();
            if (state==true) getBody += String(c);
            if (c == '\n') {
              if (getAll.length()==0) state=true; 
              getAll = "";
            } else if (c != '\r')
              getAll += String(c);
            startTime = millis();
         }
         if (getBody.length()>0) break;
      }

      //取得最新訊息json格式取值
      deserializeJson(doc, getBody);
      obj = doc.as<JsonObject>();
      //result = obj["result"].as<String>();
      //update_id =  obj["result"][0]["update_id"].as<String>().toInt();
      //message = obj["result"][0]["message"].as<String>();
      message_id = obj["result"][0]["message"]["message_id"].as<String>().toInt();
      text = obj["result"][0]["message"]["text"].as<String>();

      if (message_id!=message_id_last&&message_id) {
        int id_last = message_id_last;
        message_id_last = message_id;
        if (id_last==0) {
          message_id = 0;
          if (sendHelp == true)   // Send the command list to Telegram Bot when the board boots.
            text = "/help";
          else
            text = "";
        } else {
          Serial.println(getBody);
          Serial.println();
        }
        
        if (text!="") {
          Serial.println("["+String(message_id)+"] "+text);
          executeCommand(text);
        }
      }
      delay(500);
    }
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Connected failed.");
    WiFi.begin(ssid, password);  
    long int StartTime=millis();
    while (WiFi.status() != WL_CONNECTED) {
      delay(1000);
      if ((StartTime+10000) < millis())  {
        StartTime=millis();
        WiFi.begin(ssid, password);
      }
    }
    Serial.println("Reconnection is successful.");
  }

  //伺服器約3分鐘斷線，重新取得連線。
  getTelegramMessage(myToken);   // Client's connection time out after about 3 minutes.
}

void sendCapturedImage2Telegram(String token, String chat_id) {
  const char* myDomain = "api.telegram.org";
  String getAll="", getBody = "";

  camera_fb_t * fb = NULL;
  fb = esp_camera_fb_get();  
  if(!fb) {
    Serial.println("Camera capture failed");
    delay(1000);
    ESP.restart();
  }  
  
  String head = "--Taiwan\r\nContent-Disposition: form-data; name=\"chat_id\"; \r\n\r\n" + chat_id + "\r\n--Taiwan\r\nContent-Disposition: form-data; name=\"photo\"; filename=\"esp32-cam.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n";
  String tail = "\r\n--Taiwan--\r\n";

  uint16_t imageLen = fb->len;
  uint16_t extraLen = head.length() + tail.length();
  uint16_t totalLen = imageLen + extraLen;

  client_tcp.println("POST /bot"+token+"/sendPhoto HTTP/1.1");
  client_tcp.println("Host: " + String(myDomain));
  client_tcp.println("Content-Length: " + String(totalLen));
  client_tcp.println("Content-Type: multipart/form-data; boundary=Taiwan");
  client_tcp.println("Connection: keep-alive");
  client_tcp.println();
  client_tcp.print(head);

  uint8_t *fbBuf = fb->buf;
  size_t fbLen = fb->len;
  for (size_t n=0;n<fbLen;n=n+1024) {
    if (n+1024<fbLen) {
      client_tcp.write(fbBuf, 1024);
      fbBuf += 1024;
    } else if (fbLen%1024>0) {
      size_t remainder = fbLen%1024;
      client_tcp.write(fbBuf, remainder);
    }
  }  
  
  client_tcp.print(tail);
  
  esp_camera_fb_return(fb);
  
  int waitTime = 10000;   // timeout 10 seconds
  long startTime = millis();
  boolean state = false;
  
  while ((startTime + waitTime) > millis()) {
    Serial.print(".");
    delay(100);      
    while (client_tcp.available()) {
        char c = client_tcp.read();
        if (state==true) getBody += String(c);      
        if (c == '\n') {
          if (getAll.length()==0) state=true; 
          getAll = "";
        } else if (c != '\r')
          getAll += String(c);
        startTime = millis();
     }
     if (getBody.length()>0) break;
  }
  Serial.println(getBody);
  Serial.println();
}

void sendMessage2Telegram(String token, String chat_id, String text, String keyboard) {
  const char* myDomain = "api.telegram.org";
  String getAll="", getBody = "";
  
  String request = "parse_mode=HTML&chat_id="+chat_id+"&text="+text;
  if (keyboard!="") request += "&reply_markup="+keyboard;
  
  client_tcp.println("POST /bot"+token+"/sendMessage HTTP/1.1");
  client_tcp.println("Host: " + String(myDomain));
  client_tcp.println("Content-Length: " + String(request.length()));
  client_tcp.println("Content-Type: application/x-www-form-urlencoded");
  client_tcp.println("Connection: keep-alive");
  client_tcp.println();
  client_tcp.print(request);
  
  int waitTime = 5000;   // timeout 5 seconds
  long startTime = millis();
  boolean state = false;
  
  while ((startTime + waitTime) > millis()) {
    Serial.print(".");
    delay(100);      
    while (client_tcp.available()) {
        char c = client_tcp.read();
        if (state==true) getBody += String(c);      
        if (c == '\n') {
          if (getAll.length()==0) state=true; 
          getAll = "";
        } else if (c != '\r')
          getAll += String(c);
        startTime = millis();
     }
     if (getBody.length()>0) break;
  }
  Serial.println(getBody);
  Serial.println();
}
