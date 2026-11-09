// UI preview harness: minimal Arduino / FreeRTOS stand-ins so the real base UI code compiles on a PC.
#pragma once
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define PROGMEM

typedef void* QueueHandle_t;

uint32_t millis();
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}

struct FakeSerial {
    void printf(const char* fmt, ...) {
        va_list ap;
        va_start(ap, fmt);
        vprintf(fmt, ap);
        va_end(ap);
    }
    void println(const char* s) { puts(s); }
};
extern FakeSerial Serial;

struct FakeEsp {
    uint32_t getFreeHeap() { return 182 * 1024; }
};
extern FakeEsp ESP;
