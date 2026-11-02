/*
  DAYFRAME - "Your day, in your frame."
  Desk planner firmware, v0.1 demo (hard-coded sample data)

  Board : Seeed XIAO ESP32-S3  (also runs in Wokwi on an ESP32-S3 DevKitC-1)
  Screen: 2.8" ILI9341 SPI TFT, used as 320x240 landscape
  Input : 3 buttons -> BTN1 = Prev, BTN2 = Next, BTN3 = Select
  Libs  : Adafruit GFX Library, Adafruit ILI9341

  Controls
    Today page (home)     : Prev opens the Calendar, Next opens the To-do list
    To-do page            : Prev / Next move the highlight, Select ticks a task
    Calendar page         : Prev / Next change the selected day, Select jumps back to today
    Hold Select (0.6 s)   : go back to the Today page from anywhere

  Wi-Fi sync, real time (NTP) and battery icon are planned next steps.
*/

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>

// ---------------------------------------------------------------- pins ----
// GPIO numbers are used so the same sketch works on the XIAO and in Wokwi.
// (XIAO label in the comment.)
#define TFT_CS    4    // D3
#define TFT_DC    5    // D4
#define TFT_RST   6    // D5
#define TFT_BL    43   // D6  backlight (simple on/off)
#define TFT_SCK   7    // D8
#define TFT_MOSI  9    // D10
#define TFT_MISO  -1   // D9  not connected (we only write to the screen)

#define BTN1_PIN  1    // D0  Prev
#define BTN2_PIN  2    // D1  Next
#define BTN3_PIN  3    // D2  Select

#define DEBOUNCE_MS 30
#define LONG_MS     600

// -------------------------------------------------------------- colours ----
static constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
const uint16_t C_BG    = rgb565(250, 243, 232);  // warm cream
const uint16_t C_CARD  = rgb565(255, 252, 247);  // card white
const uint16_t C_PINK  = rgb565(240, 205, 210);  // dusty pink
const uint16_t C_ROSE  = rgb565(201, 125, 143);  // muted rose
const uint16_t C_BERRY = rgb565(136, 48, 92);    // berry
const uint16_t C_LILAC = rgb565(214, 200, 236);  // soft lilac
const uint16_t C_INK   = rgb565(74, 44, 62);     // text

// ----------------------------------------------------------- sample data ----
struct Task  { const char* name; bool done; };
struct Event { const char* title; uint8_t day; uint8_t hour; uint8_t minute; };

Task tasks[] = {
  { "Finish DAYFRAME README", false },
  { "Submit design", false },
  { "Drink water", true },
  { "Stretch break", false },
  { "Plan tomorrow", false },
};
const int TASK_COUNT = sizeof(tasks) / sizeof(tasks[0]);   // keep at 5 or fewer

// day = day of the month (November 2026). Up to 4 events per day are shown.
const Event events[] = {
  { "Design review", 2,  10,  0 },
  { "Lunch",         2,  12, 30 },
  { "Study group",   2,  15,  0 },
  { "Evening walk",  2,  18, 30 },
  { "Math quiz",     5,   9,  0 },
  { "Club meeting",  5,  16,  0 },
  { "Dentist",       9,  11,  0 },
  { "Family dinner", 14, 19,  0 },
  { "Project due",   20, 12,  0 },
  { "Movie night",   20, 20,  0 },
  { "Birthday!",     27, 18,  0 },
};
const int EVENT_COUNT = sizeof(events) / sizeof(events[0]);

const char* quotes[] = {
  "One small step at a time.",
  "Soft progress still counts.",
  "Begin where you are.",
};

// Fixed demo date: Monday 2 November 2026
const char* DATE_TEXT      = "Mon 2 Nov";
const char* MONTH_TEXT     = "November 2026";
const int   TODAY_DAY      = 2;
const int   FIRST_WEEKDAY  = 6;    // 1 Nov 2026 is a Sunday (0 = Mon ... 6 = Sun)
const int   DAYS_IN_MONTH  = 30;
const int   START_MINUTES  = 9 * 60 + 41;   // demo clock starts at 09:41

// ------------------------------------------------------------- hardware ----
Adafruit_ILI9341 tft(&SPI, TFT_DC, TFT_CS, TFT_RST);
GFXcanvas16 canvas(320, 240);   // off-screen buffer (~150 KB) -> no flicker

// --------------------------------------------------------------- buttons ----
struct Btn {
  uint8_t  pin;
  bool     raw;
  bool     pressed;
  bool     longFired;
  uint32_t changedAt;
  uint32_t pressedAt;
};
Btn btns[3] = {
  { BTN1_PIN, false, false, false, 0, 0 },
  { BTN2_PIN, false, false, false, 0, 0 },
  { BTN3_PIN, false, false, false, 0, 0 },
};

// i = 0..2 (button 1..3). Takes a plain int on purpose: Arduino auto-generates
// function prototypes above the struct definitions, so no custom types in signatures.
// returns 0 = nothing, 1 = short press (on release), 2 = long press
int pollButton(int i) {
  Btn& b = btns[i];
  bool r = (digitalRead(b.pin) == LOW);     // pressed = LOW (pull-up)
  uint32_t now = millis();
  if (r != b.raw) { b.raw = r; b.changedAt = now; }
  if (now - b.changedAt >= DEBOUNCE_MS && b.raw != b.pressed) {
    b.pressed = b.raw;
    if (b.pressed) { b.pressedAt = now; b.longFired = false; }
    else if (!b.longFired) return 1;
  }
  if (b.pressed && !b.longFired && now - b.pressedAt >= LONG_MS) {
    b.longFired = true;
    return 2;
  }
  return 0;
}

// ----------------------------------------------------------------- state ----
enum Page { PAGE_TODAY, PAGE_TODO, PAGE_CALENDAR, PAGE_COUNT };
Page page = PAGE_TODAY;
int  todoSel = 0;
int  calSel = TODAY_DAY;   // day highlighted on the Calendar page
bool dirty = true;
int  lastMinute = -1;

int nowMinutes() {
  return (START_MINUTES + (int)(millis() / 60000UL)) % (24 * 60);
}

int doneCount() {
  int n = 0;
  for (int i = 0; i < TASK_COUNT; i++) if (tasks[i].done) n++;
  return n;
}

// index of the next event today, or -1 if none are left
int nextEventIndex(int minutesNow) {
  for (int i = 0; i < EVENT_COUNT; i++) {
    if (events[i].day != TODAY_DAY) continue;
    if (events[i].hour * 60 + events[i].minute >= minutesNow) return i;
  }
  return -1;
}

// how many events are planned on a given day
int eventCountOnDay(int day) {
  int n = 0;
  for (int i = 0; i < EVENT_COUNT; i++) if (events[i].day == day) n++;
  return n;
}

// index (into events[]) of the n-th event on a given day, or -1
int nthEventOnDay(int day, int n) {
  int seen = 0;
  for (int i = 0; i < EVENT_COUNT; i++) {
    if (events[i].day != day) continue;
    if (seen == n) return i;
    seen++;
  }
  return -1;
}

// weekday name for a day of the month
const char* weekdayName(int day) {
  static const char* names[] = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };
  return names[(FIRST_WEEKDAY + day - 1) % 7];
}

// --------------------------------------------------------- drawing helpers ----
void putText(const GFXfont* f, int x, int y, uint16_t color, const char* s) {
  canvas.setFont(f);
  canvas.setTextColor(color);
  canvas.setCursor(x, y);
  canvas.print(s);
}

void putTextRight(const GFXfont* f, int xRight, int y, uint16_t color, const char* s) {
  int16_t x1, y1; uint16_t w, h;
  canvas.setFont(f);
  canvas.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  putText(f, xRight - x1 - (int)w, y, color, s);
}

void putTextCentered(const GFXfont* f, int xCenter, int y, uint16_t color, const char* s) {
  int16_t x1, y1; uint16_t w, h;
  canvas.setFont(f);
  canvas.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  putText(f, xCenter - x1 - (int)w / 2, y, color, s);
}

// The one reusable "rounded card" style used on every page.
void drawCard(int x, int y, int w, int h, uint16_t fill) {
  canvas.fillRoundRect(x, y, w, h, 10, fill);
}

void drawCheckbox(int x, int y, int size, bool done) {
  if (done) {
    canvas.fillRoundRect(x, y, size, size, 3, C_BERRY);
  } else {
    canvas.drawRoundRect(x, y, size, size, 3, C_BERRY);
  }
}

// ----------------------------------------------------------------- pages ----
void drawToday() {
  char buf[48];
  int m = nowMinutes();
  canvas.fillScreen(C_BG);

  // header: date left, clock right
  putText(&FreeSansBold12pt7b, 12, 26, C_INK, DATE_TEXT);
  snprintf(buf, sizeof(buf), "%02d:%02d", m / 60, m % 60);
  putTextRight(&FreeSansBold12pt7b, 308, 26, C_BERRY, buf);

  // NEXT UP card
  drawCard(12, 36, 296, 52, C_PINK);
  putText(&FreeSans9pt7b, 24, 54, C_INK, "NEXT UP");
  int ei = nextEventIndex(m);
  if (ei >= 0) {
    snprintf(buf, sizeof(buf), "%02d:%02d  %s", events[ei].hour, events[ei].minute, events[ei].title);
    putText(&FreeSansBold9pt7b, 24, 78, C_BERRY, buf);
  } else {
    putText(&FreeSansBold9pt7b, 24, 78, C_BERRY, "Nothing left today");
  }

  // PRIORITIES card: first three tasks that are not done yet
  drawCard(12, 96, 296, 80, C_CARD);
  putText(&FreeSans9pt7b, 24, 114, C_ROSE, "PRIORITIES");
  int row = 0;
  for (int i = 0; i < TASK_COUNT && row < 3; i++) {
    if (tasks[i].done) continue;
    int y = 134 + row * 19;
    drawCheckbox(24, y - 10, 11, false);
    putText(&FreeSans9pt7b, 42, y, C_INK, tasks[i].name);
    row++;
  }
  if (row == 0) putText(&FreeSansBold9pt7b, 24, 140, C_BERRY, "All done - nice work!");

  // PROGRESS card: counter, bar, quote
  drawCard(12, 182, 296, 54, C_CARD);
  int done = doneCount();
  snprintf(buf, sizeof(buf), "%d/%d done", done, TASK_COUNT);
  putText(&FreeSansBold9pt7b, 24, 200, C_INK, buf);
  canvas.fillRoundRect(24, 206, 272, 8, 4, C_LILAC);
  if (done > 0) {
    int w = 272 * done / TASK_COUNT;
    if (w < 8) w = 8;
    canvas.fillRoundRect(24, 206, w, 8, 4, C_ROSE);
  }
  putText(&FreeSans9pt7b, 24, 229, C_ROSE, quotes[TODAY_DAY % 3]);
}

void drawTodo() {
  char buf[24];
  canvas.fillScreen(C_BG);
  putText(&FreeSansBold12pt7b, 12, 26, C_INK, "To-do");
  snprintf(buf, sizeof(buf), "%d/%d", doneCount(), TASK_COUNT);
  putTextRight(&FreeSansBold12pt7b, 308, 26, C_BERRY, buf);

  for (int i = 0; i < TASK_COUNT; i++) {
    int y = 40 + i * 36;
    drawCard(12, y, 296, 32, (i == todoSel) ? C_LILAC : C_CARD);
    drawCheckbox(24, y + 8, 16, tasks[i].done);
    putText(&FreeSans9pt7b, 50, y + 22, tasks[i].done ? C_ROSE : C_INK, tasks[i].name);
  }
}

void drawCalendar() {
  char buf[24];
  canvas.fillScreen(C_BG);
  putText(&FreeSansBold12pt7b, 12, 26, C_INK, MONTH_TEXT);

  // month grid on the left: 7 columns x 22 px
  const char* wd = "MTWTFSS";
  for (int c = 0; c < 7; c++) {
    char s[2] = { wd[c], 0 };
    putTextCentered(&FreeSans9pt7b, 12 + c * 22 + 11, 50, C_ROSE, s);
  }
  for (int d = 1; d <= DAYS_IN_MONTH; d++) {
    int idx = FIRST_WEEKDAY + d - 1;
    int cx = 12 + (idx % 7) * 22;
    int cy = 56 + (idx / 7) * 22;
    snprintf(buf, sizeof(buf), "%d", d);
    uint16_t textColor = C_INK;
    if (d == calSel) {
      // selected day: berry fill if it is today, lilac fill for any other day
      canvas.fillRoundRect(cx + 1, cy + 1, 20, 20, 6, (d == TODAY_DAY) ? C_BERRY : C_LILAC);
      if (d == TODAY_DAY) textColor = C_CARD;
    } else if (d == TODAY_DAY) {
      canvas.drawRoundRect(cx + 1, cy + 1, 20, 20, 6, C_BERRY);   // today, not selected
    }
    putTextCentered((d == calSel || d == TODAY_DAY) ? &FreeSansBold9pt7b : &FreeSans9pt7b,
                    cx + 11, cy + 16, textColor, buf);
    if (eventCountOnDay(d) > 0) {   // little dot = something planned that day
      canvas.fillCircle(cx + 11, cy + 19, 1, (d == calSel && d == TODAY_DAY) ? C_CARD : C_ROSE);
    }
  }

  // events for the selected day on the right
  drawCard(174, 36, 134, 196, C_PINK);
  if (calSel == TODAY_DAY) {
    snprintf(buf, sizeof(buf), "Today");
  } else {
    snprintf(buf, sizeof(buf), "%s %d Nov", weekdayName(calSel), calSel);
  }
  putText(&FreeSansBold9pt7b, 186, 56, C_INK, buf);
  int shown = 0;
  for (int n = 0; n < 4; n++) {
    int ei = nthEventOnDay(calSel, n);
    if (ei < 0) break;
    int y = 82 + n * 36;
    snprintf(buf, sizeof(buf), "%02d:%02d", events[ei].hour, events[ei].minute);
    putText(&FreeSansBold9pt7b, 186, y, C_BERRY, buf);
    putText(&FreeSans9pt7b, 186, y + 16, C_INK, events[ei].title);
    shown++;
  }
  if (shown == 0) putText(&FreeSans9pt7b, 186, 86, C_ROSE, "No events");
}

void render() {
  switch (page) {
    case PAGE_TODAY:    drawToday();    break;
    case PAGE_TODO:     drawTodo();     break;
    case PAGE_CALENDAR: drawCalendar(); break;
    default: break;
  }
  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), 320, 240);
}

// ----------------------------------------------------------------- input ----
void nextPage() { page = (Page)((page + 1) % PAGE_COUNT); dirty = true; }
void prevPage() { page = (Page)((page + PAGE_COUNT - 1) % PAGE_COUNT); dirty = true; }

void onShortPress(int button) {
  if (button == 1) {                                   // Prev
    if (page == PAGE_TODO) { todoSel = (todoSel + TASK_COUNT - 1) % TASK_COUNT; dirty = true; }
    else if (page == PAGE_CALENDAR) { calSel = (calSel - 2 + DAYS_IN_MONTH) % DAYS_IN_MONTH + 1; dirty = true; }
    else prevPage();
  } else if (button == 2) {                            // Next
    if (page == PAGE_TODO) { todoSel = (todoSel + 1) % TASK_COUNT; dirty = true; }
    else if (page == PAGE_CALENDAR) { calSel = calSel % DAYS_IN_MONTH + 1; dirty = true; }
    else nextPage();
  } else if (button == 3) {                            // Select
    if (page == PAGE_TODO) { tasks[todoSel].done = !tasks[todoSel].done; dirty = true; }
    else if (page == PAGE_CALENDAR) { calSel = TODAY_DAY; dirty = true; }
  }
}

// ----------------------------------------------------------------- setup ----
void setup() {
  Serial.begin(115200);

  pinMode(BTN1_PIN, INPUT_PULLUP);
  pinMode(BTN2_PIN, INPUT_PULLUP);
  pinMode(BTN3_PIN, INPUT_PULLUP);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);        // backlight on

  if (canvas.getBuffer() == nullptr) {
    Serial.println("Canvas allocation failed");
    while (true) delay(1000);
  }

  SPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI, -1);   // our own pins; CS handled by the library
  tft.begin();
  tft.setRotation(1);                // landscape: 320 x 240
  tft.fillScreen(ILI9341_BLACK);

  Serial.println("DAYFRAME ready");
}

void loop() {
  int e1 = pollButton(0);
  int e2 = pollButton(1);
  int e3 = pollButton(2);

  if (e1 == 1) onShortPress(1);
  if (e2 == 1) onShortPress(2);
  if (e3 == 1) onShortPress(3);
  if (e3 == 2) { page = PAGE_TODAY; dirty = true; }   // hold Select: back to the Today (home) page

  int m = nowMinutes();
  if (m != lastMinute) { lastMinute = m; dirty = true; }   // clock tick

  if (dirty) {
    dirty = false;
    render();
  }
}
