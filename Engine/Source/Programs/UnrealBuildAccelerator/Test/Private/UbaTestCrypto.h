// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UbaCrypto.h"
#include "UbaNetwork.h"

namespace uba
{
	bool TestCrypto(Logger& logger, const StringBufferBase& rootDir)
	{
		u64 key128[] = { 0x1234567812345678llu, 0x1234567812345678llu };

		CryptoKey encryptKey = Crypto::CreateKey(logger, (const u8*)key128);

		for (u32 dataSize=1;dataSize!=135; ++dataSize)
		{
			CryptoKey decryptKey = encryptKey;

			u8* originalData = new u8[dataSize];
			for (u32 i=0;i!=dataSize; ++i)
				originalData[i] = u8(rand() % 256);

			u8* encryptedData = new u8[dataSize];
			memcpy(encryptedData, originalData, dataSize);

			for (u32 i=0; i!=3; ++i)
			{
				if (!Crypto::Encrypt(logger, encryptKey, encryptedData, dataSize))
					return false;

				if (dataSize > 16 && memcmp(encryptedData, originalData, dataSize) == 0)
					return false;

				if (!Crypto::Decrypt(logger, decryptKey, encryptedData, dataSize))
					return false;

				if (memcmp(encryptedData, originalData, dataSize) != 0)
					return false;

				if (i == 1)
					decryptKey = Crypto::DuplicateKey(logger, encryptKey);
			}
			delete[] encryptedData;
			delete[] originalData;
			Crypto::DestroyKey(decryptKey);
		}

		Crypto::DestroyKey(encryptKey);
		return true;
	}
}