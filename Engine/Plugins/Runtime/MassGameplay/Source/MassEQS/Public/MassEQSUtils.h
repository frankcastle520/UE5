// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "MassEQSTypes.h"

struct FEnvQueryResult;
struct FEnvQueryInstance;
struct FMassEnvQueryEntityInfo;
struct FMassEntityHandle;
struct FMassEntityHandle;
struct FMassEQSRequestData;

/** Holds Utility functions for Mass EQS Needs */
struct FMassEQSUtils
{

	/** Returns the Item stored in QueryInstance/QueryResult Items[Index] as EntityInfo */
	static FMassEnvQueryEntityInfo GetItemAsEntityInfo(const FEnvQueryInstance& QueryInstance, int32 Index);
	static FMassEnvQueryEntityInfo GetItemAsEntityInfo(const FEnvQueryResult& QueryResult, int32 Index);

	/** Returns all Items stored in QueryInstance/QueryResult as EntityInfo */
	static void GetAllAsEntityInfo(const FEnvQueryInstance& QueryInstance, TArray<FMassEnvQueryEntityInfo>& OutEntityInfo);
	static void GetAllAsEntityInfo(const FEnvQueryResult& QueryResult, TArray<FMassEnvQueryEntityInfo>& OutEntityInfo);

	/** Extracts all EntityHandles out of an array of EntityInfo */
	static void GetEntityHandles(const TArray<FMassEnvQueryEntityInfo>& EntityInfo, TArray<FMassEntityHandle>& OutHandles);
	static void GetAllAsEntityHandles(const FEnvQueryInstance& QueryInstance, TArray<FMassEntityHandle>& OutHandles);

	/**
	 * Used in MassEnvQueryProcessors, to cast generic FMassEQSRequestData to its corresponding child class.
	 * If InPtr is not null, then this Cast should never fail.
	 */
	template<typename RequestDataType>
	static FORCEINLINE RequestDataType* TryAndEnsureCast(TUniquePtr<FMassEQSRequestData>& InPtr)
	{
		if (!InPtr)
		{
			return nullptr;
		}

		RequestDataType* OutPtr = static_cast<RequestDataType*>(InPtr.Get());
		ensureMsgf(OutPtr, TEXT("RequestData was pushed to MassEQSSubsystem, but corresponding child RequestData was not found."));

		return OutPtr;
	}
};
