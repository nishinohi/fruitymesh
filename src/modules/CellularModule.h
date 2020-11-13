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

#define ATCOMMAND_SEQUENCE_BUFFER 100
#define NODE_ID_LIST_NUM 40
#define ATCOMMAND_BUFFER 400
#define RESPONSE_BUFFER 1000
#define POWERKEY_PIN 10
#define POWERSUPPLY_PIN 11
#define DEFAULT_SUCCESS "OK"
#define DEFAULT_ERROR "ERROR"
#define CONNECT_ID_NUM 12

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

    enum CellularModuleTriggerActionMessages { TRIGGER_CELLULAR = 0 };

    enum CellularModuleActionResponseMessages { MESSAGE_0_RESPONSE = 0 };

    // Cellular Module Status
    enum ModuleStatus {
        SHUTDOWN = 0,
        POWER_SUPPLY,
        WAKINGUP,
        WAKEUPED,
        SIM_ACTIVATED,
    };

    // When receiving AT Command response causes timeout, how to process left response queue
    enum ResponseTimeoutType {
        SUSPEND = 0,           // suspend receiving response and clear response queue
        CONTINUE,              // continue receiving response and discard one response queue
        CONTINUE_NON_DISCARD,  // continue receiving response.Use this type When timeout callback add new response queue
        DELAY,                 // continue receiving response and discard one response queue
    };

    ModuleStatus status = SHUTDOWN;
    ErrorType commandSequenceStatus = ErrorType::SUCCESS;
    NodeId nodeIdList[NODE_ID_LIST_NUM];
    char sendBuffer[1024];

    // すごくダサいからなんとかしたい
    void ChangeStatusShutdown() { status = SHUTDOWN; }
    void ChangeStatusPowerSupply() { status = POWER_SUPPLY; }
    void ChangeStatusWakingup() { status = WAKINGUP; }
    void ChangeStatusWakeuped() { status = WAKEUPED; }
    void ChangeStatusSimActivated() { status = SIM_ACTIVATED; }

    typedef void (CellularModule::*AtCommandCallback)();
    typedef void (CellularModule::*AtCommandCustomCallback)(void* context);

    typedef struct {
        AtCommandCallback sequenceCallback;
        u8 retryCount;
    } AtComandSequenceCallback;

    // at command sequence queue
    u32 atCommandSequenceBuffer[ATCOMMAND_SEQUENCE_BUFFER / sizeof(u32)];
    PacketQueue atCommandSequenceQueue;
    // at command queue
    u32 atCommandBuffer[ATCOMMAND_BUFFER / sizeof(u32)] = {};
    PacketQueue atCommandQueue;
    // response buffer is aligned following
    // <------------------------------ one unit -------------------------------->
    // [successStr][errorStr][returnCodeNum][[returnCode] * n][response callback][...]
    u32 responseBuffer[RESPONSE_BUFFER / sizeof(u32)];
    PacketQueue responseQueue;
    u16 responsePassedTimeDs = 0;
    // Dse only refering last arg. Never change last arg content.
    char* lastArg = nullptr;

    typedef struct {
        u16 timeoutDs;
        ResponseTimeoutType responseTimeoutType;
        AtCommandCallback successCallback;
        AtCommandCallback errorCallback;
        AtCommandCallback timeoutCallback;
        AtCommandCustomCallback responseCustomCallback;
    } ResponseCallback;

    CellularModuleConfiguration configuration;

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

    // AT Command sequence queue
    void ProcessAtCommandSequenceQueue();
    bool PushAtCommandSequenceQueue(const AtCommandCallback& sequenceCallback, const u8& retryCount = 0);
    AtCommandCallback GetSequenceCallback() const {
        return reinterpret_cast<AtComandSequenceCallback*>(atCommandSequenceQueue.PeekNext().data)->sequenceCallback;
    }
    u8 GetSequenceRetryCount() const {
        return reinterpret_cast<AtComandSequenceCallback*>(atCommandSequenceQueue.PeekNext().data)->retryCount;
    }

    void SendAtCommand(char* atCommand, const u16& atCommandLen);
    // get AT Command queue
    // warning: receive char pointer does not contain '\0'.
    char* GetAtCommandStr() const { return (char*)atCommandQueue.PeekNext().data; };
    u16 GetAtCommandStrLength() const { return atCommandQueue.PeekNext().length; }
    void DiscardAtCommandQueue() { atCommandQueue.DiscardNext(); }
    void CleanAtCommandQueue() { atCommandQueue.Clean(); }
    // get reponse queue
    // warning: receive char pointer does not contain '\0'.
    char* GetSuccessStr() const { return (char*)responseQueue.PeekNext().data; };
    u16 GetSuccessStrLength() const { return responseQueue.PeekNext().length; }
    char* GetErrorStr() const { return (char*)responseQueue.PeekNext(1).data; };
    u16 GetErrorStrLength() const { return responseQueue.PeekNext(1).length; }
    u8 GetReturnCodeNum() const { return *(responseQueue.PeekNext(2).data); };
    i8 GetReturnCode(const u8& index) const { return *(responseQueue.PeekNext(3 + index).data); }
    // 3 = [success str][error str][return code num]
    ResponseCallback* GetResponseCallback() const {
        return reinterpret_cast<ResponseCallback*>(responseQueue.PeekNext(GetReturnCodeNum() + 3).data);
    }
    u16 GetResponseTimeoutDs() const { return GetResponseCallback()->timeoutDs; }
    ResponseTimeoutType GetResponseTimeoutType() const { return GetResponseCallback()->responseTimeoutType; }
    AtCommandCallback GetSuccessCallback() const { return GetResponseCallback()->successCallback; }
    AtCommandCallback GetErrorCallback() const { return GetResponseCallback()->errorCallback; }
    AtCommandCallback GetTimeoutCallback() const { return GetResponseCallback()->timeoutCallback; }
    AtCommandCustomCallback GetResopnseCustomCallback() const { return GetResponseCallback()->responseCustomCallback; }
    void DiscardResponseQueue();
    void CleanResponseQueue();
    // queue util
    bool IsEmptyQueue(const PacketQueue& queue) { return queue._numElements == 0; }

    // AT Command Queue and Process
    bool PushAtCommandQueue(const char* atCommand);
    void ProcessAtCommandQueue();
    // Response Queue and Process
    template <class... ReturnCodes>
    bool PushResponseQueue(const u16& timeoutDs, const AtCommandCallback& successCallback = nullptr,
                           const AtCommandCallback& errorCallback = nullptr,
                           const AtCommandCallback& timeoutCallback = nullptr,
                           const AtCommandCustomCallback& responseCustomCallback = nullptr,
                           const char* successStr = DEFAULT_SUCCESS, const char* errorStr = "ERROR",
                           const ResponseTimeoutType& responseTimeoutType = SUSPEND, ReturnCodes... returnCodes);
    template <class Head, class... Tail>
    bool PushReturnCodes(Head head, Tail... tail);
    bool PushReturnCodes() { return true; }
    bool PushDelayQueue(const u8& delayDs, const AtCommandCallback& delayCallback = nullptr);
    void ProcessResponseQueue(const char* response);
    void ProcessResponseTimeout(u16 passedTimeDs);
    void LoggingTimeoutResponse();

    // wake up
    void Wakeup();
    void SupplyPower();
    void SuspendPower() { FruityHal::GpioPinClear(POWERSUPPLY_PIN); }
    void TurnOn();
    void WakeupSuccessCallback();
    void WakeupFailedCallback();
    void TurnOff();

    // sim activation
    void SimActivate();
    void CheckNetworkRegistrationStatus();
    void ConfigTcpIpParameter();
    bool isNetworkStatusReCheck = false;
    void ActivatePdpContext();
    void SimActivateSuccess();
    void SimActivateFailed();

    // Socket Open
    bool connectedIds[CONNECT_ID_NUM];
    u8 connectId;
    void SocketOpen();
    void ParseConnectedId(void* _response);
    void PushSocketOpenCommandAndResponse();
    void SocketOpenSuccess();
    void SocketOpenFailed();

    // Send packet
    void SendFiredNodeList();
    void SendBuffer();
    void SendFiredNodeListSuccess();
    void SendFiredNodeListFailed();
    void CreateNodeIdListJson(const NodeId* nodeIdList, const size_t& listLen, char* json);

   public:
    CellularModule();

    void ConfigurationLoadedHandler(ModuleConfiguration* migratableConfig, u16 migratableConfigLength) override;

    void ResetToDefaultConfiguration() override;

    void TimerEventHandler(u16 passedTimeDs) override;

    void MeshMessageReceivedHandler(BaseConnection* connection, BaseConnectionSendData* sendData,
                                    connPacketHeader const* packetHeader) override;

    void SendFiredNodeIdListByCellular(const NodeId* nodeIdList, const size_t& listLen);

#ifdef TERMINAL_ENABLED
    TerminalCommandHandlerReturnType TerminalCommandHandler(const char* commandArgs[], u8 commandArgsSize) override;
#endif

#if IS_ACTIVE(BUTTONS)
    void ButtonHandler(u8 buttonId, u32 holdTime) override final;
#endif
};
