#pragma once
#include "framework.h"
#include "resource.h"

void DebugLog(const wchar_t* format, ...);

// Alternative macro for easier usage
#ifdef _DEBUG
#define DEBUG_LOG(fmt, ...) DebugLog(fmt, ##__VA_ARGS__)
#else
#define DEBUG_LOG(fmt, ...)
#endif

// Panic macro to log an error and exit the application
#define PANIC(msg, ...) do { \
    wchar_t buffer[512]; \
    swprintf_s(buffer, L"PANIC: " msg L" at %S:%d", ##__VA_ARGS__, __FILE__, __LINE__); \
    FatalAppExit(0, buffer); \
} while(0)