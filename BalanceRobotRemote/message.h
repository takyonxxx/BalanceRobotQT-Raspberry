#ifndef MESSAGE_H
#define MESSAGE_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <QByteArray>

#define mHeader     0xb0 //Fix header
#define mWrite      0x01 //Write request
#define mRead       0x02 //Read request
#define mForward    0xa0
#define mBackward   0xa1
#define mLeft       0xb0
#define mRight      0xb1
#define mPP         0xc0 //Proportional
#define mPI         0xc1 //Integral control
#define mPD         0xc2 //Derivative constant
#define mAC         0xd0 //angle correction
#define mVS         0xd1 //speed diff constant wheel

#define MaxPayload 0x38

//message len max is 256, header, command, rw and cheksum total len is 8, therefore payload max len is 248
//max input bluetooth buffer in this chip allows a payload max 0x38

typedef struct {
    uint8_t header;    
    uint8_t len;
    uint8_t rw;
    uint8_t command;
    uint8_t data[MaxPayload];
    uint8_t CheckSum[2];
} MessagePack;


class Message
{
public:
    Message();
    uint8_t parse(uint8_t *dataUART, uint8_t size, MessagePack *message);
    uint8_t create_pack(uint8_t RW,uint8_t command, QByteArray dataSend, uint8_t *dataUART);

};

#endif // MESSAGE_H
