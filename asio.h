#ifndef __ASIO_H
#define __ASIO_H

#include "asiosys.h"

typedef long ASIOBool;
enum {
    ASIOFalse = 0,
    ASIOTrue = 1
};

typedef double ASIOSampleRate;

typedef long ASIOSampleType;
enum {
    ASIOSTInt16MSB   = 0,
    ASIOSTInt24MSB   = 1,
    ASIOSTInt32MSB   = 2,
    ASIOSTFloat32MSB = 3,
    ASIOSTFloat64MSB = 4,

    ASIOSTInt32MSB16 = 8,
    ASIOSTInt32MSB18 = 9,
    ASIOSTInt32MSB20 = 10,
    ASIOSTInt32MSB24 = 11,
    
    ASIOSTInt16LSB   = 16,
    ASIOSTInt24LSB   = 17,
    ASIOSTInt32LSB   = 18,
    ASIOSTFloat32LSB = 19,
    ASIOSTFloat64LSB = 20,

    ASIOSTInt32LSB16 = 24,
    ASIOSTInt32LSB18 = 25,
    ASIOSTInt32LSB20 = 26,
    ASIOSTInt32LSB24 = 27,

    ASIOSTDSDInt8LSB1 = 32,
    ASIOSTDSDInt8MSB1 = 33,
    ASIOSTDSDInt8NER8 = 34
};

typedef long ASIOError;
enum {
    ASE_OK = 0,
    ASE_SUCCESS = 0x3f4847a0,
    ASE_NotPresent = -1000,
    ASE_HWMalfunction = -999,
    ASE_InvalidParameter = -998,
    ASE_InvalidMode = -997,
    ASE_SPNotAdvancing = -996,
    ASE_NoClock = -995,
    ASE_NoMemory = -994
};

typedef struct ASIOSamples {
    unsigned long lo;
    unsigned long hi;
} ASIOSamples;

typedef struct ASIOTimeStamp {
    unsigned long lo;
    unsigned long hi;
} ASIOTimeStamp;

typedef struct ASIOSamplePos {
    unsigned long lo;
    unsigned long hi;
} ASIOSamplePos;

typedef struct ASIOTimeCode {
    double          speed;
    ASIOSamples     timeCodeSamples;
    unsigned long   flags;
    char            future[64];
} ASIOTimeCode;

typedef struct ASIOTimeInfo {
    double          speed;
    ASIOTimeStamp   systemTime;
    ASIOSamplePos   samplePosition;
    ASIOSampleRate  sampleRate;
    unsigned long   flags;
    char            reserved[12];
} ASIOTimeInfo;

typedef struct ASIOTime {
    long            reserved[4];
    ASIOTimeInfo    timeInfo;
    ASIOTimeCode    timeCode;
} ASIOTime;

typedef struct ASIOCallbacks {
    void (*bufferSwitch) (long doubleBufferIndex, ASIOBool directProcess);
    void (*sampleRateDidChange) (ASIOSampleRate sRate);
    long (*asioMessage) (long selector, long value, void* message, double* opt);
    ASIOTime* (*bufferSwitchTimeInfo) (ASIOTime* params, long doubleBufferIndex, ASIOBool directProcess);
} ASIOCallbacks;

typedef struct ASIOBufferInfo {
    ASIOBool isInput;
    long channelNum;
    void *buffers[2];
} ASIOBufferInfo;

typedef struct ASIOChannelInfo {
    long channel;
    ASIOBool isInput;
    ASIOBool isActive;
    long channelGroup;
    ASIOSampleType type;
    char name[32];
} ASIOChannelInfo;

typedef struct ASIOClockSource {
    long index;
    long associatedChannel;
    long associatedGroup;
    ASIOBool isCurrentSource;
    char name[32];
} ASIOClockSource;

// DSD extensions
enum {
    kAsioEnableTimeCodeRead = 1,
    kAsioDisableTimeCodeRead,
    kAsioSetInputMonitor,
    kAsioResetInputMonitor,
    kAsioSetInputGain,
    kAsioGetInputMeter,
    kAsioSetPan,
    kAsioSetOutputGain,
    kAsioGetOutputMeter,
    kAsioSetIoFormat   = 0x23111961,
    kAsioGetIoFormat   = 0x23111983,
    kAsioCanDoIoFormat = 0x23112004
};

typedef long int ASIOIoFormatType;
enum {
    kASIOFormatInvalid = -1,
    kASIOPCMFormat = 0,
    kASIODSDFormat = 1
};

typedef struct ASIOIoFormat_s {
    ASIOIoFormatType FormatType;
    char future[512 - sizeof(ASIOIoFormatType)];
} ASIOIoFormat;

#endif
