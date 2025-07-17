#pragma once

// // Including SDKDDKVer.h defines the highest available Windows platform.
// If you wish to build your application for a previous Windows platform, include WinSDKVer.h and
// set the _WIN32_WINNT macro to the platform you wish to support before including SDKDDKVer.h.
#include <WinSDKVer.h>
#define _WIN32_WINNT 0x0A00        // “Windows 10 or later” (NT 10.0)
#define WINVER 0x0A00
#define NTDDI_VERSION 0x0A000010  // Win 11 GE
#include <SDKDDKVer.h>
