// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OnlineSubsystemTypes.h"
#include "OnlineKeyValuePair.h"

// typedef FOnlineKeyValuePairs<FString, FVariantData> FStatsColumnArray;
// Temporary class to assist in deprecation of changing the key from FName to FString. After the deprecation period, the class will be deleted and replaced by the line above.
/** Representation of a single column and its data */ 
class FStatsColumnArray : public FOnlineKeyValuePairs<FString, FVariantData>
{
	typedef FOnlineKeyValuePairs<FString, FVariantData> Super;

public:
	FORCEINLINE FStatsColumnArray() {}
	FORCEINLINE FStatsColumnArray(FStatsColumnArray&& Other) : Super(MoveTemp(Other)) {}
	FORCEINLINE FStatsColumnArray(const FStatsColumnArray& Other) : Super(Other) {}
	FORCEINLINE FStatsColumnArray& operator=(FStatsColumnArray&& Other) { Super::operator=(MoveTemp(Other)); return *this; }
	FORCEINLINE FStatsColumnArray& operator=(const FStatsColumnArray& Other) { Super::operator=(Other); return *this; }

	UE_DEPRECATED(5.5, "FStatsColumnArray now uses FString for the key type instead of FName")
		FStatsColumnArray(const FOnlineKeyValuePairs< FName, FVariantData >& DeprecatedValues)
	{
		Reserve(DeprecatedValues.Num());
		for (const TPair< FName, FVariantData >& DeprecatedValue : DeprecatedValues)
		{
			Super::Emplace(DeprecatedValue.Key.ToString(), DeprecatedValue.Value);
		}
	}

	UE_DEPRECATED(5.5, "FStatsColumnArray now uses FString for the key type instead of FName")
		FStatsColumnArray(FOnlineKeyValuePairs< FName, FVariantData >&& DeprecatedValues)
	{
		Reserve(DeprecatedValues.Num());
		for (TPair< FName, FVariantData >& DeprecatedValue : DeprecatedValues)
		{
			Super::Emplace(DeprecatedValue.Key.ToString(), MoveTemp(DeprecatedValue.Value));
		}
	}

	using Super::Add;
	UE_DEPRECATED(5.5, "FStatsColumnArray now uses FString for the key type instead of FName")
		FVariantData& Add(const FName& InKey, const FVariantData& InValue)
	{
		return Super::Add(InKey.ToString(), InValue);
	}

	using Super::Emplace;
	UE_DEPRECATED(5.5, "FStatsColumnArray now uses FString for the key type instead of FName")
		FVariantData& Emplace(const FName& InKey, const FVariantData& InValue)
	{
		return Super::Emplace(InKey.ToString(), InValue);
	}

	using Super::Find;
	UE_DEPRECATED(5.5, "FStatsColumnArray now uses FString for the key type instead of FName")
		FVariantData* Find(const FName& InKey)
	{
		return Super::Find(InKey.ToString());
	}
};


// typedef FOnlineKeyValuePairs<FString, FVariantData> FStatPropertyArray;
// Temporary class to assist in deprecation of changing the key from FName to FString. After the deprecation period, the class will be deleted and replaced by the line above.
/** Representation of a single stat value to post to the backend */
class FStatPropertyArray : public FOnlineKeyValuePairs<FString, FVariantData>
{
	typedef FOnlineKeyValuePairs<FString, FVariantData> Super;

public:
	FORCEINLINE FStatPropertyArray() {}
	FORCEINLINE FStatPropertyArray(FStatPropertyArray&& Other) : Super(MoveTemp(Other)) {}
	FORCEINLINE FStatPropertyArray(const FStatPropertyArray& Other) : Super(Other) {}
	FORCEINLINE FStatPropertyArray& operator=(FStatPropertyArray&& Other) { Super::operator=(MoveTemp(Other)); return *this; }
	FORCEINLINE FStatPropertyArray& operator=(const FStatPropertyArray& Other) { Super::operator=(Other); return *this; }

	UE_DEPRECATED(5.5, "FStatPropertyArray now uses FString for the key type instead of FName")
		FStatPropertyArray(const FOnlineKeyValuePairs< FName, FVariantData >& DeprecatedValues)
	{
		Reserve(DeprecatedValues.Num());
		for (const TPair< FName, FVariantData >& DeprecatedValue : DeprecatedValues)
		{
			Super::Emplace(DeprecatedValue.Key.ToString(), DeprecatedValue.Value);
		}
	}

	UE_DEPRECATED(5.5, "FStatPropertyArray now uses FString for the key type instead of FName")
		FStatPropertyArray(FOnlineKeyValuePairs< FName, FVariantData >&& DeprecatedValues)
	{
		Reserve(DeprecatedValues.Num());
		for (TPair< FName, FVariantData >& DeprecatedValue : DeprecatedValues)
		{
			Super::Emplace(DeprecatedValue.Key.ToString(), MoveTemp(DeprecatedValue.Value));
		}
	}

	using Super::Add;
	UE_DEPRECATED(5.5, "FStatPropertyArray now uses FString for the key type instead of FName")
		FVariantData& Add(const FName& InKey, const FVariantData& InValue)
	{
		return Super::Add(InKey.ToString(), InValue);
	}

	using Super::Emplace;
	UE_DEPRECATED(5.5, "FStatPropertyArray now uses FString for the key type instead of FName")
		FVariantData& Emplace(const FName& InKey, const FVariantData& InValue)
	{
		return Super::Emplace(InKey.ToString(), InValue);
	}

	using Super::Find;
	UE_DEPRECATED(5.5, "FStatPropertyArray now uses FString for the key type instead of FName")
		FVariantData* Find(const FName& InKey)
	{
		return Super::Find(InKey.ToString());
	}
};

class FNameArrayDeprecationWrapper : public TArray<FString>
{
	typedef TArray<FString> Super;

public:
	FORCEINLINE FNameArrayDeprecationWrapper() {}
	FORCEINLINE FNameArrayDeprecationWrapper(FNameArrayDeprecationWrapper&& Other) : Super(MoveTemp(Other)) {}
	FORCEINLINE FNameArrayDeprecationWrapper(const FNameArrayDeprecationWrapper& Other) : Super(Other) {}
	FORCEINLINE FNameArrayDeprecationWrapper& operator=(FNameArrayDeprecationWrapper&& Other) { Super::operator=(MoveTemp(Other)); return *this; }
	FORCEINLINE FNameArrayDeprecationWrapper& operator=(const FNameArrayDeprecationWrapper& Other) { Super::operator=(Other); return *this; }

	FORCEINLINE FNameArrayDeprecationWrapper(TArray<FString>&& Other) : Super(MoveTemp(Other)) {}
	FORCEINLINE FNameArrayDeprecationWrapper(const TArray<FString>& Other) : Super(Other) {}
	FORCEINLINE FNameArrayDeprecationWrapper& operator=(TArray<FString>&& Other) { Super::operator=(MoveTemp(Other)); return *this; }
	FORCEINLINE FNameArrayDeprecationWrapper& operator=(const TArray<FString>& Other) { Super::operator=(Other); return *this; }

	using Super::Add;
	UE_DEPRECATED(5.5, "This variable is now a TArray<FString> instead of a TArray<FName>.")
		int32 Add(const FName& InElement)
	{
		return Super::Add(InElement.ToString());
	}

	using Super::Emplace;
	UE_DEPRECATED(5.5, "This variable is now a TArray<FString> instead of a TArray<FName>.")
		int32 Emplace(const FName& InElement)
	{
		return Super::Emplace(InElement.ToString());
	}
};

class FNameDeprecationWrapper : public FString
{
	typedef FString Super;

public:
	FORCEINLINE FNameDeprecationWrapper() {}
	FORCEINLINE FNameDeprecationWrapper(FNameDeprecationWrapper&& Other) : Super(MoveTemp(Other)) {}
	FORCEINLINE FNameDeprecationWrapper(const FNameDeprecationWrapper& Other) : Super(Other) {}
	FORCEINLINE FNameDeprecationWrapper& operator=(FNameDeprecationWrapper&& Other) { Super::operator=(MoveTemp(Other)); return *this; }
	FORCEINLINE FNameDeprecationWrapper& operator=(const FNameDeprecationWrapper& Other) { Super::operator=(Other); return *this; }

	FORCEINLINE FNameDeprecationWrapper(FString&& Other) : Super(MoveTemp(Other)) {}
	FORCEINLINE FNameDeprecationWrapper(const FString& Other) : Super(Other) {}
	FORCEINLINE FNameDeprecationWrapper& operator=(FString&& Other) { Super::operator=(MoveTemp(Other)); return *this; }
	FORCEINLINE FNameDeprecationWrapper& operator=(const FString& Other) { Super::operator=(Other); return *this; }

	UE_DEPRECATED(5.5, "This variable is now an FString instead of an FName.")
	FORCEINLINE FNameDeprecationWrapper(FName&& DeprecatedValue) : Super(DeprecatedValue.ToString()) {}

	UE_DEPRECATED(5.5, "This variable is now an FString instead of an FName.")
		FORCEINLINE FNameDeprecationWrapper(const FName& DeprecatedValue) : Super(DeprecatedValue.ToString()) {}

	UE_DEPRECATED(5.5, "This variable is now an FString instead of an FName.")
		FORCEINLINE FNameDeprecationWrapper& operator=(FName&& DeprecatedValue) { Super::operator=(DeprecatedValue.ToString()); return *this; }

	UE_DEPRECATED(5.5, "This variable is now an FString instead of an FName.")
		FORCEINLINE FNameDeprecationWrapper& operator=(const FName& DeprecatedValue) { Super::operator=(DeprecatedValue.ToString()); return *this; }

	UE_DEPRECATED(5.5, "This variable is now an FString instead of an FName.")
		FORCEINLINE FString ToString() const { return *this; }

	UE_DEPRECATED(5.5, "This variable is now an FString instead of an FName.")
		FORCEINLINE void ToString(FString& Out) const { Out = *this; }
};

/**
 * An interface used to collect and manage online stats
 */
class ONLINESUBSYSTEM_API FOnlineStats
{
public:

	/** Array of stats we are gathering */
	FStatPropertyArray Properties;

	/**
	 *	Get a key value pair by key name
	 * @param StatName key name to search for
	 * @return KeyValuePair if found, NULL otherwise
	 */
	class FVariantData* FindStatByName(const FString& StatName);

	/**
	 * Sets a stat of type SDT_Float to the value specified. Does nothing
	 * if the stat is not of the right type.
	 *
	 * @param StatName the stat to change the value of
	 * @param Value the new value to assign to the stat
	 */
	virtual void SetFloatStat(const FString& StatName, float Value);

	/**
	 * Sets a stat of type SDT_Int to the value specified. Does nothing
	 * if the stat is not of the right type.
	 *
	 * @param StatName the stat to change the value of
	 * @param Value the new value to assign to the stat
	 */
	virtual void SetIntStat(const FString& StatName, int32 Value);

	/**
	 * Increments a stat of type float by the value specified. Does nothing
	 * if the stat is not of the right type.
	 *
	 * @param StatName the stat to increment
	 * @param IncBy the value to increment by
	 */
	virtual void IncrementFloatStat(const FString& StatName, float IncBy = 1.0f);

	/**
	 * Increments a stat of type int32 by the value specified. Does nothing
	 * if the stat is not of the right type.
	 *
	 * @param StatName the stat to increment
	 * @param IncBy the value to increment by
	 */
	virtual void IncrementIntStat(const FString& StatName, int32 IncBy = 1);

	/**
	 * Decrements a stat of type float by the value specified. Does nothing
	 * if the stat is not of the right type.
	 *
	 * @param StatName the stat to decrement
	 * @param DecBy the value to decrement by
	 */
	virtual void DecrementFloatStat(const FString& StatName, float DecBy = 1.0f);

	/**
	 * Decrements a stat of type int32 by the value specified. Does nothing
	 * if the stat is not of the right type.
	 *
	 * @param StatName the stat to decrement
	 * @param DecBy the value to decrement by
	 */
	virtual void DecrementIntStat(const FString& StatName, int32 DecBy = 1);

	UE_DEPRECATED(5.5, "Use the FString overload instead")
	FVariantData* FindStatByName(const FName& StatName) { return FindStatByName(StatName.ToString()); }
	UE_DEPRECATED(5.5, "Use the FString overload instead")
	virtual void SetFloatStat(const FName& StatName, float Value) { SetFloatStat(StatName.ToString(), Value); }
	UE_DEPRECATED(5.5, "Use the FString overload instead")
	virtual void SetIntStat(const FName& StatName, int32 Value) { SetIntStat(StatName.ToString(), Value); }
	UE_DEPRECATED(5.5, "Use the FString overload instead")
	virtual void IncrementFloatStat(const FName& StatName, float IncBy = 1.0f) { IncrementFloatStat(StatName.ToString(), IncBy); }
	UE_DEPRECATED(5.5, "Use the FString overload instead")
	virtual void IncrementIntStat(const FName& StatName, int32 IncBy = 1) { IncrementIntStat(StatName.ToString(), IncBy); }
	UE_DEPRECATED(5.5, "Use the FString overload instead")
	virtual void DecrementFloatStat(const FName& StatName, float DecBy = 1.0f) { DecrementFloatStat(StatName.ToString(), DecBy); }
	UE_DEPRECATED(5.5, "Use the FString overload instead")
	virtual void DecrementIntStat(const FName& StatName, int32 DecBy = 1) { DecrementIntStat(StatName.ToString(), DecBy); }

	/**
	 * Destructor
	 */
	virtual ~FOnlineStats()
	{
		/** no-op */
	}
};

/**
 *	Interface for storing/writing data to a leaderboard
 */
class ONLINESUBSYSTEM_API FOnlineLeaderboardWrite : public FOnlineStats
{
public:

	/** Sort Method */
	ELeaderboardSort::Type SortMethod;
	/** Display Type */
	ELeaderboardFormat::Type DisplayFormat;
	/** Update Method */
	ELeaderboardUpdateMethod::Type UpdateMethod;

	/** Names of the leaderboards to write to */
	FNameArrayDeprecationWrapper LeaderboardNames;

	/** Name of the stat that the leaderboard is rated by */
	FNameDeprecationWrapper RatedStat;

	FOnlineLeaderboardWrite() :
		SortMethod(ELeaderboardSort::None),
		DisplayFormat(ELeaderboardFormat::Number),
		UpdateMethod(ELeaderboardUpdateMethod::KeepBest)
	{
	}
};

/**
 *	Representation of a single row in a retrieved leaderboard
 */
struct FOnlineStatsRow
{
private:
    /** Hidden on purpose */
    FOnlineStatsRow() : NickName() {}

public:
	/** Name of player in this row */
	const FString NickName;
	/** Unique Id for the player in this row */
    const FUniqueNetIdPtr PlayerId;
	/** Player's rank in this leaderboard */
    int32 Rank;
	/** All requested data on the leaderboard for this player */
	FStatsColumnArray Columns;

	FOnlineStatsRow(const FString& InNickname, const FUniqueNetIdRef& InPlayerId) :
		NickName(InNickname),
		PlayerId(InPlayerId)
	{
	}

	FString ToLogString() const;
};

/**
 *	Representation of a single column of data in a leaderboard
 */
struct FColumnMetaData
{
private:
	FColumnMetaData() :
		DataType(EOnlineKeyValuePairDataType::Empty)
	{}

public:

	/** Name of the column to retrieve */
	const FNameDeprecationWrapper ColumnName;
	/** Type of data this column represents */
	const EOnlineKeyValuePairDataType::Type DataType;

	UE_DEPRECATED(5.5, "Use the FString overload instead")
	FColumnMetaData(const FName InColumnName, EOnlineKeyValuePairDataType::Type InDataType) :
		ColumnName(InColumnName.ToString()),
		DataType(InDataType)
	{
	}

	FColumnMetaData(const FString& InColumnName, EOnlineKeyValuePairDataType::Type InDataType) :
		ColumnName(InColumnName),
		DataType(InDataType)
	{
	}
};

/**
 *	Interface for reading data from a leaderboard service
 */
class ONLINESUBSYSTEM_API FOnlineLeaderboardRead
{
public:
	/** Name of the leaderboard read */
	FNameDeprecationWrapper LeaderboardName;
	/** Column this leaderboard is sorted by */
	FNameDeprecationWrapper SortedColumn;
	/** Column metadata for this leaderboard */
	TArray<FColumnMetaData> ColumnMetadata;
	/** Array of ranked users retrieved (not necessarily sorted yet) */
	TArray<FOnlineStatsRow> Rows;
	/** Indicates an error reading data occurred while processing */
	EOnlineAsyncTaskState::Type ReadState;

	FOnlineLeaderboardRead() :
		ReadState(EOnlineAsyncTaskState::NotStarted)
	{
	}

	/**
	 *	Retrieve a single record from the leaderboard for a given user
	 *
	 * @param UserId user id to retrieve a record for
	 * @return the requested user row or NULL if not found
	 */
	FOnlineStatsRow* FindPlayerRecord(const FUniqueNetId& UserId)
	{
		for (int32 UserIdx=0; UserIdx<Rows.Num(); UserIdx++)
		{
			if (*Rows[UserIdx].PlayerId == UserId)
			{
				return &Rows[UserIdx];
			}
		}

		return NULL;
	}

	FString ToLogString() const;
};

typedef TSharedRef<FOnlineLeaderboardRead, ESPMode::ThreadSafe> FOnlineLeaderboardReadRef;
typedef TSharedPtr<FOnlineLeaderboardRead, ESPMode::ThreadSafe> FOnlineLeaderboardReadPtr;

// TODO ONLINE
class FOnlinePlayerScore
{

};

/**
 * The interface for writing achievement stats to the server.
 */
class ONLINESUBSYSTEM_API FOnlineAchievementsWrite : public FOnlineStats
{
public:
	/**
	 * Constructor
	 */
	FOnlineAchievementsWrite() :
		WriteState(EOnlineAsyncTaskState::NotStarted)
	{

	}

	/** Indicates an error reading data occurred while processing */
	EOnlineAsyncTaskState::Type WriteState;
};

typedef TSharedRef<FOnlineAchievementsWrite, ESPMode::ThreadSafe> FOnlineAchievementsWriteRef;
typedef TSharedPtr<FOnlineAchievementsWrite, ESPMode::ThreadSafe> FOnlineAchievementsWritePtr;

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2
#include "CoreMinimal.h"
#endif
