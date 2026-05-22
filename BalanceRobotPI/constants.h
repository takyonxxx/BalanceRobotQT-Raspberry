#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <QString>
#include <QDebug>
#include <QObject>
#include <QSettings>
#include <QNetworkInterface>
#include <iostream>
#include <deque>
#include <vector>
#include <math.h>
#include <message.h>
#include <sys/time.h>

// -----------------------------------------------------------------------------
// Loop timing
// -----------------------------------------------------------------------------
#define SLEEP_PERIOD 1000   // microseconds — main loop pacing
#define SERIAL_TIME  100    // milliseconds — telemetry / BLE rate
#define SAMPLE_TIME  1      // milliseconds — IMU sample interval

// -----------------------------------------------------------------------------
// I²C
// -----------------------------------------------------------------------------
#define MPU6050_I2C_ADDRESS 0x68
#define RESTRICT_PITCH                // MPU6050 fusion mode flag

// -----------------------------------------------------------------------------
// Motor driver pins — Waveshare RPi Motor Driver Board (MC33886 × 2)
//
// Pin numbers are PHYSICAL header positions (the code calls
// wiringPiSetupPhys()). The driver mounts as a HAT, so these pins mate
// through the 40-pin header, not as loose wires.
//
// Each motor needs 3 lines: IN1 + IN2 (direction) + ENA (PWM enable).
// -----------------------------------------------------------------------------
#define PWMR1  31    // Right motor — IN1 (direction bit 1)
#define PWMR2  33    // Right motor — IN2 (direction bit 2)
#define PWMR   32    // Right motor — ENA (PWM speed,  softPwm 0..255)
#define PWML1  38    // Left  motor — IN1 (direction bit 1)
#define PWML2  40    // Left  motor — IN2 (direction bit 2)
#define PWML   37    // Left  motor — ENA (PWM speed,  softPwm 0..255)

// -----------------------------------------------------------------------------
// Quadrature encoder pins — Hall encoder on each 37 mm DC motor
//
// Channel A (SPD_INT_*) triggers an ISR on rising edge; the ISR then reads
// channel B (SPD_PUL_*) to recover direction. All four inputs use internal
// pull-ups (PUD_UP).
// -----------------------------------------------------------------------------
#define SPD_INT_L 16    // Left  motor encoder — channel A (interrupt)
#define SPD_PUL_L 12    // Left  motor encoder — channel B (direction sample)
#define SPD_INT_R 18    // Right motor encoder — channel A (interrupt)
#define SPD_PUL_R 22    // Right motor encoder — channel B (direction sample)

using namespace std;

// -----------------------------------------------------------------------------
// Language flag for the on-board TTS (Speaker class wraps espeak)
// -----------------------------------------------------------------------------
enum SType
{
    TR,
    EN
};

// -----------------------------------------------------------------------------
// Run a shell command and discard its output. Used by a few helpers that
// need to invoke aplay / amixer / espeak.
// -----------------------------------------------------------------------------
static void execCommand(char* cmd)
{
    char appended[512];
    snprintf(appended, sizeof(appended), "%s >>/dev/null 2>>/dev/null", cmd);
    char buffer[128];
    FILE* pipe = popen(appended, "r");
    if (!pipe) throw std::runtime_error("popen() failed!");
    try {
        while (!feof(pipe)) {
            if (fgets(buffer, 128, pipe) == nullptr) break;
        }
    } catch (...) {
        pclose(pipe);
        throw;
    }
    pclose(pipe);
}

// -----------------------------------------------------------------------------
// Discover the Pi's primary IPv4 address + MAC + netmask, skipping loopback,
// point-to-point and VM interfaces. Used at startup so we can broadcast
// the robot's IP to the iOS app.
// -----------------------------------------------------------------------------
static void getDeviceInfo(QString &device, QString &ip, QString &mac, QString &mask)
{
    bool found = false;
    foreach (QNetworkInterface interface, QNetworkInterface::allInterfaces())
    {
        unsigned int flags = interface.flags();
        bool isLoopback = (bool)(flags & QNetworkInterface::IsLoopBack);
        bool isP2P      = (bool)(flags & QNetworkInterface::IsPointToPoint);
        bool isRunning  = (bool)(flags & QNetworkInterface::IsRunning);
        if (!isRunning) continue;
        if (!interface.isValid() || isLoopback || isP2P) continue;

        foreach (QNetworkAddressEntry entry, interface.addressEntries())
        {
            if (entry.ip() == QHostAddress::LocalHost) continue;
            if (!entry.ip().toIPv4Address()) continue;

            if (!found
                && interface.hardwareAddress() != "00:00:00:00:00:00"
                && entry.ip().toString().contains(".")
                && !interface.humanReadableName().contains("VM"))
            {
                device = interface.humanReadableName();
                ip     = entry.ip().toString();
                mac    = interface.hardwareAddress();
                mask   = entry.netmask().toString();
                found  = true;
            }
        }
    }
}

#endif // CONSTANTS_H
