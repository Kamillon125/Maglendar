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

// NOTE: Ensure your specific ESP32 board supports Touch on these pins. 
// Standard ESP32-WROOM does not have Touch on GPIO 5 or 6. 
// ESP32-S2/S3 might.
#define TOUCH1_PIN 4
#define TOUCH2_PIN 5
#define TOUCH3_PIN 6
#define TOUCH_THRESHOLD 30000 

// --- Sleep Configuration ---
#define uS_TO_S_FACTOR 1000000ULL  
#define TIME_TO_SLEEP  24 * 60 * 60  // 24 Hours

// --- e-ink display ---
GxEPD2_BW<GxEPD2_420_GDEY042T81, GxEPD2_420_GDEY042T81::HEIGHT> display(
  GxEPD2_420_GDEY042T81(CS, DC, RST, BUSY)
);

// --- RTC memory (Preserved during Deep Sleep) ---
RTC_DATA_ATTR int day = 1;
RTC_DATA_ATTR int monthIndex = 0;
RTC_DATA_ATTR int weekdayIndex = 3;
RTC_DATA_ATTR bool isCalendarMode = true; // Default to Calendar view

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

// --- Date Math ---
void incrementDate() {
  day++;
  weekdayIndex = (weekdayIndex + 1) % 7;
  if (day > daysInMonth[monthIndex]) {
    day = 1;
    monthIndex++;
    if (monthIndex > 11) monthIndex = 0;
  }
}

void decrementDate() {
  day--;
  weekdayIndex--;
  if (weekdayIndex < 0) weekdayIndex = 6;
  
  if (day < 1) {
    monthIndex--;
    if (monthIndex < 0) monthIndex = 11;
    day = daysInMonth[monthIndex];
  }
}

// --- SD Card Operations ---
void loadEvents() {
  if (!SD.begin(SD_CS)) return;
  File file = SD.open("/events.json");
  if (!file) return;
  deserializeJson(eventsDoc, file);
  file.close();

  // If this is a cold boot (not sleep wake), try to load saved date
  // (We check wake cause in setup, so here we just load if available)
  if (eventsDoc.containsKey("current_date")) {
    JsonObject dateObj = eventsDoc["current_date"];
    day = dateObj["day"];
    monthIndex = dateObj["month"];
    weekdayIndex = dateObj["weekday"];
  }
}

void saveDateToSD() {
  JsonObject dateObj = eventsDoc["current_date"];
  if (dateObj.isNull()) dateObj = eventsDoc.createNestedObject("current_date");
  
  dateObj["day"] = day;
  dateObj["month"] = monthIndex;
  dateObj["weekday"] = weekdayIndex;

  SD.remove("/events.json"); 
  File file = SD.open("/events.json", FILE_WRITE);
  if (file) {
    serializeJson(eventsDoc, file);
    file.close();
  }
}

// --- Upcoming events structure ---
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

// --- Drawing Logic ---

void drawCalendarMode(String todayEvent, UpcomingEvent* nextEvents, int eventsFound) {
  char dateStr[10];
  sprintf(dateStr, "%02d.%02d", day, monthIndex + 1);

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
}

void drawEventMode(String todayEvent, UpcomingEvent* nextEvents, int eventsFound) {
  display.setFont(&FreeSansBold18pt7b);
  display.setTextSize(1);
  printCenteredX("Nadchodzace", 35);
  display.drawLine(20, 45, 380, 45, GxEPD_BLACK);

  display.setFont(&FreeSansBold9pt7b);
  int y = 70;

  // 1. Draw Today's event if exists
  if (todayEvent.length() > 0) {
    char buf[100];
    sprintf(buf, "DZIS: %s", todayEvent.c_str());
    printCenteredX(buf, y);
    y += 25;
  } else {
    printCenteredX("Dzis: Brak wydarzen", y);
    y += 25;
  }
  
  display.drawLine(100, y - 10, 300, y - 10, GxEPD_BLACK); // Separator
  y += 15;

  // 2. Draw next events
  for (int i = 0; i < eventsFound; i++) {
    char buf[100];
    if (nextEvents[i].daysAway == 1) sprintf(buf, "Jutro: %s", nextEvents[i].text.c_str());
    else sprintf(buf, "+%d dni: %s", nextEvents[i].daysAway, nextEvents[i].text.c_str());
    
    printCenteredX(buf, y);
    y += 25;
    if(y > 290) break; // Don't overflow screen
  }
}

void setup() {
  Serial.begin(115200);
  SPI.begin(SCK, MISO, MOSI, -1);

  // 1. Initialize SD Power
  pinMode(SD_POWER_PIN, OUTPUT);
  digitalWrite(SD_POWER_PIN, HIGH); // SD ON
  delay(20); 

  display.init(115200, true, 2, false);
  display.setRotation(1);

  // 2. Load Data (Always need events loaded to display)
  loadEvents(); 

  // 3. Determine Wakeup Cause and Update State
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  bool stateChanged = false;

  if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
    // 24H passed -> Increment
    incrementDate();
    stateChanged = true;
  } 
  else if (wakeup_reason == ESP_SLEEP_WAKEUP_TOUCHPAD) {
    // Touch Wakeup - Check which pin
    // We use touchRead because it's simple. 
    // We check all 3. If multiple are pressed, we prioritize T1 > T2 > T3
    
    if (touchRead(TOUCH1_PIN) < TOUCH_THRESHOLD) {
      // Toggle Mode
      isCalendarMode = !isCalendarMode;
      // No date change
    } 
    else if (touchRead(TOUCH2_PIN) < TOUCH_THRESHOLD) {
      // Manual Increment
      incrementDate();
      stateChanged = true;
    } 
    else if (touchRead(TOUCH3_PIN) < TOUCH_THRESHOLD) {
      // Manual Decrement
      decrementDate();
      stateChanged = true;
    }
  }
  // Else: Cold boot (Reset button or Battery insert) -> Do nothing, just use loaded date from SD

  // 4. Save if Date Changed
  if (stateChanged) {
    saveDateToSD();
  }

  // 5. Prepare Data for Display
  char currentEventKey[6];
  sprintf(currentEventKey, "%02d-%02d", monthIndex + 1, day);
  String todayEvent = "";
  if (eventsDoc.containsKey(currentEventKey)) todayEvent = eventsDoc[currentEventKey][0].as<String>();

  // Get more events for Event Mode (max 6), fewer for Calendar Mode (max 3)
  int maxEvts = isCalendarMode ? 3 : 6;
  UpcomingEvent nextEvents[6];
  int eventsFound = getNextEvents(nextEvents, maxEvts);

  // 6. Draw Display
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);

    if (isCalendarMode) {
      drawCalendarMode(todayEvent, nextEvents, eventsFound);
    } else {
      drawEventMode(todayEvent, nextEvents, eventsFound);
    }

  } while (display.nextPage());

  // 7. Shutdown
  delay(100); 
  digitalWrite(SD_POWER_PIN, LOW); // SD OFF

  // Enable Wakeups
  touchSleepWakeUpEnable(TOUCH1_PIN, TOUCH_THRESHOLD);
  touchSleepWakeUpEnable(TOUCH2_PIN, TOUCH_THRESHOLD);
  touchSleepWakeUpEnable(TOUCH3_PIN, TOUCH_THRESHOLD);
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);

  esp_deep_sleep_start();
}

void loop() {
  // Not used
}