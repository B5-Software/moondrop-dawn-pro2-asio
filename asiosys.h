#ifndef __asiosys__
#define __asiosys__

#define ASIO_DEBUG 0

#if defined(_WIN32) || defined(WIN32)
    #define WINDOWS 1
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#endif

#endif
