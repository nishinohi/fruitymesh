#include "AtCommandController.h"

#include <FmTypes.h>
#include <Utility.h>
#include <mini-printf.h>

#include <initializer_list>

#include "GlobalState.h"
#include "Terminal.h"

#define WAKEUP_SIGNAL_TIME_MS 300
#define CONNECTION_WAIT_MS 10000  // 10 sec
#define CONNECT_ID_NUM 12

i32 AtCommandController::TokenizeResponse(char* response) {
    const char tokens[] = {' ', ','};
    return GS->terminal.TokenizeLine(response, strlen(response), tokens, 2);
}

// warning: If "waitLineFeedCode" is false, strlen(response) doesn't return correct value
template <class... T>
bool AtCommandController::ReadResponseAndCheck(const u32& timeout, const char* succcessResponse,
                                               const char* errorResponse, const bool& waitLineFeedCode,
                                               FruityHal::TimerHandler timeoutCallback, T... returnCodes) {
    u32 preTimeMs = FruityHal::GetRtcMs();
    GS->terminal.lineToReadAvailable = false;
    GS->terminal.ClearReadBufferOffset();
    const char* response = GS->terminal.GetReadBuffer();
    while (FruityHal::GetRtcMs() - preTimeMs < timeout) {
        if (!FruityHal::UartCheckInputAvailable()) { continue; }
        if (!ReadLine() && waitLineFeedCode) { continue; }
        if (CheckMultiResponse(response, errorResponse)) { return false; }
        if (CheckMultiResponse(response, succcessResponse)) {
            if (sizeof...(returnCodes) < 1) {
                GS->terminal.SeggerRttPutString("receive: ");
                GS->terminal.SeggerRttPutString(response);
                GS->terminal.SeggerRttPutString("\n");
                return true;
            }
            if (CheckResponseReturnCodes(response, returnCodes...)) {
                GS->terminal.SeggerRttPutString("receive success return Code\n");
                return true;
            }
            return false;
        }
    }
    // if (timeoutCallback != nullptr) { timeoutCallback(NULL); }
    GS->terminal.SeggerRttPutString("timeout\n");
    return false;
}

bool AtCommandController::CheckMultiResponse(const char* response, const char* checkStr) {
    char splitCheckStr[strlen(checkStr) + 1];
    strcpy(splitCheckStr, checkStr);
    char* cp = splitCheckStr;
    char* tp = strstr(splitCheckStr, "|");
    if (tp == NULL) { return strncmp(checkStr, response, strlen(checkStr)) == 0; }
    while (tp != NULL) {
        *tp = '\0';
        if (strncmp(response, cp, strlen(cp)) == 0) { return true; }
        cp = ++tp;
        tp = strstr(cp, "|");
        if (!tp) { return strncmp(response, cp, strlen(cp)) == 0; }
    }
    return false;
}

template <class... T>
bool AtCommandController::CheckResponseReturnCodes(const char* response, T... returnCodes) {
    const i32 commandArgsSize = TokenizeResponse(const_cast<char*>(response));
    if (commandArgsSize == -1 || static_cast<u32>(commandArgsSize) < sizeof...(returnCodes) + 1) { return false; }
    const char* returnCodeArgPtr;
    bool didError;
    u8 argsIndex = 1;
    for (i32 returnCode : std::initializer_list<i32>{returnCodes...}) {
        returnCodeArgPtr = GS->terminal.GetCommandArgsPtr()[argsIndex];
        // -1 is skipped
        if (returnCode == -1) {
            ++argsIndex;
            continue;
        }
        didError = false;
        i32 receivedReturnCode = Utility::StringToI16(returnCodeArgPtr, &didError);
        char temp[16];
        snprintf(temp, 16, "rc:%d\n", receivedReturnCode);
        GS->terminal.SeggerRttPutString(temp);
        if (didError || receivedReturnCode != returnCode) { return false; }
        ++argsIndex;
    }
    return true;
}

bool AtCommandController::ReadLine() {
    u8 byteBuffer = 0;
    // Read in an infinite loop until \r is recognized
    FruityHal::UartReadCharBlockingResult readCharBlockingResult = FruityHal::UartReadCharBlocking();
    if (readCharBlockingResult.didError) { GS->terminal.ClearReadBufferOffset(); }
    byteBuffer = readCharBlockingResult.c;
    char* readBuffer = GS->terminal.GetReadBuffer();

    if (!GS->terminal.UartCheckLineFeedCode(byteBuffer)) {
        CheckedMemcpy(readBuffer + GS->terminal.GetReadBufferOffset(), &byteBuffer, sizeof(u8));
        GS->terminal.SetReadBufferOffset(GS->terminal.GetReadBufferOffset() + 1);
        return false;
    }
    if (Conf::GetInstance().lineFeedCode == LineFeedCode::CRLF) {
        GS->terminal.SetReadBufferOffset(GS->terminal.GetReadBufferOffset() - 1);
    }
    readBuffer[GS->terminal.GetReadBufferOffset()] = '\0';
    // get only line feed code
    if (GS->terminal.GetReadBufferOffset() <= 0) { return false; }
    GS->terminal.SeggerRttPutString("ReadLine: ");
    GS->terminal.SeggerRttPutString(readBuffer);
    GS->terminal.SeggerRttPutString("\n");
    FruityHal::SetPendingEventIRQ();
    GS->terminal.ClearReadBufferOffset();
    return true;
}

template <class... T>
bool AtCommandController::SendAtCommandAndCheck(const char* atCommand, const u32& timeout, const char* succcessResponse,
                                                const char* errorResponse, const bool& waitLineFeedCode,
                                                FruityHal::TimerHandler timeoutCallback, T... returnCodes) {
    const u8 atCommandLen = strlen(atCommand) + 2;
    char atCommandWithCr[atCommandLen];
    snprintf(atCommandWithCr, atCommandLen, "%s\r", atCommand);
    GS->terminal.SeggerRttPutString("Send:");
    GS->terminal.SeggerRttPutString(atCommand);
    GS->terminal.SeggerRttPutString("\n");
    FruityHal::UartPutStringBlockingWithTimeout(atCommandWithCr);
    return ReadResponseAndCheck(timeout, succcessResponse, errorResponse, waitLineFeedCode, timeoutCallback,
                                returnCodes...);
}

void AtCommandController::Init() {
    FruityHal::GpioConfigureOutput(POWERSUPPLY_PIN);
    FruityHal::GpioConfigureOutput(POWERKEY_PIN);
}

void AtCommandController::PowerSupply(const bool& on) {
    on ? FruityHal::GpioPinSet(POWERSUPPLY_PIN) : FruityHal::GpioPinClear(POWERSUPPLY_PIN);
}

// TODO: reset function
bool AtCommandController::TurnOnOrReset(const u16& timeout) {
    // already started
    // TODO: reset
    if (SendAtCommandAndCheck("AT")) {
        if (!SendAtCommandAndCheck("ATE0")) { return false; }
        if (!SendAtCommandAndCheck("AT+QURCCFG=\"urcport\",\"uart1\"")) { return false; }
        return true;
    }
    // turn on signal
    FruityHal::GpioPinSet(POWERKEY_PIN);
    FruityHal::DelayMs(WAKEUP_SIGNAL_TIME_MS);
    FruityHal::GpioPinClear(POWERKEY_PIN);
    // receive start message
    if (!ReadResponseAndCheck(12000, "RDY")) { return false; }
    if (!ReadResponseAndCheck(3000, "+QIND: PB DONE")) { return false; }
    FruityHal::DelayMs(ATCOMMAND_TIMEOUT_MS);
    if (!SendAtCommandAndCheck("AT")) { return false; }
    if (!SendAtCommandAndCheck("ATE0")) { return false; }
    if (!SendAtCommandAndCheck("AT+QURCCFG=\"urcport\",\"uart1\"")) { return false; }
    return true;
}

bool AtCommandController::TurnOff(const u16& timeout) {
    if (!SendAtCommandAndCheck("AT+QPOWD", timeout, "POWERED DOWN")) {
        PowerSuspend();
        return false;
    }
    PowerSuspend();
    return true;
}

bool AtCommandController::WaitForPSRegistration(const u32& timeout) {
    u32 prev = FruityHal::GetRtcMs();
    auto CheckReturnCode = [this](char* response) -> bool {
        const i32 commandArgsSize = TokenizeResponse(response);
        if (commandArgsSize < 3) { return false; }
        const char* commandArgsPtr = GS->terminal.GetCommandArgsPtr()[2];  // check second return Code
        bool didError = false;
        i16 returnCode = Utility::StringToI16(commandArgsPtr, &didError);
        if (didError) { return false; }
        return returnCode == 1 || returnCode == 5;
    };
    char* response = GS->terminal.GetReadBuffer();
    bool result = false;
    while (FruityHal::GetRtcMs() - prev < timeout) {
        if (!SendAtCommandAndCheck("AT+CGREG?", ATCOMMAND_TIMEOUT_MS, "+CGREG")) { return false; }
        result = CheckReturnCode(response);
        if (!ReadResponseOK()) { return false; }
        if (result) { return true; }
        if (!SendAtCommandAndCheck("AT+CEREG?", ATCOMMAND_TIMEOUT_MS, "+CEREG")) { return false; }
        result = CheckReturnCode(response);
        if (!ReadResponseOK()) { return false; }
        if (result) { return true; }
    }
    return false;
}
bool AtCommandController::Activate(const char* accessPointName, const char* userName, const char* password,
                                   const u32& timeout) {
    if (!WaitForPSRegistration(2000)) {
        char command[256];
        snprintf(command, 256, "AT+QICSGP=1,1,\"%s\",\"%s\",\"%s\",3", accessPointName, userName, password);
        if (!SendAtCommandAndCheck(command)) { return false; }
        if (!WaitForPSRegistration(CONNECTION_WAIT_MS)) { return false; }
    }
    if (!SendAtCommandAndCheck("AT+QIACT=1", 15000, "OK|ERROR", "NON")) { return false; }
    if (!SendAtCommandAndCheck("AT+QIGETERROR", ATCOMMAND_TIMEOUT_MS, "+QIGETERROR", DEFAULT_ERROR, true, nullptr, 0)) {
        return false;
    }
    if (!ReadResponseOK()) { return false; }
    return true;
}

i8 AtCommandController::SocketOpen(const char* host, const u16& port, const SocketType& socketType) {
    if (host == NULL || host == nullptr || strlen(host) == 0) { return -1; }
    if (!SendAtCommandAndCheck("AT+QISTATE", CONNECTION_WAIT_MS, "OK|+QISTATE")) { return -1; }
    char* response = GS->terminal.GetReadBuffer();
    const char tokens[] = {' ', ','};
    bool didError;
    while (strncmp(response, "OK", 2) != 0) {
        if (TokenizeResponse(response) < 2) { return -1; }
        didError = false;
        i8 connectId = Utility::StringToI8(GS->terminal.GetCommandArgsPtr()[1], &didError);
        if (didError || connectId < 0 || CONNECT_ID_NUM <= connectId) { return -1; }
        connectIds[connectId] = true;
        if (!ReadResponseAndCheck(CONNECTION_WAIT_MS, "OK|+QISTATE")) { return -1; }
    }
    // check available connectId
    i8 connectId = -1;
    for (i8 _connectId = 0; _connectId < CONNECT_ID_NUM; ++_connectId) {
        connectId = _connectId;
        if (!connectIds[_connectId]) { break; }
    }
    if (!CheckValidConnectId(connectId)) { return -1; }
    char command[256];
    snprintf(command, 256, "AT+QIOPEN=1,%d,\"%s\",\"%s\",%d", connectId, socketType == SOCKET_TCP ? "TCP" : "UDP", host,
             port);
    if (!SendAtCommandAndCheck(command, CONNECTION_WAIT_MS, "OK")) { return -1; }
    if (!ReadResponseAndCheck(ATCOMMAND_TIMEOUT_MS, "+QIOPEN", DEFAULT_ERROR, true, nullptr, connectId, 0)) {
        return -1;
    }
    return connectId;
}

bool AtCommandController::SocketClose(const i8& _connectId) {
    if (!CheckValidConnectId(_connectId)) { return false; }
    char command[16];
    snprintf(command, 16, "AT+CLOSE=%d", _connectId);
    if (!SendAtCommandAndCheck(command, CONNECTION_WAIT_MS)) { return false; };
    connectIds[_connectId] = false;
    return true;
}

i32 AtCommandController::SocketReceive(const i8& _connectId, u8* data, const u16& dataSize) {
    if (!CheckValidConnectId(_connectId)) { return false; }
    char command[16];
    snprintf(command, 16, "AT+QIRD=%d", _connectId);
    if (!SendAtCommandAndCheck(command, 500, "+QIRD")) { return -1; }
    char* response = GS->terminal.GetReadBuffer();
    i32 argSize = TokenizeResponse(response);
    if (argSize < 2) { return -1; }
    bool didError = false;
    const u16 dataLen = Utility::StringToI16(GS->terminal.GetCommandArgsPtr()[1], &didError);
    if (dataSize < dataLen) { return -1; }
    if (didError) { return -1; }
    // nothing read
    if (dataLen == 0) {
        if (!ReadResponseOK()) { return -1; };
        return 0;
    }
    u32 prev = FruityHal::GetRtcMs();
    while (FruityHal::GetRtcMs() - prev < 1000) {
        if (ReadLine()) { break; }
    }
    // copy contain \0
    CheckedMemcpy(data, response, strlen(response) + 1);
    if (!ReadResponseOK()) { return -1; }
    return dataLen;
}

i32 AtCommandController::SocketReceive(const i8& _connectId, u8* data, const u16& dataSize, const u16& timeout) {
    if (!CheckValidConnectId(_connectId)) { return false; }
    u32 prev = FruityHal::GetRtcMs();
    i32 receiveLen = 0;
    while (FruityHal::GetRtcMs() - prev < timeout) {
        receiveLen = SocketReceive(_connectId, NULL, 2048);
        if (receiveLen == -1) { return -1; }
        // receive return code
        if (receiveLen == 3) {
            bool didError = false;
            const u16 returnCode = Utility::StringToU16(reinterpret_cast<const char*>(data), &didError);
            if (didError) { return receiveLen; }
            if (returnCode < 200 || returnCode > 299) { return receiveLen; }
            // there is available data
            FruityHal::DelayMs(100);
            continue;
        }
        if (receiveLen > 0) { return receiveLen; }
        FruityHal::DelayMs(100);
    }
    GS->terminal.SeggerRttPutString("socket read timeout");
    return -1;
}

bool AtCommandController::SocketSend(const i8& _connectId, const u8* data, const u16& dataSize) {
    if (!CheckValidConnectId(_connectId)) { return false; }
    char command[256];
    snprintf(command, 256, "AT+QISEND=%d,%d", _connectId, dataSize);
    if (!SendAtCommandAndCheck(command, ATCOMMAND_TIMEOUT_MS, "> ", DEFAULT_ERROR, false)) { return false; }
    u16 ii = 0;
    while (ii < dataSize) {
        GS->terminal.SeggerRttPutChar(data[ii]);
        ++ii;
    }
    GS->terminal.SeggerRttPutString("\n");
    FruityHal::UartPutDataBlockingWithTimeout(data, dataSize);
    if (!ReadResponseAndCheck(CONNECTION_WAIT_MS, "SEND OK")) { return false; }
    if (!ReadResponseAndCheck(CONNECTION_WAIT_MS, "+QIURC")) { return false; }
    return true;
}
