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
#include <FruityHal.h>
#include <GlobalState.h>
#include <Logger.h>
#include <MatageekModule.h>
#include <Node.h>
#include <Utility.h>
#include <mini-printf.h>

#include <initializer_list>

// {"nodeIds":[]} = 14 char, NodeId max num "65535" = 5 char, "," is needed (listLen - 1) char, \0 = 1 char
#define CALC_NODEIDS_JSON_MAX_LEN(listLen) 14 + (listLen * 5) + (listLen - 1) + 1
// {"cluster":} = 12 char, cluster size max num "32767" = 5 char, \0 = 1 char
#define CLUSTER_SIZE_JSON_MAX_LEN (12 + 5 + 1)

void TrapFireHandler(u32 pin, FruityHal::GpioTransistion transistion) {
    logt(MATAGEEK_LOG_TAG, "Trap fired");
    MatageekModule* matageekModule = reinterpret_cast<MatageekModule*>(GS->node.GetModuleById(MATAGEEK_MODULE_ID));
    if (matageekModule->configuration.matageekMode == MatageekMode::SETUP) return;
    matageekModule->SendTrapFireMessage(NODE_ID_BROADCAST);
}

MatageekModule::MatageekModule() : Module(MATAGEEK_MODULE_ID, "matageek") {
    // Register callbacks n' stuff

    // Enable the logtag for our vendor module template
    GS->logger.EnableTag(MATAGEEK_LOG_TAG);

    // Save configuration to base class variables
    // sizeof configuration must be a multiple of 4 bytes
    vendorConfigurationPointer = &configuration;
    configurationLength = sizeof(MatageekModuleConfiguration);

    // Set defaults
    ResetToDefaultConfiguration();
}

void MatageekModule::ResetToDefaultConfiguration() {
    // Set default configuration values
    configuration.moduleId = vendorModuleId;
    configuration.moduleActive = true;
    configuration.moduleVersion = MATAGEEK_MODULE_CONFIG_VERSION;

    // Set additional config values...
    configuration.matageekMode = MatageekMode::SETUP;
    configuration.trapStateResendIntervalDs = RESEND_INTERVAL_DS;
    configuration.batteryResendIntervalDs = RESEND_INTERVAL_DS;
    configuration.updateNumberOfEnrolledDevicesIntervalDs = UPDATE_NUMBER_OF_ENROLLED_DEVICES_INTERVAL_DS;
    // This line allows us to have different configurations of this module depending on the featureset
    SET_FEATURESET_CONFIGURATION_VENDOR(&configuration, this);
}

void MatageekModule::ConfigurationLoadedHandler(u8* migratableConfig, u16 migratableConfigLength) {
    VendorModuleConfiguration* newConfig = (VendorModuleConfiguration*)migratableConfig;

    // Version migration can be added here, e.g. if module has version 2 and config is version 1
    if (newConfig != nullptr && newConfig->moduleVersion == 1) { /* ... */
    };

    // Do additional initialization upon loading the config

    // Start the Module...
}

void MatageekModule::TimerEventHandler(u16 passedTimeDs) {
    if (ShouldResendTrapFireMessage() && SHOULD_IV_TRIGGER(GS->appTimerDs + GS->appTimerRandomOffsetDs, passedTimeDs,
                                                           configuration.trapStateResendIntervalDs)) {
        trapResendCounter.retryCount--;
        SendTrapFireMessage(NODE_ID_BROADCAST);
    }
    if (ShouldResendBatteryDeadMessage() && SHOULD_IV_TRIGGER(GS->appTimerDs + GS->appTimerRandomOffsetDs, passedTimeDs,
                                                              configuration.batteryResendIntervalDs)) {
        batteryResendCounter.retryCount--;
        SendBatteryDeadMessage(NODE_ID_BROADCAST);
    }
    if (updateEnrolledNodesFlag) {
        updateEnrolledNodesPassedTimeds += passedTimeDs;
        if (updateEnrolledNodesPassedTimeds > configuration.updateNumberOfEnrolledDevicesIntervalDs) {
            updateEnrolledNodesFlag = false;
            updateEnrolledNodesPassedTimeds = 0;
            // if node has cellular module, it will send Enrolled nodes for broadcast
            CellularModule* cellularModule =
                reinterpret_cast<CellularModule*>(GS->node.GetModuleById(CELLULAR_MODULE_ID));
            if (cellularModule != nullptr) {
                ClusterSize clusterSize = GS->node.GetClusterSize();
                char json[CLUSTER_SIZE_JSON_MAX_LEN];
                snprintf(json, CLUSTER_SIZE_JSON_MAX_LEN, "{\"cluster\":%d}", clusterSize);
                cellularModule->SendMqttContents(json);
                logt(MATAGEEK_LOG_TAG, "Send new Enrolled nodes: %u", clusterSize);
                GS->node.SendEnrolledNodes(clusterSize > 0 ? clusterSize : 0, NODE_ID_BROADCAST);
            }
        }
    }
}

#if IS_ACTIVE(BUTTONS)
void MatageekModule::ButtonHandler(u8 buttonId, u32 holdTime) {
    logt(MATAGEEK_LOG_TAG, "button trap fire\n");
    SendTrapFireMessage(NODE_ID_BROADCAST);
}
#endif

#ifdef TERMINAL_ENABLED
TerminalCommandHandlerReturnType MatageekModule::TerminalCommandHandler(const char* commandArgs[], u8 commandArgsSize) {
    // React on commands, return true if handled, false otherwise
    if (commandArgsSize >= 3 && TERMARGS(2, moduleName)) {
        if (!TERMARGS(0, "action")) return Module::TerminalCommandHandler(commandArgs, commandArgsSize);

        NodeId destinationNode = Utility::TerminalArgumentToNodeId(commandArgs[1]);
        if (destinationNode == NODE_ID_INVALID) return TerminalCommandHandlerReturnType::WRONG_ARGUMENT;

        bool didError = false;
        if (commandArgsSize >= 4 && TERMARGS(3, "get_state")) {
            logt(MATAGEEK_LOG_TAG, "Trying to request trap state %u", destinationNode);
            SendModuleActionMessage(MessageType::MODULE_TRIGGER_ACTION, destinationNode,
                                    MatageekModuleTriggerActionMessages::STATE, 0, nullptr, 0, false);
            return TerminalCommandHandlerReturnType::SUCCESS;
        }
        if (commandArgsSize >= 4 && TERMARGS(3, "trap_fire")) {
            logt(MATAGEEK_LOG_TAG, "Trap fired");
            SendTrapFireMessage(NODE_ID_BROADCAST);
            return TerminalCommandHandlerReturnType::SUCCESS;
        }
        if (commandArgsSize >= 4 && TERMARGS(3, "battery_dead")) {
            logt(MATAGEEK_LOG_TAG, "Battery dead");
            SendBatteryDeadMessage(NODE_ID_BROADCAST);
            return TerminalCommandHandlerReturnType::SUCCESS;
        }
        if (commandArgsSize >= 5 && TERMARGS(3, "mode_change")) {
            MatageekModuleModeChangeMessage modeChange;
            // set mode
            const u8 newModeSend = Utility::StringToU8(commandArgs[4], &didError);
            if (didError) return TerminalCommandHandlerReturnType::WRONG_ARGUMENT;
            modeChange.mode = newModeSend == 0 ? MatageekMode::SETUP : MatageekMode::DETECT;
            // set cluster size
            if (commandArgsSize >= 6) {
                const ClusterSize clusterSize = Utility::StringToI16(commandArgs[5], &didError);
                if (didError) return TerminalCommandHandlerReturnType::WRONG_ARGUMENT;
                modeChange.clusterSize = clusterSize;
            } else {
                modeChange.clusterSize = GS->node.GetClusterSize();
            }

            logt(MATAGEEK_LOG_TAG, "Trying to request change mode %u, mode: %s, clusterSize: %d", destinationNode,
                 modeChange.mode == MatageekMode::SETUP ? "SETUP" : "DETECT", modeChange.clusterSize);
            SendModuleActionMessage(
                MessageType::MODULE_TRIGGER_ACTION, destinationNode, MatageekModuleTriggerActionMessages::MODE_CHANGE,
                0, reinterpret_cast<u8*>(&modeChange), SIZEOF_MATAGEEK_MODULE_MODE_CHANGE_MESSAGE, false);
            return TerminalCommandHandlerReturnType::SUCCESS;
        }
        return TerminalCommandHandlerReturnType::UNKNOWN;
    }

    // Must be called to allow the module to get and set the config
    return Module::TerminalCommandHandler(commandArgs, commandArgsSize);
}
#endif

void MatageekModule::MeshMessageReceivedHandler(BaseConnection* connection, BaseConnectionSendData* sendData,
                                                ConnPacketHeader const* packetHeader) {
    // Must call superclass for handling
    Module::MeshMessageReceivedHandler(connection, sendData, packetHeader);

    if (packetHeader->messageType == MessageType::MODULE_TRIGGER_ACTION &&
        sendData->dataLength >= SIZEOF_CONN_PACKET_MODULE_VENDOR) {
        const ConnPacketModuleVendor* packet = reinterpret_cast<const ConnPacketModuleVendor*>(packetHeader);

        auto sendMqttByCellular = [this](const char* key, const NodeId& nodeId) {
            CellularModule* pCellularModule = (CellularModule*)GS->node.GetModuleById(CELLULAR_MODULE_ID);
            if (pCellularModule == nullptr) return false;
            char json[CALC_NODEIDS_JSON_MAX_LEN(1)];
            snprintf(json, CALC_NODEIDS_JSON_MAX_LEN(1), "{\"%s\":[%u]}", key, nodeId);
            return pCellularModule->SendMqttContents(json);
        };

        // Check if our module is meant and we should trigger an action
        if (packet->moduleId == vendorModuleId) {
            Node* nodeModule = nullptr;
            switch (packet->actionType) {
                case MatageekModule::MatageekModuleTriggerActionMessages::STATE:
                    logt(MATAGEEK_LOG_TAG, "trap state received");
                    SendStateMessageResponse(packet->header.sender);
                    break;
                case MatageekModule::MatageekModuleTriggerActionMessages::TRAP_FIRE:
                    logt(MATAGEEK_LOG_TAG, "trap fire received");
                    if (sendMqttByCellular("trap", packet->header.sender)) {
                        SendMatageekResponse(packet->header.sender,
                                             MatageekModuleActionResponseMessages::TRAP_FIRE_RESPONSE,
                                             MatageekResponseCode::OK, 0);
                    }
                    break;
                case MatageekModule::MatageekModuleTriggerActionMessages::MODE_CHANGE: {
                    logt(MATAGEEK_LOG_TAG, "change mode received");
                    const MatageekModuleModeChangeMessage* modeChange =
                        reinterpret_cast<const MatageekModuleModeChangeMessage*>(packet->data);
                    ChangeMatageekMode(modeChange->mode, modeChange->clusterSize);
                    SendMatageekResponse(packet->header.sender,
                                         MatageekModuleActionResponseMessages::MODE_CHANGE_RESPONSE,
                                         MatageekResponseCode::OK, 0);
                } break;
                case MatageekModule::MatageekModuleTriggerActionMessages::BATTERY_DEAD: {
                    logt(MATAGEEK_LOG_TAG, "battery dead received %u", packet->header.sender);
                    if (sendMqttByCellular("battery", packet->header.sender)) {
                        SendMatageekResponse(packet->header.sender,
                                             MatageekModuleActionResponseMessages::BATTERY_DEAD_RESPONSE,
                                             MatageekResponseCode::OK, 0);
                    }
                } break;
                default:
                    break;
            }
        }
    }

    // Parse Module responses
    if (packetHeader->messageType == MessageType::MODULE_ACTION_RESPONSE &&
        sendData->dataLength >= SIZEOF_CONN_PACKET_MODULE_VENDOR) {
        ConnPacketModuleVendor const* packet = (ConnPacketModuleVendor const*)packetHeader;

        // Check if our module is meant and we should trigger an action
        if (packet->moduleId == vendorModuleId) {
            switch (packet->actionType) {
                case MatageekModuleActionResponseMessages::STATE_RESPONSE:
                    logt(MATAGEEK_LOG_TAG, "Trap state came back from %u with data %u", packet->header.sender,
                         packet->data[0]);
                    break;
                case MatageekModuleActionResponseMessages::TRAP_FIRE_RESPONSE: {
                    logt(MATAGEEK_LOG_TAG, "TRAP_FIRE_RESPONSE received");
                    MatageekModuleResponse const* data = reinterpret_cast<MatageekModuleResponse const*>(packet->data);
                    if (data->result == MatageekResponseCode::OK) trapResendCounter.isSent = true;
                } break;
                case MatageekModuleActionResponseMessages::BATTERY_DEAD_RESPONSE: {
                    logt(MATAGEEK_LOG_TAG, "BATTERY_DEAD_RESPONSE received");
                    MatageekModuleResponse const* data = reinterpret_cast<MatageekModuleResponse const*>(packet->data);
                    if (data->result == MatageekResponseCode::OK) batteryResendCounter.isSent = true;
                } break;
                default:
                    logt(MATAGEEK_LOG_TAG, "unknown response message: %u", packet->actionType);
                    break;
            }
        }
    }
}

void MatageekModule::MeshConnectionChangedHandler(MeshConnection& connection) {
    if (configuration.matageekMode == MatageekMode::SETUP) return;
    updateEnrolledNodesFlag = true;
    updateEnrolledNodesPassedTimeds = 0;
}

ErrorTypeUnchecked MatageekModule::SendStateMessageResponse(const NodeId& targetNodeId) const {
    MatageekModuleStatusMessage status;
    status.trapState = GetTrapState() ? (u8)1 : (u8)0;
    status.mode = configuration.matageekMode;
    logt(MATAGEEK_LOG_TAG, "Trying to send state %u,trap: %s, mode: %s", targetNodeId,
         status.trapState == 0 ? "not fired" : "fired", status.mode == MatageekMode::SETUP ? "setup" : "detect");
    return SendModuleActionMessage(MessageType::MODULE_ACTION_RESPONSE, targetNodeId,
                                   MatageekModuleActionResponseMessages::STATE_RESPONSE, 0,
                                   reinterpret_cast<u8*>(&status), SIZEOF_MATAGEEK_MODULE_STATUS_MESSAGE, false);
}

ErrorTypeUnchecked MatageekModule::SendTrapFireMessage(const NodeId& targetNodeId) {
    logt(MATAGEEK_LOG_TAG, "Trying to send trap fire message");
    trapResendCounter.isSent = false;
    trapResendCounter.retryCount = RETRY_COUNT;
    return SendModuleActionMessage(MessageType::MODULE_TRIGGER_ACTION, targetNodeId,
                                   MatageekModuleTriggerActionMessages::TRAP_FIRE, 0, nullptr, 0, false);
}

ErrorTypeUnchecked MatageekModule::SendBatteryDeadMessage(const NodeId& targetNodeId) {
    logt(MATAGEEK_LOG_TAG, "Trying to send battery dead %u", targetNodeId);
    batteryResendCounter.isSent = false;
    batteryResendCounter.retryCount = RETRY_COUNT;
    return SendModuleActionMessage(MessageType::MODULE_TRIGGER_ACTION, targetNodeId,
                                   MatageekModuleTriggerActionMessages::BATTERY_DEAD, 0, nullptr, 0, false);
}

void MatageekModule::SendMatageekResponse(const NodeId& toSend,
                                          const MatageekModuleActionResponseMessages& responseType,
                                          const MatageekResponseCode& result, const u8& requestHandle) {
    logt(MATAGEEK_LOG_TAG, "Trying to send action response %u, to NodeId: %u", responseType, toSend);
    MatageekModuleResponse response;
    response.result = result;
    SendModuleActionMessage(MessageType::MODULE_ACTION_RESPONSE, toSend, responseType, requestHandle,
                            reinterpret_cast<u8*>(&response), SIZEOF_MATAGEEK_MODULE_RESPONSE_MESSAGE, true);
}

void MatageekModule::ChangeMatageekMode(const MatageekMode& newMode, const ClusterSize& clusterSize) {
    // if same mode, do nothing. May be Reset highDiscoveryTimeoutSec.
    logt(MATAGEEK_LOG_TAG, "change mode %s", newMode == MatageekMode::SETUP ? "SETUP" : "DETECT");
    if (configuration.matageekMode == newMode) return;
    configuration.matageekMode = newMode;

    newMode == MatageekMode::SETUP
        ? GS->node.SendEnrolledNodes(0, GS->node.configuration.nodeId)
        : GS->node.SendEnrolledNodes(clusterSize > 0 ? clusterSize : 0, GS->node.configuration.nodeId);
}
