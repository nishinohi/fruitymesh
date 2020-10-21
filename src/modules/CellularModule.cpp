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

#define POWERKEY_PIN 10
#define POWERSUPPLY_PIN 11
#define CELLSEND_TAG "CELLSEND"
#define ATCOMMAND_TIMEOUTDS 3  // 300msec

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
    // PushAtCommandQueue("test1", 5);
    // PushResponseQueue("test1", 50, nullptr, &CellularModule::DefaultResponseTimeout);
    // PushAtCommandQueue("test2", 5);
    // PushResponseQueue("test2", 50, nullptr, &CellularModule::DefaultResponseTimeout);
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
    ProcessWakeup(passedTimeDs);
    ProcessResponseTimeout(passedTimeDs);
}

void CellularModule::InitializeResponseCallback(ResponseCallback* responseCallback) {
    responseCallback->timeoutDs = 0;
    responseCallback->responseCallback = nullptr;
    responseCallback->timeoutCallback = nullptr;
}

void CellularModule::DiscardResponseQueue() {
    responseQueue.DiscardNext();  // for response str
    responseQueue.DiscardNext();  // for response callback
    responsePassedTimeDs = 0;
}

void CellularModule::ProcessAtCommandQueue() {
    if (IsEmptyQueue(atCommandQueue)) { return; }
    u16 atCommandLen = GetAtCommandStrLength();
    char atCommand[atCommandLen + 1];
    CheckedMemset(atCommand, '\0', atCommandLen);
    CheckedMemcpy(atCommand, GetAtCommandStr(), atCommandLen);
    GS->terminal.PutString(atCommand);
    logt(CELLSEND_TAG, "send AT Command:%s", atCommand);
    DiscardAtCommandQueue();
}

bool CellularModule::PushAtCommandQueue(const char* atCommand) {
    // Queue ATCommand and response
    if (atCommand == NULL || strlen(atCommand) < 1) { return false; }
    return atCommandQueue.Put(reinterpret_cast<u8*>(const_cast<char*>(atCommand)), strlen(atCommand));
}

bool CellularModule::PushResponseQueue(const char* response, const u8& timeoutDs,
                                       const AtCommandCallback& responseCallback,
                                       const AtCommandCallback& timeoutCallback) {
    // Queue ATCommand and response
    if (response == NULL || strlen(response) < 1) { return false; }
    bool isQueued = responseQueue.Put(reinterpret_cast<u8*>(const_cast<char*>(response)), strlen(response));
    if (!isQueued) { return false; }
    // Queue ResponseCallback
    ResponseCallback addResponseCallback = {.timeoutDs = timeoutDs,
                                            .responseCallback = responseCallback == NULL ? nullptr : responseCallback,
                                            .timeoutCallback = timeoutCallback == NULL ? nullptr : timeoutCallback};
    isQueued = responseQueue.Put(reinterpret_cast<u8*>(&addResponseCallback), sizeof(ResponseCallback));
    if (!isQueued) {
        responseQueue.DiscardLast();
        return false;
    }
    return true;
}

void CellularModule::ProcessResponseTimeout(u16 passedTimeDs) {
    if (IsEmptyQueue(responseQueue)) { return; }
    // TODO: fatal error
    if (!IsValidResponseQueue()) { return; }
    if (responsePassedTimeDs < GetResponseTimeoutDs()) {
        responsePassedTimeDs += passedTimeDs;
        return;
    }
    AtCommandCallback timeoutCallback = GetTimeoutCallback();
    if (timeoutCallback != nullptr) { (this->*(timeoutCallback))(); }
    responsePassedTimeDs = 0;
    DiscardResponseQueue();
    return;
}

void CellularModule::ProcessResponseQueue(const char* arg) {
    if (IsEmptyQueue(responseQueue)) { return; }
    // TODO: fatal error
    if (!IsValidResponseQueue()) { return; }
    u16 responseStrLen = GetResponseStrLength();
    char response[responseStrLen + 1];
    CheckedMemset(response, '\0', responseStrLen + 1);
    CheckedMemcpy(response, GetResponseStr(), responseStrLen);
    // TODO: notice response success
    if (strncmp(response, arg, responseStrLen) == 0) {
        logt(CELLSEND_TAG, "get response:%s", response);
        DiscardResponseQueue();
    }
}

void CellularModule::DefaultResponseTimeout() {
    // switch (status) {
    //     case WAKINGUP:
    //         status = SHUTDOWN;
    //         break;
    //     default:
    //         break;
    // }
    logt(CELLSEND_TAG, "AT Command response timeout");
}

void CellularModule::Initialize() {
    SupplyPower();
    TurnOn();
}

void CellularModule::SupplyPower() {
    FruityHal::GpioConfigureOutput(POWERSUPPLY_PIN);
    FruityHal::GpioPinSet(POWERSUPPLY_PIN);
}

void CellularModule::TurnOn() {
    FruityHal::GpioConfigureOutput(POWERKEY_PIN);
    FruityHal::GpioPinSet(POWERKEY_PIN);
    wakeupSignalPassedTime = 0;
    status = POWER_SUPPLY;
}

// Toggle POWERKEY PIN high to low at least 200msec to send waking up signal.
void CellularModule::ProcessWakeup(u16 passedTimeDs) {
    // check response
    switch (status) {
        case POWER_SUPPLY:
            wakeupSignalPassedTime += passedTimeDs;
            // waking up module needs 5 - 7sec.
            if (wakeupSignalPassedTime < wakeupSignalTimeDs) { break; }
            status = WAKINGUP;
            PushResponseQueue("RDY", 70, nullptr, &CellularModule::DefaultResponseTimeout);
            // after receive following packet, check module accepting at command
            PushResponseQueue("DONE", ATCOMMAND_TIMEOUTDS * 20, &CellularModule::ProcessAtCommandQueue,
                              &CellularModule::DefaultResponseTimeout);
            PushAtCommandQueue("AT");
            PushResponseQueue("OK", ATCOMMAND_TIMEOUTDS, &CellularModule::ChangeStatusWakeuped,
                              &CellularModule::DefaultResponseTimeout);
            FruityHal::GpioPinClear(POWERKEY_PIN);
            break;
        default:
            break;
    }
}

void CellularModule::SimActivate() { ChangeStatusSimActivated(); }

#if IS_ACTIVE(BUTTONS)
void CellularModule::ButtonHandler(u8 buttonId, u32 holdTime) { logs("button pressed"); }
#endif

#ifdef TERMINAL_ENABLED
TerminalCommandHandlerReturnType CellularModule::TerminalCommandHandler(const char* commandArgs[], u8 commandArgsSize) {
    ProcessResponseQueue(commandArgs[0]);
    if (TERMARGS(0, "cellularsend")) {
        logt(CELLSEND_TAG, "Trying to send data by cellular module");
        Initialize();
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
