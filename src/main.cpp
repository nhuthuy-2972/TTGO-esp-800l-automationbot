#include "Adafruit_FONA.h"
#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#include <Arduino_JSON.h>

#define SIM800L_RX 27
#define SIM800L_TX 26
#define SIM800L_PWRKEY 4
#define SIM800L_RST 5
#define SIM800L_POWER 23
#define APPLICATION_ID "d91b47af8bd27e39704139d448340365"
#define REST_API_KEY "12038d977e3c0f2b53a5939973f48acd"
HardwareSerial *sim800lSerial = &Serial1;
Adafruit_FONA sim800l = Adafruit_FONA(SIM800L_PWRKEY);

const char *ssid = "001-INNO-DEV";
const char *password = "Innoria@@081120";
//const char *ssid = "yanbi";
//const char *password = "thucuoi2012";
const char *messageClass = "http://192.168.1.189:1337/parse/classes/Message";
String simidurl = "http://192.168.1.189:1337/parse/classes/SimRobot?where={\"ccid\":\"";
char httpdata[350];
char ccid[21] = {0};
String objectid;
char replybuffer[255];
uint8_t readline(char *buff, uint8_t maxbuff, uint16_t timeout = 0);

#define LED_BLUE 13
#define RELAY 14

String smsString = "";
String simobject = "";
long prevMillis = 0;
int interval = 1000;
char sim800lNotificationBuffer[64]; //for notifications from the FONA
char smsBuffer[250];
boolean ledState = false;

void wifi_config()
{
  WiFi.begin(ssid, password);
  Serial.println("Connecting");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to WiFi network with IP Address: ");
  Serial.println(WiFi.localIP());
}

String getsimid(String url)
{
  String payload = "{}";
  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println(F("http start"));
    HTTPClient http;
    http.begin(url.c_str());
    // Your Domain name with URL path or IP address with path
    // Specify content-type header
    http.addHeader("X-Parse-Application-Id", APPLICATION_ID);
    http.addHeader("X-Parse-REST-API-Key", REST_API_KEY);

    int httpResponseCode = http.GET();
    if (httpResponseCode > 0)
    {
      Serial.print("HTTP Response code: ");
      Serial.println(httpResponseCode);
      payload = http.getString();
    }
    else
    {
      Serial.print("Error code: ");
      Serial.println(httpResponseCode);
    }
    // Free resources
    http.end();
  }
  else
  {
    Serial.println("WiFi Disconnected");
  }
  return payload;
}
void post_message(const char *url, char *httpdata)
{
  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println(F("http start"));
    HTTPClient http;
    // Your Domain name with URL path or IP address with path
    http.begin(url);
    // Specify content-type header
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-Parse-Application-Id", APPLICATION_ID);
    http.addHeader("X-Parse-REST-API-Key", REST_API_KEY);
    // Send HTTP POST request
    Serial.println(httpdata);
    int httpResponseCode = http.POST(httpdata);
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
    // Free resources
    http.end();
  }
  else
  {
    Serial.println("WiFi Disconnected");
  }
}

void setup()
{
  pinMode(LED_BLUE, OUTPUT);
  pinMode(RELAY, OUTPUT);
  pinMode(SIM800L_POWER, OUTPUT);

  digitalWrite(LED_BLUE, HIGH);
  digitalWrite(SIM800L_POWER, HIGH);

  Serial.begin(115200);
  Serial.println();
  Serial.println();
  Serial.println(F("Config Wifi"));
  Serial.flush();
  wifi_config();

  Serial.println(F("ESP32 with GSM SIM800L"));
  Serial.println(F("Initializing....(May take more than 10 seconds)"));

  delay(10000);

  // Make it slow so its easy to read!
  sim800lSerial->begin(115200, SERIAL_8N1, SIM800L_TX, SIM800L_RX);
  if (!sim800l.begin(*sim800lSerial))
  {
    Serial.println(F("Couldn't find GSM SIM800L"));
    while (1)
      ;
  }

  Serial.println(F("GSM SIM800L is OK"));

  uint8_t ccid_len = sim800l.getSIMCCID(ccid);

  if (ccid_len > 0)
  {
    Serial.print("SIM CCID: ");
    Serial.println(ccid);
  }

  sim800lSerial->print("AT+CNMI=2,1\r\n");

  Serial.println("GSM SIM800L Ready");
  simidurl = simidurl + ccid + "\"}";
  simobject = getsimid(simidurl);

  while (simobject.equals("{}"))
  {
    simobject = getsimid(simidurl);
  }

  JSONVar myObject = JSON.parse(simobject);
  if (JSON.typeof(myObject) == "undefined")
  {
    Serial.println("Parsing input failed!");
    return;
  }

  objectid = myObject["results"][0]["objectId"];
  Serial.println(objectid);
}

void loop()
{
  if (millis() - prevMillis > interval)
  {
    ledState = !ledState;
    digitalWrite(LED_BLUE, ledState);

    prevMillis = millis();
  }

  char *bufPtr = sim800lNotificationBuffer; //handy buffer pointer

  if (sim800l.available())
  {
    int slot = 0; // this will be the slot number of the SMS
    int charCount = 0;

    // Read the notification into fonaInBuffer
    do
    {
      *bufPtr = sim800l.read();
      Serial.write(*bufPtr);
      delay(1);
    } while ((*bufPtr++ != '\n') && (sim800l.available()) && (++charCount < (sizeof(sim800lNotificationBuffer) - 1)));

    //Add a terminal NULL to the notification string
    *bufPtr = 0;

    //Scan the notification string for an SMS received notification.
    //  If it's an SMS message, we'll get the slot number in 'slot'
    if (1 == sscanf(sim800lNotificationBuffer, "+CMTI: \"SM\",%d", &slot))
    {
      Serial.print("slot: ");
      Serial.println(slot);

      char callerIDbuffer[32]; //we'll store the SMS sender number in here

      // Retrieve SMS sender address/phone number.
      if (!sim800l.getSMSSender(slot, callerIDbuffer, 31))
      {
        Serial.println("Didn't find SMS message in slot!");
      }
      Serial.print(F("FROM: "));
      Serial.println(callerIDbuffer);

      // Retrieve SMS value.
      uint16_t smslen;
      // Pass in buffer and max len!

      if (sim800l.readSMS(slot, smsBuffer, 250, &smslen))
      {
        String smsparse = String(smsBuffer);
        smsparse.trim();
        smsparse.replace("\n", "\\n");
        Serial.println(smsparse);
        snprintf(httpdata, sizeof(httpdata), "{\"message\": \"%s\",\"phoneNumber\": \"%s\",\"sim\": {\"__type\":\"Pointer\",\"className\":\"SimRobot\",\"objectId\":\"%s\"}}", smsparse.c_str(), callerIDbuffer, objectid.c_str());

        post_message(messageClass, httpdata);
      }
    }
  }
}
