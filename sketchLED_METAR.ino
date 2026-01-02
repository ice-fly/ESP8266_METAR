#include <ESP8266WiFi.h>
#include <FastLED.h>

#define FASTLED_ESP8266_RAW_PIN_ORDER
#define DATA_PIN    14 
#define LED_TYPE    WS2811
#define COLOR_ORDER RGB
static const uint8_t BRIGHTNESS = 200;
static const uint8_t NUM_AIRPORTS = 50; 
static const uint8_t WIND_THRESHOLD = 30;
static const uint16_t LOOP_INTERVAL = 5000; 
static const bool DO_LIGHTNING = true;
static const bool DO_WINDS = true;
static const uint32_t REQUEST_INTERVAL = 262140; //~4.39 min

static const char airports[NUM_AIRPORTS][5] PROGMEM = { //Use PROGMEM to store the array in Flash memory
  "KABE", //1 Allentown
  "KRDG", //2 Reading
  "KPTW", //3 Pottstown
  "KPHL", //4 Philly
  "KWRI", //5 JB MGD Lakehurst
  "KBLM", //6 Monmouth Exec
  "KJFK", //7 Kennedy
  "KEWR", //8 Newark
  "KSMQ", //9 Somerset NJ
  "KFWN", //10 Sussex
  "KMGJ", //11 Orange County
  "KPOU", //12 Hudson Valley
  "KDXR", //13 Danbury Muni
  "KBDR", //14 Bridgeport Sikorsky
  "KHPN", //15 Westchester county
  "KTEB", //16 Teterboro
  "KLGA", //17 Laguardia
  "KFRG", //18 Republic
  "KISP", //19 Long Island Mac Arthur
  "KFOK", //20 Francis Gabreski
  "KJPX", //21 East Hampton
  "KBID", //22 Block Island State
  "KUUU", //23 Newport state
  "KMVY", //24 Martha's Vineyard
  "KACK", //25 Nantucket Memorial
  "KCQX", //26 Chatham Muni
  "KPVC", //27 Province town Muni
  "KHYA", //28 Cape Cod Gateway
  "KFMH", //29 Cape Cod Coast Gaurd Air station
  "KEWB", //30 New Bedford Regional
  "KGHG", //31 Marshfield Muni
  "KTAN", //32 Taunton Muni
  "KOQU", //33 Quonset State
  "KGON", //34 Groton New London
  "KSNC", //35 Chester
  "KMMK", //36 Meriden Markham Muni
  "KIJD", //37 Windham
  "KSFZ", //38 North Central State (KSFC)
  "KOWD", //39 Norwood Memorial
  "KBOS", //40 Boston
  "KBVY", //41 Beverly Reg
  "KPSM", //42 Portsmouth Int
  "KCON", //43 Concord Muni
  "KMHT", //44 Manchester Boston Reg
  "KFIT", //45 Fitchburg Muni
  "KEEN", //46 Dillant Hopkins
  "KORE", //47 Orange Muni
  "KORH", //48 Worcester Reg
  "KCEF", //49 Westover ARB Metro
  "KBDL" //50 Bradley Intl
};
static const char SERVER[] PROGMEM = "aviationweather.gov"; //Website
static const char BASE_URI[] PROGMEM = "/api/data/metar?format=xml&ids="; //Endpoint

///Wifi Consts (change these)
static const char* ssid2 PROGMEM = ""; 
static const char* pass2 PROGMEM = ""; 
//static const char* ssid1 PROGMEM = ""; 
//static const char* pass1 PROGMEM = ""; 
static const char* ssid1 PROGMEM = ""; 
static const char* pass1 PROGMEM = ""; 

CRGB ledColors[NUM_AIRPORTS]; //Colors to be displayed on LEDs
CRGB baseColors[NUM_AIRPORTS]; //Backup of LEDs to overright for lightning
bool hasLightning[NUM_AIRPORTS]; //Mask for lightning airports
bool lightningActive = false; //trigger for the animation logic
bool flicker = false;
uint16_t flickerCount = 0;
uint16_t requestCount = 0;
unsigned long lastRequest = 0;
unsigned long lastEvent = 0;
unsigned long lastFlicker = 0;

char buf[5];
String tag;
String raw;
String id;
String cat; 
uint16_t wind = NULL;
uint16_t gusts = NULL;
uint8_t results = NULL;
String wx;

void setup() { //Setup Serial, Wifi and LEDs
  pinMode(2, OUTPUT); Serial.begin(115200); digitalWrite(2, LOW); delay(LOOP_INTERVAL); Serial.println(F("\n\nESP8266 Booting...")); //Delay & Start serial
  tag.reserve(120); raw.reserve(128); id.reserve(8); cat.reserve(8); wx.reserve(120); //Reserves for mem
  delay(NUM_AIRPORTS);ESP.wdtFeed(); //Now's a good time to chillout, and feed the beast
  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(ledColors, NUM_AIRPORTS).setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(15);
  delay(1);
  FastLED.show();
  WiFi.setSleepMode(WIFI_NONE_SLEEP);WiFi.persistent(false);WiFi.setAutoReconnect(true); //Don't write wifi credentials to flash every time
  digitalWrite(2, HIGH);
}

void loop() {
  delay(5);yield(); //Yeild every loop
  unsigned long currentMillis = millis();
  if (currentMillis - lastRequest >= REQUEST_INTERVAL || lastRequest == 0) { //Every request interval
    if ((requestCount >= 256)||(ESP.getMaxFreeBlockSize() < 8192)){ //Restart on 256 cycles & low mem
      Serial.println(F("Rebooting..."));delay(LOOP_INTERVAL);ESP.restart();
    }
    requestCount++;
    FastLED.setBrightness(BRIGHTNESS/5);FastLED.show(); //Dim for upcoming network/CPU load and to indicate stale data on fail
    digitalWrite(2, LOW);
    delay(1);
    lastRequest = currentMillis;
    if (connectWifi(ssid1, pass1)) {} else if (connectWifi(ssid2, pass2)) {} else {Serial.println(F("Failled to find wifi"));} //ensure we are on wifi
    delay(1);
    if (getMetars()) { //Get the metars
      Serial.println(F("Refreshing LEDs"));
      for (int i=0;i<NUM_AIRPORTS;i++){ledColors[i]=baseColors[i];} //Hard-sync both buffers after a data update
      delay(NUM_AIRPORTS);ESP.wdtFeed(); //Now's a good time to chillout, and feed the beast
      digitalWrite(2, HIGH);
      FastLED.setBrightness(BRIGHTNESS);
      FastLED.show();
    } else {
      lastRequest-=LOOP_INTERVAL;
    }
  }
  if (DO_LIGHTNING && lightningActive) { //Lightning
    if (currentMillis - lastEvent >= LOOP_INTERVAL) {
      lastEvent = currentMillis;
      if (flickerCount > LOOP_INTERVAL) { //If it has done more than the loop interval.
        flicker = false; flickerCount = 0; //Restore flicker state
        for (int i=0;i<NUM_AIRPORTS;i++){ledColors[i]=baseColors[i];}
        FastLED.setBrightness(BRIGHTNESS);
        FastLED.show();
        delay(NUM_AIRPORTS);ESP.wdtFeed(); //Now's a good time to chillout, and feed the beast
      }
      delay(1);
    }
    else if (currentMillis - lastFlicker >= NUM_AIRPORTS) {
      lastFlicker = currentMillis;
      flicker = !flicker;
      flickerCount++;
      FastLED.setBrightness(BRIGHTNESS/5);
      for (int i = 0; i < NUM_AIRPORTS; i++) { //If toggle is true, show White. If false, show the weather color from baseColors.
        if (hasLightning[i]) {ledColors[i] = flicker ? CRGB::White : baseColors[i];}
      }
      FastLED.show();
      delay(5);ESP.wdtFeed(); //Now's a good time to chill a bit, and feed the beast
    }
  } else {
    ESP.wdtDisable(); delay(LOOP_INTERVAL);ESP.wdtEnable(LOOP_INTERVAL); //Now's a great time to chillout, and feed the beast
  }
}

bool connectWifi(const char ssid[], const char pass[]) { //Take in a wifi & pass and connect
  if (WiFi.status() == WL_CONNECTED) return true; //If we are connected just skip
  else {
    fill_solid(ledColors, NUM_AIRPORTS, CRGB::FireBrick);
    unsigned long start = millis();
    FastLED.show();
    WiFi.mode(WIFI_STA);
    Serial.print(F("Connecting to: ")); Serial.println(ssid);
    WiFi.begin(ssid, pass);
    delay(NUM_AIRPORTS);ESP.wdtFeed(); //Now's a good time to chillout, and feed the beast
    while (WiFi.status() != WL_CONNECTED && millis() - start < LOOP_INTERVAL+LOOP_INTERVAL+LOOP_INTERVAL+LOOP_INTERVAL+LOOP_INTERVAL) {
      delay(NUM_AIRPORTS);
    }
    fill_solid(ledColors, NUM_AIRPORTS, CRGB::Black);
    FastLED.show();
    delay(1);
  }
  return WiFi.status() == WL_CONNECTED;
}

bool getMetars() { //Get metar data
  tag.clear(); raw.clear(); id.clear(); wind=NULL; gusts=NULL; cat.clear(); wx.clear();
  lightningActive = false;
  memset(hasLightning, 0, sizeof(hasLightning));
  delay(1);
  {// Start of local scope forBear SSL memory
    BearSSL::WiFiClientSecure client;
    client.setInsecure();
    client.setBufferSizes(4096, 512);
    if (!client.connect(FPSTR(SERVER), 443)) {
      Serial.println(F("Failed to connect!"));
      client.stopAll();
      delay(NUM_AIRPORTS);ESP.wdtFeed(); //Now's a good time to chillout, and feed the beast
      return false;
    }
    while (!client.availableForWrite()){
      delay(NUM_AIRPORTS);ESP.wdtFeed(); //Now's a good time to chillout, and feed the beast
    }
  //Send Request
    client.print(F("GET ")); client.print(FPSTR(BASE_URI));
    for (int i = 0; i < NUM_AIRPORTS; i++) { //Loop over the Airports and push them onto the URI end
      memcpy_P(buf, airports[i], 5);
      client.print(buf); 
      if (i < NUM_AIRPORTS - 1) client.print(F(","));
    }
    client.println(F(" HTTP/1.1"));
    client.print(F("Host: ")); client.println(FPSTR(SERVER));
    client.println(F("User-Agent: LanyiMetarMap Final")); //USer agent required
    client.println(F("Connection: close"));
    client.println(); //Terminate the GET
    while (client.connected() && !client.available()) {
      delay(5);ESP.wdtFeed(); //Now's a good time to chill a bit, and feed the beast
    }
    Serial.println(client.readStringUntil('\n'));
    if (!client.find("response")) { //Stop if we can't find headers
      Serial.println(F("failed to parse response"));
      client.stopAll();
      delay(NUM_AIRPORTS);ESP.wdtFeed(); //Now's a good time to chillout, and feed the beast
      return false;
    } 
    if (!client.find("<request_index>")) { //Stop if we can't find the index
      Serial.println(F("failed to parse index"));
      client.stopAll();
      delay(NUM_AIRPORTS);ESP.wdtFeed(); //Now's a good time to chillout, and feed the beast
      return false;
    } else { //otherwise parse the XML
      delay(5);ESP.wdtFeed(); //Now's a good time to chill a bit, and feed the beast
      Serial.print(F("Parsing XML Stream Request #: ")); Serial.println(client.readStringUntil('<'));
      client.find("<data num_results=\"");
      delay(1);
      results = client.readStringUntil('\"').toInt();
      Serial.print(F("Located "));Serial.print(results); 
      Serial.print(F(" missing "));Serial.println(NUM_AIRPORTS-results);
      delay(1);
      if(!(results>0)||!client.find("<METAR")) {
        Serial.println(F("failed to find metar"));
        client.stopAll();
        delay(NUM_AIRPORTS);ESP.wdtFeed(); //Now's a good time to chillout, and feed the beast
        return false;
      }
    }
    lastRequest = millis();
    
    while (results > 0) {
      if (millis() - lastRequest > LOOP_INTERVAL) { //5-second safety timeout
        Serial.println(F("Client Timeout!"));
        client.stopAll();
        tag.clear(); raw.clear(); id.clear(); wind=NULL; gusts=NULL; cat.clear(); wx.clear();
        delay(NUM_AIRPORTS);ESP.wdtFeed(); //Now's a good time to chillout, and feed the beast
        break; 
      }
      digitalWrite(2, LOW);
      if (!client.available()&&client.connected()) {
        delay(NUM_AIRPORTS);ESP.wdtFeed(); //Now's a good time to chillout, and feed the beast
        continue;
      }
      delay(1);
      tag = client.readStringUntil('>'); //Use .readStringUntil carefully
      digitalWrite(2, HIGH);

      if (tag.length()==0||tag==F("METAR") || tag==F("") || tag==F("\n") || tag==F("\r")) {
        delay(1);
        client.find("<");
        continue;
      } else if (tag.startsWith("/") || tag.endsWith("/")) { //If this is a closing tag or an empty tag, skip data reading
        if (tag==F("/METAR")) { //We finished an airport block! Update it.
          Serial.print(results);Serial.print(F(" Found: ")); Serial.print(id); Serial.print(F(" [")); Serial.print(cat); Serial.print(F("]"));
          for (int i = 0; i < NUM_AIRPORTS; i++) {
            delay(1);
            memcpy_P(buf, airports[i], 5);
            if (id == String(buf)) {
              updateAirportColor(i, wind, gusts, cat, wx);
              results--;
              raw.clear(); id.clear(); wind=NULL; gusts=NULL; cat.clear(); wx.clear(); //RESET variables for next airport
              break;
            }
          }
          client.find("<");
        } else if (tag==F("/response")) {
          tag.clear(); raw.clear(); id.clear(); wind=NULL; gusts=NULL; cat.clear(); wx.clear();
          client.stopAll();
          delay(NUM_AIRPORTS);ESP.wdtFeed();
          return results==0;
        }
        client.find("<");
        continue;
      } else if (tag==F("raw_text")) { raw = client.readStringUntil('<');
      } else if (tag==F("station_id")) { id = client.readStringUntil('<');
      } else if (tag==F("flight_category")) {cat = client.readStringUntil('<');
      } else if (tag==F("wind_speed_kt")) {wind = client.readStringUntil('<').toInt();
      } else if (tag==F("wind_gust_kt")) {gusts = client.readStringUntil('<').toInt();
      } else if (tag==F("wx_string")) {wx = client.readStringUntil('<');
      } else {client.find("<");}
      tag.clear();
      if (results==0){
        tag.clear(); raw.clear(); id.clear(); wind=NULL; gusts=NULL; cat.clear(); wx.clear();
        client.stopAll();
        delay(NUM_AIRPORTS);ESP.wdtFeed(); //Now's a good time to chillout, and feed the beast
        return results==0;
      }
    }
    tag.clear(); raw.clear(); id.clear(); wind=NULL; gusts=NULL; cat.clear(); wx.clear();
    client.stopAll();
    delay(NUM_AIRPORTS);ESP.wdtFeed(); //Now's a good time to chillout, and feed the beast
  }
  return results==0;
}

static void updateAirportColor(int ledIdx, int wind, int gusts, const String& condition, const String& wx) {
  CRGB color = CRGB::Black;
  if (condition == F("VFR")) {//IF it's VFR is it really or windy?
    if ((wind > WIND_THRESHOLD || gusts > WIND_THRESHOLD) && DO_WINDS) color = CRGB::LightGoldenrodYellow;
    else color = CRGB::Green;
  }
  else if (condition == F("LIFR")) color = CRGB::Magenta;
  else if (condition == F("IFR")) color = CRGB::OrangeRed;
  else if (condition == F("MVFR")) color = CRGB::Blue;
  ledColors[ledIdx] = color;//Set the strip color
  baseColors[ledIdx] = color;//Set the backup color
  delay(1);
  if (DO_LIGHTNING && wx.indexOf(F("TS")) != -1) {
    hasLightning[ledIdx] = true;lightningActive = true; flicker=true;//Tell the loop to start flickering
  }
}