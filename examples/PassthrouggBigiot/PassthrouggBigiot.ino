/*
    此文件需安装Arduino esp8266开发环境支持，环境搭建参见：http://www.bigiot.net/talk/237.html
    本程序可以用来控制四路继电器
    ESP8266烧入此程序直接，使用高低电频控制光耦继电器来控制电灯
    我的继电器默认高电频关闭，所以在初始化时都初始化为高电频，play关闭开启，stop关闭关闭，输入1-4打开或关闭对应的引脚
    代码基于https://github.com/bigiot/bigiotArduino/blob/master/examples/ESP8266/kaiguan/kaiguan.ino
    上的代码进行调整，修复了部分bug，解决了断线重连问题，此代码可以直接烧入到nodemcu模块，分享代码希望对大家有帮助
    本例子依赖的库：WiFiManager aJSON ESP8266WiFi ESP8266WebServer DNSServer
*/
#include <FS.h>                   //this needs to be first, or it all crashes and burns...
#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
//needed for library
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager
//#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
#include <aJSON.h>

// start reading from the first byte (address 0) of the EEPROM
int address = 0;
//define your default values here, if there are different values in config.json, they are overwritten.
char baud_rate[10]="";
//flag for saving data
bool shouldSaveConfig = false;

unsigned long lastCheckInTime = 0; //记录上次报到时间
const unsigned long postingInterval = 40000; // 每隔40秒向服务器报到一次
const char* host = "www.bigiot.net";
const int httpPort = 8181;
bool isOffline=false;//是否掉线，用来记录是否掉线，掉线之后重新连接需要注销账户再登录 
int login_status=0; //登录状态 0未登录 1登录中 2已登录 3登录失败 
unsigned long first_login_time = 0;//首次登录时间
int pins[4] = {D5,D6,D7,D8};
int state[4] = {HIGH,HIGH,HIGH,HIGH};
int arr_len = sizeof(pins)/sizeof(pins[0]);
String filepath="/config1.json";
void setup() {
  // put your setup code here, to run once:
  delay(1000);
  Serial.begin(115200);
  //读取配置文件中的参数
  initReadFile();
  initAutoConnect();
  if(String(baud_rate).length()>0&&String(baud_rate)!="115200")
  Serial.begin(String(baud_rate).toInt());
}
WiFiClient client;
void loop() {
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  // Use WiFiClient class to create TCP connections
  while (!client.connected()) {
    if (!client.connect(host, httpPort)) {
      Serial.println("tcp client connection failed");
      delay(3000);
      return;
    }
    Serial.println("connected bigiot");
    delay(1000);
  }
  if (Serial.available() > 0)//判读是否串口有数据
  {
    String comdata = "";//缓存清零
    while (Serial.available() > 0)//循环串口是否有数据
    {
      comdata += char(Serial.read());//叠加数据到comdata
      delay(1);//延时等待响应
    }
     comdata.trim();
    if(comdata=="clean"){
      cleanConfigFile();
      ESP.reset();
      }
     else
    client.println(comdata);
  }

  // Read all the lines of the reply from server and print them to Serial
  if (client.available()) {
    String inputString = client.readStringUntil('\n');
    inputString.trim();
    Serial.println(inputString);
  }
}
//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}
void initReadFile(){
   //read configuration from FS json
  Serial.println("mounting FS...");
    if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists(filepath)) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open(filepath, "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);
        configFile.readBytes(buf.get(), size);
        aJsonObject *json1 = aJson.parse(buf.get());
        aJsonObject* br = aJson.getObjectItem(json1, "baud_rate");
        String b_r=br->valuestring;
        strcpy(baud_rate, b_r.c_str());
        configFile.close();
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  //end read
  }
void initAutoConnect(){
  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  WiFiManagerParameter custom_baud_rate("baud_rate", "\u6ce2\u7279\u7387", baud_rate, 10);

  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;

  wifiManager.setDebugOutput(false);
  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //set static ip
  //wifiManager.setSTAStaticIPConfig(IPAddress(10,0,1,99), IPAddress(10,0,1,1), IPAddress(255,255,255,0));
  
  //add all your parameters here
  wifiManager.addParameter(&custom_baud_rate);

  wifiManager.setTimeout(30);

  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect("esp8266_connect_Bigiot", "12345678")) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.restart();
    delay(5000);
  }

  //if you get here you have connected to the WiFi
  Serial.println("connected...yeey :)");

  //read updated parameters
  strcpy(baud_rate, custom_baud_rate.getValue());

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");

    aJsonObject *json = aJson.createObject();
    aJson.addStringToObject(json, "baud_rate", baud_rate);
    char* jsonString = aJson.print(json);
    if (jsonString != NULL) {
      Serial.println(jsonString);
      File configFile = SPIFFS.open(filepath, "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }
    configFile.write((uint8_t *)jsonString, strlen(jsonString));
    configFile.close();
    }
    //end save
  }
  Serial.println("\nlocal ip");
  Serial.println(WiFi.localIP());
  }
void cleanConfigFile(){
  if (SPIFFS.begin()) {
    Serial.println("clean config.json begin");
    if (SPIFFS.exists(filepath)) {
      SPIFFS.remove(filepath);
      }}
    Serial.println("clean config file end");
}
