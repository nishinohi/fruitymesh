#pragma once

#include <Client.h>

#include <queue>

#include "PacketQueue.h"

// #include "WioLTE.h"
#include "AtCommandController.h"
#define RECEIVE_BUFFER 2000

class EcTwoOneModuleClient : public Client {
   protected:
    AtCommandController* atComCtl;
    i8 _connectId;
    u8 receiveBuffer[1600];
    u32 receiveQueueBuffer[RECEIVE_BUFFER / sizeof(u32)];
    PacketQueue receiveQueue;

   public:
    EcTwoOneModuleClient();
    EcTwoOneModuleClient(AtCommandController* wio);
    virtual ~EcTwoOneModuleClient();

    virtual int connect(IPAddress ip, uint16_t port);
    virtual int connect(const char* host, uint16_t port);
    virtual size_t write(uint8_t data);
    virtual size_t write(const uint8_t* buf, size_t size);
    virtual int available();
    virtual int read();
    virtual int read(uint8_t* buf, size_t size);
    virtual int peek();
    virtual void flush();
    virtual void stop();
    virtual uint8_t connected();
    virtual operator bool();
};
