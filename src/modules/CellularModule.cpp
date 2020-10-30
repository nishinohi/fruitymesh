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

#include "GlobalState.h"
#include "Terminal.h"

#define CELLSEND_TAG "CELLSEND"
#define WAKEUP_SIGNAL_TIME_DS 3  // 300msec
#define ATCOMMAND_TIMEOUTDS 3    // 300msec
#define DELAY_RESPONSE "$$"

constexpr u8 CELLULAR_MODULE_CONFIG_VERSION = 1;

CellularModule::CellularModule()
    : Module(ModuleId::CELLULAR_MODULE, "cellular"),
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
    responseCallback->responseCallback = nullptr;
    responseCallback->timeoutCallback = nullptr;
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
    responseQueue.DiscardNext();  // for response str
    responseQueue.DiscardNext();  // for response callback
    responsePassedTimeDs = 0;
}

void CellularModule::CleanResponseQueue() {
    responseQueue.Clean();
    responsePassedTimeDs = 0;
}

void CellularModule::ProcessAtCommandQueue() {
    if (IsEmptyQueue(atCommandQueue)) { return; }
    GS->terminal.SeggerRttPutString("At Command Process Fire");
    SendAtCommand(GetAtCommandStr(), GetAtCommandStrLength());
    DiscardAtCommandQueue();
}

bool CellularModule::PushAtCommandQueue(const char* atCommand) {
    // Queue ATCommand and response
    if (atCommand == NULL || strlen(atCommand) < 1) { return false; }
    return atCommandQueue.Put(reinterpret_cast<u8*>(const_cast<char*>(atCommand)), strlen(atCommand));
}

bool CellularModule::PushResponseQueue(const char* response, const u8& timeoutDs,
                                       const ResponseTimeoutType& responseTimeoutType,
                                       const AtCommandCallback& responseCallback,
                                       const AtCommandCallback& timeoutCallback) {
    // Queue ATCommand and response
    if (response == NULL || strlen(response) < 1) { return false; }
    bool isQueued = responseQueue.Put(reinterpret_cast<u8*>(const_cast<char*>(response)), strlen(response));
    if (!isQueued) { return false; }
    // Queue ResponseCallback
    ResponseCallback addResponseCallback = {.timeoutDs = timeoutDs,
                                            .responseTimeoutType = responseTimeoutType,
                                            .responseCallback = responseCallback == NULL ? nullptr : responseCallback,
                                            .timeoutCallback = timeoutCallback == NULL ? nullptr : timeoutCallback};
    isQueued = responseQueue.Put(reinterpret_cast<u8*>(&addResponseCallback), sizeof(ResponseCallback));
    if (!isQueued) {
        responseQueue.DiscardLast();
        return false;
    }
    return true;
}

bool CellularModule::PushDelayQueue(const u8& delayDs, const AtCommandCallback& delayCallback) {
    return PushResponseQueue(DELAY_RESPONSE, delayDs, DELAY, nullptr, delayCallback);
}

void CellularModule::ProcessResponseQueue(const char* arg) {
    if (IsEmptyQueue(responseQueue)) { return; }
    // TODO: fatal error
    if (!IsValidResponseQueue()) { return; }
    lastArg = const_cast<char*>(arg);
    u16 responseStrLen = GetResponseStrLength();
    char response[responseStrLen + 1];
    CheckedMemset(response, '\0', responseStrLen + 1);
    CheckedMemcpy(response, GetResponseStr(), responseStrLen);
    // TODO: notice response success
    if (strncmp(response, arg, responseStrLen) != 0) { return; }
    GS->terminal.SeggerRttPutString("get response:");
    GS->terminal.SeggerRttPutString(response);
    logt(CELLSEND_TAG, "get response:%s", response);
    AtCommandCallback responseCallback = GetResponseCallback();
    if (responseCallback != nullptr) { (this->*responseCallback)(); }
    DiscardResponseQueue();
}

void CellularModule::ProcessResponseTimeout(u16 passedTimeDs) {
    if (IsEmptyQueue(responseQueue)) { return; }
    // TODO: fatal error
    if (!IsValidResponseQueue()) { return; }
    u16 timeoutDs = GetResponseTimeoutDs();
    if (timeoutDs != 0 && responsePassedTimeDs < timeoutDs) {
        responsePassedTimeDs += passedTimeDs;
        return;
    }
#if IS_ACTIVE(LOGGING)
    LoggingTimeoutResponse();
#endif
    AtCommandCallback timeoutCallback = GetTimeoutCallback();
    if (timeoutCallback != nullptr) { (this->*(timeoutCallback))(); }
    responsePassedTimeDs = 0;
    AtCommandCallback responseCallback = nullptr;
    switch (GetResponseTimeoutType()) {
        case SUSPEND:
            CleanAtCommandQueue();
            CleanResponseQueue();
            break;
        case CONTINUE:
            DiscardResponseQueue();
            break;
        case DELAY:
            DiscardResponseQueue();
            GS->terminal.SeggerRttPutString("DELAY");
            break;
        default:
            break;
    }
    return;
}

void CellularModule::LoggingTimeoutResponse() {
    u16 responseStrLen = GetResponseStrLength();
    char response[responseStrLen + 1];
    CheckedMemset(response, '\0', responseStrLen + 1);
    CheckedMemcpy(response, GetResponseStr(), responseStrLen);
    logt(CELLSEND_TAG, "AT Command response timeout: %s", response);
}

void CellularModule::Wakeup() {
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
    // waking up module needs 10sec.
    PushResponseQueue("RDY", 100, SUSPEND, nullptr, &CellularModule::ChangeStatusShutdown);
    // After receive following packet, check module accepting AT command
    PushResponseQueue("+QIND: PB DONE", ATCOMMAND_TIMEOUTDS * 5, SUSPEND, nullptr,
                      &CellularModule::ChangeStatusShutdown);
    PushDelayQueue(5, &CellularModule::ProcessAtCommandQueue);
    PushAtCommandQueue("AT");
    PushResponseQueue("OK", ATCOMMAND_TIMEOUTDS, SUSPEND, &CellularModule::ProcessAtCommandQueue,
                      &CellularModule::ChangeStatusShutdown);
    PushAtCommandQueue("ATE0");
    PushResponseQueue("OK", ATCOMMAND_TIMEOUTDS, SUSPEND, &CellularModule::ProcessAtCommandQueue,
                      &CellularModule::ChangeStatusShutdown);
    PushAtCommandQueue("AT+QURCCFG=\"urcport\",\"uart1\"");
    PushResponseQueue("OK", ATCOMMAND_TIMEOUTDS, SUSPEND, &CellularModule::ChangeStatusWakeuped,
                      &CellularModule::ChangeStatusShutdown);
    // note: Add following commands if you need. [AT+QSCLK=1],[AT+CPIN?]
}

void CellularModule::TurnOff() {
    PushAtCommandQueue("AT+QPOWD");
    PushResponseQueue("POWERED DOWN", 100, SUSPEND, &CellularModule::SuspendPower, &CellularModule::SuspendPower);
    ProcessAtCommandQueue();
}

void CellularModule::SimActivate() { ChangeStatusSimActivated(); }

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
        GS->terminal.SeggerRttPutString("cellularsend");
        logt(CELLSEND_TAG, "Trying to send data by cellular module");
        Wakeup();
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
