// Copyright Epic Games, Inc. All Rights Reserved.

#include "Iris/ReplicationSystem/ReplicationBridgeTypes.h"


FString LexToString(EEndReplicationFlags EndReplicationFlags)
{
	
	if (EndReplicationFlags == EEndReplicationFlags::None)
	{
		return TEXT("None");
	}

	FString Flags;

	if (EnumHasAnyFlags(EndReplicationFlags, EEndReplicationFlags::Destroy))
	{
		Flags += TEXT("Destroy");
	}
	if (EnumHasAnyFlags(EndReplicationFlags, EEndReplicationFlags::TearOff))
	{
		if (!Flags.IsEmpty()) { Flags += TEXT(','); }
		Flags += TEXT("TearOff");
	}
	if (EnumHasAnyFlags(EndReplicationFlags, EEndReplicationFlags::Flush))
	{
		if (!Flags.IsEmpty()) { Flags += TEXT(','); }
		Flags += TEXT("Flush");
	}
	if (EnumHasAnyFlags(EndReplicationFlags, EEndReplicationFlags::DestroyNetHandle))
	{
		if (!Flags.IsEmpty()) { Flags += TEXT(','); };
		Flags += TEXT("DestroyNetHandle");
	}
	if (EnumHasAnyFlags(EndReplicationFlags, EEndReplicationFlags::ClearNetPushId))
	{
		if (!Flags.IsEmpty()) { Flags += TEXT(','); };
		Flags += TEXT("ClearNetPushId");
	}
	if (EnumHasAnyFlags(EndReplicationFlags, EEndReplicationFlags::SkipPendingEndReplicationValidation))
	{
		if (!Flags.IsEmpty()) { Flags += TEXT(','); };
		Flags += TEXT("SkipPendingEndReplicationValidation");
	}
	
	return Flags;
}

const TCHAR* LexToString(EReplicationBridgeDestroyInstanceReason Reason)
{
	switch (Reason)
	{
	case EReplicationBridgeDestroyInstanceReason::DoNotDestroy:
	{
		return TEXT("DoNotDestroy");
	}
	case EReplicationBridgeDestroyInstanceReason::TearOff:
	{
		return TEXT("TearOff");
	}
	case EReplicationBridgeDestroyInstanceReason::Destroy:
	{
		return TEXT("Destroy");
	}
	default:
	{
		return TEXT("[Invalid]");
	}
	}
}

const TCHAR* LexToString(EReplicationBridgeDestroyInstanceFlags DestroyFlags)
{
	switch (DestroyFlags)
	{
	case EReplicationBridgeDestroyInstanceFlags::None:
	{
		return TEXT("None");
	}
	case EReplicationBridgeDestroyInstanceFlags::AllowDestroyInstanceFromRemote:
	{
		return TEXT("AllowDestroyInstanceFromRemote");
	}
	default:
	{
		return TEXT("[Invalid]");
	}
	}
}