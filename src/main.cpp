// gpxcam-t1000e firmware for the Seeed SenseCAP T1000-E.
// Copyright (C) 2026 Tomasz Bojanowski. License: GPL-3.0 (see LICENSE).
#include <Arduino.h>
// The core always uses Serial (TinyUSB CDC, included by Arduino.h). This include lets
// PlatformIO's library finder link the Adafruit TinyUSB library; it does not follow
// includes inside core headers.
#include <Adafruit_TinyUSB.h>

#include "board.h"

void setup()
{
    board::init();
}

void loop()
{
    delay(1000);
}
