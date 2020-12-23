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

#pragma once

#include <Module.h>

/*
 * This is a template for a FruityMesh module.
 * A comment should be here to provide a least a short description of its purpose.
 */

// This should be set to the correct vendor and subId
constexpr VendorModuleId MATAGEEK_MODULE_ID = GET_VENDOR_MODULE_ID(0xAB24, 1);

constexpr u8 MATAGEEK_MODULE_CONFIG_VERSION = 1;

// only use for matageek
enum class MatageekMode {
    SETUP = 0,
    DETECT = 1,
};

#pragma pack(push)
#pragma pack(1)
// Module configuration that is saved persistently (size must be multiple of 4)
struct MatageekModuleConfiguration : VendorModuleConfiguration {
    // Insert more persistent config values here
    u8 exampleValue;
    MatageekMode matageekMode;
};
#pragma pack(pop)

class MatageekModule : public Module {
   public:
    enum MatageekModuleTriggerActionMessages {
        TRAP_STATE = 0,
        TRAP_FIRE,
        MODE_CHANGE,
        BATTERY_DEAD,
    };

    enum MatageekModuleActionResponseMessages {
        TRAP_STATE_RESPONSE = 0,
    };

    //####### Module messages (these need to be packed)
#pragma pack(push)
#pragma pack(1)

    static constexpr int SIZEOF_MATAGEEK_MODULE_COMMAND_ONE_MESSAGE = 1;
    typedef struct {
        // Insert values here
        u8 exampleValue;

    } MatageekModuleCommandOneMessage;
    STATIC_ASSERT_SIZE(MatageekModuleCommandOneMessage, SIZEOF_MATAGEEK_MODULE_COMMAND_ONE_MESSAGE);

#pragma pack(pop)
    //####### Module messages end

    // Declare the configuration used for this module
    DECLARE_CONFIG_AND_PACKED_STRUCT(MatageekModuleConfiguration);

    MatageekModule();

    void ConfigurationLoadedHandler(u8* migratableConfig, u16 migratableConfigLength) override;

    void ResetToDefaultConfiguration() override;

    void TimerEventHandler(u16 passedTimeDs) override;

    void MeshMessageReceivedHandler(BaseConnection* connection, BaseConnectionSendData* sendData,
                                    ConnPacketHeader const* packetHeader) override;

#ifdef TERMINAL_ENABLED
    TerminalCommandHandlerReturnType TerminalCommandHandler(const char* commandArgs[], u8 commandArgsSize) override;
#endif

    // only use for matageek
   private:
    // true: trap fired, false: trap not fired
    bool GetTrapState() const { return true; }  // not implmented
    void ChangeMatageekMode(const MatageekMode& newMode);
    ErrorTypeUnchecked SendTrapStateMessageResponse(const NodeId& targetNodeId) const;
    // true: available, false: dead
    bool CheckBattery() const { return true; }  // not implemented
    ErrorTypeUnchecked SendBatteryDeadMessage(const NodeId& targetNodeId) const;

    //############################ Gateway Method
    bool CommitCurrentState(const bool& network, const bool& detect);  // not implmented
    bool CommitBatteryDead(const NodeId& batteryDeadNodeId);           // not implmented
};
