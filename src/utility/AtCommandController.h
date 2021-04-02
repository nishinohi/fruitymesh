#pragma once

#include <FruityHal.h>

#define DEFAULT_SUCCESS "OK"
#define DEFAULT_ERROR "ERROR"
#define POWERSUPPLY_PIN 22
#define POWERKEY_PIN 23
#define ATCOMMAND_TIMEOUT_MS 300
#define SORACOM_APN "soracom.io"
#define SORACOM_USERNAME "sora"
#define SORACOM_PASSWORD "sora"
#define CONNECT_ID_NUM 12

class AtCommandController {
   private:
    i32 TokenizeResponse(char* response);
    template <class... T>
    bool ReadResponseAndCheck(const u32& timeout = ATCOMMAND_TIMEOUT_MS, const char* succcessResponse = DEFAULT_SUCCESS,
                              const char* errorResponse = DEFAULT_ERROR, const bool& waitLineFeedCode = true,
                              FruityHal::TimerHandler timeoutCallback = nullptr, T... returnCodes);
    bool CheckMultiResponse(const char* response, const char* checkStr);
    bool ReadResponseOK(const u32& timeout = ATCOMMAND_TIMEOUT_MS) { return ReadResponseAndCheck(timeout); }
    // this method is only use for i16 type
    template <class... T>
    bool CheckResponseReturnCodes(const char* response, T... returnCodes);
    bool ReadLine();  // true: receive line feed code, false: not receive line feed code
    template <class... T>
    bool SendAtCommandAndCheck(const char* atCommand, const u32& timeout = ATCOMMAND_TIMEOUT_MS,
                               const char* succcessResponse = DEFAULT_SUCCESS,
                               const char* errorResponse = DEFAULT_ERROR, const bool& waitLineFeedCode = true,
                               FruityHal::TimerHandler timeoutCallback = nullptr, T... returnCodes);

    bool WaitForPSRegistration(const u32& timeout = 120000);
    bool CheckValidConnectId(const i8& _connectId) const { return _connectId >= 0 && _connectId < CONNECT_ID_NUM; }
    // Socket Open
    bool connectIds[CONNECT_ID_NUM];

   public:
    enum SocketType { SOCKET_TCP = 0, SOCKET_UDP };

    void Init();
    void PowerSupply(const bool& on);
    void PowerSuspend() { FruityHal::GpioPinClear(POWERSUPPLY_PIN); }
    bool TurnOnOrReset(const u16& timeout = 12000);
    bool TurnOff(const u16& timeout);
    bool Activate(const char* accessPointName = SORACOM_APN, const char* userName = SORACOM_USERNAME,
                  const char* password = SORACOM_PASSWORD, const u32& timeout = 120000);
    bool Deactivate() { return SendAtCommandAndCheck("AT+QIDEACT", 10000); }
    i8 SocketOpen(const char* host, const u16& port, const SocketType& socketType);
    bool SocketClose(const i8& _connectId);
    i32 SocketReceive(const i8& _connectId, u8* data, const u16& dataSize);
    i32 SocketReceive(const i8& _connectId, u8* data, const u16& dataSize, const u16& timeout);
    bool SocketSend(const i8& _connectId, const u8* data, const u16& dataSize);
};