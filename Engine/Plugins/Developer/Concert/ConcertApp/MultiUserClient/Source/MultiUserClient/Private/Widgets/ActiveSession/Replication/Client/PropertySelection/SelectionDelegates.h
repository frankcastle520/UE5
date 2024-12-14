// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Delegates/Delegate.h"
#include "UObject/SoftObjectPtr.h"

class FText;

namespace UE::MultiUserClient::Replication
{
	DECLARE_DELEGATE_RetVal_OneParam(FText, FGetObjectDisplayString, const TSoftObjectPtr<>&)
}

