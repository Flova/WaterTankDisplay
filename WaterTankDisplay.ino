#include <Adafruit_GFX.h>
#include <Adafruit_TFTLCD.h>
#include <CircularBuffer.hpp>
#include <EEPROM.h>
#include <TouchScreen.h>

// ---- Config ----
// Domain specific config
#define TANK_VOLUME 400      // Liters
#define TANK_EMPTY_DEPTH 88  // cm
#define TANK_FULL_DEPTH 8    // cm

// Touch screen config
#define MINPRESSURE 100
#define MAXPRESSURE 2000

// ALL Touch panels and wiring is DIFFERENT
// copy-paste results from this script:
// https://github.com/prenticedavid/MCUFRIEND_kbv/blob/6792ce7caffc75b89a95ae659a0e98bd43d98258/examples/TouchScreen_Calibr_native/TouchScreen_Calibr_native.ino
const int XP = 8, XM = A2, YP = A3, YM = 9;  //240x320 ID=0x9341
const int TS_LEFT = 93, TS_RT = 928, TS_TOP = 67, TS_BOT = 875;

// LCD Pin Layout
#define LCD_CS A3
#define LCD_CD A2
#define LCD_WR A1
#define LCD_RD A0
#define LCD_RESET A4

// Sonar Pin Layout
#define SONAR_TRIGGER_PIN 11
#define SONAR_ECHO_PIN 12

// ---- Global objects ----
// Graphics / Screen client
Adafruit_TFTLCD tft(LCD_CS, LCD_CD, LCD_WR, LCD_RD, LCD_RESET);
// Touch screen client
TouchScreen ts = TouchScreen(XP, YP, XM, YM, 300);
// Custom touch screen result
struct TouchResult {
  bool pressed = false;
  int x = 0;
  int y = 0;
};

// ---- UI ----
// Colors
#define BLACK 0x0000
#define BLUE 0x001F
#define RED 0xF800
#define GREEN 0x07E0
#define CYAN 0x07FF
#define MAGENTA 0xF81F
#define YELLOW 0xFFE0
#define WHITE 0xFFFF
#define ORANGE 0xFD20
#define GREENYELLOW 0xAFE5
#define NAVY 0x000F
#define DARKGREEN 0x03E0
#define DARKCYAN 0x03EF
#define MAROON 0x7800
#define PURPLE 0x780F
#define OLIVE 0x7BE0
#define LIGHTGREY 0xC618
#define DARKGREY 0x7BEF

// UI elements
Adafruit_GFX_Button left_btn, right_btn;

// Constants
const int control_bar_btn_margin = 5;
const int control_bar_btn_width = 40;
const int control_bar_btn_height = 40;
int control_bar_btn_y = 0, control_bar_divider_y = 0, control_bar_dot_y = 0;  // Will be init later on

// All available pages
enum Pages {
  BIG_NUM_LITER = 0,
  BIG_NUM_PERCENT = 1,
  TANK_VIEW = 2,
  SETTINGS = 3,
};
// Number of pages in the enum above
const int page_count = 4;

// ---- States ----
// Current page
enum Pages current_page = BIG_NUM_LITER;

#define LAST_PAGE_EEPROM_ADDR 0

// History
#define LONG_HISTORY_SIZE 24 // 24 hours
#define SHORT_HISTORY_SIZE 64 // samples
#define HISTORY_PLOT_WIDTH 128 // pixels

// Rolling buffer of uint16_t values
CircularBuffer<float, HISTORY_PLOT_WIDTH> long_history;
unsigned long last_long_history_update = 0;
#define LONG_HISTORY_UPDATE_INTERVAL LONG_HISTORY_SIZE * 60 * 60 * 1000 / HISTORY_PLOT_WIDTH // ms

// Rolling buffer of uint16_t values
CircularBuffer<float, SHORT_HISTORY_SIZE> short_history;

// Initialization
void setup() {
  // Check if the last page was saved in the EEPROM
  int eeprom_page = EEPROM.read(LAST_PAGE_EEPROM_ADDR);
  current_page = (enum Pages)constrain(eeprom_page, 0, page_count - 1);

  // Setup TFT display
  tft.begin(tft.readID());
  tft.setRotation(1);
  // Draw black background
  tft.fillScreen(BLACK);

  // Calculate "constants" that depend on runtime data
  control_bar_btn_y = tft.height() - control_bar_btn_height - control_bar_btn_margin;
  control_bar_divider_y = control_bar_btn_y - control_bar_btn_margin;
  control_bar_dot_y = control_bar_btn_y + control_bar_btn_height / 2;

  // Draw the control bar at the bottom of the screen
  drawControlBar();

  // Set the pins for the sonar sensor
  pinMode(SONAR_TRIGGER_PIN, OUTPUT);
  pinMode(SONAR_ECHO_PIN, INPUT);
}

// Returns if the screen was touched and where it was touched
TouchResult TouchGetXY(void) {
  // Get the pressed point from the driver
  TSPoint p = ts.getPoint();
  // This is weird...
  pinMode(YP, OUTPUT);  // Restore shared pins
  pinMode(XM, OUTPUT);  // Recause TFT control pins
  // Create result object
  TouchResult result;
  // Threshold the pressure to determine a clear pressed or not pressed
  result.pressed = (p.z > MINPRESSURE && p.z < MAXPRESSURE);
  if (result.pressed) {
    // If we pressed we map the touch screen space to the screen dimensions
    // This includes some additional logic to handle the screen rotation
    result.x = tft.width() - map(p.y, TS_LEFT, TS_RT, 0, tft.width());
    result.y = map(p.x, TS_TOP, TS_BOT, 0, tft.height());
  }
  return result;
}

// Measures the distance (cm) to the water surface using the sonar
float measureDistance() {
  // Send Pulse
  digitalWrite(SONAR_TRIGGER_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(SONAR_TRIGGER_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(SONAR_TRIGGER_PIN, LOW);

  // Calculate duration of the reflection
  float duration = pulseIn(SONAR_ECHO_PIN, HIGH, 10000);

  // Calculate distance
  float distance = (duration * .0343) / 2;

  // Clamp distance to min/max
  return constrain(distance, TANK_FULL_DEPTH, TANK_EMPTY_DEPTH);
}

// Advances the displayed page state
void goToNextPage(bool reverse) {
  // Is -1 for reverse and 1 if not
  int step = -reverse * 2 + 1;

  // Keep ref to old page
  auto old_page = current_page;

  // Update page pointer
  current_page = (current_page + page_count + step) % page_count;

  // Store the current page in the EEPROM
  EEPROM.write(LAST_PAGE_EEPROM_ADDR, current_page);

  // Update dots
  drawDot(true, false, pageIdxToPointX(current_page), control_bar_dot_y);
  drawDot(false, true, pageIdxToPointX(old_page), control_bar_dot_y);

  // Clear page
  tft.fillRect(0, 0, tft.width(), control_bar_divider_y - 1, BLACK);
}

// Get the x position of a point that represents a given page in the control bar
// The y position is equal for all of them, so no function is needed
int pageIdxToPointX(enum Pages page) {
  // Horizontal space that is not available for the dots, because a button is there (including its margin)
  const int space_occupied_by_btn = 2 * control_bar_btn_margin + control_bar_btn_width;
  // Horizontal space that is available between the buttons in the control bar
  const int space_between_buttons = tft.width() - 2 * space_occupied_by_btn;
  // How much vertical space needs to be between the dots for an even spacing
  const int distance_between_points = space_between_buttons / (page_count + 1);
  // Calculate the dot position based on the queried page and the previous values
  return (page + 1) * distance_between_points + space_occupied_by_btn;
}

// (Re-)draws a dot indicating a page in the control bar
// The dots visuals can be changed if it is the currently active page
// Previous dot visuals at the same position can be erased
void drawDot(bool active_page, bool erase, int x, int y) {
  // If there was a large dot (active page) before and we draw a small dot now, we need to clear a larger area before drawing
  if (erase) {
    tft.fillCircle(x, y, 7, BLACK);
  }
  // Draw a larger dot with a border and some colored filling if we draw the active page
  if (active_page) {
    tft.fillCircle(x, y, 6, NAVY);
    tft.drawCircle(x, y, 7, WHITE);
  } else {
    // Otherwise draw small dot
    tft.fillCircle(x, y, 3, WHITE);
  }
}

// Draw a big 3 digit number plus its unit, filling the whole page
void drawBigNumber(float value, char unit) {
  // Some parameters to draw the numbers at the correct position
  const int big_letters_left_margin = 35;
  const int big_letters_y = 60;
  // This mainly determines where the unit symbol is drawn and how much distance it has to the number
  const int three_letter_width = 200;

  // Set text formatting
  tft.setTextColor(WHITE, BLACK);
  tft.setTextSize(10);

  // Set position
  tft.setCursor(big_letters_left_margin, big_letters_y);

  // Buffer to hold the formatted string
  char buffer[4];

  // Format the value to a string with leading spaces and no remainder
  dtostrf(value, 3, 0, buffer);

  // Draw number to the screen
  tft.println(buffer);

  // Draw unit symbol
  tft.setCursor(big_letters_left_margin + three_letter_width, big_letters_y);
  tft.println(unit);
}

// Draws the full control bar (only partial updates are performed later on)
// Should look like this:  < * O * * >
void drawControlBar()
{
  // Create / Draw page buttons
  left_btn.initButtonUL(&tft, control_bar_btn_margin, control_bar_btn_y, control_bar_btn_width, control_bar_btn_height, BLACK, WHITE, BLACK, "<", 2);
  right_btn.initButtonUL(&tft, tft.width() - control_bar_btn_margin - control_bar_btn_width, control_bar_btn_y, control_bar_btn_width, control_bar_btn_height, BLACK, WHITE, BLACK, ">", 2);
  left_btn.drawButton(false);
  right_btn.drawButton(false);

  // Draw seperator line
  tft.drawLine(0, control_bar_divider_y, tft.width(), control_bar_divider_y, WHITE);

  // Draw dots (highlight the current page)
  for (size_t i = 0; i < page_count; i++) {
    auto dot_x = pageIdxToPointX(i);
    drawDot(i == current_page, false, dot_x, control_bar_dot_y);
  }
}

// Checks if a button was pressed, by comparing it to a touch event
// If so the visuals are changed until it is released (if this function is called regularly)
// The function returns true if the button has just been pressed
bool processBtn(Adafruit_GFX_Button *btn, TouchResult *touch_state)
{
  // "Press" button UI element of the screen was pressed and the touch event in proximity of the button
  btn->press(touch_state->pressed && btn->contains(touch_state->x, touch_state->y));
  // Redraw the button uppon release (undo the inverted colors)
  if (btn->justReleased())
    btn->drawButton();
  // Invert the colors of the button and return true to signal that the button was pressed
  if (btn->justPressed()) {
    btn->drawButton(true);
    return true;
  }
  // The button was not pressed
  return false;
}

// Draws the tank illustration on the screen
void drawTank(int positon_x, int position_y, int percent)
{
  // Define dimensions of the tank
  const int width = 100;
  const int height = 20;
  const int offset_x = 0;
  const int offset_y = 40;
  // Use timestamp for animation of the wave
  float step = millis() * 0.002;
  // Draw two vertical lines for each pixel collumn of the wave
  for (int x = 0; x < width; x++) {
    // Determine the y point where the black line turns into a white line
    int y = height / 2 + 5 * sin(step + 2 * PI * x / width);
    // Draw the lines above and below the waterline at that x position (y)
    tft.drawLine(x + offset_x, height + offset_y, x + offset_x, y + offset_y, WHITE);
    tft.drawLine(x + offset_x, offset_y, x + offset_x, y - 1 + offset_y, BLACK);
  }
  // Fill the rest of the tank
  tft.fillRect(offset_x, offset_y + height, width, 100, WHITE);
}

// Main loop
void loop() {
  // First get the distance to the water from the sensor
  float distance = measureDistance();

  // Add the distance to the history
  short_history.push(distance);

  // Calculate the average of the short history
  float distance_avg = 0;
  for (size_t i = 0; i < short_history.size(); i++)
  {
    distance_avg += short_history[i];
  }
  distance_avg /= short_history.size();

  // Update the long history every LONG_HISTORY_UPDATE_INTERVAL
  auto current_time = millis();
  if (current_time > last_long_history_update + LONG_HISTORY_UPDATE_INTERVAL)
  {
    // Add the average of the short history to the long history
    long_history.push(distance_avg);
    // Update the last update timestamp
    last_long_history_update = current_time;
  }

  // Convert distance to liter and percent based on provided tank dimensions
  float liter = map(distance_avg * 10, TANK_FULL_DEPTH * 10, TANK_EMPTY_DEPTH *  10, TANK_VOLUME, 0); // Scale by 10 here, because we cast to an int and the liters have more prescision than the cm (as an int)
  float percent = map(distance_avg, TANK_FULL_DEPTH, TANK_EMPTY_DEPTH, 100, 0);

  // Query the touch screen and process the control bar buttons
  auto touch_state = TouchGetXY();

  // Handle button presses
  if (processBtn(&left_btn, &touch_state))
    goToNextPage(true);
  if (processBtn(&right_btn, &touch_state))
    goToNextPage(false);

  // Draw pages
  switch (current_page)
  {
    // Draw page that displays the liters as big numbers
    case BIG_NUM_LITER:
      drawBigNumber(liter, 'L');
      break;
    // Draw page that displays the level in percent as big numbers
    case BIG_NUM_PERCENT:
      drawBigNumber(percent, '%');
      break;
    // Draw combined view of tank illustration, percent and liter
    case TANK_VIEW:
      tft.setTextSize(2);
      tft.setTextColor(WHITE);
      tft.setCursor(10, 10);
      tft.println("Tank View");
      drawTank(0,0,100);
      break;
    // Draw settings page
    case SETTINGS:
      tft.setTextSize(2);
      tft.setTextColor(WHITE);
      tft.setCursor(10, 10);
      tft.println("Settings");
      break;
  }
}
