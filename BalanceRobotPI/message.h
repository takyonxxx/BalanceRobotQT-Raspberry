#ifndef MESSAGE_H
#define MESSAGE_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <QByteArray>

#define mHeader     0xb0 // Fix header

// İstek tipleri
#define mWrite      0x01
#define mRead       0x02

// Sistem komutları
#define mArmed      0x03
#define mDisArmed   0x04

// Hareket komutları
#define mForward    0xa0
#define mBackward   0xa1
#define mLeft       0xb0
#define mRight      0xb1

// PID ve ayar parametreleri
#define mPP         0xc0 // Pitch Proportional
#define mPI         0xc1 // Pitch Integral
#define mPD         0xc2 // Pitch Derivative
#define mAC         0xd0 // Angle correction (trim)
#define mSD         0xd1 // Speed diff / wheel sync

// Speed PID parametreleri (yeni 2026-05-21)
#define mSpdKp      0xc3 // Speed PID Kp (value/100, e.g. 12 → 0.12)
#define mSpdKi      0xc4 // Speed PID Ki (value/100, e.g. 20 → 0.20)
#define mSpdMaxTilt 0xc5 // Max speed tilt in degrees (direct, 0-15)
#define mSpdMaxVel  0xc6 // Max target velocity (value/10, e.g. 30 → 3.0)

// Ses
#define mSpeak      0xe0
#define mData       0xe1 // IP/diagnostics text

// --- Yeni komutlar (v2) ---
#define mTelemetry  0xf0 // Pi -> Phone canlı telemetri paketi
#define mAutoMode   0xf1 // Otomatik kalk/iç dengeleme aç/kapa
#define mTrimFine   0xf2 // Hassas trim (signed, +/-)
#define mPositionHold 0xf3 // Position-hold modunu aç/kapa
#define mResetTrim  0xf4 // Trim integratörünü ve auto-bias sıfırla
#define mPidLearn   0xf5 // PID öğrenme modu: write 1=başlat 0=durdur, read=durum
#define mPidStatus  0xf6 // Pi -> Phone: PID öğrenme durum metni (UTF-8)

#define MaxPayload 1024

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
    bool parse(uint8_t *dataUART, uint8_t size, MessagePack *message);
    uint8_t create_pack(uint8_t RW, uint8_t command, QByteArray dataSend, uint8_t *dataUART);
};

#endif // MESSAGE_H
