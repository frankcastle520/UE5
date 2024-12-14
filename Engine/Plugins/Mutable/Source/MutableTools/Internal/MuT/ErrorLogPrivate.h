// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuT/ErrorLog.h"

#include "MuR/ModelPrivate.h"
#include "MuR/Operations.h"

namespace mu
{
	class ErrorLog::Private
	{
	public:

        struct FErrorData
        {
            TArray< float > UnassignedUVs;
        };

		struct FMessage
		{
			ErrorLogMessageType Type = ELMT_NONE;
			ErrorLogMessageSpamBin Spam = ELMSB_ALL;
			FString Text;
            TSharedPtr<FErrorData> Data;
			const void* Context = nullptr;
			const void* Context2 = nullptr;
		};

		TArray<FMessage> Messages;


		//!
		void Add(const FString& Message, ErrorLogMessageType Type, const void* Context, ErrorLogMessageSpamBin SpamBin = ELMSB_ALL);
		void Add(const FString& Message, ErrorLogMessageType Type, const void* Context, const void* Context2, ErrorLogMessageSpamBin SpamBin = ELMSB_ALL);

        //!
        void Add(const FString& Message, const ErrorLogMessageAttachedDataView& Data, ErrorLogMessageType Type, const void* Context, ErrorLogMessageSpamBin SpamBin = ELMSB_ALL);
	};


    extern const TCHAR* GetOpName( OP_TYPE type );

}

