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

#include <Logger.h>
#include <MatageekModule.h>
#include <Node.h>
#include <Utility.h>

constexpr u8 MATAGEEK_MODULE_CONFIG_VERSION = 1;

MatageekModule::MatageekModule() : Module(ModuleId::MATAGEEK_MODULE, "matageek") {
    // Register callbacks n' stuff

    // Save configuration to base class variables
    // sizeof configuration must be a multiple of 4 bytes
    configurationPointer = &configuration;
    configurationLength = sizeof(MatageekModuleConfiguration);

    // Set defaults
    ResetToDefaultConfiguration();
}

void MatageekModule::ResetToDefaultConfiguration() {
    // Set default configuration values
    configuration.moduleId = moduleId;
    configuration.moduleActive = true;
    configuration.moduleVersion = MATAGEEK_MODULE_CONFIG_VERSION;

    // Set additional config values...
}

void MatageekModule::ConfigurationLoadedHandler(ModuleConfiguration* migratableConfig, u16 migratableConfigLength) {
    // Version migration can be added here, e.g. if module has version 2 and config is version 1
    if (migratableConfig->moduleVersion == 1) { /* ... */
    };

    // Do additional initialization upon loading the config

    // Start the Module...
}

void MatageekModule::TimerEventHandler(u16 passedTimeDs) {
    // Do stuff on timer...
}

#ifdef TERMINAL_ENABLED
TerminalCommandHandlerReturnType MatageekModule::TerminalCommandHandler(const char* commandArgs[], u8 commandArgsSize) {
    // Get the id of the target node
    if (TERMARGS(0, "matamod")) {
        NodeId targetNodeId = Utility::StringToI16(commandArgs[1]);
        logt("MATAMOD", "Trying to ping node %u", targetNodeId);

        // some data
        u8 data[1];
        data[0] = 123;

        SendModuleActionMessage(MessageType::MODULE_TRIGGER_ACTION, targetNodeId,
                                MatageekModuleTriggerActionMessages::TRIGGER_PING, 0, data, 1, false);

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

void MatageekModule::MeshMessageReceivedHandler(BaseConnection* connection, BaseConnectionSendData* sendData,
                                                connPacketHeader const* packetHeader) {
    // Must call superclass for handling
    Module::MeshMessageReceivedHandler(connection, sendData, packetHeader);

    // Filter trigger action message
    if (packetHeader->messageType == MessageType::MODULE_TRIGGER_ACTION) {
        connPacketModule const* packet = (connPacketModule const*)packetHeader;

        // Check if our module is meant and we should trigger an action
        if (packet->moduleId == moduleId) {
            if (packet->actionType == MatageekModuleTriggerActionMessages::TRIGGER_PING) {
                // inform the uer
                logt("MATAMOD", "Ping request received with data: %d", packet->data[0]);

                u8 data[2];
                data[0] = packet->data[0];
                data[1] = 111;

                SendModuleActionMessage(MessageType::MODULE_ACTION_RESPONSE, packetHeader->sender,
                                        MatageekModuleActionResponseMessages::PING_RESPONSE, 0, data, 2, false);
            }
        }
    }

    // Parse Module responses
    if (packetHeader->messageType == MessageType::MODULE_ACTION_RESPONSE) {
        connPacketModule const* packet = (connPacketModule const*)packetHeader;

        // Check if our module is meant and we should trigger an action
        if (packet->moduleId == moduleId) {
            if (packet->actionType == MatageekModuleActionResponseMessages::PING_RESPONSE) {
                logt("MATAMOD", "Ping came back from %u with data %d, %d", packet->header.sender, packet->data[0],
                     packet->data[1]);
            }
        }
    }
}
