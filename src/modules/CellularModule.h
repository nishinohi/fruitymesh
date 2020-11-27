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

    NodeId nodeIdList[NODE_ID_LIST_NUM];
    char sendBuffer[1024];

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

    // read response
    bool ReadReponseAndCheck(const char* succcessResponse, const u32& timeout,
                             const char* errorResponse = DEFAULT_ERROR,
                             FruityHal::TimerHandler timeoutCallback = nullptr);
    bool ReadLine();  // true: receive line feed code, false: not receive line feed code
    bool SendAtCommandAndCheck(const char* atCommand, const char* succcessResponse, const u32& timeout,
                               const char* errorResponse = DEFAULT_ERROR,
                               FruityHal::TimerHandler timeoutCallback = nullptr);

    // GPIO
    void PowerKeyPinSet() { FruityHal::GpioPinSet(POWERKEY_PIN); }
    void PowerKeyPinClear() { FruityHal::GpioPinClear(POWERKEY_PIN); }
    // wake up
    void SupplyPower();
    void SuspendPower() { FruityHal::GpioPinClear(POWERSUPPLY_PIN); }
    bool TurnOn();
    void TurnOff();
    // sim activation
    void SimActivate();
    void CheckNetworkRegistrationStatus();
    void ConfigTcpIpParameter();
    bool isNetworkStatusReCheck = false;
    void ActivatePdpContext();
    // Socket Open
    bool connectedIds[CONNECT_ID_NUM];
    u8 connectId;
    void SocketOpen();
    void ParseConnectedId(void* _response);
    void PushSocketOpenCommandAndResponse();
    // Send packet
    void SendFiredNodeList();
    void SendBuffer();
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
