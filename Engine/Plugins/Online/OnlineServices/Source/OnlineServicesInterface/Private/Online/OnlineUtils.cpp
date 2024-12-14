// Copyright Epic Games, Inc. All Rights Reserved.

#include "Online/OnlineUtils.h"
#include "Misc/DateTime.h"

namespace UE::Online
{

FString ToLogString(const FDateTime& Time)
{
	return Time.ToString();
}

}
