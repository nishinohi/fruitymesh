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

#include <FruityHal.h>
#include <GlobalState.h>
#include <Logger.h>
#include <MatageekModule.h>
#include <Node.h>
#include <Utility.h>

void TrapFireHandler(u32 pin, FruityHal::GpioTransistion transistion) { logt(MATAGEEK_LOG_TAG, "Trap fired"); }

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
    // Do stuff on timer...
}

#ifdef TERMINAL_ENABLED
TerminalCommandHandlerReturnType MatageekModule::TerminalCommandHandler(const char* commandArgs[], u8 commandArgsSize) {
    // React on commands, return true if handled, false otherwise
    if (commandArgsSize >= 3 && TERMARGS(2, moduleName)) {
        if (!TERMARGS(0, "action")) return Module::TerminalCommandHandler(commandArgs, commandArgsSize);

        bool didError = false;
        if (commandArgsSize >= 4 && TERMARGS(3, "trap_state")) {
            const NodeId targetNodeId = Utility::StringToU16(commandArgs[1], &didError);
            if (didError) return TerminalCommandHandlerReturnType::WRONG_ARGUMENT;
            logt(MATAGEEK_LOG_TAG, "Trying to request trap state %u", targetNodeId);
            SendModuleActionMessage(MessageType::MODULE_TRIGGER_ACTION, targetNodeId,
                                    MatageekModuleTriggerActionMessages::TRAP_STATE, 0, nullptr, 0, false);
            return TerminalCommandHandlerReturnType::SUCCESS;
        }
        if (commandArgsSize >= 5 && TERMARGS(3, "mode_change")) {
            const NodeId targetNodeId = Utility::StringToU16(commandArgs[1], &didError);
            if (didError) return TerminalCommandHandlerReturnType::WRONG_ARGUMENT;
            const u8 newMode[1] = {Utility::StringToU8(commandArgs[4], &didError)};
            if (didError) return TerminalCommandHandlerReturnType::WRONG_ARGUMENT;
            logt(MATAGEEK_LOG_TAG, "Trying to request change mode %u, %s", targetNodeId,
                 newMode[0] == 0 ? "SETUP" : "DETECT");
            SendModuleActionMessage(MessageType::MODULE_TRIGGER_ACTION, targetNodeId,
                                    MatageekModuleTriggerActionMessages::MODE_CHANGE, 0, newMode, 1, false);
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
        ConnPacketModuleVendor const* packet = (ConnPacketModuleVendor const*)packetHeader;

        // Check if our module is meant and we should trigger an action
        if (packet->moduleId == vendorModuleId) {
            Node* nodeModule = nullptr;
            switch (packet->actionType) {
                case MatageekModule::MatageekModuleTriggerActionMessages::TRAP_STATE:
                    logt(MATAGEEK_LOG_TAG, "trap request received");
                    SendTrapStateMessageResponse(packet->header.sender);
                    break;
                case MatageekModule::MatageekModuleTriggerActionMessages::MODE_CHANGE:
                    logt(MATAGEEK_LOG_TAG, "change mode received %u, %u", packet->header.sender, packet->data[0]);
                    ChangeMatageekMode(packet->data[0] == 0 ? MatageekMode::SETUP : MatageekMode::DETECT);
                    break;
                case MatageekModule::MatageekModuleTriggerActionMessages::BATTERY_DEAD:
                    logt(MATAGEEK_LOG_TAG, "battery dead received %u", packet->header.sender);
                    // CommitBatteryDead(packet->header.sender);
                    break;
                case MatageekModule::MatageekModuleTriggerActionMessages::DISCOVERY_OFF:
                    logt(MATAGEEK_LOG_TAG, "discovery off received");
                    if (configuration.matageekMode == MatageekMode::SETUP) {
                        logt(MATAGEEK_LOG_TAG, "In setup mode, discovery state cannot turn off");
                        break;
                    }
                    nodeModule = GetNodeModule();
                    if (nodeModule != nullptr) nodeModule->ChangeState(DiscoveryState::OFF);
                    break;
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
            if (packet->actionType == MatageekModuleActionResponseMessages::TRAP_STATE_RESPONSE) {
                logt(MATAGEEK_LOG_TAG, "Trap state came back from %u with data %u", packet->header.sender,
                     packet->data[0]);
            }
        }
    }
}

void MatageekModule::MeshConnectionChangedHandler(MeshConnection& connection) {
    // If DiscoveryState is OFF, DiscoveryState never changes without manual changing
    if (connection.IsDisconnected()) {
        if (configuration.matageekMode == MatageekMode::SETUP) return;
        Node* nodeModule = GetNodeModule();
        if (nodeModule == nullptr) return;
        // If disconnect event happen in detect mode, change discovery state HIGH
        nodeModule->ChangeState(DiscoveryState::HIGH);
    }
}

ErrorTypeUnchecked MatageekModule::SendTrapStateMessageResponse(const NodeId& targetNodeId) const {
    const u8 trapState[1] = {GetTrapState() ? (u8)1 : (u8)0};
    logt(MATAGEEK_LOG_TAG, "Trying to send trap state %u, %s", targetNodeId, trapState[0] == 0 ? "not fired" : "fired");
    return SendModuleActionMessage(MessageType::MODULE_ACTION_RESPONSE, targetNodeId,
                                   MatageekModuleActionResponseMessages::TRAP_STATE_RESPONSE, 0, trapState, 1, false);
}

ErrorTypeUnchecked MatageekModule::SendBatteryDeadMessage(const NodeId& targetNodeId) const {
    logt(MATAGEEK_LOG_TAG, "Trying to send battery dead %u", targetNodeId);
    return SendModuleActionMessage(MessageType::MODULE_TRIGGER_ACTION, targetNodeId,
                                   MatageekModuleTriggerActionMessages::BATTERY_DEAD, 0, nullptr, 0, false);
}

void MatageekModule::ChangeMatageekMode(const MatageekMode& newMode) {
    // if same mode, do nothing. May be Reset highToLowDiscoveryTimeSec.
    if (configuration.matageekMode == newMode) return;

    Node* nodeModule = GetNodeModule();
    if (nodeModule == nullptr) return;

    logt(MATAGEEK_LOG_TAG, "change mode %s", newMode == MatageekMode::SETUP ? "SETUP" : "DETECT");
    switch (newMode) {
        case MatageekMode::SETUP:
            Conf::GetInstance().highToLowDiscoveryTimeSec = SETUP_MODE_HIGH_TO_LOW_DISCOVERY_TIME_SEC;
            configuration.matageekMode = MatageekMode::SETUP;
            nodeModule->ChangeState(DiscoveryState::HIGH);
            break;
        case MatageekMode::DETECT:
            Conf::GetInstance().highToLowDiscoveryTimeSec = DETECT_MODE_HIGH_TO_LOW_DISCOVERY_TIME_SEC;
            configuration.matageekMode = MatageekMode::DETECT;
            nodeModule->ChangeState(DiscoveryState::OFF);
            break;
        default:
            break;
    }
}

Node* MatageekModule::GetNodeModule() {
    Node* nodeModule = nullptr;
    for (auto activateModule : GS->activeModules) {
        if (activateModule->moduleId == ModuleId::NODE) {
            nodeModule = reinterpret_cast<Node*>(activateModule);
            break;
        }
    }
    return nodeModule;
}
