#include "message.h"

Message::Message()
{
}

bool Message::parse(uint8_t *dataUART, uint8_t size, MessagePack *message)
{
    static uint16_t data_index=0;
    int16_t uart_index=-1;

    if (dataUART[0]!= mHeader) return false;

    message->header = dataUART[0];
    message->len = dataUART[1];
    message->rw = dataUART[2];
    message->command = dataUART[3];

    data_index=0;
    uart_index=3;

    while(data_index<(message->len)){
        uart_index++;
        if (uart_index==size) break;

        message->data[data_index] = (dataUART[uart_index]);
        data_index++;
    }

    return true;
}

//creates a pack ready to serialyze
uint8_t Message::create_pack(uint8_t RW, uint8_t command, QByteArray dataSend, uint8_t *dataUART)
{   
    static uint16_t data_index=0;

    int dataSendLen = dataSend.length();

    dataUART[0]= mHeader;
    dataUART[1]= uint8_t (dataSendLen);
    dataUART[2]= RW;
    dataUART[3]= command;

    for(data_index = 0; data_index< dataSendLen; data_index++)
    {
        dataUART[4+data_index] = dataSend.at(data_index);
    }

    return dataSendLen + 4;
}

