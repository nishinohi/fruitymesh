#include "AtCommandController.h"

#include <Utility.h>
#include <mini-printf.h>

#include <initializer_list>

#include "GlobalState.h"
#include "Terminal.h"
#include "types.h"

#define WAKEUP_SIGNAL_TIME_MS 300
#define CONNECTION_WAIT_MS 10000  // 10 sec
#define CONNECT_ID_NUM 12

// warning: If "waitLineFeedCode" is false, strlen(response) doesn't return correct value
template <class... T>
bool AtCommandController::ReadResponseAndCheck(const u32& timeout, const char* succcessResponse,
                                               const char* errorResponse, const bool& waitLineFeedCode,
                                               FruityHal::TimerHandler timeoutCallback, T... returnCodes) {
    u32 preTimeMs = FruityHal::GetRtcMs();
    GS->terminal.lineToReadAvailable = false;
    GS->terminal.ClearReadBufferOffset();
    const char* response = GS->terminal.getReadBuffer();
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
    const char tokens[2] = {' ', ','};
    const i32 commandArgsSize = GS->terminal.TokenizeLine(const_cast<char*>(response), strlen(response), tokens, 2);
    if (commandArgsSize == -1 || static_cast<u32>(commandArgsSize) < sizeof...(returnCodes) + 1) { return false; }
    const char* returnCodeArgPtr = GS->terminal.getCommandArgsPtr()[1];
    bool didError;
    for (i32 returnCode : std::initializer_list<i32>{returnCodes...}) {
        if (returnCode == -1) { continue; }  // -1 is skipped
        didError = false;
        i32 receivedReturnCode = Utility::StringToI16(returnCodeArgPtr, &didError);
        if (didError || receivedReturnCode != returnCode) { return false; }
        ++returnCodeArgPtr;
    }
    return true;
}

bool AtCommandController::ReadLine() {
    u8 byteBuffer = 0;
    // Read in an infinite loop until \r is recognized
    FruityHal::UartReadCharBlockingResult readCharBlockingResult = FruityHal::UartReadCharBlocking();
    if (readCharBlockingResult.didError) { GS->terminal.ClearReadBufferOffset(); }
    byteBuffer = readCharBlockingResult.c;
    char* readBuffer = GS->terminal.getReadBuffer();

    if (!GS->terminal.UartCheckLineFeedCode(byteBuffer)) {
        CheckedMemcpy(readBuffer + GS->terminal.getReadBufferOffset(), &byteBuffer, sizeof(u8));
        GS->terminal.setReadBufferOffset(GS->terminal.getReadBufferOffset() + 1);
        return false;
    }
    if (Conf::getInstance().lineFeedCode == LineFeedCode::CRLF) {
        GS->terminal.setReadBufferOffset(GS->terminal.getReadBufferOffset() - 1);
    }
    readBuffer[GS->terminal.getReadBufferOffset()] = '\0';
    // get only line feed code
    if (GS->terminal.getReadBufferOffset() <= 0) { return false; }
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
    auto CheckReturnCode = [](char* response) -> bool {
        const char tokens[2] = {' ', ','};
        const i32 commandArgsSize = GS->terminal.TokenizeLine(response, strlen(response), tokens, 2);
        if (commandArgsSize < 3) { return false; }
        const char* commandArgsPtr = GS->terminal.getCommandArgsPtr()[2];  // check second return Code
        bool didError = false;
        i16 returnCode = Utility::StringToI16(commandArgsPtr, &didError);
        if (didError) { return false; }
        return returnCode == 1 || returnCode == 5;
    };
    char* response = GS->terminal.getReadBuffer();
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
    if (!SendAtCommandAndCheck("AT+QIACT=1", 15000)) { return false; }
    if (!SendAtCommandAndCheck("AT+QIGETERROR", ATCOMMAND_TIMEOUT_MS, "+QIGETERROR", DEFAULT_ERROR, true, nullptr, 0)) {
        return false;
    }
    if (!ReadResponseOK()) { return false; }
    return true;
}

bool AtCommandController::SocketOpen(const char* host, const u16& port, const SocketType& socketType) {
    if (host == NULL || host == nullptr || strlen(host) == 0) { return false; }
    if (!SendAtCommandAndCheck("AT+QISTATE", CONNECTION_WAIT_MS, "OK|+QISTATE")) { return false; }
    char* response = GS->terminal.getReadBuffer();
    const char tokens[] = {' ', ','};
    bool didError;
    while (strncmp(response, "OK", 2) != 0) {
        if (GS->terminal.TokenizeLine(response, strlen(response), tokens, 2) < 2) { return false; }
        didError = false;
        i8 connectId = Utility::StringToI8(GS->terminal.getCommandArgsPtr()[1], &didError);
        if (didError || connectId < 0 || CONNECT_ID_NUM <= connectId) { return false; }
        connectIds[connectId] = true;
        if (!ReadResponseAndCheck(CONNECTION_WAIT_MS, "OK|+QISTATE")) { return false; }
    }
    // check available connectId
    for (u8 _connectId = 0; connectId < CONNECT_ID_NUM; ++_connectId) {
        connectId = _connectId;
        if (!connectIds[_connectId]) { break; }
    }
    if (connectId >= CONNECT_ID_NUM) {
        GS->terminal.SeggerRttPutString("There is no available connect IDs");
        return false;
    }
    char command[256];
    snprintf(command, 256, "AT+QIOPEN=1,%d,\"%s\",\"%s\",%d", connectId, socketType == SOCKET_TCP ? "TCP" : "UDP", host,
             port);
    if (!SendAtCommandAndCheck(command, CONNECTION_WAIT_MS, "OK")) { return false; }
    if (!ReadResponseAndCheck(ATCOMMAND_TIMEOUT_MS, "+QIOPEN", DEFAULT_ERROR, true, nullptr, connectId, 0)) {
        return false;
    }
    return true;
}
