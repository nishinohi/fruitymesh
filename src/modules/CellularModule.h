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

#define ATCOMMAND_BUFFER 400
#define RESPONSE_BUFFER 400
#define POWERKEY_PIN 10
#define POWERSUPPLY_PIN 11

/*
 * This is a cellular for a FruityMesh module.
 * A comment should be here to provide a least a short description of its purpose.
 */
class CellularModule : public Module {
   private:
    // Module configuration that is saved persistently (size must be multiple of 4)
    struct CellularModuleConfiguration : ModuleConfiguration {
        // Insert more persistent config values here
        // TODO: changeable line feed code mode.
    };

    // Cellular Module Status
    enum ModuleStatus {
        SHUTDOWN = 0,
        POWER_SUPPLY,
        WAKINGUP,
        WAKEUPED,
        SIM_ACTIVATED,
    };

    // When receiving AT Command response causes timeout, how to process left command and response
    enum ResponseTimeoutType {
        SUSPEND = 0,  // suspend sending command and receiving response
        CONTINUE,     // continue sending command and receiveing response
        DELAY,        // just wait
    };

    ModuleStatus status = SHUTDOWN;

    // すごくダサいからなんとかしたい
    void ChangeStatusShutdown() { status = SHUTDOWN; }
    void ChangeStatusPowerSupply() { status = POWER_SUPPLY; }
    void ChangeStatusWakingup() { status = WAKINGUP; }
    void ChangeStatusWakeuped() { status = WAKEUPED; }
    void ChangeStatusSimActivated() { status = SIM_ACTIVATED; }

    u32 atCommandBuffer[ATCOMMAND_BUFFER / sizeof(u32)] = {};
    PacketQueue atCommandQueue;
    // response buffer is aligned following
    // <---------- one unit ----------->
    // [response str][response callback][...]
    u32 responseBuffer[RESPONSE_BUFFER / sizeof(u32)];
    PacketQueue responseQueue;
    u16 responsePassedTimeDs = 0;
    // Dse only refering last arg. Never change last arg content.
    char* lastArg = nullptr;

    typedef void (CellularModule::*AtCommandCallback)();

    typedef struct {
        u8 timeoutDs;
        ResponseTimeoutType responseTimeoutType;
        AtCommandCallback responseCallback;
        AtCommandCallback timeoutCallback;
    } ResponseCallback;

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

    // GPIO
    void PowerKeyPinSet() { FruityHal::GpioPinSet(POWERKEY_PIN); }
    void PowerKeyPinClear() { FruityHal::GpioPinClear(POWERKEY_PIN); }

    void InitializeResponseCallback(ResponseCallback* responseCallback);

    void SendAtCommand(char* atCommand, const u16& atCommandLen);
    // get AT Command queue
    // warning: receive char pointer does not contain '\0'.
    char* GetAtCommandStr() const { return (char*)atCommandQueue.PeekNext().data; };
    u16 GetAtCommandStrLength() const { return atCommandQueue.PeekNext().length; }
    void DiscardAtCommandQueue() { atCommandQueue.DiscardNext(); }
    void CleanAtCommandQueue() { atCommandQueue.Clean(); }
    // get reponse queue
    // warning: receive char pointer does not contain '\0'.
    char* GetResponseStr() const { return (char*)responseQueue.PeekNext().data; };
    ResponseTimeoutType GetResponseTimeoutType() const {
        return reinterpret_cast<ResponseCallback*>(responseQueue.PeekNext(1).data)->responseTimeoutType;
    }
    u16 GetResponseStrLength() const { return responseQueue.PeekNext().length; }
    u16 GetResponseTimeoutDs() const {
        return reinterpret_cast<ResponseCallback*>(responseQueue.PeekNext(1).data)->timeoutDs;
    }
    AtCommandCallback GetResponseCallback() const {
        return reinterpret_cast<ResponseCallback*>(responseQueue.PeekNext(1).data)->responseCallback;
    }
    AtCommandCallback GetTimeoutCallback() const {
        return reinterpret_cast<ResponseCallback*>(responseQueue.PeekNext(1).data)->timeoutCallback;
    }
    void DiscardResponseQueue();
    void CleanResponseQueue();
    // queue util
    bool IsEmptyQueue(const PacketQueue& queue) { return queue._numElements == 0; }
    bool IsValidResponseQueue() { return responseQueue._numElements % 2 == 0; }

    // AT Command Queue and Process
    bool PushAtCommandQueue(const char* atCommand);
    void ProcessAtCommandQueue();
    // Response Queue and Process
    bool PushResponseQueue(const char* response, const u8& timeoutDs, const ResponseTimeoutType& responseTimeoutType,
                           const AtCommandCallback& responseCallback = nullptr,
                           const AtCommandCallback& timeoutCallback = nullptr);
    bool PushDelayQueue(const u8& delayDs, const AtCommandCallback& delayCallback = nullptr);
    void ProcessResponseQueue(const char* arg);
    void ProcessResponseTimeout(u16 passedTimeDs);
    void LoggingTimeoutResponse();

   public:
    CellularModule();

    void ConfigurationLoadedHandler(ModuleConfiguration* migratableConfig, u16 migratableConfigLength) override;

    void ResetToDefaultConfiguration() override;

    void TimerEventHandler(u16 passedTimeDs) override;

    void MeshMessageReceivedHandler(BaseConnection* connection, BaseConnectionSendData* sendData,
                                    connPacketHeader const* packetHeader) override;
    void Wakeup();
    void SupplyPower();
    void SuspendPower() { FruityHal::GpioPinClear(POWERSUPPLY_PIN); }
    void TurnOn();
    void TurnOff();
    void SimActivate();

#ifdef TERMINAL_ENABLED
    TerminalCommandHandlerReturnType TerminalCommandHandler(const char* commandArgs[], u8 commandArgsSize) override;
#endif

#if IS_ACTIVE(BUTTONS)
    void ButtonHandler(u8 buttonId, u32 holdTime) override final;
#endif
};
