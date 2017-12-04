/**

Copyright (c) 2014-2017 "M-Way Solutions GmbH"
FruityMesh - Bluetooth Low Energy mesh protocol [http://mwaysolutions.com/]

This file is part of FruityMesh

FruityMesh is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

/*
 * The Utility class holds a number of auxiliary functions
 */

#pragma once

#include <types.h>

typedef struct Aes128Block {
	uint8_t data[16];
}Aes128Block;

class Utility
{
public:
	static u32 GetRandomInteger(void);
	static void CheckFreeHeap(void);
	static void GetVersionStringFromInt(u32 version, char* outputBuffer);
	static uint8_t CalculateCrc8(u8* data, u16 dataLength);
	static uint16_t CalculateCrc16(const uint8_t * p_data, uint32_t size, const uint16_t * p_crc);
	static u32 CalculateCrc32(u8* message, i32 messageLength);
	static void Aes128BlockEncrypt(Aes128Block* messageBlock, Aes128Block* key, Aes128Block* encryptedMessage);
};

