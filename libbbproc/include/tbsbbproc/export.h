#pragma once

#if defined(TBSBBPROC_STATIC)
#define TBSBBPROC_API
#elif defined(_WIN32) || defined(__CYGWIN__)
#if defined(TBSBBPROC_BUILDING)
#define TBSBBPROC_API __declspec(dllexport)
#else
#define TBSBBPROC_API __declspec(dllimport)
#endif
#else
#define TBSBBPROC_API __attribute__((visibility("default")))
#endif
