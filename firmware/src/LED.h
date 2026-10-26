// ===================================================================
// File:            LED.h
// Author:          EideardVMR
// Year:            2026
// Last modified:   2026-10-24
// ===================================================================
#pragma once
#include <stdint.h>
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "pinmapping.h"

#ifndef PIN_LED
#define PIN_LED 1
#endif

#ifndef LED_COUNT
#define LED_COUNT 1
#endif

/// @brief Steuerung der WS2812 LEDs
namespace LED {

    struct Color {
        uint8_t r, g, b;
    };

    constexpr Color Off     = {0,   0,   0};
    constexpr Color White   = {255, 255, 255};
    constexpr Color Red     = {255, 0,   0};
    constexpr Color Green   = {0,   255, 0};
    constexpr Color Blue    = {0,   0,   255};
    constexpr Color Yellow  = {255, 200, 0};
    constexpr Color Orange  = {255, 80,  0};
    constexpr Color Cyan    = {0,   255, 255};
    constexpr Color Magenta = {255, 0,   255};

    /// @brief Startet den Task. Einmal in setup() aufrufen; die LED ist danach aus.
    void begin();

    /// @brief Gesamthelligkeit 0..255 (Standard 64: eine WS2812 ist bei voller Helligkeit sehr hell).
    void setBrightness(uint8_t brightness);

    /// @brief LED aus.
    void off();

    /// @brief Dauerhaft in einer Farbe leuchten.
    void solid(Color color);

    /// @brief Hart blinken: je eine halbe Periode an und aus.
    /// @param periodMs Dauer eines vollen Blinkens (an + aus).
    void blink(Color color, uint16_t periodMs = 1000);

    /// @brief Weich ein- und ausblenden (Atmen).
    /// @param periodMs Dauer eines vollen Durchgangs (hell -> dunkel -> hell).
    void fadeInOut(Color color, uint16_t periodMs = 2000);
}
