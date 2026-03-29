#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <time.h>
#include <ArduinoJson.h>
#include "mbedtls/base64.h"

#define POT_PIN 34
#define TOUCH_PIN 4

U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C u8g2(U8G2_R0);

enum Screen { HOME, MENU, TIMER, STOPWATCH, ALARM, WEATHER, SPOTIFY, STATS };

void handlePot();
void handleTouch();
void drawUI();
void drawHome();
void drawMenu();
void drawTimer();
void drawStopwatch();
void drawAlarm();
void drawWeather();
void drawSpotify();
void drawStats();
void fetchWeather();
void selectOption();
void goBack();
void refreshAccessToken();
void fetchPlaybackInfo();

Screen currentScreen = HOME;

const char* menuItems[] = {"Timer","Stopwatch","Alarm","Weather","Spotify","Stats"};
int menuSize = 6;
int menuIndex = 0;
int menuOffset = 0;

unsigned long lastInteractionTime = 0;
unsigned long lastTapTime = 0;
int tapCount = 0;

unsigned long timerStartMillis = 0;
bool timerRunning = false;

unsigned long stopwatchStart = 0;
bool stopwatchRunning = false;

int alarmHour = 7;
int alarmMin = 0;

int lastPot = 0;
unsigned long lastPotMove = 0;

#define POT_SAMPLES 5
int potBuffer[POT_SAMPLES];
int potIndex = 0;

const char* ssid = "";
const char* password = "";

String apiKey = "";
String city = "Mumbai";

String weatherMain = "--";
float temperature = 0;

String spotifyAccessToken = "";
unsigned long tokenExpiresAt = 0;

const char* SPOTIFY_REFRESH_TOKEN = "";
const char* SPOTIFY_CLIENT_ID     = "";
const char* SPOTIFY_CLIENT_SECRET = "";

String currentSong = "";
String currentArtist = "";
int progressMs = 0;
int durationMs = 1;

unsigned long lastSpotifyUpdate = 0;
int scrollOffset = 0;
unsigned long lastScroll = 0;

bool isPlaying = false;
int currentVolume = 50;
String activeDeviceId = "";

String base64Encode(String input) {
  size_t len;
  unsigned char out[128];
  mbedtls_base64_encode(out, sizeof(out), &len,
                        (const unsigned char*)input.c_str(), input.length());
  return String((char*)out);
}

void refreshAccessToken() {
  if (millis() < tokenExpiresAt && spotifyAccessToken.length() > 0) return;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  http.begin(client, "https://accounts.spotify.com/api/token");
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");

  String auth = String(SPOTIFY_CLIENT_ID) + ":" + SPOTIFY_CLIENT_SECRET;
  http.addHeader("Authorization", "Basic " + base64Encode(auth));

  String body = "grant_type=refresh_token&refresh_token=" + String(SPOTIFY_REFRESH_TOKEN);

  if (http.POST(body) == 200) {
    DynamicJsonDocument doc(2048);
    deserializeJson(doc, http.getString());

    spotifyAccessToken = doc["access_token"].as<String>();
    int expiresIn = doc["expires_in"] | 3600;
    tokenExpiresAt = millis() + (expiresIn - 60) * 1000;
  }

  http.end();
}

void fetchPlaybackInfo() {
  refreshAccessToken();

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  http.begin(client, "https://api.spotify.com/v1/me/player");
  http.addHeader("Authorization", "Bearer " + spotifyAccessToken);

  if (http.GET() == 200) {
    DynamicJsonDocument doc(8192);
    deserializeJson(doc, http.getString());

    isPlaying = doc["is_playing"] | false;
    currentVolume = doc["device"]["volume_percent"] | 0;
    activeDeviceId = doc["device"]["id"] | "";
    currentSong = doc["item"]["name"] | "";
    currentArtist = doc["item"]["artists"][0]["name"] | "";
    progressMs = doc["progress_ms"] | 0;
    durationMs = doc["item"]["duration_ms"] | 1;
  }

  http.end();
}

void togglePlayPause(){
  if(spotifyAccessToken=="") return;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  String url = isPlaying ?
    "https://api.spotify.com/v1/me/player/pause" :
    "https://api.spotify.com/v1/me/player/play";

  http.begin(client, url);
  http.addHeader("Authorization", "Bearer " + spotifyAccessToken);
  http.PUT("");

  http.end();
}

void setVolume(int vol){
  if(spotifyAccessToken=="" || activeDeviceId=="") return;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  String url = "https://api.spotify.com/v1/me/player/volume?volume_percent=" 
                + String(vol) + "&device_id=" + activeDeviceId;

  http.begin(client, url);
  http.addHeader("Authorization", "Bearer " + spotifyAccessToken);
  http.PUT("");

  http.end();
}

void setup() {
  pinMode(POT_PIN, INPUT);
  pinMode(TOUCH_PIN, INPUT);

  u8g2.begin();

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(300);

  configTime(19800,0,"pool.ntp.org");
  fetchWeather();

  lastPot = analogRead(POT_PIN);
  for(int i=0;i<POT_SAMPLES;i++) potBuffer[i] = lastPot;
}

void loop() {
  handlePot();
  handleTouch();

  if (currentScreen == MENU && millis() - lastInteractionTime > 3000)
    currentScreen = HOME;

  drawUI();
  delay(20);
}

void handlePot() {
  int raw = analogRead(POT_PIN);

  potBuffer[potIndex] = raw;
  potIndex = (potIndex + 1) % POT_SAMPLES;

  int current = 0;
  for(int i=0;i<POT_SAMPLES;i++) current += potBuffer[i];
  current /= POT_SAMPLES;

  if (currentScreen == HOME) {
    if (abs(current - lastPot) > 250) {
      currentScreen = MENU;
      lastInteractionTime = millis();
      lastPot = current;
      return;
    }
  }

  if (currentScreen == MENU) {
    if(millis() - lastPotMove > 150){
      if (current > lastPot + 200) {
        if (menuIndex < menuSize - 1) menuIndex++;
      } else if (current < lastPot - 200) {
        if (menuIndex > 0) menuIndex--;
      }

      lastPot = current;
      lastInteractionTime = millis();
      lastPotMove = millis();
    }

    if (menuIndex >= menuOffset + 3)
      menuOffset = menuIndex - 2;

    if (menuIndex < menuOffset)
      menuOffset = menuIndex;
  }

  if(currentScreen == SPOTIFY){
    int vol = map(current, 0, 4095, 0, 100);
    if(abs(vol - currentVolume) > 5){
      currentVolume = vol;
      setVolume(currentVolume);
    }
  }
}

void handleTouch() {
  static bool lastState = LOW;
  bool state = digitalRead(TOUCH_PIN);

  if (state == HIGH && lastState == LOW) {
    tapCount++;
    lastTapTime = millis();
    lastInteractionTime = millis();
  }

  lastState = state;

  if (millis() - lastTapTime > 250 && tapCount > 0) {
    if (tapCount == 1) {
      if(currentScreen == SPOTIFY){
        togglePlayPause();
        isPlaying = !isPlaying;
      } else {
        selectOption();
      }
    } else if (tapCount == 2) {
      goBack();
    }
    tapCount = 0;
  }
}

void selectOption() {
  if (currentScreen == MENU) {
    if (menuIndex == 0) currentScreen = TIMER;
    if (menuIndex == 1) currentScreen = STOPWATCH;
    if (menuIndex == 2) currentScreen = ALARM;
    if (menuIndex == 3) currentScreen = WEATHER;
    if (menuIndex == 4) currentScreen = SPOTIFY;
    if (menuIndex == 5) currentScreen = STATS;
  } 
}

void goBack() {
  if (currentScreen != HOME && currentScreen != MENU)
    currentScreen = MENU;
  else if (currentScreen == MENU)
    currentScreen = HOME;
}

void drawUI() {
  u8g2.clearBuffer();

  switch(currentScreen){
    case HOME: drawHome(); break;
    case MENU: drawMenu(); break;
    case TIMER: drawTimer(); break;
    case STOPWATCH: drawStopwatch(); break;
    case ALARM: drawAlarm(); break;
    case WEATHER: drawWeather(); break;
    case SPOTIFY: drawSpotify(); break;
    case STATS: drawStats(); break;
  }

  u8g2.sendBuffer();
}

void drawHome() {
  struct tm t;
  if(getLocalTime(&t)){
    char buf[6];

    int hour = t.tm_hour;
    bool isPM = hour >= 12;
    if(hour > 12) hour -= 12;
    if(hour == 0) hour = 12;

    sprintf(buf,"%02d:%02d",hour,t.tm_min);

    u8g2.setFont(u8g2_font_logisoso16_tf);
    int w = u8g2.getStrWidth(buf);
    int x = (128 - w)/2;

    u8g2.drawStr(x, 32, buf);

    u8g2.setFont(u8g2_font_5x8_tr);
    u8g2.drawStr(x + w + 2, 32, isPM ? "PM" : "AM");
  }

  char topBuf[20];
  sprintf(topBuf,"%s - %.0fC",city.c_str(),temperature);

  u8g2.setFont(u8g2_font_5x8_tr);
  u8g2.drawStr(2,8,topBuf);
}

void drawMenu() {
  u8g2.setFont(u8g2_font_6x10_tr);

  for(int i=0;i<3;i++){
    int item = i + menuOffset;
    if(item >= menuSize) break;

    int y = 10 + i*10;
    int x = (128 - u8g2.getStrWidth(menuItems[item]))/2;

    if(item == menuIndex){
      u8g2.drawBox(10,y-9,108,11);
      u8g2.setDrawColor(0);
      u8g2.drawStr(x,y,menuItems[item]);
      u8g2.setDrawColor(1);
    } else {
      u8g2.drawStr(x,y,menuItems[item]);
    }
  }
}

void drawTimer(){
  u8g2.setFont(u8g2_font_5x8_tr);
  u8g2.drawStr(2,8,"TIMER");

  u8g2.drawHLine(0,10,128);

  unsigned long t = timerRunning ? (millis()-timerStartMillis)/1000 : 0;

  char buf[12];
  sprintf(buf,"%02lu:%02lu", t/60, t%60);

  u8g2.setFont(u8g2_font_logisoso16_tf);

  int w = u8g2.getStrWidth(buf);
  u8g2.drawStr((128-w)/2, 32, buf);
}

void drawStopwatch(){
  u8g2.setFont(u8g2_font_5x8_tr);
  u8g2.drawStr(2,8,"STOPWATCH");

  u8g2.drawHLine(0,10,128);

  unsigned long t = stopwatchRunning ? (millis()-stopwatchStart)/1000 : 0;

  char buf[12];
  sprintf(buf,"%02lu:%02lu", t/60, t%60);

  u8g2.setFont(u8g2_font_logisoso16_tf);

  int w = u8g2.getStrWidth(buf);
  u8g2.drawStr((128-w)/2, 32, buf);
}

void drawAlarm(){
  u8g2.setFont(u8g2_font_5x8_tr);
  u8g2.drawStr(2,8,"ALARM");

  u8g2.drawHLine(0,10,128);

  char buf[10];
  sprintf(buf,"%02d:%02d",alarmHour,alarmMin);

  u8g2.setFont(u8g2_font_logisoso16_tf);

  int w = u8g2.getStrWidth(buf);
  u8g2.drawStr((128-w)/2, 32, buf);

  u8g2.setFont(u8g2_font_5x8_tr);
  u8g2.drawStr(90,8,"ON");
}

void drawWeather(){
  u8g2.setFont(u8g2_font_5x8_tr);
  u8g2.drawStr(2,8,"WEATHER");

  u8g2.drawHLine(0,10,128);

  char tempBuf[10];
  sprintf(tempBuf,"%.0fC",temperature);

  u8g2.setFont(u8g2_font_logisoso16_tf);

  int w = u8g2.getStrWidth(tempBuf);
  u8g2.drawStr((128-w)/2, 32, tempBuf);

  u8g2.setFont(u8g2_font_5x8_tr);
  u8g2.drawStr(2,20, weatherMain.c_str());
}

void drawSpotify(){
  if(millis()-lastSpotifyUpdate>2000){
    fetchPlaybackInfo();
    lastSpotifyUpdate=millis();
  }

  u8g2.setFont(u8g2_font_5x8_tr);
  u8g2.drawStr(2,8,"SPOTIFY");
  u8g2.drawHLine(0,10,128);
  u8g2.drawStr(90,8,isPlaying ? "PLAY" : "PAUSE");

  int vh = map(currentVolume,0,100,0,20);
  u8g2.drawFrame(122,11,5,21);
  u8g2.drawBox(123,11+(20-vh),3,vh);

  String line = currentSong + " - " + currentArtist;
  int textWidth = u8g2.getStrWidth(line.c_str());

  u8g2.setClipWindow(0,11,116,22);
  if (textWidth <= 114) {
    u8g2.drawStr((114-textWidth)/2, 20, line.c_str());
  } else {
    if (millis() - lastScroll > 40) {
      scrollOffset++;
      lastScroll = millis();
    }
    int gap = 30;
    int cycle = textWidth + gap;
    if (scrollOffset >= cycle) scrollOffset = 0;
    u8g2.drawStr(-scrollOffset, 20, line.c_str());
    u8g2.drawStr(-scrollOffset + cycle, 20, line.c_str());
  }
  u8g2.setMaxClipWindow();

  int pw = map(progressMs,0,durationMs,0,114);
  u8g2.drawFrame(1,29,114,3);
  u8g2.drawBox(2,30,pw,1);
}

void drawStats(){
  u8g2.setFont(u8g2_font_5x8_tr);
  u8g2.drawStr(2,8,"SYSTEM");
  u8g2.drawHLine(0,10,128);

  char buf[30];

  sprintf(buf,"WiFi:%d dBm",WiFi.RSSI());
  u8g2.drawStr(2,18,buf);

  sprintf(buf,"RAM:%d KB",ESP.getFreeHeap()/1024);
  u8g2.drawStr(2,26,buf);

  sprintf(buf,"Uptime:%lus",millis()/1000);
  u8g2.drawStr(70,26,buf);

  sprintf(buf,"CPU:%d MHz",getCpuFrequencyMhz());
  u8g2.drawStr(70,18,buf);
}

void fetchWeather(){
  if(WiFi.status()==WL_CONNECTED){
    HTTPClient http;
    String url="http://api.openweathermap.org/data/2.5/weather?q="+city+"&appid="+apiKey+"&units=metric";
    http.begin(url);
    http.GET();
    String p=http.getString();

    int t=p.indexOf("\"temp\":");
    int w=p.indexOf("\"main\":\"");

    if(t!=-1) temperature=p.substring(t+7).toFloat();
    if(w!=-1){
      weatherMain=p.substring(w+8);
      weatherMain=weatherMain.substring(0,weatherMain.indexOf("\""));
    }
    http.end();
  }
}
