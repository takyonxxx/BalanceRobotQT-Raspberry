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

#define SLEEP_PERIOD 1000 //us;s
#define SERIAL_TIME  100 //ms
#define SAMPLE_TIME  1//ms

#define MPU6050_I2C_ADDRESS 0x68
#define RESTRICT_PITCH

//physcal pins
#define PWMR1  31
#define PWMR2  33
#define PWML1  38
#define PWML2  40
#define PWMR   32
#define PWML   37

//encoder define
#define SPD_INT_L 12   //interrupt R Phys:12
#define SPD_PUL_L 16   //Phys:16
#define SPD_INT_R 22   //interrupt L Phys:22
#define SPD_PUL_R 18   //Phys:16

#define Ref_Meters		180.0		// Reference for meters in adaptive predictive control
#define NL				0.005		// Noise Level for Adaptive Mechanism.
#define GainA 			0.6			// Gain for Adaptive Mechanism A
#define GainB 			0.6			// Gain for Adaptive Mechanism B
#define PmA				2			// Delay Parameters a
#define PmB				2			// Delay Parameters b
#define nCP				9.0			// Conductor block periods control for rise to set point ts = n * CP
#define hz				5			// Prediction Horizon (Horizon max = n + 2)
#define UP_Roll			800.0		// Upper limit out
#define UP_Yaw			150.0		// Upper limit out
#define GainT_Roll		12.0		// Total Gain Roll Out Controller
#define GainT_Yaw		5.0			// Total Gain Yaw Out Controller
#define MaxOut_Roll		UP_Roll/GainT_Roll
#define MaxOut_Yaw		UP_Yaw/GainT_Yaw

#define Kp_ROLLPITCH 0.2		// Pitch&Roll Proportional Gain
#define Ki_ROLLPITCH 0.000001	// Pitch&Roll Integrator Gain

using namespace std;

static QString baseSpeechApi = "https://speech.googleapis.com/v1/speech:recognize";
static QString apiSpeechKey = "AIzaSyCY8Xg5wfn6Ld67287SGDBQPZvGCEN6Fsg";
static QString baseDuckduckgo = "http://api.duckduckgo.com";
static QString baseWikiApi = "https://tr.wikipedia.org/w/api.php";


enum SType
{
    TR,
    EN
};


template <typename T>
double mov_avg(vector<T> vec, int len){
  deque<T> dq = {};
  for(auto i = 0;i < vec.size();i++){
    if(i < len){
      dq.push_back(vec[i]);
    }
    else {
      dq.pop_front();
      dq.push_back(vec[i]);
    }
  }
  double cs = 0;
  for(auto i : dq){
    cs += i;
  }
  return cs / len;
}


template<class T>
const T& constrain(const T& x, const T& a, const T& b) {
    if(x < a) {
        return a;
    }
    else if(b < x) {
        return b;
    }
    else
        return x;
}

static char * appendChar(char * string1, char * string2)
{
    char * result = NULL;
    asprintf(&result, "%s%s", string1, string2);
    return result;
}


static void execCommand(char* cmd)
{
    auto command = appendChar(cmd, (char*)">>/dev/null 2>>/dev/null");
    char buffer[128];
    std::string result = "";
    FILE* pipe = popen(command, "r");
    if (!pipe) throw std::runtime_error("popen() failed!");
    try {
        while (!feof(pipe)) {
            if (fgets(buffer, 128, pipe) != nullptr)
                result += buffer;
        }
    } catch (...) {
        pclose(pipe);
        throw;
    }
    pclose(pipe);
}

static void getDeviceInfo (QString &device, QString &ip, QString &mac, QString &mask)
{

    bool found = false;
    foreach(QNetworkInterface interface, QNetworkInterface::allInterfaces())
    {
        unsigned int flags = interface.flags();
        bool isLoopback = (bool)(flags & QNetworkInterface::IsLoopBack);
        bool isP2P = (bool)(flags & QNetworkInterface::IsPointToPoint);
        bool isRunning = (bool)(flags & QNetworkInterface::IsRunning);
        if ( !isRunning ) continue;
        if ( !interface.isValid() || isLoopback || isP2P ) continue;

        foreach (QNetworkAddressEntry entry, interface.addressEntries())
        {
            // Ignore local host
            if ( entry.ip() == QHostAddress::LocalHost ) continue;

            // Ignore non-ipv4 addresses
            if ( !entry.ip().toIPv4Address() ) continue;

            if ( !found && interface.hardwareAddress() != "00:00:00:00:00:00" && entry.ip().toString().contains(".")
                 && !interface.humanReadableName().contains("VM"))
            {
                device = interface.humanReadableName();
                ip = entry.ip().toString();
                mac = interface.hardwareAddress();
                mask =  entry.netmask().toString();
                found = true;
            }
        }
    }
}

#endif // CONSTANTS_H
