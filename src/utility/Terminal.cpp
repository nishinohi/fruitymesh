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
#include <Terminal.h>
#include <Config.h>
#include <Utility.h>
#include <mini-printf.h>
#include <GlobalState.h>

#ifdef SIM_ENABLED
#include <CherrySim.h>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <iostream>
static std::mutex terminalMutex;
#endif

#if defined(__unix)
#include <stdio.h>
#include <unistd.h>
#endif


extern "C"
{
#if IS_ACTIVE(SEGGER_RTT)
#include "SEGGER_RTT.h"
#endif
#if IS_ACTIVE(STDIO)
#ifdef _WIN32
#include <conio.h>
#else
#undef trace
#include <ncurses.h>
#define trace(message, ...) 
#endif
#endif
}

// ######################### GENERAL
#define ________________GENERAL___________________

Terminal::Terminal(){
    CheckedMemset(commandArgsPtr, 0, sizeof(commandArgsPtr));
    CheckedMemset(readBuffer, 0, sizeof(readBuffer));
}

void Terminal::AddTerminalJsonListener(TerminalJsonListener * callback)
{
#ifdef TERMINAL_ENABLED
    if (registeredJsonCallbacksNum >= MAX_TERMINAL_JSON_LISTENER_CALLBACKS) {
        SIMEXCEPTION(TooManyTerminalJsonListenersException); //LCOV_EXCL_LINE assertion
        return;
    }
    registeredJsonCallbacks[registeredJsonCallbacksNum] = callback;
    registeredJsonCallbacksNum++;
#endif
}

//Initialize the mhTerminal
void Terminal::Init()
{
#ifdef TERMINAL_ENABLED
#if defined(__unix) && !defined(SIM_ENABLED)
    initscr();
    cbreak();
    noecho();
    scrollok(stdscr, TRUE);
    nodelay(stdscr, TRUE);
#endif //defined(__unix) && !defined(SIM_ENABLED)
    //UART

#if IS_ACTIVE(UART)
    if(Conf::getInstance().terminalMode != TerminalMode::DISABLED){
        UartEnable(Conf::getInstance().terminalMode == TerminalMode::PROMPT);
    }
    GS->SetUartHandler([]()->void {
        Terminal::getInstance().UartInterruptHandler();
    });
#endif
#if IS_ACTIVE(SEGGER_RTT)
    SeggerRttInit();
#endif
#if IS_ACTIVE(STDIO)
    StdioInit();
#endif

    terminalIsInitialized = true;

#if IS_INACTIVE(GW_SAVE_SPACE)
    char versionString[15];
    Utility::GetVersionStringFromInt(GS->config.getFruityMeshVersion(), versionString);

    if (Conf::getInstance().terminalMode == TerminalMode::PROMPT)
    {
        //Send Escape sequence
        log_transport_put(27); //ESC
        log_transport_putstring("[2J"); //Clear Screen
        log_transport_put(27); //ESC
        log_transport_putstring("[H"); //Cursor to Home

        //Send App start header
        log_transport_putstring("--------------------------------------------------" EOL);
        log_transport_putstring("Terminal started, compile date: ");
        log_transport_putstring(__DATE__);
        log_transport_putstring("  ");
        log_transport_putstring(__TIME__);
        log_transport_putstring(", version: ");
        log_transport_putstring(versionString);

        log_transport_putstring(", ");
        log_transport_putstring(CHIPSET_NAME);

        if(RamConfig->deviceConfigOrigin == DeviceConfigOrigins::RANDOM_CONFIG){
            log_transport_putstring(", RANDOM Config");
        } else if(RamConfig->deviceConfigOrigin == DeviceConfigOrigins::UICR_CONFIG){
            log_transport_putstring(", UICR Config");
        } else if(RamConfig->deviceConfigOrigin == DeviceConfigOrigins::TESTDEVICE_CONFIG){
            log_transport_putstring(", TESTDEVICE Config");
        }


        log_transport_putstring(EOL "--------------------------------------------------" EOL);
    } else {
        
    }
#endif
#endif //IS_ACTIVE(UART)
}

void Terminal::ProcessTerminalCommandHandlerReturnType(TerminalCommandHandlerReturnType handled, i32 commandArgsSize)
{
    //Output result
    if (handled == TerminalCommandHandlerReturnType::UNKNOWN)
    {
        if (Conf::getInstance().terminalMode == TerminalMode::PROMPT)
        {
            log_transport_putstring("Command not found:");
            for (u32 i = 0; i < (u8)commandArgsSize; i++)
            {
                log_transport_putstring(" ");
                log_transport_putstring(commandArgsPtr[i]);
            }
            log_transport_putstring(EOL);
        }
        else
        {
            logjson_partial("ERROR", "{\"type\":\"error\",\"code\":1,\"text\":\"Command not found:");
            for (u32 i = 0; i < (u8)commandArgsSize; i++)
            {
                logjson_partial("ERROR", " %s", commandArgsPtr[i]);
            }
            logjson("ERROR", "\"}" SEP);
        }
#ifdef CHERRYSIM_TESTER_ENABLED
        SIMEXCEPTION(CommandNotFoundException);
#endif
    }
    else if (handled == TerminalCommandHandlerReturnType::SUCCESS)
    {
        if (Conf::getInstance().terminalMode == TerminalMode::JSON)
        {
            logjson_error(Logger::UartErrorType::SUCCESS);
        }
    }
    else if (handled == TerminalCommandHandlerReturnType::WRONG_ARGUMENT)
    {
        if (Conf::getInstance().terminalMode == TerminalMode::PROMPT)
        {
            log_transport_putstring("Wrong Arguments" EOL);
        }
        else
        {
            logjson_error(Logger::UartErrorType::ARGUMENTS_WRONG);
        }
#ifdef CHERRYSIM_TESTER_ENABLED
        SIMEXCEPTION(WrongCommandParameterException);
#endif
    }
    else if (handled == TerminalCommandHandlerReturnType::NOT_ENOUGH_ARGUMENTS)
    {
        if (Conf::getInstance().terminalMode == TerminalMode::PROMPT)
        {
            log_transport_putstring("Not enough arguments" EOL);
        }
        else
        {
            logjson_error(Logger::UartErrorType::TOO_FEW_ARGUMENTS);
        }
#ifdef CHERRYSIM_TESTER_ENABLED
        SIMEXCEPTION(TooFewParameterException);
#endif
    }
    else if (handled == TerminalCommandHandlerReturnType::INTERNAL_ERROR)
    {
        if (Conf::getInstance().terminalMode == TerminalMode::PROMPT)
        {
            log_transport_putstring("Internal Terminal Command Error" EOL);
        }
        else
        {
            logjson_error(Logger::UartErrorType::INTERNAL_ERROR);
        }
#ifdef CHERRYSIM_TESTER_ENABLED
        SIMEXCEPTION(InternalTerminalCommandErrorException);
#endif
    }

#if IS_INACTIVE(SAVE_SPACE)
    else if (handled == TerminalCommandHandlerReturnType::WARN_DEPRECATED)
    {
        if (Conf::getInstance().terminalMode == TerminalMode::PROMPT)
        {
            log_transport_putstring("Warning: Command is marked deprecated!" EOL);
        }
        else
        {
            logjson_error(Logger::UartErrorType::WARN_DEPRECATED);
        }
    }
#endif
}

Terminal & Terminal::getInstance()
{
    return GS->terminal;
}

void Terminal::PutString(const char* buffer)
{
    if(!terminalIsInitialized) return;

#if IS_ACTIVE(UART)
    UartPutStringBlockingWithTimeout(buffer);
#endif
#if IS_ACTIVE(SEGGER_RTT)
    Terminal::SeggerRttPutString(buffer);
#endif
#if IS_ACTIVE(STDIO)
    Terminal::StdioPutString(buffer);
#endif
#if IS_ACTIVE(VIRTUAL_COM_PORT)
    FruityHal::VirtualComWriteData((const u8*)buffer, strlen(buffer));
#endif
}

void Terminal::PutChar(const char character)
{
    if(!terminalIsInitialized) return;

#if IS_ACTIVE(UART)
    UartPutCharBlockingWithTimeout(character);
#endif
#if IS_ACTIVE(SEGGER_RTT)
    SeggerRttPutChar(character);
#endif
}

void Terminal::OnJsonLogged(const char * json)
{
#if defined(TERMINAL_ENABLED) && IS_ACTIVE(JSON_LOGGING)
    if (!currentlyExecutingJsonCallbacks)
    {
        currentlyExecutingJsonCallbacks = true;
        for (u32 i = 0; i < registeredJsonCallbacksNum; i++)
        {
            (registeredJsonCallbacks[i])->TerminalJsonHandler(json);
        }
        currentlyExecutingJsonCallbacks = false;
    }
    else
    {
        //It looks like you printed a json inside the JsonHandler, which is forbidden as it would create an endless recursion.
        SIMEXCEPTION(IllegalStateException);
    }
#endif
}

const char ** Terminal::getCommandArgsPtr()
{
    return commandArgsPtr;
}

u8 Terminal::getReadBufferOffset()
{
    return readBufferOffset;
}

char * Terminal::getReadBuffer()
{
    return readBuffer;
}

void Terminal::EnableCrcChecks()
{
    crcChecksEnabled = true;
}

#ifdef SIM_ENABLED
void Terminal::DisableCrcChecks()
{
    crcChecksEnabled = false;
}
#endif

bool Terminal::IsCrcChecksEnabled()
{
    return crcChecksEnabled;
}

// Checks all transports if a line is available (or retrieves a line)
// Then processes it
void Terminal::CheckAndProcessLine()
{
    if(!terminalIsInitialized) return;

#if IS_ACTIVE(UART)
    UartCheckAndProcessLine();
#endif
#if IS_ACTIVE(SEGGER_RTT)
    SeggerRttCheckAndProcessLine();
#endif
#if IS_ACTIVE(STDIO)
    StdioCheckAndProcessLine();
#endif
#if IS_ACTIVE(VIRTUAL_COM_PORT)
    VirtualComCheckAndProcessLine();
#endif
}

//Processes a line (give to all handlers and print response)
void Terminal::ProcessLine(char* line)
{
#ifdef TERMINAL_ENABLED
    receivedProcessableLine = true;

    char* crcLocation = Utility::FindLast(line, " CRC: ");
    if (crcLocation != nullptr)
    {
        crcChecksEnabled = true; // In case we restarted but our meshGw did not yet process the reboot message, we just enable crcChecks if we find some CRC.
        crcLocation[0] = '\0'; // Cut off the CRC part from the rest of the message.
        crcLocation += 6; // The length of " CRC: "
        bool didError = false;
        const u32 passedCrc   = Utility::StringToU32(crcLocation, &didError);
        const u32 expectedCrc = Utility::CalculateCrc32String(line);
        if (didError || passedCrc != expectedCrc)
        {
            if (Conf::getInstance().terminalMode == TerminalMode::PROMPT) {
                log_transport_putstring("CRC invalid!" EOL);
            }
            else {
                logjson_error(Logger::UartErrorType::CRC_INVALID);
            }
            SIMEXCEPTION(CRCInvalidException);
            return;
        }
    }
    else if (crcChecksEnabled)
    {
        if (Conf::getInstance().terminalMode == TerminalMode::PROMPT) {
            log_transport_putstring("CRC missing!" EOL);
        }
        else {
            logjson_error(Logger::UartErrorType::CRC_MISSING);
        }
        SIMEXCEPTION(CRCMissingException);
        return;
    }

    //Tokenize input string into vector
    bool atCommandMode = Conf::getInstance().atCommandMode;
    u16 size = (u16)strlen(line);
    i32 commandArgsSize = atCommandMode ? 1 : TokenizeLine(line, size);
    if (atCommandMode) { commandArgsPtr[0] = &(line[0]); }
    if (commandArgsSize < 0) {
        if (Conf::getInstance().terminalMode == TerminalMode::PROMPT) {
            log_transport_putstring("Too many arguments!" EOL);
        }
        else {
            logjson_error(Logger::UartErrorType::TOO_MANY_ARGUMENTS);
        }
        return;
    }

    //Call all callbacks
    TerminalCommandHandlerReturnType handled = Logger::getInstance().TerminalCommandHandler(commandArgsPtr, (u8)commandArgsSize);

    
    for(u32 i=0; i<GS->amountOfModules; i++){
        TerminalCommandHandlerReturnType currentHandled = GS->activeModules[i]->TerminalCommandHandler(commandArgsPtr, (u8)commandArgsSize);

        if (          handled != TerminalCommandHandlerReturnType::UNKNOWN
            && currentHandled != TerminalCommandHandlerReturnType::UNKNOWN)
        {
            SIMEXCEPTION(MoreThanOneTerminalCommandHandlerReactedOnCommandException);
        }

        if (currentHandled > handled)
        {
            handled = currentHandled;
        }
    }

#if IS_ACTIVE(SAVE_SPACE)
    if (handled == TerminalCommandHandlerReturnType::WARN_DEPRECATED) handled = TerminalCommandHandlerReturnType::SUCCESS;
#endif

    ProcessTerminalCommandHandlerReturnType(handled, commandArgsSize);
#endif
}

i32 Terminal::TokenizeLine(char* line, u16 lineLength)
{
    CheckedMemset(commandArgsPtr, 0, MAX_NUM_TERM_ARGS * sizeof(char*));
    commandArgsPtr[0] = &(line[0]);
    i32 commandArgsSize = 1;

    for(u32 i=0; i<lineLength; i++){
        if (line[i] == ' ' && line[i+1] > '!' && line[i+1] < '~') {
            if (commandArgsSize >= MAX_NUM_TERM_ARGS) {
                SIMEXCEPTION(TooManyArgumentsException); //LCOV_EXCL_LINE assertion
                return -1;                                 //LCOV_EXCL_LINE assertion
            }
            commandArgsPtr[commandArgsSize] = &line[i+1];
            line[i] = '\0';
            commandArgsSize++;
        }
    }

    return commandArgsSize;
}

// ############################### UART
// Uart communication expects a \r delimiter after a line to process the command
// Results such as JSON objects are delimtied by \r\n

#define ________________UART___________________
#if IS_ACTIVE(UART)


void Terminal::UartEnable(bool promptAndEchoMode)
{
    if (Boardconfig->uartRXPin == -1) return;

    //Disable UART if it was active before
    FruityHal::DisableUart();

    //Delay to fix successive stop or startterm commands
    FruityHal::DelayMs(10);

    readBufferOffset = 0;
    lineToReadAvailable = false;

    FruityHal::EnableUart(promptAndEchoMode);

    uartActive = true;
}

//Checks whether a character is waiting on the input line
void Terminal::UartCheckAndProcessLine(){
    //Check if a line is available
    if(Conf::getInstance().terminalMode == TerminalMode::PROMPT)
    {
        if (FruityHal::UartCheckInputAvailable())
        {
            uartActive = true;
            UartReadLineBlocking();
        }
    }

    //Check if a line is available either through blocking or interrupt mode
    if(!lineToReadAvailable) return;

    //Set uart active if input was received
    uartActive = true;

    //Some special stuff
    if (strcmp(readBuffer, "cls") == 0)
    {
        //Send Escape sequence
        UartPutCharBlockingWithTimeout(27); //ESC
        UartPutStringBlockingWithTimeout("[2J"); //Clear Screen
        UartPutCharBlockingWithTimeout(27); //ESC
        UartPutStringBlockingWithTimeout("[H"); //Cursor to Home
    }
#if IS_INACTIVE(GW_SAVE_SPACE)
    else if(strcmp(readBuffer, "startterm") == 0){
        Conf::getInstance().terminalMode = TerminalMode::PROMPT;
        UartEnable(true);
        return;
    }
#endif
    else if(strcmp(readBuffer, "stopterm") == 0){
        Conf::getInstance().terminalMode = TerminalMode::JSON;
        UartEnable(false);
        return;
    }
    else
    {
        ProcessLine(readBuffer);
    }

    //Reset buffer
    readBufferOffset = 0;
    lineToReadAvailable = false;

    //Re-enable Read interrupt after line was processed
    if(Conf::getInstance().terminalMode != TerminalMode::PROMPT){
        FruityHal::UartEnableReadInterrupt();
    }
}

bool Terminal::UartCheckLineFeedCode(const u8& byteBuffer) {
    bool isGetLine = false;
    const LineFeedCode lineFeedCode = Conf::getInstance().lineFeedCode;
    if (lineFeedCode == LineFeedCode::CRLF && readBufferOffset > 0) {
        isGetLine = ((byteBuffer == '\n' && readBuffer[readBufferOffset - 1] == '\r') ||
                     readBufferOffset >= TERMINAL_READ_BUFFER_LENGTH - 2);
    } else {
        const char lineFeedCodeChar = lineFeedCode == LineFeedCode::CR ? '\r' : '\n';
        isGetLine = (byteBuffer == lineFeedCodeChar || readBufferOffset >= TERMINAL_READ_BUFFER_LENGTH - 1);
    }
    return isGetLine;
}

//############################ UART_BLOCKING_READ
#define ___________UART_BLOCKING_READ______________

// Reads a String from UART (until the user has pressed ENTER)
// and provides a nice terminal emulation
// ATTENTION: If no system events are fired, this function will never execute as
// a non-interrupt driven UART will not generate an event
void Terminal::UartReadLineBlocking()
{
#if IS_INACTIVE(GW_SAVE_SPACE)
    if (!uartActive)
        return;

    UartPutStringBlockingWithTimeout("mhTerm: ");

    u8 byteBuffer = 0;

    //Read in an infinite loop until \r is recognized
    while (true)
    {
        //Read a byte from UART
        FruityHal::UartReadCharBlockingResult readCharBlockingResult = FruityHal::UartReadCharBlocking();
        if (readCharBlockingResult.didError)
        {
            readBufferOffset = 0;
        }
        byteBuffer = readCharBlockingResult.c;

        //BACKSPACE
        if (byteBuffer == 127)
        {
            if (readBufferOffset > 0)
            {
                //Output Backspace
                UartPutCharBlockingWithTimeout(byteBuffer);

                readBuffer[readBufferOffset - 1] = 0;
                readBufferOffset--;
            }
        }
        //ALL OTHER CHARACTERS
        else
        {
            //Display entered character in terminal
            UartPutCharBlockingWithTimeout(byteBuffer);
            if (!UartCheckLineFeedCode(byteBuffer)) {
                CheckedMemcpy(readBuffer + readBufferOffset, &byteBuffer, sizeof(u8));
                ++readBufferOffset;
                continue;
            }
            if (Conf::getInstance().lineFeedCode == LineFeedCode::CRLF) {
                --readBufferOffset;
            }
            readBuffer[readBufferOffset] = '\0';
            UartPutStringBlockingWithTimeout(EOL);
            if (readBufferOffset > 0) {
                FruityHal::SetPendingEventIRQ();
                lineToReadAvailable = true;
                // => Will then be processed in the main event handler
            }
            break;
        }
    }
#endif
}

//############################ UART_BLOCKING_WRITE
#define ___________UART_BLOCKING_WRITE______________

void Terminal::UartPutStringBlockingWithTimeout(const char* message)
{
    if(!uartActive) return;
    if(Conf::getInstance().silentStart && 
        !receivedProcessableLine && 
        GS->ramRetainStructPreviousBootPtr->rebootReason == RebootReason::UNKNOWN &&
        Utility::IsUnknownRebootReason(GS->ramRetainStructPtr->rebootReason)) return;

    FruityHal::UartPutStringBlockingWithTimeout(message);
}

void Terminal::UartPutCharBlockingWithTimeout(const char character)
{
    char tmp[2] = {character, '\0'};
    UartPutStringBlockingWithTimeout(tmp);
}

//############################ UART_NON_BLOCKING_READ
#define _________UART_NON_BLOCKING_READ____________


void Terminal::UartInterruptHandler()
{
    if(!uartActive) return;

    //Checks if an error occured
    if (FruityHal::IsUartErroredAndClear())
    {
        readBufferOffset = 0;
    }

    //Checks if the receiver received a new byte

    FruityHal::UartReadCharResult uartReadCharResult = FruityHal::UartReadChar();
    if (uartReadCharResult.hasNewChar)
    {
        //Tell somebody that we received something
        UartHandleInterruptRX(uartReadCharResult.c);
    }

    //Checks if a timeout occured
    if (FruityHal::IsUartTimedOutAndClear())
    {
        readBufferOffset = 0;
    }
}

void Terminal::UartHandleInterruptRX(char byte)
{
    //Set uart active if input was received
    uartActive = true;
    // UartPutCharBlockingWithTimeout(byte);

    // Read the received byte
    // If the line is finished, it should be processed before additional data is read
    // Otherwise, we keep reading more bytes
    if (!UartCheckLineFeedCode(static_cast<u8>(byte))) {
        readBuffer[readBufferOffset] = byte;
        readBufferOffset++;
        FruityHal::UartEnableReadInterrupt();
        return;
    }

    if (Conf::getInstance().lineFeedCode == LineFeedCode::CRLF) { --readBufferOffset; }
    readBuffer[readBufferOffset] = '\0';
    if (readBufferOffset == 0) {
        FruityHal::UartEnableReadInterrupt();
        return;
    }
    lineToReadAvailable = true;  // Should be the last statement
    FruityHal::SetPendingEventIRQ();
    // => next, the main event loop will process the line from the main context
}
#endif
//############################ SEGGER RTT
#define ________________SEGGER_RTT___________________

#if IS_ACTIVE(SEGGER_RTT)
void Terminal::SeggerRttInit()
{

}

void Terminal::SeggerRttCheckAndProcessLine()
{
    if(SEGGER_RTT_HasKey()){
        int seggerKey = 0;
        while(seggerKey != '\r' && seggerKey != '\n' && seggerKey != '#' && readBufferOffset < TERMINAL_READ_BUFFER_LENGTH - 1){
            seggerKey = SEGGER_RTT_GetKey();
            if(seggerKey < 0) continue;
            readBuffer[readBufferOffset] = (char)seggerKey;
            readBufferOffset++;
        }
        readBuffer[readBufferOffset-1] = '\0';
        lineToReadAvailable = true;

        ProcessLine(readBuffer);

        //Reset buffer
        readBufferOffset = 0;
        lineToReadAvailable = false;
    }
}

extern "C" {
    //This is useful if we need to debug some functionality from c code (define it as extern in your code)
    void SeggerRttPrintf_c(const char* message, ...)
    {
#if (ACTIVATE_SEGGER_RTT == 1)
        char tmp[250];
        CheckedMemset(tmp, 0, 250);

        //Variable argument list must be passed to vnsprintf
        va_list aptr;
        va_start(aptr, message);
        vsnprintf(tmp, 250, message, aptr);
        va_end(aptr);

        SEGGER_RTT_WriteString(0, (const char*) tmp);
#endif
    }
}

void Terminal::SeggerRttPutString(const char*message)
{
    SEGGER_RTT_WriteString(0, (const char*) message);
}

void Terminal::SeggerRttPutChar(char character)
{
    u8 buffer[1];
    buffer[0] = character;
    SEGGER_RTT_Write(0, (const char*)buffer, 1);
}
#endif


//############################ STDIO
#define ________________STDIO___________________
#if IS_ACTIVE(STDIO)
#if !defined(_WIN32) && !defined(CHERRYSIM_TESTER_ENABLED)
static int _kbhit(void)
{
    int ch = getch();

    if (ch != ERR) {
        ungetch(ch);
        return 1;
    } else {
        return 0;
    }
}
#endif

void Terminal::StdioInit()
{
    setbuf(stdout, nullptr);
}

std::string Terminal::ReadStdioLine() {
    size_t i;
    std::string retVal = "";
#ifdef __unix
    nodelay(stdscr, FALSE);
#endif
    for (i = 0; i < TERMINAL_READ_BUFFER_LENGTH - 1; i++) {
        int c = fgetc(stdin);
        if(c == EOF) break;
        if(c == '\n') break;

        retVal += (char)c;
    }
#ifdef __unix
    nodelay(stdscr, TRUE);
#endif
    return retVal;
}

extern bool meshGwCommunication;

//Used to inject a message into the readBuffer directly
void Terminal::PutIntoTerminalCommandQueue(std::string &message, bool skipCrc)
{
    std::unique_lock<std::mutex> guard(terminalMutex);
    TerminalCommandQueueEntry terminalCommandQueueEntry;
    terminalCommandQueueEntry.terminalCommand = std::move(message);
    terminalCommandQueueEntry.skipCrcCheck = skipCrc;
    terminalCommandQueue.push(terminalCommandQueueEntry);
    message = "";
}

bool Terminal::GetNextTerminalQueueEntry(TerminalCommandQueueEntry & out)
{
    std::unique_lock<std::mutex> guard(terminalMutex);
    if (terminalCommandQueue.size() > 0)
    {
        out = terminalCommandQueue.front();
        terminalCommandQueue.pop();
        return true;
    }
    else
    {
        return false;
    }
}

void Terminal::StdioCheckAndProcessLine()
{
    if (cherrySimInstance->simConfig.terminalId != cherrySimInstance->currentNode->id && cherrySimInstance->simConfig.terminalId != 0) return;

#if ((defined(__unix) || defined(_WIN32)) && !defined(CHERRYSIM_TESTER_ENABLED))
    if(!meshGwCommunication && _kbhit() != 0){ //FIXME: Not supported by eclipse console
        printf("mhTerm: ");
        std::string line = ReadStdioLine();
        PutIntoTerminalCommandQueue(line, true);
    }
#endif

    TerminalCommandQueueEntry entry;
    if (GetNextTerminalQueueEntry(entry))
    {
        const std::string& message = entry.terminalCommand;
        
        if (cherrySimInstance->simConfig.logReplayCommands)
        {
            std::string executionReplayLine = 
                  "[!]COMMAND EXECUTION START:[!]index:"
                + std::to_string(cherrySimInstance->currentNode->index)
                + ",time:"
                + std::to_string(cherrySimInstance->simState.simTimeMs)
                + ",cmd:"
                + message
                + "[!]COMMAND EXECUTION END[!]" EOL;
            
            StdioPutString(executionReplayLine.c_str());
        }

        const char *simPos = strstr(message.c_str(), "sim ");
        if (simPos == message.c_str())
        {
            //Simulator commands are immediately redirected to cherrySim.
            //This way, sim commands don't have to follow the same simulated
            //restrictions like command length and amount of tokens.
            std::cout << "SIM COMMAND: " << message << std::endl;
            TerminalCommandHandlerReturnType handled = cherrySimInstance->TerminalCommandHandler(message.c_str());
            ProcessTerminalCommandHandlerReturnType(handled, 0);
        }
        else
        {
            //Other commands are simulated via the internal buffers of the Terminal.
            if (message.size() >= TERMINAL_READ_BUFFER_LENGTH)
            {
                SIMEXCEPTION(CommandTooLongException);
            }

            const bool oldCrcCheckEnabledValue = crcChecksEnabled;
            if (entry.skipCrcCheck)
            {
                crcChecksEnabled = false;
            }

            const u16 len = (u16)(message.size() + 1);
            CheckedMemcpy(readBuffer, message.c_str(), len);
            readBufferOffset = (u8)len;
            Terminal::ProcessLine(readBuffer);

            if (entry.skipCrcCheck)
            {
                crcChecksEnabled = oldCrcCheckEnabledValue;
            }
        }
    }

    readBufferOffset = 0;
}

void Terminal::StdioPutString(const char*message)
{
    cherrySimInstance->TerminalPrintHandler(message);
}

#endif

//############################ VIRTUAL COM PORT
#define ________________VIRTUAL_COM_PORT___________________

#if IS_ACTIVE(VIRTUAL_COM_PORT)

void Terminal::VirtualComCheckAndProcessLine()
{
    //We process up to 5 Lines in the buffer to not starve the rest of the logic
    for(u32 i = 0; i < 5; i++){
        //We are using the same buffer that UART is using as we only have to support one of the two at the same time
        ErrorType err = FruityHal::VirtualComCheckAndProcessLine((u8*)readBuffer, TERMINAL_READ_BUFFER_LENGTH);

        if(err == ErrorType::SUCCESS){
            ProcessLine(readBuffer);
            readBufferOffset = 0;
        } else {
            break;
        }
    };
}

void Terminal::VirtualComPortEventHandler(bool portOpened)
{
    //Once the Virtual Com Port is opened by another device, we send the reboot message
    if(portOpened){
        Utility::LogRebootJson();
    }
}
#endif


