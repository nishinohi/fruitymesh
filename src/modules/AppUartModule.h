////////////////////////////////////////////////////////////////////////////////
// /****************************************************************************
// **
// ** Copyright (C) 2015-2021 M-Way Solutions GmbH
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

#include <FmTypes.h>
#include <Module.h>
#include <PacketQueue.h>

/*
 * This is a template for a FruityMesh module.
 * A comment should be here to provide a least a short description of its purpose.
 */

// This should be set to the correct vendor and subId
constexpr VendorModuleId APP_UART_MODULE_ID = GET_VENDOR_MODULE_ID(0xAAAA, 1);

constexpr u8 APP_UART_MODULE_CONFIG_VERSION = 1;

constexpr u32 APP_UART_LOG_BUFFER_SIZE = 1024;

constexpr u16 LOG_SENT_INTERVAL_DS = 5;

#pragma pack(push)
#pragma pack(1)
// Module configuration that is saved persistently (size must be multiple of 4)
struct AppUartModuleConfiguration : VendorModuleConfiguration {
    // Insert more persistent config values here
    u8 exampleValue;
};

struct AppUartLogRemain {
    u16 sentLength;
    u16 logLen;
};

#pragma pack(pop)

class AppUartModule : public Module {
   private:
    u16 readBufferOffset = 0;
    char readBuffer[TERMINAL_READ_BUFFER_LENGTH];
    bool lineToReadAvailable = false;
    u32 logBuffer[APP_UART_LOG_BUFFER_SIZE / sizeof(u32)] = {};

    PacketQueue logQueue;

   public:
    enum AppUartModuleTriggerActionMessages {
        TERMINAL_COMMAND = 0,
        SEND_LOG = 1,
    };

    enum AppUartModuleActionResponseMessages {
        TERMINAL_RETURN_TYPE = 0,
        RECEIVE_LOG = 1,
    };

    //####### Module messages (these need to be packed)
#pragma pack(push)
#pragma pack(1)

    static constexpr int SIZEOF_APP_UART_MODULE_TERMINAL_COMMAND_MESSAGE_STATIC = 3;
    static constexpr int SIZEOF_APP_UART_MODULE_TERMINAL_COMMAND_MESSAGE =
        MAX_MESH_PACKET_SIZE - SIZEOF_CONN_PACKET_MODULE_VENDOR;
    static constexpr int DATA_MAX_LEN = MAX_MESH_PACKET_SIZE - SIZEOF_CONN_PACKET_MODULE_VENDOR -
                                        SIZEOF_APP_UART_MODULE_TERMINAL_COMMAND_MESSAGE_STATIC;
    typedef struct {
        // Insert values here
        MessageType splitHeader;
        u8 splitCount;
        u8 partLen;
        char data[DATA_MAX_LEN];
    } AppUartModuleTerminalCommandMessage;
    STATIC_ASSERT_SIZE(AppUartModuleTerminalCommandMessage, SIZEOF_APP_UART_MODULE_TERMINAL_COMMAND_MESSAGE);

    typedef AppUartModuleTerminalCommandMessage AppUartModuleTerminalLogMessage;

#pragma pack(pop)
    //####### Module messages end

    // Declare the configuration used for this module
    DECLARE_CONFIG_AND_PACKED_STRUCT(AppUartModuleConfiguration);

    AppUartModule();

    void ConfigurationLoadedHandler(u8* migratableConfig, u16 migratableConfigLength) override;

    void ResetToDefaultConfiguration() override;

    void TimerEventHandler(u16 passedTimeDs) override;

    void MeshMessageReceivedHandler(BaseConnection* connection, BaseConnectionSendData* sendData,
                                    ConnPacketHeader const* packetHeader) override;

#ifdef TERMINAL_ENABLED
    TerminalCommandHandlerReturnType TerminalCommandHandler(const char* commandArgs[], u8 commandArgsSize) override;
#endif

    NodeId GetMeshAccessConnectionInNodeId(bool isVirtual = false) const;
    bool IsConnectSmartPhone();
    void SendAppLogQueue();
    char* GetReadBuffer() { return readBuffer; }
    u16 GetReadBufferOffset() const { return readBufferOffset; }
    bool GetLineToReadAvailable() const { return lineToReadAvailable; }
    // Log Queue
    bool PutAppLogQueue(const char* log, u16 lenth);
    void DiscardLogQueue();
};
