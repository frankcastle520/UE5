// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangeHelper.h"

namespace UE::Interchange
{
	void SanitizeName(FString& OutName, bool bIsJoint)
	{
		const TCHAR* InvalidChar = bIsJoint ? INVALID_OBJECTNAME_CHARACTERS TEXT("+ ") : INVALID_OBJECTNAME_CHARACTERS;

		while (*InvalidChar)
		{
			OutName.ReplaceCharInline(*InvalidChar, TCHAR('_'), ESearchCase::CaseSensitive);
			++InvalidChar;
		}
	}

	FString MakeName(const FString& InName, bool bIsJoint)
	{
		FString TmpName = InName;
		SanitizeName(TmpName, bIsJoint);
		return TmpName;
	}
};