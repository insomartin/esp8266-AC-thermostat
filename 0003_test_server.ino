/*
   author: martin miranda
   date: 11/26/2020
   revision: 0
   HTS221 sensor
  remarks: sensors working
*/

#include <Arduino.h>
#ifdef ESP32
#include <WiFi.h>
#include <AsyncTCP.h>
#include <SPIFFS.h>
#else
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <Hash.h>
#include <FS.h>
#endif
#include <ESPAsyncWebServer.h>

#include <Wire.h>
#include <Adafruit_HTS221.h>
#include <Adafruit_Sensor.h>

Adafruit_HTS221 hts;
float hts221_temperature;
float hts221_humidity;
int compressor_state = 0; // 0 off, 1 on
int fan_speed = 0; // 1 = low, 2 = mid, 3 = high
const int led = D4;
const int compressor_motor = D5;
const int fan_low = D6;
const int fan_mid = D7;
const int fan_hi = D8;
String str_sens_temp;
String str_sens_humi;
int initialize_AC = true;
int compressor_timeout = 60;
int State_compressor_timeout ;
float hysterisis = 1.2;

unsigned long previousMillis = 0;
unsigned long currentMillis;
const long interval = compressor_timeout * 1000;
int aircon_enabled;
int temp_setpoint;

// Set your Static IP address
IPAddress local_IP(192, 168, 254, 201);
// Set your Gateway IP address
IPAddress gateway(192, 168, 254, 254);

IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(8, 8, 8, 8);   //optional
IPAddress secondaryDNS(8, 8, 4, 4); //optional

AsyncWebServer server(80);

// REPLACE WITH YOUR NETWORK CREDENTIALS
const char* ssid = "wifissid";
const char* password = "password";

const char* PARAM_ACSTATUS = "input_Enable_ac";
const char* PARAM_FANSPEED = "input_Fanspeed";
const char* PARAM_SETPOINT = "input_Setpoint";
int count_test;

// HTML web page to handle 3 input fields (inputString, inputInt, inputFloat)
const char index_html[] PROGMEM = R"rawliteral(
   <!DOCTYPE html>
<html>
  <script>
    function httpGetAsync(theUrl, callback) {
      var xmlHttp = new XMLHttpRequest();
      xmlHttp.onreadystatechange = function () {
        if (xmlHttp.readyState == 4 && xmlHttp.status == 200)
          callback(xmlHttp.responseText);
      };
      xmlHttp.open("GET", theUrl, true);
      xmlHttp.send(null);
    }
    function updateTemp(data) {
      document.getElementById("temperature").innerHTML = "Temperature: " + data + "&deg;C";
    }
        function updatehumidity(data) {
      document.getElementById("humidity").innerHTML = "Humidity: " + data + " rH%";
    }
    function autoWrite(duration) {
      setInterval(function () {
        document.getElementById("dateinfo").innerHTML = Date();
        httpGetAsync("/temp01", updateTemp);
        httpGetAsync("/humidity01", updatehumidity);
      }, duration);
    }
  </script>
  <body onload="JavaScript:autoWrite(1000)">
  <p>esp8266 thermostat</p>
    <p id="dateinfo"></p>
    <p id="temperature"></p> 
    <p id="humidity"></p> 
   
  <form action="/get" target="_parent">
  Temperature Setpoint (current value %input_Setpoint%): <input type="number " name="input_Setpoint">
  <input type="submit" value="Submit" onclick="submitMessage()">
  </form>

  <form action="/get" target="_parent">
  Fan Speed (current value %input_Fanspeed%): <input type="number " name="input_Fanspeed">
  <input type="submit" value="Submit" onclick="submitMessage()">
  </form>
  <form action="/get" target="_parent">
  Enable AC (current value %input_Enable_ac%): <input type="number " name="input_Enable_ac">
  <input type="submit" value="Submit" onclick="submitMessage()">
  </form>

  </body>
</html>
)rawliteral";

void notFound(AsyncWebServerRequest *request) {
  request->send(404, "text/plain", "Not found");
}


String readFile(fs::FS &fs, const char * path) {
  //Serial.printf("Reading file: %s\r\n", path);
  File file = fs.open(path, "r");
  if (!file || file.isDirectory()) {
    //Serial.println("- empty file or failed to open file");
    return String();
  }
 // Serial.println("- read from file:");
  String fileContent;
  while (file.available()) {
    fileContent += String((char)file.read());
  }
 // Serial.println(fileContent);
  return fileContent;
}

void writeFile(fs::FS &fs, const char * path, const char * message) {
  Serial.printf("Writing file: %s\r\n", path);
  File file = fs.open(path, "w");
  if (!file) {
    Serial.println("- failed to open file for writing");
    return;
  }
  if (file.print(message)) {
    Serial.println("- file written");
  } else {
    Serial.println("- write failed");
  }
}

// Replaces placeholder with stored values
String processor(const String& var) {
  //Serial.println(var);
  if (var == "input_Enable_ac") {
    return readFile(SPIFFS, "/input_Enable_ac.txt");
  }
  else if (var == "input_Fanspeed") {
    return readFile(SPIFFS, "/input_Fanspeed.txt");
  }
  else if (var == "input_Setpoint") {
    return readFile(SPIFFS, "/input_Setpoint.txt");
  }
  else if (var == "count_test"){
    return String(count_test);
  }
  return String();
}

void setup() {
  Serial.begin(115200);
  pinMode(compressor_motor, OUTPUT);
  pinMode(fan_low, OUTPUT);
  pinMode(fan_mid, OUTPUT);
  pinMode(fan_hi, OUTPUT);

  digitalWrite(compressor_motor, LOW);
  digitalWrite(fan_low, LOW);
  digitalWrite(fan_mid, LOW);
  digitalWrite(fan_hi, LOW);
  

  
  if (!hts.begin_I2C()) {
    Serial.println("Failed to find HTS221 chip");
    while (1) { delay(10); }
  }
  Serial.println("HTS221 Found!");
  hts.setDataRate(HTS221_RATE_1_HZ);

  // Initialize SPIFFS
#ifdef ESP32
  if (!SPIFFS.begin(true)) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }
#else
  if (!SPIFFS.begin()) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }
#endif
Serial.print("ESP8266 MAC: ");

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("WiFi Failed!");
    return;
  }

  Serial.println(WiFi.macAddress());
   if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
    Serial.println("STA Failed to configure");
  }
  
  Serial.println();
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // Send web page with input fields to client
  server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send_P(200, "text/html", index_html, processor);
  });

  // Send a GET request to <ESP_IP>/get?inputString=<inputMessage>
  server.on("/get", HTTP_GET, [] (AsyncWebServerRequest * request) {
    String inputMessage;
    // GET inputString value on <ESP_IP>/get?inputString=<inputMessage>
    if (request->hasParam(PARAM_ACSTATUS)) {
      inputMessage = request->getParam(PARAM_ACSTATUS)->value();
      writeFile(SPIFFS, "/input_Enable_ac.txt", inputMessage.c_str());
    }
    // GET inputInt value on <ESP_IP>/get?inputInt=<inputMessage>
    else if (request->hasParam(PARAM_FANSPEED)) {
      inputMessage = request->getParam(PARAM_FANSPEED)->value();
      writeFile(SPIFFS, "/input_Fanspeed.txt", inputMessage.c_str());
    }
    // GET inputFloat value on <ESP_IP>/get?inputFloat=<inputMessage>
    else if (request->hasParam(PARAM_SETPOINT)) {
      inputMessage = request->getParam(PARAM_SETPOINT)->value();
      writeFile(SPIFFS, "/input_Setpoint.txt", inputMessage.c_str());
    }
    else {
      inputMessage = "No message sent";
    }
    Serial.println(inputMessage);
    request->send(204, "text/text", ""); // 200 for normal reply and debug, 204 for production.
  });

  server.on("/temp01",HTTP_GET, [](AsyncWebServerRequest * request) {
   request->send(200, "text/html", str_sens_temp);
  });

    server.on("/humidity01",HTTP_GET, [](AsyncWebServerRequest * request) {
   request->send(200, "text/html", str_sens_humi);
  });
    
/*
  server.on("/temp01", [](AsyncWebServerRequest * request) {
    request->send_P(200, "text/html",sen_temp );
    //server.send(200, "text/html", String(hts221_temperature));
  });

  */
/*
    server.on("/humidity01", [](AsyncWebServerRequest * request) {
      request->send_P(200, "text/html", String(hts221_humidity));
    //server.send(200, "text/html", String(hts221_humidity));
  });
  */
  server.onNotFound(notFound);
  server.begin();

}

void loop() {
  // To access your stored values on inputString, inputInt, inputFloat

  /*
  String yourInputString = readFile(SPIFFS, "/inputString.txt");
  Serial.print("*** Your inputString: ");
  Serial.println(yourInputString);

  int yourInputInt = readFile(SPIFFS, "/inputInt.txt").toInt();
  Serial.print("*** Your inputInt: ");
  Serial.println(yourInputInt);

  float yourInputFloat = readFile(SPIFFS, "/inputFloat.txt").toFloat();
  Serial.print("*** Your inputFloat: ");
  Serial.println(yourInputFloat);
  count_test++;
  Serial.print(count_test);
  */

  aircon_enabled = readFile(SPIFFS, "/input_Enable_ac.txt").toInt();
  fan_speed = readFile(SPIFFS, "/input_Fanspeed.txt").toInt();
  temp_setpoint = readFile(SPIFFS, "/input_Setpoint.txt").toInt();
  
  sensors_event_t temp;
  sensors_event_t humidity;
  hts.getEvent(&humidity, &temp);// populate temp and humidity objects with fresh data
  //Serial.print("Temperature: "); Serial.print(temp.temperature); Serial.println(" degrees C");
  //Serial.print("Humidity: "); Serial.print(humidity.relative_humidity); Serial.println("% rH");
  
 hts221_temperature = temp.temperature;
 hts221_humidity = humidity.relative_humidity;

  str_sens_temp = String(hts221_temperature);
  str_sens_humi = String(hts221_humidity);
  
 if (aircon_enabled){
    switch (fan_speed){
        case 0:
        // fan off, invalid command.
        //aircon_enabled = false;
        break;
        case 1:
            // low speed
            digitalWrite(fan_low, HIGH);
            digitalWrite(fan_mid, LOW);
            digitalWrite(fan_hi, LOW);
        break;
        case 2:
            // medium speed
            digitalWrite(fan_low, LOW);
            digitalWrite(fan_mid, HIGH);
            digitalWrite(fan_hi, LOW);
        break;
        case 3:
            // high speed
            digitalWrite(fan_low, LOW);
            digitalWrite(fan_mid, LOW);
            digitalWrite(fan_hi, HIGH);
        break;
    }
    if (initialize_AC){
      delay(5000);
      initialize_AC = false;
      }
    
    else{
      if ( hts221_temperature > temp_setpoint + hysterisis ){ 
          digitalWrite(compressor_motor, HIGH);
      }
      if (hts221_temperature < temp_setpoint  ){
          digitalWrite(compressor_motor, LOW);
      }
    }
   }
 else{
      // aircon is turned off. ignore fanspeed variable. 
      digitalWrite(compressor_motor, LOW);
      digitalWrite(fan_low, LOW);
      digitalWrite(fan_mid, LOW);
      digitalWrite(fan_hi, LOW);
      initialize_AC = true;
 }
//  delay(1000);



}
