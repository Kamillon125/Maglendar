#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <ArduinoJson.h>
#include <GxEPD2_BW.h>
#include <Adafruit_GFX.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>

// --- Pin definitions ---
#define CS   10
#define DC   9
#define RST  8
#define BUSY 46
#define SCK  12
#define MOSI 11
#define MISO 13
#define SD_CS 21
#define SD_POWER_PIN 41
#define TOUCH1_PIN 4
#define TOUCH2_PIN 5
#define TOUCH3_PIN 6
#define TOUCH_THRESHOLD 30000

// --- e-ink display ---
GxEPD2_BW<GxEPD2_420_GDEY042T81, GxEPD2_420_GDEY042T81::HEIGHT> display(
  GxEPD2_420_GDEY042T81(CS, DC, RST, BUSY)
);

// --- RTC memory ---
RTC_DATA_ATTR int day = 1;
RTC_DATA_ATTR int monthIndex = 0;
RTC_DATA_ATTR int weekdayIndex = 3;

const char* months[] = {
  "Styczen","Luty","Marzec","Kwiecien","Maj","Czerwiec",
  "Lipiec","Sierpien","Wrzesien","Pazdziernik","Listopad","Grudzien"
};
const char* weekdays[] = {
  "Poniedzialek","Wtorek","Sroda","Czwartek",
  "Piatek","Sobota","Niedziela"
};
int daysInMonth[] = {31,28,31,30,31,30,31,31,30,31,30,31};

// --- JSON document ---
DynamicJsonDocument eventsDoc(4096);

// --- Helper to center text ---
void printCenteredX(const char* text, int y) {
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  int16_t x = (display.width() - w) / 2 - x1;
  display.setCursor(x, y);
  display.print(text);
}

// --- Auto-scalable event ---
void printScalableEvent(const char* text, int y) {
  int16_t x1, y1;
  uint16_t w, h;
  display.setFont(&FreeSansBold18pt7b);
  display.setTextSize(1);
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  if (w > 380) {
    display.setFont(&FreeSansBold12pt7b);
    display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    if (w > 380) display.setFont(&FreeSansBold9pt7b);
  }
  printCenteredX(text, y);
}

// --- Date increment ---
void incrementDate() {
  day++;
  weekdayIndex = (weekdayIndex + 1) % 7;
  if (day > daysInMonth[monthIndex]) {
    day = 1;
    monthIndex++;
    if (monthIndex > 11) monthIndex = 0;
  }
}

// --- Load events from SD ---
void loadEvents() {
  if (!SD.begin(SD_CS)) {
    Serial.println("SD begin failed!");
    return;
  }
  File file = SD.open("/events.json");
  if (!file) return;
  deserializeJson(eventsDoc, file);
  file.close();
}

// --- Upcoming events ---
struct UpcomingEvent { String text; int daysAway; };

int getNextEvents(UpcomingEvent* events, int maxCount) {
  int count = 0;
  int tempDay = day;
  int tempMonth = monthIndex;
  for (int i = 1; i <= 60; i++) {
    if (count >= maxCount) break;
    tempDay++;
    if (tempDay > daysInMonth[tempMonth]) {
      tempDay = 1;
      tempMonth++;
      if (tempMonth > 11) tempMonth = 0;
    }
    char dateKey[6];
    sprintf(dateKey, "%02d-%02d", tempMonth + 1, tempDay);
    if (eventsDoc.containsKey(dateKey)) {
      JsonArray arr = eventsDoc[dateKey].as<JsonArray>();
      for (JsonVariant v : arr) {
        if (count < maxCount) {
          events[count].text = v.as<String>();
          events[count].daysAway = i;
          count++;
        }
      }
    }
  }
  return count;
}

void setup() {
  Serial.begin(115200);
  SPI.begin(SCK, MISO, MOSI, -1);

  // --- SD power pin setup ---
  pinMode(SD_POWER_PIN, OUTPUT);
  digitalWrite(SD_POWER_PIN, HIGH); // LOW → SD ON
  delay(10); // allow SD module to power up

  display.init(115200, true, 2, false);
  display.setRotation(1);

  loadEvents(); // SD.begin() happens here

  char dateStr[10];
  sprintf(dateStr, "%02d.%02d", day, monthIndex + 1);

  char currentEventKey[6];
  sprintf(currentEventKey, "%02d-%02d", monthIndex + 1, day);
  String todayEvent = "";
  if (eventsDoc.containsKey(currentEventKey)) todayEvent = eventsDoc[currentEventKey][0].as<String>();

  UpcomingEvent nextEvents[3];
  int eventsFound = getNextEvents(nextEvents, 3);

  // --- Draw display ---
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);

    display.setFont(&FreeSansBold24pt7b);
    display.setTextSize(1);
    printCenteredX(weekdays[weekdayIndex], 38);
    display.drawLine(20, 48, 380, 48, GxEPD_BLACK);

    display.setFont(&FreeSansBold24pt7b);
    display.setTextSize(2);
    printCenteredX(dateStr, 125);

    if (todayEvent.length() > 0) printScalableEvent(todayEvent.c_str(), 200);

    if (eventsFound > 0) {
      display.drawLine(20, 230, 380, 230, GxEPD_BLACK);
      display.setFont(&FreeSansBold9pt7b);
      display.setTextSize(1);
      int footerY = 252;
      for (int i = 0; i < eventsFound; i++) {
        char footerBuffer[64];
        if (nextEvents[i].daysAway == 1) sprintf(footerBuffer, "Jutro: %s", nextEvents[i].text.c_str());
        else sprintf(footerBuffer, "Za %d dni %s", nextEvents[i].daysAway, nextEvents[i].text.c_str());
        printCenteredX(footerBuffer, footerY);
        footerY += 20;
      }
    }
  } while (display.nextPage());

  delay(500);
  incrementDate();

  // --- Cut SD power before deep sleep ---
  digitalWrite(SD_POWER_PIN, LOW); // HIGH → SD OFF

  touchSleepWakeUpEnable(TOUCH1_PIN, TOUCH_THRESHOLD);
  esp_deep_sleep_start();
}

void loop() {
  // Not used
}