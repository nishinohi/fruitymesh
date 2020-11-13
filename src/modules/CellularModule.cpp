////////////////////////////////////////////////////////////////////////////////
// /****************************************************************************
// **
// ** Copyright (C) 2015-2020 M-Way Solutions GmbH
// ** Contact: https://www.blureange.io/licensing
// **
// ** This file is part of the Bluerange/FruityMesh implementation
// **
// ** $BR_BEGIN_LICENSE:GPL-EXCEPT$
// ** Commercial License Usage
// ** Licensees holding valid commercial Bluerange licenses may use this file in
// ** accordance with the commercial license agreement provided with the
// ** Software or, alternatively, in accordance with the terms contained in
// ** a written agreement between them and M-Way Solutions GmbH.
// ** For licensing terms and conditions see https://www.bluerange.io/terms-conditions. For further
// ** information use the contact form at https://www.bluerange.io/contact.
// **
// ** GNU General Public License Usage
// ** Alternatively, this file may be used under the terms of the GNU
// ** General Public License version 3 as published by the Free Software
// ** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
// ** included in the packaging of this file. Please review the following
// ** information to ensure the GNU General Public License requirements will
// ** be met: https://www.gnu.org/licenses/gpl-3.0.html.
// **
// ** $BR_END_LICENSE$
// **
// ****************************************************************************/
////////////////////////////////////////////////////////////////////////////////

#include <CellularModule.h>
#include <Logger.h>
#include <Node.h>
#include <Utility.h>
#include <mini-printf.h>

#include "GlobalState.h"
#include "Terminal.h"

#define CELLSEND_TAG "CELLSEND"
#define WAKEUP_SIGNAL_TIME_DS 3  // 300msec
#define ATCOMMAND_TIMEOUTDS 5    // 300msec
#define DELAY_RESPONSE "$$"

constexpr u8 CELLULAR_MODULE_CONFIG_VERSION = 1;

CellularModule::CellularModule()
    : Module(ModuleId::CELLULAR_MODULE, "cellular"),
      atCommandSequenceQueue(atCommandSequenceBuffer, ATCOMMAND_SEQUENCE_BUFFER),
      atCommandQueue(atCommandBuffer, ATCOMMAND_BUFFER),
      responseQueue(responseBuffer, RESPONSE_BUFFER) {
    // Register callbacks n' stuff

    // Save configuration to base class variables
    // sizeof configuration must be a multiple of 4 bytes
    configurationPointer = &configuration;
    configurationLength = sizeof(CellularModuleConfiguration);

    // Set defaults
    ResetToDefaultConfiguration();
}

void CellularModule::ResetToDefaultConfiguration() {
    // Set default configuration values
    configuration.moduleId = moduleId;
    configuration.moduleActive = true;
    configuration.moduleVersion = CELLULAR_MODULE_CONFIG_VERSION;
    // Set additional config values...
}

void CellularModule::ConfigurationLoadedHandler(ModuleConfiguration* migratableConfig, u16 migratableConfigLength) {
    // Version migration can be added here, e.g. if module has version 2 and config is version 1
    if (migratableConfig->moduleVersion == 1) { /* ... */
    };

    // Do additional initialization upon loading the config

    // Start the Module...
}

void CellularModule::TimerEventHandler(u16 passedTimeDs) {
    // Do stuff on timer...
    // ProcessWakeup(passedTimeDs);
    ProcessResponseTimeout(passedTimeDs);
}

void CellularModule::InitializeResponseCallback(ResponseCallback* responseCallback) {
    responseCallback->timeoutDs = 0;
    responseCallback->responseTimeoutType = SUSPEND;
    responseCallback->successCallback = nullptr;
    responseCallback->errorCallback = nullptr;
    responseCallback->timeoutCallback = nullptr;
}

void CellularModule::ProcessAtCommandSequenceQueue() {
    if (IsEmptyQueue(atCommandSequenceQueue)) { return; }
    if (commandSequenceStatus != ErrorType::SUCCESS) {
        atCommandSequenceQueue.Clean();
        return;
    }
    if (!IsEmptyQueue(atCommandQueue)) { CleanAtCommandQueue(); }
    if (!IsEmptyQueue(responseQueue)) { CleanResponseQueue(); }
    (this->*GetSequenceCallback())();
    atCommandSequenceQueue.DiscardNext();
}

bool CellularModule::PushAtCommandSequenceQueue(const AtCommandCallback& sequenceCallback, const u8& retryCount) {
    AtComandSequenceCallback atCommandSequenceCallback = {.sequenceCallback = sequenceCallback,
                                                          .retryCount = retryCount};
    return atCommandSequenceQueue.Put(reinterpret_cast<u8*>(&atCommandSequenceCallback),
                                      sizeof(AtComandSequenceCallback));
}

void CellularModule::SendAtCommand(char* atCommand, const u16& atCommandLen) {
    char atCommandWithCr[atCommandLen + 2];
    CheckedMemset(atCommandWithCr, '\0', atCommandLen + 2);
    CheckedMemcpy(atCommandWithCr, atCommand, atCommandLen);
    atCommandWithCr[atCommandLen] = '\r';
    GS->terminal.SeggerRttPutString("Send AT Command:");
    GS->terminal.PutString(atCommandWithCr);
    logt("Send AT Command:%s", atCommandWithCr);
}

void CellularModule::DiscardResponseQueue() {
    const u8 returnCodeNum = GetReturnCodeNum();
    // 4 = [successStr], [errorStr], [returnCodeNum], [ResponseCallback]
    for (int ii = 0; ii < 4 + returnCodeNum; ++ii) { responseQueue.DiscardNext(); }
    responsePassedTimeDs = 0;
}

void CellularModule::CleanResponseQueue() {
    responseQueue.Clean();
    responsePassedTimeDs = 0;
}

void CellularModule::ProcessAtCommandQueue() {
    if (IsEmptyQueue(atCommandQueue)) { return; }
    SendAtCommand(GetAtCommandStr(), GetAtCommandStrLength());
    DiscardAtCommandQueue();
}

bool CellularModule::PushAtCommandQueue(const char* atCommand) {
    // Queue ATCommand and response
    if (atCommand == NULL || strlen(atCommand) < 1) { return false; }
    return atCommandQueue.Put(reinterpret_cast<u8*>(const_cast<char*>(atCommand)), strlen(atCommand));
}

template <class... ReturnCodes>
bool CellularModule::PushResponseQueue(const u16& timeoutDs, const AtCommandCallback& successCallback,
                                       const AtCommandCallback& errorCallback, const AtCommandCallback& timeoutCallback,
                                       const AtCommandCustomCallback& responseCustomCallback, const char* successStr,
                                       const char* errorStr, const ResponseTimeoutType& responseTimeoutType,
                                       ReturnCodes... returnCodes) {
    // Queue ATCommand and response
    if (successStr == NULL || strlen(successStr) < 1 || errorStr == NULL || strlen(errorStr) < 1) { return false; }
    if (!responseQueue.Put(reinterpret_cast<u8*>(const_cast<char*>(successStr)), strlen(successStr))) { return false; }
    if (!responseQueue.Put(reinterpret_cast<u8*>(const_cast<char*>(errorStr)), strlen(errorStr))) {
        responseQueue.DiscardLast();  // success str
        return false;
    }
    u8 returnCodeNum = static_cast<u8>(sizeof...(returnCodes));
    if (!responseQueue.Put(reinterpret_cast<u8*>(&returnCodeNum), sizeof(u8))) {
        responseQueue.DiscardLast();  // success str
        responseQueue.DiscardLast();  // error str
        return false;
    }
    if (returnCodeNum > 0) {
        if (!PushReturnCodes(returnCodes...)) {
            responseQueue.DiscardLast();  // success str
            responseQueue.DiscardLast();  // error str
            responseQueue.DiscardLast();  // return code num
            return false;
        };
    }
    // Queue ResponseCallback
    ResponseCallback newResponseCallback = {
        .timeoutDs = timeoutDs,
        .responseTimeoutType = responseTimeoutType,
        .successCallback = successCallback == NULL ? nullptr : successCallback,
        .errorCallback = errorCallback == NULL ? nullptr : errorCallback,
        .timeoutCallback = timeoutCallback == NULL ? nullptr : timeoutCallback,
        .responseCustomCallback = responseCustomCallback == NULL ? nullptr : responseCustomCallback};
    if (!responseQueue.Put(reinterpret_cast<u8*>(&newResponseCallback), sizeof(ResponseCallback))) {
        responseQueue.DiscardLast();  // success str
        responseQueue.DiscardLast();  // error str
        responseQueue.DiscardLast();  // return code num
        return false;
    }
    return true;
}

template <class Head, class... Tail>
bool CellularModule::PushReturnCodes(Head head, Tail... tail) {
    if (!responseQueue.Put(reinterpret_cast<u8*>(&head), sizeof(Head))) { return false; }
    if (!PushReturnCodes(tail...)) {
        responseQueue.DiscardLast();
        return false;
    }
    return true;
}

bool CellularModule::PushDelayQueue(const u8& delayDs, const AtCommandCallback& delayCallback) {
    return PushResponseQueue(delayDs, nullptr, nullptr, delayCallback, nullptr, DELAY_RESPONSE, DELAY_RESPONSE, DELAY);
}

void CellularModule::ProcessResponseQueue(const char* response) {
    if (IsEmptyQueue(responseQueue)) { return; }
    lastArg = const_cast<char*>(response);
    // check error str
    const u16 errorStrLen = GetErrorStrLength();
    char errorStr[errorStrLen + 1];
    CheckedMemset(errorStr, '\0', errorStrLen + 1);
    CheckedMemcpy(errorStr, GetErrorStr(), errorStrLen);
    if (strncmp(errorStr, response, errorStrLen) == 0) {
        GS->terminal.SeggerRttPutString("get error:");
        GS->terminal.SeggerRttPutString(errorStr);
        AtCommandCallback errorCallback = GetErrorCallback();
        if (errorCallback != nullptr) { (this->*errorCallback)(); }
        responsePassedTimeDs = 0;
        return;
    }
    // custom callback
    AtCommandCustomCallback responseCustomCallback = GetResopnseCustomCallback();
    if (responseCustomCallback != nullptr) { (this->*responseCustomCallback)(const_cast<char*>(response)); }
    // check success str
    const u16 successStrLen = GetSuccessStrLength();
    char successStr[successStrLen + 1];
    CheckedMemset(successStr, '\0', successStrLen + 1);
    CheckedMemcpy(successStr, GetSuccessStr(), successStrLen);
    if (strncmp(successStr, response, successStrLen) != 0) { return; }
    GS->terminal.SeggerRttPutString("get response:");
    GS->terminal.SeggerRttPutString(successStr);
    logt(CELLSEND_TAG, "get response:%s", successStr);
    // check return code
    const i8 returnCodeNum = GetReturnCodeNum();
    if (returnCodeNum != 0) {
        const char tokens[2] = {' ', ','};
        GS->terminal.TokenizeLine(const_cast<char*>(response), strlen(response), tokens);
        const char** commandArgsPtr = GS->terminal.getCommandArgsPtr();
        bool didError;
        for (u8 ii = 0; ii < returnCodeNum; ++ii) {
            const i8 returnCode = GetReturnCode(ii);
            if (returnCode == -1) { continue; }  // -1 is non check return code
            didError = false;
            i8 receiveReturnCode = Utility::StringToI8(commandArgsPtr[ii + 1], &didError);
            if (didError || receiveReturnCode != returnCode) {
                GS->terminal.SeggerRttPutString("wrong return code:");
                GS->terminal.SeggerRttPutString(commandArgsPtr[ii]);
                AtCommandCallback errorCallback = GetErrorCallback();
                if (errorCallback != nullptr) { (this->*errorCallback)(); }
                responsePassedTimeDs = 0;
                return;
            }
        }
    }
    GS->terminal.SeggerRttPutString("success response\n");
    AtCommandCallback successCallback = GetSuccessCallback();
    DiscardResponseQueue();
    if (successCallback != nullptr) { (this->*successCallback)(); }
}

void CellularModule::ProcessResponseTimeout(u16 passedTimeDs) {
    if (IsEmptyQueue(responseQueue)) { return; }
    // TODO: fatal error
    u16 timeoutDs = GetResponseTimeoutDs();
    if (timeoutDs != 0 && responsePassedTimeDs < timeoutDs) {
        responsePassedTimeDs += passedTimeDs;
        return;
    }
    responsePassedTimeDs = 0;
    // #if IS_ACTIVE(LOGGING)
    LoggingTimeoutResponse();
    // #endif
    const ResponseTimeoutType responseTimeoutType = GetResponseTimeoutType();
    const AtCommandCallback timeoutCallback = GetTimeoutCallback();
    switch (responseTimeoutType) {
        case SUSPEND:
            CleanAtCommandQueue();
            CleanResponseQueue();
            break;
        case CONTINUE_NON_DISCARD:
            break;
        case CONTINUE:
        case DELAY:
            DiscardResponseQueue();
            break;
        default:
            break;
    }
    if (timeoutCallback != nullptr) { (this->*(timeoutCallback))(); }
    return;
}

// TODO: add more info
void CellularModule::LoggingTimeoutResponse() {
    u16 successStrLen = GetSuccessStrLength();
    char response[successStrLen + 1];
    CheckedMemset(response, '\0', successStrLen + 1);
    CheckedMemcpy(response, GetSuccessStr(), successStrLen);
    GS->terminal.SeggerRttPutString("AT Command response timeout: ");
    GS->terminal.SeggerRttPutString(response);
    GS->terminal.SeggerRttPutChar('\n');
}

void CellularModule::Wakeup() {
    GS->terminal.SeggerRttPutString("Wakeup function fire\n");
    SupplyPower();
    TurnOn();
}

void CellularModule::SupplyPower() {
    FruityHal::GpioConfigureOutput(POWERSUPPLY_PIN);
    FruityHal::GpioPinSet(POWERSUPPLY_PIN);
    ChangeStatusPowerSupply();
}

void CellularModule::TurnOn() {
    FruityHal::GpioConfigureOutput(POWERKEY_PIN);
    PowerKeyPinSet();
    PushDelayQueue(WAKEUP_SIGNAL_TIME_DS, &CellularModule::PowerKeyPinClear);
    PushDelayQueue(0, &CellularModule::ChangeStatusWakingup);
    // waking up module needs 15sec.
    PushResponseQueue(150, nullptr, &CellularModule::WakeupFailedCallback, &CellularModule::WakeupFailedCallback,
                      nullptr, "RDY");
    // After receive following packet, check module accepting AT command
    PushResponseQueue(ATCOMMAND_TIMEOUTDS * 5, nullptr, &CellularModule::WakeupFailedCallback,
                      &CellularModule::WakeupFailedCallback, nullptr, "+QIND: PB DONE");
    PushDelayQueue(5, &CellularModule::ProcessAtCommandQueue);
    PushAtCommandQueue("AT");
    PushResponseQueue(ATCOMMAND_TIMEOUTDS, &CellularModule::ProcessAtCommandQueue,
                      &CellularModule::WakeupFailedCallback, &CellularModule::WakeupFailedCallback);
    PushAtCommandQueue("ATE0");
    PushResponseQueue(ATCOMMAND_TIMEOUTDS, &CellularModule::ProcessAtCommandQueue,
                      &CellularModule::WakeupFailedCallback, &CellularModule::WakeupFailedCallback);
    PushAtCommandQueue("AT+QURCCFG=\"urcport\",\"uart1\"");
    PushResponseQueue(ATCOMMAND_TIMEOUTDS, &CellularModule::WakeupSuccessCallback,
                      &CellularModule::WakeupFailedCallback, &CellularModule::WakeupFailedCallback);
    // note: Add following commands if you need. [AT+QSCLK=1],[AT+CPIN?]
}

void CellularModule::WakeupFailedCallback() {
    GS->terminal.SeggerRttPutString("Turn on failed\n");
    ChangeStatusShutdown();
    CleanResponseQueue();
    CleanAtCommandQueue();
    commandSequenceStatus = ErrorType::INTERNAL;
    ProcessAtCommandSequenceQueue();
}

void CellularModule::WakeupSuccessCallback() {
    GS->terminal.SeggerRttPutString("Turn on success\n");
    ChangeStatusWakeuped();
    commandSequenceStatus = ErrorType::SUCCESS;
    ProcessAtCommandSequenceQueue();
}

void CellularModule::TurnOff() {
    PushAtCommandQueue("AT+QPOWD");
    PushResponseQueue(100, &CellularModule::SuspendPower, nullptr, &CellularModule::SuspendPower, nullptr,
                      "POWERED DOWN");
    ProcessAtCommandQueue();
}

void CellularModule::SimActivate() {
    GS->terminal.SeggerRttPutString("SimActivate function fire\n");
    isNetworkStatusReCheck = false;
    CheckNetworkRegistrationStatus();
}

// TODO: When called CheckNetworkRegistrationStatus for second time and failed, call SimActivateFailed
void CellularModule::ConfigTcpIpParameter() {
    CleanAtCommandQueue();
    CleanResponseQueue();
    isNetworkStatusReCheck = true;
    PushAtCommandQueue("AT+QICSGP=1,1,\"soracom.io\",\"sora\",\"sora\",3");
    PushResponseQueue(ATCOMMAND_TIMEOUTDS, &CellularModule::CheckNetworkRegistrationStatus,
                      &CellularModule::SimActivateFailed, &CellularModule::SimActivateFailed);
    ProcessAtCommandQueue();
}

void CellularModule::CheckNetworkRegistrationStatus() {
    PushAtCommandQueue("AT+CGREG?");
    PushResponseQueue(
        ATCOMMAND_TIMEOUTDS * 10, &CellularModule::ActivatePdpContext,
        isNetworkStatusReCheck ? &CellularModule::SimActivateFailed : &CellularModule::ConfigTcpIpParameter,
        &CellularModule::SimActivateFailed, nullptr, "+CGREG", DEFAULT_ERROR,
        isNetworkStatusReCheck ? SUSPEND : CONTINUE_NON_DISCARD, -1, 1);
    // never call following command and receving response
    // PushAtCommandQueue("AT+CEREG?");
    // PushResponseQueue(
    //     ATCOMMAND_TIMEOUTDS, &CellularModule::ActivatePdpContext,
    //     isNetworkStatusReCheck ? &CellularModule::SimActivateFailed : &CellularModule::ConfigTcpIpParameter,
    //     &CellularModule::SimActivateFailed, "+CEREG", DEFAULT_ERROR,
    //     isNetworkStatusReCheck ? SUSPEND : CONTINUE_NON_DISCARD, -1, 1);
    ProcessAtCommandQueue();
}

void CellularModule::ActivatePdpContext() {
    CleanAtCommandQueue();
    CleanResponseQueue();
    PushAtCommandQueue("AT+QIACT=1");
    PushResponseQueue(1000, &CellularModule::ProcessAtCommandQueue);
    PushAtCommandQueue("AT+QIGETERROR");
    PushResponseQueue(ATCOMMAND_TIMEOUTDS, &CellularModule::SimActivateSuccess, &CellularModule::SimActivateFailed,
                      &CellularModule::SimActivateFailed);
    ProcessAtCommandQueue();
}

void CellularModule::SimActivateSuccess() {
    GS->terminal.SeggerRttPutString("sim activate success\n");
    isNetworkStatusReCheck = false;
    ChangeStatusSimActivated();
    commandSequenceStatus = ErrorType::SUCCESS;
    ProcessAtCommandSequenceQueue();
}
void CellularModule::SimActivateFailed() {
    GS->terminal.SeggerRttPutString("sim activate failed\n");
    isNetworkStatusReCheck = false;
    ChangeStatusWakeuped();
    CleanAtCommandQueue();
    CleanResponseQueue();
    commandSequenceStatus = ErrorType::INTERNAL;
    ProcessAtCommandSequenceQueue();
}

void CellularModule::SocketOpen() {
    GS->terminal.SeggerRttPutString("Socket Open function fired\n");
    CheckedMemset(connectedIds, 0, sizeof(connectedIds));
    PushAtCommandQueue("AT+QISTATE?");
    PushResponseQueue(ATCOMMAND_TIMEOUTDS, &CellularModule::PushSocketOpenCommandAndResponse,
                      &CellularModule::SocketOpenFailed, &CellularModule::SocketOpenFailed,
                      &CellularModule::ParseConnectedId);
    ProcessAtCommandQueue();
}

void CellularModule::PushSocketOpenCommandAndResponse() {
    u8 _connectId;
    for (_connectId = 0; _connectId < CONNECT_ID_NUM; ++_connectId) {
        connectId = _connectId;
        if (!connectedIds[_connectId]) { break; }
    }
    if (_connectId == CONNECT_ID_NUM) {
        SocketOpenFailed();
        return;
    }
    char socketOpenCommand[128];
    snprintf(socketOpenCommand, 128, "AT+QIOPEN=1,%d,\"UDP\",\"harvest.soracom.io\",8514", connectId);
    PushAtCommandQueue(socketOpenCommand);
    PushResponseQueue(1500, &CellularModule::SocketOpenSuccess, &CellularModule::SocketOpenFailed,
                      &CellularModule::SocketOpenFailed, nullptr, "+QIOPEN:", DEFAULT_ERROR, SUSPEND, connectId, 0);
    ProcessAtCommandQueue();
}

void CellularModule::SocketOpenSuccess() {
    GS->terminal.SeggerRttPutString("socket open success\n");
    commandSequenceStatus = ErrorType::SUCCESS;
    ProcessAtCommandSequenceQueue();
}

void CellularModule::SocketOpenFailed() {
    GS->terminal.SeggerRttPutString("socket open failed\n");
    commandSequenceStatus = ErrorType::INTERNAL;
    ProcessAtCommandSequenceQueue();
}

void CellularModule::ParseConnectedId(void* _response) {
    const char* response = reinterpret_cast<char*>(_response);
    const char* checkStr = "+QISTATE";
    const u8 checkLen = strlen(checkStr);
    if (strlen(response) < checkLen || strncmp(response, checkStr, checkLen) != 0) { return; }
    const char tokens[2] = {' ', ','};
    const i32 commandArgsSize = GS->terminal.TokenizeLine(const_cast<char*>(response), strlen(response), tokens);
    if (commandArgsSize < 2) { return; }
    const char** commandArgsPtr = GS->terminal.getCommandArgsPtr();
    bool didError = false;
    u8 connectedId = Utility::StringToU8(commandArgsPtr[1], &didError);
    if (didError) { return; }
    connectedIds[connectedId] = true;
}

void CellularModule::SendFiredNodeList() {
    char pushQueueBuffer[18];
    snprintf(pushQueueBuffer, 18, "AT+QISEND=%d,%d", connectId, strlen(sendBuffer));
    PushAtCommandQueue(pushQueueBuffer);
    PushDelayQueue(ATCOMMAND_TIMEOUTDS, &CellularModule::SendBuffer);
    PushResponseQueue(ATCOMMAND_TIMEOUTDS * 10, nullptr, &CellularModule::SendFiredNodeListFailed,
                      &CellularModule::SendFiredNodeListFailed, nullptr, "SEND OK");
    PushResponseQueue(ATCOMMAND_TIMEOUTDS * 10, &CellularModule::ProcessAtCommandQueue,
                      &CellularModule::SendFiredNodeListFailed, &CellularModule::SendFiredNodeListFailed, nullptr,
                      "+QIURC");
    snprintf(pushQueueBuffer, 18, "AT+QISEND=%d,0", connectId);
    PushAtCommandQueue(pushQueueBuffer);
    PushResponseQueue(ATCOMMAND_TIMEOUTDS * 10, &CellularModule::SendFiredNodeListSuccess,
                      &CellularModule::SendFiredNodeListFailed, &CellularModule::SendFiredNodeListFailed, nullptr,
                      "+QISEND", DEFAULT_ERROR, SUSPEND, strlen(sendBuffer));
    ProcessAtCommandQueue();
}
void CellularModule::SendFiredNodeListSuccess() {
    GS->terminal.SeggerRttPutString("SendPacketSuccess");
    commandSequenceStatus = ErrorType::SUCCESS;
    ProcessAtCommandSequenceQueue();
}
void CellularModule::SendFiredNodeListFailed() {
    GS->terminal.SeggerRttPutString("Failed Send Packet");
    commandSequenceStatus = ErrorType::INTERNAL;
    ProcessAtCommandSequenceQueue();
}

void CellularModule::CreateNodeIdListJson(const NodeId* nodeIdList, const size_t& listLen, char* json) {
    const char temp[] = "{\"nodeId\":[";
    CheckedMemcpy(json, temp, strlen(temp));
    u16 jsonLen = strlen(temp);
    // max char num of u16 is "65535]}" = 8
    char nodeIdStr[8];
    for (u8 ii = 0; ii < listLen; ++ii) {
        snprintf(nodeIdStr, 8, ii == listLen - 1 ? "%d]}" : "%d,", nodeIdList[ii]);
        const u8 nodeIdLen = strlen(nodeIdStr);
        CheckedMemcpy(&json[jsonLen], nodeIdStr, nodeIdLen);
        jsonLen += nodeIdLen;
    }
    json[jsonLen] = '\0';
}

void CellularModule::SendBuffer() {
    GS->terminal.ClearReadBufferOffset();
    GS->terminal.PutString(sendBuffer);
    GS->terminal.SeggerRttPutString("send packet:");
    GS->terminal.SeggerRttPutString(sendBuffer);
    GS->terminal.SeggerRttPutChar('\n');
    // After send "AT+QISEND", module receives "> " str without line feed code.
    // So clear uart read buffer
}

void CellularModule::SendFiredNodeIdListByCellular(const NodeId* nodeIdList, const size_t& listLen) {
    GS->terminal.SeggerRttPutString("SendFiredNodeIdListByCellular");
    if (listLen > NODE_ID_LIST_NUM) {
        GS->terminal.SeggerRttPutString("Sending part of this list since too Many Node Id");
    }
    CreateNodeIdListJson(nodeIdList, listLen, sendBuffer);
    commandSequenceStatus = ErrorType::SUCCESS;
    PushAtCommandSequenceQueue(&CellularModule::Wakeup);
    PushAtCommandSequenceQueue(&CellularModule::SimActivate, 10);
    PushAtCommandSequenceQueue(&CellularModule::SocketOpen, 10);
    PushAtCommandSequenceQueue(&CellularModule::SendFiredNodeList, 10);
    ProcessAtCommandSequenceQueue();
}

#if IS_ACTIVE(BUTTONS)
void CellularModule::ButtonHandler(u8 buttonId, u32 holdTime) { logs("button pressed"); }
#endif

#ifdef TERMINAL_ENABLED
TerminalCommandHandlerReturnType CellularModule::TerminalCommandHandler(const char* commandArgs[], u8 commandArgsSize) {
    if (status != SHUTDOWN) {
        ProcessResponseQueue(commandArgs[0]);
        return TerminalCommandHandlerReturnType::SUCCESS;
    }
    if (TERMARGS(0, "cellsend")) {
        GS->terminal.SeggerRttPutString("cellsend");
        logt(CELLSEND_TAG, "Trying to send data by cellular module");
        // Wakeup();
        NodeId nodeIdList[] = {0, 1, 2, 3};
        SendFiredNodeIdListByCellular(nodeIdList, sizeof(nodeIdList) / sizeof(NodeId));
        return TerminalCommandHandlerReturnType::SUCCESS;
    }
    if (TERMARGS(0, "cellularmod")) {
        NodeId targetNodeId = Utility::StringToU16(commandArgs[0]);
        logt("CELLMOD", "Trying to cellular node %u", targetNodeId);

        u8 data[1];
        data[0] = 123;

        // send some packets
        SendModuleActionMessage(MessageType::MODULE_TRIGGER_ACTION, targetNodeId,
                                CellularModuleTriggerActionMessages::TRIGGER_CELLULAR, 0, data,
                                sizeof(data) / sizeof(data[0]), false);

        return TerminalCommandHandlerReturnType::SUCCESS;
    }
    // React on commands, return true if handled, false otherwise
    if (commandArgsSize >= 3 && TERMARGS(2, moduleName)) {
        if (TERMARGS(0, "action")) {
            if (!TERMARGS(2, moduleName)) return TerminalCommandHandlerReturnType::UNKNOWN;

            if (commandArgsSize >= 4 && TERMARGS(3, "argument_a")) {
                return TerminalCommandHandlerReturnType::SUCCESS;
            } else if (commandArgsSize >= 4 && TERMARGS(3, "argument_b")) {
                return TerminalCommandHandlerReturnType::SUCCESS;
            }

            return TerminalCommandHandlerReturnType::UNKNOWN;
        }
    }

    // Must be called to allow the module to get and set the config
    return Module::TerminalCommandHandler(commandArgs, commandArgsSize);
}
#endif

void CellularModule::MeshMessageReceivedHandler(BaseConnection* connection, BaseConnectionSendData* sendData,
                                                connPacketHeader const* packetHeader) {
    // Must call superclass for handling
    Module::MeshMessageReceivedHandler(connection, sendData, packetHeader);

    if (packetHeader->messageType == MessageType::MODULE_TRIGGER_ACTION) {
        connPacketModule const* packet = (connPacketModule const*)packetHeader;

        // Check if our module is meant and we should trigger an action
        if (packet->moduleId == moduleId) {
            if (packet->actionType == CellularModuleTriggerActionMessages::TRIGGER_CELLULAR) {
                logt("CELLMOD", "Cellular request received with data:%d", packet->data[0]);
            }
        }
    }

    // Parse Module responses
    if (packetHeader->messageType == MessageType::MODULE_ACTION_RESPONSE) {
        connPacketModule const* packet = (connPacketModule const*)packetHeader;

        // Check if our module is meant and we should trigger an action
        if (packet->moduleId == moduleId) {
            if (packet->actionType == CellularModuleActionResponseMessages::MESSAGE_0_RESPONSE) {}
        }
    }
}
