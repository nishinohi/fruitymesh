#include "EcTwoOneModuleClient.h"

#include <mini-printf.h>

#define RECEIVE_MAX_LENGTH (1500)

#define CONNECT_SUCCESS (1)
#define CONNECT_TIMED_OUT (-1)
#define CONNECT_INVALID_SERVER (-2)
#define CONNECT_TRUNCATED (-3)
#define CONNECT_INVALID_RESPONSE (-4)

EcTwoOneModuleClient::EcTwoOneModuleClient(AtCommandController* _atComCtl)
    : receiveQueue(receiveQueueBuffer, RECEIVE_BUFFER) {
    atComCtl = _atComCtl;
    _connectId = -1;
}

EcTwoOneModuleClient::~EcTwoOneModuleClient() {}

int EcTwoOneModuleClient::connect(IPAddress ip, uint16_t port) {
    if (connected()) return CONNECT_INVALID_RESPONSE;  // Already connected.
    char ipStr[16];
    snprintf(ipStr, 16, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
    int connectId = atComCtl->SocketOpen(ipStr, port, AtCommandController::SOCKET_TCP);
    if (connectId < 0) { return CONNECT_INVALID_SERVER; }
    _connectId = connectId;
    return CONNECT_SUCCESS;
}

int EcTwoOneModuleClient::connect(const char* host, uint16_t port) {
    if (connected()) { return CONNECT_INVALID_RESPONSE; }  // Already connected.
    int connectId = atComCtl->SocketOpen(host, port, AtCommandController::SOCKET_TCP);
    if (connectId < 0) return CONNECT_INVALID_SERVER;
    _connectId = connectId;
    return CONNECT_SUCCESS;
}

size_t EcTwoOneModuleClient::write(uint8_t data) {
    if (!connected()) return 0;
    if (!atComCtl->SocketSend(_connectId, &data, 1)) return 0;
    return 1;
}

size_t EcTwoOneModuleClient::write(const uint8_t* buf, size_t size) {
    if (!connected()) return 0;
    if (!atComCtl->SocketSend(_connectId, buf, size)) return 0;
    return size;
}

int EcTwoOneModuleClient::available() {
    if (!connected()) return 0;
    int receiveSize = atComCtl->SocketReceive(_connectId, receiveBuffer, RECEIVE_MAX_LENGTH);
    for (u16 index = 0; index < receiveSize; ++index) { receiveQueue.Put(&(receiveBuffer[index]), 1); }
    return receiveQueue._numElements;
}

int EcTwoOneModuleClient::read() {
    if (!connected()) return -1;
    int actualSize = available();
    if (actualSize <= 0) return -1;  // None is available.
    u8 data = *(receiveQueue.PeekNext().data);
    receiveQueue.DiscardNext();
    return data;
}

int EcTwoOneModuleClient::read(uint8_t* buf, size_t size) {
    if (!connected()) return 0;
    int actualSize = available();
    if (actualSize <= 0) return 0;  // None is available.

    int popSize = (unsigned)actualSize <= size ? actualSize : size;
    for (int i = 0; i < popSize; i++) {
        buf[i] = *(receiveQueue.PeekNext().data);
        receiveQueue.DiscardNext();
    }

    return popSize;
}

int EcTwoOneModuleClient::peek() {
    if (!connected()) return 0;
    int actualSize = available();
    if (actualSize <= 0) return -1;  // None is available.

    return *(receiveQueue.PeekNext().data);
}

void EcTwoOneModuleClient::flush() {
    // Nothing to do.
}

void EcTwoOneModuleClient::stop() {
    if (!connected()) return;
    atComCtl->SocketClose(_connectId);
    _connectId = -1;
    receiveQueue.Clean();
}

uint8_t EcTwoOneModuleClient::connected() { return _connectId >= 0; }

EcTwoOneModuleClient::operator bool() { return _connectId >= 0; }
