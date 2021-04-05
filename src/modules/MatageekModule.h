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

#define MATAGEEK_LOG_TAG "MATAMOD"

/*
 * This is a template for a FruityMesh module.
 * A comment should be here to provide a least a short description of its purpose.
 */

// This should be set to the correct vendor and subId
constexpr VendorModuleId MATAGEEK_MODULE_ID = GET_VENDOR_MODULE_ID(0xAB24, 1);

constexpr u8 MATAGEEK_MODULE_CONFIG_VERSION = 1;

// discovery mode sec
constexpr u16 SETUP_MODE_HIGH_TO_LOW_DISCOVERY_TIME_SEC = 3600;

// only use for matageek
enum class MatageekMode : u8 {
    SETUP = 0,
    DETECT = 1,
};

enum class MatageekResponseCode : u8 {
    OK = 0,
    FAILED = 1,
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

void TrapFireHandler(u32 pin, FruityHal::GpioTransistion transistion);

class MatageekModule : public Module {
   public:
    enum MatageekModuleTriggerActionMessages : u8 {
        STATE = 0,
        TRAP_FIRE,
        MODE_CHANGE,
        BATTERY_DEAD,
    };

    enum MatageekModuleActionResponseMessages : u8 {
        STATE_RESPONSE = 0,
        TRAP_FIRE_RESPONSE = 1,
        MODE_CHANGE_RESPONSE = 2,
        BATTERY_DEAD_RESPONSE = 3,
    };

    //####### Module messages (these need to be packed)
#pragma pack(push)
#pragma pack(1)

    static constexpr int SIZEOF_MATAGEEK_MODULE_STATUS_MESSAGE = 2;
    typedef struct {
        u8 trapState;
        MatageekMode mode;

    } MatageekModuleStatusMessage;
    STATIC_ASSERT_SIZE(MatageekModuleStatusMessage, SIZEOF_MATAGEEK_MODULE_STATUS_MESSAGE);

    static constexpr int SIZEOF_MATAGEEK_MODULE_MODE_CHANGE_MESSAGE = 3;
    typedef struct {
        MatageekMode mode;
        ClusterSize clusterSize;

    } MatageekModuleModeChangeMessage;
    STATIC_ASSERT_SIZE(MatageekModuleModeChangeMessage, SIZEOF_MATAGEEK_MODULE_MODE_CHANGE_MESSAGE);

    // Answers
    static constexpr int SIZEOF_MATAGEEK_MODULE_RESPONSE_MESSAGE = 1;
    struct MatageekModuleResponse {
        MatageekResponseCode result;
    };
    STATIC_ASSERT_SIZE(MatageekModuleResponse, SIZEOF_MATAGEEK_MODULE_RESPONSE_MESSAGE);

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
#if IS_ACTIVE(BUTTONS)
    void ButtonHandler(u8 buttonId, u32 holdTime) override final;
#endif

    void MeshConnectionChangedHandler(MeshConnection& connection) override final;

    // only use for matageek
   private:
    // return response
    void SendMatageekResponse(const NodeId& toSend, const MatageekModuleActionResponseMessages& responseType,
                              const MatageekResponseCode& result, const u8& requestHandle);
    // true: trap fired, false: trap not fired
    bool GetTrapState() const { return FruityHal::GpioPinRead(14); }  // not implmented
    void ChangeMatageekMode(const MatageekMode& newMode, const ClusterSize& clusterSize);
    ErrorTypeUnchecked SendStateMessageResponse(const NodeId& targetNodeId) const;
    // true: available, false: dead
    bool CheckBattery() const { return true; }  // not implemented
    ErrorTypeUnchecked SendBatteryDeadMessage(const NodeId& targetNodeId) const;

    //############################ util
    template <class T>
    T* GetModuleById(const VendorModuleId moduleId);

    //############################ Gateway Method
    bool CommitCurrentState(const bool& network, const bool& detect);  // not implmented
    bool CommitBatteryDead(const NodeId& batteryDeadNodeId);           // not implmented

   public:
    ErrorTypeUnchecked SendTrapFireMessage(const NodeId& targetNodeId) const;
};
