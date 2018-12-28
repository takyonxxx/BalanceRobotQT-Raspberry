#include "message.h"
#include "iostream"
#include "QDebug"
#include <QBuffer>
#include <QDataStream>
#include <type_traits>

class SerialSize {
public:
  SerialSize() : stream(&data) { data.open(QIODevice::WriteOnly); }

  template <typename T>
  quint64 operator ()(const T & t) {
    data.seek(0);
    stream << t;
    return data.pos();
  }

private:
  QBuffer data;
  QDataStream stream;
};

Message::Message()
{
}

uint8_t Message::parse(uint8_t *dataUART, uint8_t size, MessagePack *message)
{
    static uint16_t data_index=0;
    static uint16_t checksum=0;
    int16_t uart_index=-1;
    if (data_index==0){
        checksum=0;       
        if (dataUART[0]!=mHeader) return 0;
        message->header = dataUART[0];
        message->len=dataUART[1];
        checksum=checksum+dataUART[1];
        message->rw=dataUART[2];
        checksum=checksum+dataUART[2];
        message->command=dataUART[3];
        checksum=checksum+dataUART[3];
        data_index=0;
        uart_index=3;
    }   

    while(data_index<(message->len)){
        uart_index++;       
        if (uart_index==size) return 1;//mensaje incompleto, espera nuevo

        message->data[data_index] = (dataUART[uart_index]);
        checksum = checksum + message->data[data_index];
        data_index++;
    }

    data_index=0;
    checksum= checksum ^ 0xFFFF;//xor   

    message->CheckSum[0]=(checksum & 0xff);
    message->CheckSum[1]=(checksum>>8);

    if (((checksum>>8)== message->CheckSum[1] )&& ((checksum & 0xff)== message->CheckSum[0])) return 1;
    else return 0;
}

//creates a pack ready to serialyze
uint8_t Message::create_pack(uint8_t RW, uint8_t command, QByteArray dataSend, uint8_t *dataUART)
{
    static uint16_t checksum=0;
    static uint16_t data_index=0;

    checksum=0;

    MessagePack message;
    int dataSendLen = dataSend.length();

    message.header = mHeader;
    message.len = uint8_t (dataSendLen);
    checksum = checksum + message.len;    
    message.rw = RW;
    checksum = checksum + message.rw;
    message.command = command;
    checksum = checksum + message.command;

    for(data_index = 0; data_index< dataSendLen; data_index++)
    {
        message.data[data_index] = dataSend.at(data_index);
        checksum = checksum + message.data[data_index];
    }

    checksum= checksum ^ 0xFFFF;//xor

    message.CheckSum[0]=(checksum & 0xff);
    message.CheckSum[1]=(checksum>>8);

    dataUART[0]=message.header;
    dataUART[1]=message.len;
    dataUART[2]=message.rw;
    dataUART[3]=message.command;

    for(data_index = 0; data_index< dataSendLen; data_index++)
    {
        dataUART[4+data_index] = message.data[data_index];
    }

    dataUART[message.len + 4] = message.CheckSum[0];
    dataUART[message.len + 5] = message.CheckSum[1];

    return dataSendLen + 6;
}

