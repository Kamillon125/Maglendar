#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <ArduinoJson.h>
#include <GxEPD2_BW.h>
#include <Adafruit_GFX.h>
// Include 9pt for footer and fallback scaling
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>

// --- e-ink wiring ---
#define CS   10
#define DC   9
#define RST  8
#define BUSY 7
#define SCK  12
#define MOSI 11
#define MISO 13  
#define SD_CS 4  

// --- touch pin ---
#define TOUCH_PIN 3
#define TOUCH_THRESHOLD 30000

GxEPD2_BW<GxEPD2_420_GDEY042T81, GxEPD2_420_GDEY042T81::HEIGHT> display(
  GxEPD2_420_GDEY042T81(CS, DC, RST, BUSY)
);

// === Store date in RTC memory ===
RTC_DATA_ATTR int day = 1;
RTC_DATA_ATTR int monthIndex = 0;
RTC_DATA_ATTR int weekdayIndex = 3;

const char* months[] = {
  "Styczen", "Luty", "Marzec", "Kwiecien", "Maj", "Czerwiec",
  "Lipiec", "Sierpien", "Wrzesien", "Pazdziernik", "Listopad", "Grudzien"
};

const char* weekdays[] = {
  "Poniedzialek", "Wtorek", "Sroda", "Czwartek",
  "Piatek", "Sobota", "Niedziela"
};

int daysInMonth[] = {31,28,31,30,31,30,31,31,30,31,30,31};

// --- JSON document ---
DynamicJsonDocument eventsDoc(4096); 

// --- Helper to center text with X offset calculation ---
void printCenteredX(const char* text, int y) {
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  int16_t x = (display.width() - w) / 2 - x1;
  display.setCursor(x, y);
  display.print(text);
}

// --- Helper: Print Event with Auto-Scaling (Request #2) ---
void printScalableEvent(const char* text, int y) {
  int16_t x1, y1;
  uint16_t w, h;
  
  // Try Largest Font (18pt)
  display.setFont(&FreeSansBold18pt7b);
  display.setTextSize(1);
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);

  // If width > 380 (screen is 400), downgrade
  if (w > 380) {
    // Try Medium Font (12pt)
    display.setFont(&FreeSansBold12pt7b);
    display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    
    if (w > 380) {
      // Try Smallest Font (9pt)
      display.setFont(&FreeSansBold9pt7b);
    }
  }

  // Draw with whichever font is now set
  printCenteredX(text, y);
}

void incrementDate() {
  day++;
  weekdayIndex = (weekdayIndex + 1) % 7;
  if (day > daysInMonth[monthIndex]) {
    day = 1;
    monthIndex++;
    if (monthIndex > 11) monthIndex = 0;
  }
}

void loadEvents() {
  if (!SD.begin(SD_CS)) return;
  File file = SD.open("/events.json");
  if (!file) return;
  deserializeJson(eventsDoc, file);
  file.close();
}

// --- Struct for upcoming events ---
struct UpcomingEvent {
  String text;
  int daysAway;
};

// --- Find next 3 events (Request #3) ---
// Returns number of events found (up to 3)
int getNextEvents(UpcomingEvent* events, int maxCount) {
  int count = 0;
  int tempDay = day;
  int tempMonth = monthIndex;
  
  // Look ahead 60 days
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
       // Add all events found on this day (up to maxCount total)
       for(JsonVariant v : arr) {
          if(count < maxCount) {
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

  display.init(115200, true, 2, false);
  display.setRotation(1);
  
  loadEvents();

  // 1. Prepare Data
  char dateStr[10];
  sprintf(dateStr, "%02d.%02d", day, monthIndex + 1);

  char currentEventKey[6];
  sprintf(currentEventKey, "%02d-%02d", monthIndex + 1, day);
  String todayEvent = "";
  if (eventsDoc.containsKey(currentEventKey)) {
    todayEvent = eventsDoc[currentEventKey][0].as<String>();
  }

  UpcomingEvent nextEvents[3];
  int eventsFound = getNextEvents(nextEvents, 3);

  // 2. Draw Screen
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);

    // --- TOP SECTION ---
    // Weekday: Y=38
    display.setFont(&FreeSansBold24pt7b);
    display.setTextSize(1);
    printCenteredX(weekdays[weekdayIndex], 38);

    // Top Divider: Y=48
    display.drawLine(20, 48, 380, 48, GxEPD_BLACK);

    // --- MIDDLE SECTION (Symmetric Spacing) ---
    // Date: Y=125 (Huge)
    display.setFont(&FreeSansBold24pt7b); 
    display.setTextSize(2); 
    printCenteredX(dateStr, 125);

    // Current Event: Y=200
    // Note: Top divider is at 48. Date roughly starts at 60. Gap ~12px.
    // Bottom divider is at 230. Event (18pt) ends roughly 210. Gap ~20px.
    // This provides the requested visual symmetry.
    if (todayEvent.length() > 0) {
      printScalableEvent(todayEvent.c_str(), 200);
    }

    // --- BOTTOM SECTION ---
    if (eventsFound > 0) {
      // Bottom Divider: Y=230
      display.drawLine(20, 230, 380, 230, GxEPD_BLACK);
      
      // Footer Lines (Using 9pt to fit 3 lines comfortably)
      display.setFont(&FreeSansBold9pt7b);
      display.setTextSize(1);
      
      int footerY = 252; // Start Y position
      
      for(int i=0; i<eventsFound; i++) {
        char footerBuffer[64];
        if(nextEvents[i].daysAway == 1) {
          sprintf(footerBuffer, "Jutro: %s", nextEvents[i].text.c_str());
        } else {
          sprintf(footerBuffer, "Za %d dni %s", nextEvents[i].daysAway, nextEvents[i].text.c_str());
        }
        printCenteredX(footerBuffer, footerY);
        footerY += 20; // Increment for next line
      }
    }

  } while (display.nextPage());

  delay(500);
  incrementDate();
  touchSleepWakeUpEnable(TOUCH_PIN, TOUCH_THRESHOLD);
  esp_deep_sleep_start();
}

void loop() {
  // Not used
}