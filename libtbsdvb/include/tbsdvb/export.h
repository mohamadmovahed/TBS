#pragma once

// Symbol visibility for the tbsdvb library (static or shared).
#if defined(TBSDVB_STATIC)
#define TBSDVB_API
#elif defined(_WIN32) || defined(__CYGWIN__)
#if defined(TBSDVB_BUILDING)
#define TBSDVB_API __declspec(dllexport)
#else
#define TBSDVB_API __declspec(dllimport)
#endif
#else
#define TBSDVB_API __attribute__((visibility("default")))
#endif
