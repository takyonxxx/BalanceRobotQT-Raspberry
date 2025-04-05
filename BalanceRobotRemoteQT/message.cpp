#include "message.h"

Message::Message()
{
}

bool Message::parse(uint8_t *dataUART, uint8_t size, MessagePack *message)
{
    // Safety check for null pointers and minimum packet size
    if (!dataUART || !message || size < 4) {
        return false;
    }

    // Check for valid header
    if (dataUART[0] != mHeader) {
        return false;
    }

    // Initialize message fields
    message->header = dataUART[0];
    message->len = dataUART[1];
    message->rw = dataUART[2];
    message->command = dataUART[3];

    // Safety check for data length
    if (message->len > MaxPayload || message->len > (size - 4)) {
        return false;  // Prevent buffer overflow
    }

    // Copy data with bounds checking
    int data_index = 0;
    for (int uart_index = 4; uart_index < (4 + message->len) && uart_index < size; uart_index++) {
        if (data_index < MaxPayload) {
            message->data[data_index++] = dataUART[uart_index];
        } else {
            return false;  // Buffer would overflow
        }
    }

    return true;
}

uint8_t Message::create_pack(uint8_t RW, uint8_t command, QByteArray dataSend, uint8_t *dataUART)
{
    // Safety check
    if (!dataUART) {
        return 0;
    }

    int dataSendLen = dataSend.length();

    // Limit length to prevent buffer overflow
    if (dataSendLen > MaxPayload) {
        dataSendLen = MaxPayload;
    }

    // Create header
    dataUART[0] = mHeader;
    dataUART[1] = uint8_t(dataSendLen);
    dataUART[2] = RW;
    dataUART[3] = command;

    // Copy data with bounds checking
    for (int data_index = 0; data_index < dataSendLen; data_index++) {
        dataUART[4 + data_index] = dataSend.at(data_index);
    }

    return dataSendLen + 4;
}
