/*
    此文件需安装Arduino esp8266开发环境支持，环境搭建参见：http://www.bigiot.net/talk/237.html
    本程序可以用来控制四路继电器
    ESP8266烧入此程序直接，使用高低电频控制光耦继电器来控制电灯
    我的继电器默认高电频关闭，所以在初始化时都初始化为高电频，play关闭开启，stop关闭关闭，输入1-4打开或关闭对应的引脚
    代码基于https://github.com/bigiot/bigiotArduino/blob/master/examples/ESP8266/kaiguan/kaiguan.ino
    上的代码进行调整，修复了部分bug，解决了断线重连问题，此代码可以直接烧入到nodemcu模块，分享代码希望对大家有帮助
*/
#include <FS.h>                   //this needs to be first, or it all crashes and burns...

#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino

//needed for library
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager

#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
#include <aJSON.h>
//define your default values here, if there are different values in config.json, they are overwritten.
char device_id[10]="";
char api_key[20] = "";
String DEVICEID=""; // 你的设备编号   ==
String  APIKEY = ""; // 设备密码==
//flag for saving data
bool shouldSaveConfig = false;

unsigned long lastCheckInTime = 0; //记录上次报到时间
const unsigned long postingInterval = 40000; // 每隔40秒向服务器报到一次

const char* host = "www.bigiot.net";
const int httpPort = 8181;
bool isOffline=false;//是否掉线，用来记录是否掉线，掉线之后重新连接需要注销账户再登录 {"M":"checkout","ID":"xx1","K":"xx2"}\n
int pins[4] = {D5,D6,D7,D8};
int state[4] = {HIGH,HIGH,HIGH,HIGH};
int arr_len = sizeof(pins)/sizeof(pins[0]);

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}


void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  delay(1000);
  Serial.println();
  
  //clean FS, for testing
  //SPIFFS.format();

  //read configuration from FS json
  Serial.println("mounting FS...");

  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config1.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config1.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");
          
          strcpy(device_id, json["device_id"]);
          strcpy(api_key, json["api_key"]);

        } else {
          Serial.println("failed to load json config");
        }
        configFile.close();
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  //end read



  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  WiFiManagerParameter custom_device_id("server", "device id", device_id, 10);
  WiFiManagerParameter custom_api_key("port", "api key", api_key, 20);

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;

  //exit after config instead of connecting
  //wifiManager.setBreakAfterConfig(true);
  
  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //set static ip
  //wifiManager.setSTAStaticIPConfig(IPAddress(10,0,1,99), IPAddress(10,0,1,1), IPAddress(255,255,255,0));
  
  //add all your parameters here
  wifiManager.addParameter(&custom_device_id);
  wifiManager.addParameter(&custom_api_key);

  //reset settings - for testing
  //wifiManager.resetSettings();

  //set minimu quality of signal so it ignores AP's under that quality
  //defaults to 8%
  //wifiManager.setMinimumSignalQuality();
  
  //sets timeout until configuration portal gets turned off
  //useful to make it all retry or go to sleep
  //in seconds
  wifiManager.setTimeout(60);

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect("ESP8266AutoConnect", "12345678")) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }

  //if you get here you have connected to the WiFi
  Serial.println("connected...yeey :)");

  //read updated parameters
  strcpy(device_id, custom_device_id.getValue());
  strcpy(api_key, custom_api_key.getValue());

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["device_id"] = device_id;
    json["api_key"] = api_key;

    File configFile = SPIFFS.open("/config.json1", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }

  Serial.println("local ip");
  Serial.println(WiFi.localIP());
  for(int i=0;i<10;i++){
    DEVICEID += device_id[i];
    }
    for(int i=0;i<20;i++){
    APIKEY += api_key[i];
    }
    DEVICEID.trim();
    APIKEY.trim();
    Serial.println("DEVICEID:"+DEVICEID+",APIKEY:"+APIKEY);
  //默认输出关闭电频
  for(int i=0;i<arr_len;i++){
    pinMode(pins[i], OUTPUT);
    digitalWrite(pins[i], state[i]);
  }
}
WiFiClient client;
void loop() {
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
    isOffline=true;
  }
if(isOffline){
  isOffline=false;
  //如果之前掉过线 则先发送一次注销
  checkOut();
  delay(2000);
  checkIn();
  }
  // Use WiFiClient class to create TCP connections
  while (!client.connected()) {
    if (!client.connect(host, httpPort)) {
      Serial.println("connection failed");
      delay(5000);
      return;
    }
  }

  if(millis() - lastCheckInTime > postingInterval || lastCheckInTime==0) {
    checkIn();
  }
  
  // Read all the lines of the reply from server and print them to Serial
  if (client.available()) {
    String inputString = client.readStringUntil('\n');
    inputString.trim();
    Serial.println(inputString);
    int len = inputString.length()+1;
    if(inputString.startsWith("{") && inputString.endsWith("}")){
      char jsonString[len];
      inputString.toCharArray(jsonString,len);
      aJsonObject *msg = aJson.parse(jsonString);
      processMessage(msg);
      aJson.deleteItem(msg);          
    }
  }
}

void processMessage(aJsonObject *msg){
  aJsonObject* method = aJson.getObjectItem(msg, "M");
  aJsonObject* content = aJson.getObjectItem(msg, "C");     
  aJsonObject* client_id = aJson.getObjectItem(msg, "ID");
  if (!method) {
    return;
  }
    String M = method->valuestring;
    if(M == "say"){
      String C = content->valuestring;
      String F_C_ID = client_id->valuestring;
      if(C == "play"){
        for(int i=0;i<arr_len;i++){
          state[i] = LOW;
          digitalWrite(pins[i], state[i]);
        }
        sayToClient(F_C_ID,"LED All on!");    
      }else if(C == "stop"){
        for(int i=0;i<arr_len;i++){
          state[i] = HIGH;
          digitalWrite(pins[i], state[i]);
        }
        sayToClient(F_C_ID,"LED All off!");    
      }else{
        int pin = C.toInt();
        if(pin > 0 && pin <= arr_len){
          pin--;
          state[pin] = !state[pin];
          digitalWrite(pins[pin], state[pin]);
        }
        sayToClient(F_C_ID,"LED pin:"+pin); 
      }
    }
}

void checkIn() {
    String msg = "{\"M\":\"checkin\",\"ID\":\"" + DEVICEID + "\",\"K\":\"" + APIKEY + "\"}\n";
    client.print(msg);
    Serial.println(msg);
    lastCheckInTime = millis(); 
}
//{"M":"checkout","ID":"xx1","K":"xx2"}\n
void checkOut() {
    String msg = "{\"M\":\"checkout\",\"ID\":\"" + DEVICEID + "\",\"K\":\"" + APIKEY + "\"}\n";
    client.print(msg);
}
void sayToClient(String client_id, String content){
  String msg = "{\"M\":\"say\",\"ID\":\"" + client_id + "\",\"C\":\"" + content + "\"}\n";
  client.print(msg);
  lastCheckInTime = millis();
}
