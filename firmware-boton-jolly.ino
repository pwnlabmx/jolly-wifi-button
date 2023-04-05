/*To compile this code, you need to select first the ESP32 on Boards.
  Please enter your sensitive data in the Secret tab/arduino_secrets.h */

#include <WiFi.h>
#include "time.h"
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
#include <WiFiManager.h>          //WiFiManager.h .- WiFi Configuration Magic https://github.com/tzapu/WiFiManager
#include <FS.h>                   //this needs to be first, or it all crashes and burns...
#ifdef ESP32
#include <SPIFFS.h>
#endif

#define FORMAT_SPIFFS_IF_FAILED true

const int PIN_AP = 12; // ESP32S2 pin 12 (GPIO12) push button to return to AP mode

//For the BUTTON and the LED
#define BUTTON_PIN 15    // ESP32S2 pin 15 (GPIO15), which connected to main button
#define LED_PIN    13   // ESP32S2 pin 13 (GPIO13), which connected to main led
int button_state = 0;   // variable for reading the main button status
int lastButton_state = HIGH;
long lastDebounceTime = 0;
long debounceDelay = 50;

char ssidId[15]; //Create a Unique AP from MAC address

//define your default values here, if there are different values in config.json, they are overwritten.
char print_server[40] = "10.0.4.250";
char print_port[6]  = "9100";
char table_no[3];
#define product "MIMOSA"

//NTP server and offsets
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = -18000; //-18000 for GMT-5, -21600 for GMT-6, -25200 for GMT-7
const int   daylightOffset_sec = 0; //Set 3600 for summer time, 0 for winter time
char timeWeekDayName[10];
char timeWeekDayNumber[3];
char timeHour[3];
char timeMinute[3];
char timeSeconds[3];

//FORMAT FS SO THE JSON FILE CAN BE SAVED AND READ
void listDir(fs::FS &fs, const char * dirname, uint8_t levels){
    //Serial.printf("Listing directory: %s\r\n", dirname);
    File root = fs.open(dirname);
    if(!root){
        //Serial.println("- failed to open directory");
        return;
    }
    if(!root.isDirectory()){
        //Serial.println(" - not a directory");
        return;
    }

   File file = root.openNextFile();
    while(file){
        if(file.isDirectory()){
            //Serial.print("  DIR : ");
            //Serial.println(file.name());
            if(levels){
                listDir(fs, file.path(), levels -1);
            }
        } else {
            //Serial.print("  FILE: ");
            //Serial.print(file.name());
            //Serial.print("\tSIZE: ");
            //Serial.println(file.size());
        }
        file = root.openNextFile();
    }
}
//WiFi Setup
void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Modo de configuración ingresado");
  Serial.println(WiFi.softAPIP());
  Serial.println(myWiFiManager->getConfigPortalSSID());
}

//flag for saving data
bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Debería guardar la configuración");
  shouldSaveConfig = true;
}

void WiFiConfig () {
  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  WiFiManagerParameter custom_print_server("server", "Print IP", print_server, 40);
  WiFiManagerParameter custom_print_port("port", "Print port", print_port, 6);
  WiFiManagerParameter custom_table_no("table","Table Number",table_no, 3);

  //WiFiManager Object Declaration
  WiFiManager wifiManager;

  //Add All Your Parameters Here
  wifiManager.addParameter(&custom_print_server);
  wifiManager.addParameter(&custom_print_port);
  wifiManager.addParameter(&custom_table_no);

  // using that command, as settings will be turned off in memory
  // in case the newsroom automatically connects, it will turn off.
  if (digitalRead(PIN_AP) == 0) {
    Serial.println("Reset settings");
    wifiManager.resetSettings();
    delay(100);
  }

  //callback for when entering AP configuration mode
  wifiManager.setAPCallback(configModeCallback);
  //callback when connected to a network, i.e. when it goes to work in EST mode
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  uint64_t chipid = ESP.getEfuseMac(); //The chip ID is essentially its MAC address(length: 6 bytes).
  uint16_t chip = (uint16_t)(chipid >> 32);
  snprintf(ssidId, 15, u8"\U0001F9C7""%04X", chip);
  //create a network named JollyWiFi+(Chip_ID) with pass 12345678
  wifiManager.autoConnect(ssidId);

  Serial.println("Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    blinky(10, 500);
  }
  Serial.println("");
  Serial.println("Connected to WiFi network with IP Address:" + String(WiFi.localIP()));

  //Read Updated Parameters
  strcpy(print_server, custom_print_server.getValue());
  strcpy(print_port, custom_print_port.getValue());
  strcpy(table_no, custom_table_no.getValue());
  Serial.println("The values in the file are: ");
  Serial.println("\tPrinter IP: " + String(print_server));
  Serial.println("\tPrinter port: " + String(print_port));
  Serial.println("\tTable Number: " + String(table_no));

  //Save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");
#if defined(ARDUINOJSON_VERSION_MAJOR) && ARDUINOJSON_VERSION_MAJOR >= 6
    DynamicJsonDocument json(1024);
#else
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
#endif
    json["print_server"] = print_server;
    json["print_port"] = print_port;
    json["table_no"]  = table_no;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

#if defined(ARDUINOJSON_VERSION_MAJOR) && ARDUINOJSON_VERSION_MAJOR >= 6
    serializeJson(json, Serial);
    serializeJson(json, configFile);
#else
    json.printTo(Serial);
    json.printTo(configFile);
#endif
    configFile.close();
    //end save
  }
}

//Sequence time to get the date and time form the NTP Server
void LocalTime()
{
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("No time available (yet)");
    return;
  }
  Serial.println(&timeinfo, "%B %d %Y %H:%M");
  //Segment the date and time information and save it in single variables.
  strftime(timeWeekDayName,10, "%A", &timeinfo);
  strftime(timeWeekDayNumber,3, "%d", &timeinfo);
  strftime(timeHour,3, "%H", &timeinfo);
  strftime(timeMinute,3, "%M", &timeinfo);
  strftime(timeSeconds,3, "%S", &timeinfo);
}

//Sequence to send a message through TCP to an IP address
void messageTCP(){
  WiFiClient client;
  Serial.println("Enviando mensaje a la IP " + String(print_server) + ":" + String(print_port));
  if (client.connect(print_server, atoi(print_port))) // Intenta acceder a la dirección de destino
       {
        Serial.println("Conexion exitosa");
        char message [45];
        snprintf(message,45,"\t\t MESA %d - %s \t\t", atoi(table_no), product);
        client.print("------------------------------------------------------------------------------------------------\n\n");
        client.print(timeWeekDayName);
        client.print(" ");
        client.print(timeWeekDayNumber);
        client.print(", ");
        client.print(timeHour);
        client.print(":");
        client.print(timeMinute);
        client.print(":");
        client.print(timeSeconds);
        client.print("\n\n");
        client.print(message); 
        client.print("\n\n");            
        client.print("------------------------------------------------------------------------------------------------\n\n\n\n\n\n\n\n\n");
        Serial.println("Cerrar la conexión actual");
        client.stop(); // Cerrar el cliente
    }
    else
    {
        Serial.println("El envio de mensaje a la impresora falló");
        client.stop(); // Cerrar el cliente
    }
}

void setup() {
  //while(!Serial);
  Serial.begin(9600);
  pinMode(PIN_AP, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  if(!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)){
        Serial.println("SPIFFS Mount Failed");
        return;
    }
    listDir(SPIFFS, "/", 0);
    //Serial.println( "Test complete" );

  //read configuration from FS json
  Serial.println("mounting FS...");

  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);

#if defined(ARDUINOJSON_VERSION_MAJOR) && ARDUINOJSON_VERSION_MAJOR >= 6
        DynamicJsonDocument json(1024);
        auto deserializeError = deserializeJson(json, buf.get());
        serializeJson(json, Serial);
        if ( ! deserializeError ) {
#else
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
#endif
          Serial.println("\nparsed json");
          strcpy(print_server, json["print_server"]);
          strcpy(print_port, json["print_port"]);
          strcpy(table_no,json["table_no"]);
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
  WiFiConfig();
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
}

void loop() {
  // read the state of the button value:
  button_state = digitalRead(BUTTON_PIN);
  delay(100);
  if (button_state == LOW) {      // if button is pressed
    digitalWrite(LED_PIN, HIGH); // turn on LED
    //Check WiFi connection status
    if (WiFi.status() == WL_CONNECTED) {
      LocalTime();
      messageTCP(); //Secuencia para enviar mensaje TCP
      delay (2000); //Delay to avoid multiple accidental requests
    }
    else {
      Serial.println("WiFi Disconnected");
      blinky(15,500);
    }
    while(button_state == LOW){
    button_state = digitalRead(BUTTON_PIN);
    delay(100);
    }
  }
  else {
    // otherwise, button is not pressing
    digitalWrite(LED_PIN, LOW);  // turn off LED
  } 
}

//Led Status for different functions
void blinky (int repeats, int time) {
  for (int i = 0; i < repeats; i ++) {
    digitalWrite(LED_PIN, HIGH);
    delay(time);
    digitalWrite(LED_PIN, LOW);
    delay(time);
  }
}
