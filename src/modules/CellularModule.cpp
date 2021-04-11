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

CellularModule::CellularModule() : Module(CELLULAR_MODULE_ID, "cellular"), atComCtl(), ecTwoOneClient(&atComCtl) {
    // Register callbacks n' stuff

    // Enable the logtag for our vendor module template
    GS->logger.EnableTag(CELLULAR_LOG_TAG);
    GS->logger.EnableTag("ATC");

    // Save configuration to base class variables
    // sizeof configuration must be a multiple of 4 bytes
    vendorConfigurationPointer = &configuration;
    configurationLength = sizeof(CellularModuleConfiguration);

    // Set defaults
    ResetToDefaultConfiguration();
}

void CellularModule::ResetToDefaultConfiguration() {
    // Set default configuration values
    configuration.moduleId = vendorModuleId;
    configuration.moduleActive = true;
    configuration.moduleVersion = CELLULAR_MODULE_CONFIG_VERSION;
    // Set additional config values...
    FruityHal::EnableUart(true);
    pubSubClient.setClient(ecTwoOneClient);
    pubSubClient.setServer("beam.soracom.io", 1883);
    // This line allows us to have different configurations of this module depending on the featureset
    SET_FEATURESET_CONFIGURATION_VENDOR(&configuration, this);
}

void CellularModule::ConfigurationLoadedHandler(u8* migratableConfig, u16 migratableConfigLength) {
    VendorModuleConfiguration* newConfig = (VendorModuleConfiguration*)migratableConfig;

    // Version migration can be added here, e.g. if module has version 2 and config is version 1
    if (newConfig != nullptr && newConfig->moduleVersion == 1) { /* ... */
    };

    // Do additional initialization upon loading the config

    // Start the Module...
}

void CellularModule::TimerEventHandler(u16 passedTimeDs) {
    // Do stuff on timer...
}

void CellularModule::SendFiredNodeIdListByCellular(const NodeId* nodeIdList, const size_t& listLen) {
    // if (ecModuleStatus == EcModuleStatus::SHUTDOWN) { atComCtl.TurnOnOrReset(); }
    char data[] = "{\"t\0ime\":10}";
    atComCtl.UartEnable(true);
    if (!atComCtl.TurnOnOrReset()) { return; }
    if (!atComCtl.Activate()) { return; }

    if (!pubSubClient.connect("testId")) {
        logt(CELLULAR_LOG_TAG, "connect fail");
        return;
    }
    if (pubSubClient.publish("beamdemo", "Hello ec21")) {
        logt(CELLULAR_LOG_TAG, "send fail");
        pubSubClient.disconnect();
        return;
    }
    pubSubClient.disconnect();
    atComCtl.TurnOff(10000);
    atComCtl.UartDisable();
}

#if IS_ACTIVE(BUTTONS)
void CellularModule::ButtonHandler(u8 buttonId, u32 holdTime) {
    logt(CELLULAR_LOG_TAG, "button lte\n");
    NodeId nodeIdList[] = {0, 1, 2, 3};
    SendFiredNodeIdListByCellular(nodeIdList, sizeof(nodeIdList) / sizeof(NodeId));
}
#endif

#ifdef TERMINAL_ENABLED
TerminalCommandHandlerReturnType CellularModule::TerminalCommandHandler(const char* commandArgs[], u8 commandArgsSize) {
    if (commandArgsSize >= 3 && TERMARGS(2, moduleName)) {
        if (!TERMARGS(0, "action")) return Module::TerminalCommandHandler(commandArgs, commandArgsSize);

        if (commandArgsSize >= 4 && TERMARGS(3, "send")) {
            logt(CELLULAR_LOG_TAG, "Trying to send data by cellular module");
            logt(CELLULAR_LOG_TAG, "Trying to send data by cellular module");
            NodeId nodeIdList[] = {0, 1, 2, 3};
            SendFiredNodeIdListByCellular(nodeIdList, sizeof(nodeIdList) / sizeof(NodeId));
            return TerminalCommandHandlerReturnType::SUCCESS;
        }

        if (commandArgsSize >= 4 && TERMARGS(3, "shutdown")) {
            logt(CELLULAR_LOG_TAG, "Shutdown Module");
            logt(CELLULAR_LOG_TAG, "Shutdown Module");
            atComCtl.TurnOff(10000);
            return TerminalCommandHandlerReturnType::SUCCESS;
        }
    }
    // Must be called to allow the module to get and set the config
    return Module::TerminalCommandHandler(commandArgs, commandArgsSize);
}
#endif

void CellularModule::MeshMessageReceivedHandler(BaseConnection* connection, BaseConnectionSendData* sendData,
                                                ConnPacketHeader const* packetHeader) {
    // Must call superclass for handling
    Module::MeshMessageReceivedHandler(connection, sendData, packetHeader);

    if (packetHeader->messageType == MessageType::MODULE_TRIGGER_ACTION &&
        sendData->dataLength >= SIZEOF_CONN_PACKET_MODULE_VENDOR) {
        ConnPacketModuleVendor const* packet = (ConnPacketModuleVendor const*)packetHeader;

        // Check if our module is meant and we should trigger an action
        if (packet->moduleId == vendorModuleId) {}
    }

    // Parse Module responses
    if (packetHeader->messageType == MessageType::MODULE_ACTION_RESPONSE &&
        sendData->dataLength >= SIZEOF_CONN_PACKET_MODULE_VENDOR) {
        ConnPacketModuleVendor const* packet = (ConnPacketModuleVendor const*)packetHeader;

        // Check if our module is meant and we should trigger an action
        if (packet->moduleId == vendorModuleId) {}
    }
}
