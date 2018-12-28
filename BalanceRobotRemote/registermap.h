#ifndef REGISTERMAP_H
#define REGISTERMAP_H
#include <QtGlobal>

// Buffer structure
//
//  +---+---+---+---+---+---+---+---+---+
//  |x55|xAA| l |x11|x01| c |...|ck0|ck1|
//  +---+---+---+---+---+---+---+---+---+
//
//  ... Are a UInt8 array of l-2 elements
//  ck0, ck1 are computed from the elements from l to the last of ...
//  x55 and xAA are fixed and are Beggining of buffer
//  l is the size of ... + 2
//  c is a command or variable. For the same value the data is similar

#define NinebotHeader0 0x55
#define NinebotHeader1 0xAA
#define Ninebotdirection 0x11
#define Ninebotread 0x01
#define Ninebotwrite 0x03
#define NinebotMaxPayload 0x38

//message len max is 256, header, command, rw and cheksum total len is 8, therefore payload max len is 248
//max input bluetooth buffer in this chip allows a payload max 0x38
typedef struct {
    uint8_t direction;
    uint8_t RW;
    uint8_t len;
    uint8_t command;
    uint8_t data[NinebotMaxPayload];
    uint8_t CheckSum[2];
} MessagePack;

#endif // REGISTERMAP_H
