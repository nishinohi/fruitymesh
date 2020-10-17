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
#include <PacketQueue.h>

#define ATCOMMAND_BUFFER 1024
#define RESPONSE_BUFFER 256

/*
 * This is a cellular for a FruityMesh module.
 * A comment should be here to provide a least a short description of its purpose.
 */
class CellularModule : public Module {
   private:
    // Module configuration that is saved persistently (size must be multiple of 4)
    struct CellularModuleConfiguration : ModuleConfiguration {
        bool atcommandMode;
        bool activated;
        // Insert more persistent config values here
    };

    u32 atCommandBuffer[ATCOMMAND_BUFFER / sizeof(u32)] = {};
    u32 responseBuffer[RESPONSE_BUFFER / sizeof(u32)] = {};
    PacketQueue atCommandQueue;
    PacketQueue responseQueue;
    u16 responsePassedTimeDs = 0;

    typedef void (CellularModule::*AtCommandCallback)();

    typedef struct {
        u16 timeoutDs;
        AtCommandCallback commandCallback;
        AtCommandCallback timeoutCallback;
    } ResponseCallback;

    // wakeupSignal
    static constexpr u8 wakeupSignalTimeDs = 2;  // 200msec
    char wakeupSignalPassedTime = -1;

    CellularModuleConfiguration configuration;

    enum CellularModuleTriggerActionMessages { TRIGGER_CELLULAR = 0 };

    enum CellularModuleActionResponseMessages { MESSAGE_0_RESPONSE = 0 };

    /*
    //####### Module messages (these need to be packed)
    #pragma pack(push)
    #pragma pack(1)

        #define SIZEOF_CELLULAR_MODULE_***_MESSAGE 10
        typedef struct
        {
            //Insert values here

        }CellularModule***Message;

    #pragma pack(pop)
    //####### Module messages end
    */

    void InitializeResponseCallback(ResponseCallback* responseCallBack);

    void ProcessAtCommands(u16 passedTimeDs);
    bool PushAtCommand(const char* atCommand, const char* response, const char timeoutDs,
                       const AtCommandCallback& commandCallBack = nullptr, const AtCommandCallback& timeoutCallBack = nullptr);

    void SupplyPower();
    void ProcessWakeup(u16 passedTimeDs);

   public:
    CellularModule();

    void ConfigurationLoadedHandler(ModuleConfiguration* migratableConfig, u16 migratableConfigLength) override;

    void ResetToDefaultConfiguration() override;

    void TimerEventHandler(u16 passedTimeDs) override;

    void MeshMessageReceivedHandler(BaseConnection* connection, BaseConnectionSendData* sendData,
                                    connPacketHeader const* packetHeader) override;
    void Initialize();
    void TurnOn();
    void TurnOff();
    void Activate();

#ifdef TERMINAL_ENABLED
    TerminalCommandHandlerReturnType TerminalCommandHandler(const char* commandArgs[], u8 commandArgsSize) override;
#endif

#if IS_ACTIVE(BUTTONS)
    void ButtonHandler(u8 buttonId, u32 holdTime) override final;
#endif
};
