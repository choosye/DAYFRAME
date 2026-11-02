# DAYFRAME
A TFT Display that shows your calendar, to-do list, and quotes! A personal productivity device that can sit on your desk while still being portable to be brought wherever.
<img width="1600" height="1180" alt="dayframe-render" src="https://github.com/user-attachments/assets/2a26b583-d1a9-49bb-8785-e32fb9850ec6" />

## What it is
DAYFRAME is a compact desk planner with its own 2.8" color screen, so you can see today's date, schedule, priorities and progress without opening a phone. A Seeed XIAO ESP32-S3 draws a soft, rounded card-style interface on an ILI9341 display, and three buttons (Prev, Next, Select) move between pages and tick off tasks. It was designed for Hack Club Half Life, Week 4 (Design with Displays).

## What it does
- **Today** page: date, clock, next event, top three priorities, a progress bar and a short quote.
- **To-do** page: a checklist you can tick off with the Select button.
- **Calendar** page: the month at a glance. Move the highlight to any day to see that day's events, and days with something planned get a small dot.
- Planned: Wi-Fi sync through a simple web page hosted by the device, real time over NTP, a battery icon and a rechargeable LiPo with USB-C charging.

## How it works
<img width="715" height="445" alt="Screenshot 2026-11-02 at 11 59 52 AM" src="https://github.com/user-attachments/assets/4e11d95b-e238-4e20-99a3-f623db0c3682" />
The XIAO ESP32-S3 talks to the display over SPI. Each screen is first drawn into an off-screen 320x240 buffer using Adafruit GFX, then sent to the display in one go, which avoids flicker. The three buttons use the chip's internal pull-ups with a 30 ms software debounce. On the To-do page Prev and Next move the highlight and Select ticks a task. On the Calendar page Prev and Next move the selected day and Select jumps back to today. The Today page is home: Prev opens the Calendar, Next opens the To-do list, and holding Select on any page returns to Today. This version uses hard-coded sample data.

## Schematic
<img width="1039" height="477" alt="Screenshot 2026-11-02 at 11 58 32 AM" src="https://github.com/user-attachments/assets/3c31840c-deec-444a-870d-433a1d39cfed" />

## Hardware
| Part | Notes |
|---|---|
| Seeed XIAO ESP32-S3 | Microcontroller with Wi-Fi, USB-C and a built-in LiPo charger |
| 2.8" ILI9341 SPI TFT module, 240x320 | Used in landscape (320x240); touch pins left unused |
| 3 x right-angle (side-press) 6x6 mm tactile switches | Prev, Next, Select, pressed through the top edge of the case |
| Perfboard, hookup wire, female headers | This version is hand-wired; a custom PCB is a next step |
| 3.7 V LiPo (optional) | Soldered to the XIAO's battery pads for portable use |

## Wiring
| Signal | XIAO pin | GPIO |
|---|---|---|
| TFT CS | D3 | 4 |
| TFT DC | D4 | 5 |
| TFT RESET | D5 | 6 |
| TFT backlight (LED) | D6 | 43 |
| TFT SCK | D8 | 7 |
| TFT MOSI (SDI) | D10 | 9 |
| TFT VCC / GND | 3V3 / GND | - |
| Button 1 (Prev) | D0 | 1 |
| Button 2 (Next) | D1 | 2 |
| Button 3 (Select) | D2 | 3 |
The display's MISO and touch pins are not connected. Buttons connect their pin to GND.

## Firmware
The sketch is in `firmware/DAYFRAME/DAYFRAME.ino`. Install the **Adafruit GFX Library** and **Adafruit ILI9341** libraries in the Arduino IDE, choose the **XIAO_ESP32S3** board (from the "esp32 by Espressif Systems" package), and upload. To try it without hardware, paste it into a Wokwi ESP32-S3 project with an ILI9341 display and three pushbuttons wired to the GPIO numbers above.

## Project Status 
Only designed in software, not yet tested on actual hardware. Plans to make it have WiFI sync, NTP time, and a kickstand :) 

## Repository Contents
