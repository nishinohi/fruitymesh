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

#include <initializer_list>

#include "GlobalState.h"
#include "Terminal.h"

#define CELLSEND_TAG "CELLSEND"
#define WAKEUP_SIGNAL_TIME_MS 300
#define ATCOMMAND_TIMEOUT_MS 500

#define APN "soracom.io"
#define USERNAME "sora"
#define PASSWORD "sora"

constexpr u8 CELLULAR_MODULE_CONFIG_VERSION = 1;

CellularModule::CellularModule() : Module(ModuleId::CELLULAR_MODULE, "cellular") {
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
}

template <class... T>
bool CellularModule::ReadReponseAndCheck(const char* succcessResponse, const u32& timeout, const char* errorResponse,
                                         const bool& waitLineFeedCode, FruityHal::TimerHandler timeoutCallback,
                                         T... returnCodes) {
    u32 preTimeMs = FruityHal::GetRtcMs();
    GS->terminal.lineToReadAvailable = false;
    GS->terminal.ClearReadBufferOffset();
    const char* response = GS->terminal.getReadBuffer();
    while (FruityHal::GetRtcMs() - preTimeMs < timeout) {
        if (!FruityHal::UartCheckInputAvailable()) { continue; }
        if (!ReadLine() && waitLineFeedCode) { continue; }
        if (strncmp(errorResponse, response, strlen(errorResponse)) == 0) { return false; }
        if (strncmp(succcessResponse, response, strlen(succcessResponse)) == 0) {
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

template <class... T>
bool CellularModule::CheckResponseReturnCodes(const char* response, T... returnCodes) {
    const char tokens[2] = {' ', ','};
    const i32 commandArgsSize = GS->terminal.TokenizeLine(const_cast<char*>(response), strlen(response), tokens, 2);
    if (commandArgsSize == -1 || static_cast<u32>(commandArgsSize) < sizeof...(returnCodes) + 1) { return false; }
    const char* returnCodeArgPtr = GS->terminal.getCommandArgsPtr()[1];
    bool didError;
    for (i32 returnCode : std::initializer_list<i32>{returnCodes...}) {
        if (returnCode == -1) { continue; }  // -1 is skipped
        didError = false;
        i32 receivedReturnCode = Utility::StringToI16(returnCodeArgPtr, &didError);
        char temp[5];
        snprintf(temp, 5, "retCode:%d\n", receivedReturnCode);
        GS->terminal.SeggerRttPutString(temp);
        if (didError || receivedReturnCode != returnCode) { return false; }
        ++returnCodeArgPtr;
    }
    return true;
}

bool CellularModule::ReadLine() {
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
bool CellularModule::SendAtCommandAndCheck(const char* atCommand, const char* succcessResponse, const u32& timeout,
                                           const char* errorResponse, const bool& waitLineFeedCode,
                                           FruityHal::TimerHandler timeoutCallback, T... returnCodes) {
    const u8 atCommandLen = strlen(atCommand) + 2;
    char atCommandWithCr[atCommandLen];
    snprintf(atCommandWithCr, atCommandLen, "%s\r", atCommand);
    FruityHal::UartPutStringBlockingWithTimeout(atCommandWithCr);
    return ReadReponseAndCheck(succcessResponse, timeout, errorResponse, waitLineFeedCode, timeoutCallback,
                               returnCodes...);
}

void CellularModule::SupplyPower() {
    FruityHal::GpioConfigureOutput(POWERSUPPLY_PIN);
    FruityHal::GpioPinSet(POWERSUPPLY_PIN);
}

bool CellularModule::TurnOn() {
    SupplyPower();
    if (!ReadReponseAndCheck("RDY", 10000)) { return false; }
    if (!ReadReponseAndCheck("+QIND: PB DONE", 3000)) { return false; }
    FruityHal::DelayMs(100);
    if (!SendAtCommandAndCheck("AT", "OK", ATCOMMAND_TIMEOUT_MS)) { return false; }
    if (!SendAtCommandAndCheck("ATE0", "OK", ATCOMMAND_TIMEOUT_MS)) { return false; }
    if (!SendAtCommandAndCheck("AT+QURCCFG=\"urcport\",\"uart1\"", "OK", ATCOMMAND_TIMEOUT_MS)) { return false; }
    return true;
}

bool CellularModule::TurnOff() {
    if (!SendAtCommandAndCheck("AT+QPOWD", "POWERED DOWN", 10000)) {
        SuspendPower();
        return false;
    }
    SuspendPower();
    return true;
}

bool CellularModule::SimActivate() {
    if (!CheckNetworkRegistrationStatus(2000)) {
        if (!SendAtCommandAndCheck("AT+QICSGP=1,1,\"" APN "\",\"" USERNAME "\",\"" PASSWORD "\"", "OK", 3000)) {
            return false;
        }
        if (!CheckNetworkRegistrationStatus(10000)) { return false; }
    };
    if (!SendAtCommandAndCheck("AT+QIACT=1", "OK", 15000)) { return false; }
    // can't receive "+QIGETERROR"
    // if (!SendAtCommandAndCheck("AT+QIGETERROR", "+QIGETERROR", ATCOMMAND_TIMEOUT_MS, DEFAULT_ERROR, true, nullptr,
    // 0)) {
    //     return false;
    // }
    return true;
}

bool CellularModule::CheckNetworkRegistrationStatus(const u32& timeout) {
    i32 prev = FruityHal::GetRtcMs();
    auto CheckReturnCode = []() -> bool {
        const char* response = GS->terminal.getReadBuffer();
        const char tokens[2] = {' ', ','};
        const i32 commandArgsSize = GS->terminal.TokenizeLine(const_cast<char*>(response), strlen(response), tokens, 2);
        if (commandArgsSize < 3) { return false; }
        const char* commandArgsPtr = GS->terminal.getCommandArgsPtr()[2];  // check second return Code
        bool didError = false;
        i16 returnCode = Utility::StringToI16(commandArgsPtr, &didError);
        if (didError) { return false; }
        return returnCode == 1 || returnCode == 5;
    };
    while (FruityHal::GetRtcMs() - prev < timeout) {
        if (!SendAtCommandAndCheck("AT+CGREG?", "+CGREG", ATCOMMAND_TIMEOUT_MS)) { continue; }
        if (CheckReturnCode()) { return true; }
        if (!SendAtCommandAndCheck("AT+CEREG?", "+CEREG", ATCOMMAND_TIMEOUT_MS)) { continue; }
        if (CheckReturnCode()) { return true; }
    }
    return false;
}

void CellularModule::ActivatePdpContext() {}

void CellularModule::SocketOpen() {}

void CellularModule::PushSocketOpenCommandAndResponse() {}

void CellularModule::ParseConnectedId(void* _response) {}

void CellularModule::SendFiredNodeList() {}

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

void CellularModule::SendBuffer() {}

void CellularModule::SendFiredNodeIdListByCellular(const NodeId* nodeIdList, const size_t& listLen) {
    if (!TurnOn()) { return; }
    if (!SimActivate()) { return; }
}

#if IS_ACTIVE(BUTTONS)
void CellularModule::ButtonHandler(u8 buttonId, u32 holdTime) { logs("button pressed"); }
#endif

#ifdef TERMINAL_ENABLED
TerminalCommandHandlerReturnType CellularModule::TerminalCommandHandler(const char* commandArgs[], u8 commandArgsSize) {
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
