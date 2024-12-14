// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "RewindData.h"
#include "Components/ActorComponent.h"
#include "GameFramework/PawnMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/NetConnection.h"
#include "Engine/World.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "Subsystems/WorldSubsystem.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "Chaos/PhysicsObject.h"
#include "Physics/NetworkPhysicsSettingsComponent.h"

#include "NetworkPhysicsComponent.generated.h"

class FAsyncNetworkPhysicsComponent;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnPreProcessInputsInternal, const int32);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnPostProcessInputsInternal, const int32);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnInjectInputsExternal, const int32 /* PhysicsStep */, const int32 /* NumSteps */);

/** Templated data history, holding a data buffer */
template<typename DataType>
struct TNetRewindHistory : public Chaos::TDataRewindHistory<DataType>
{
	using Super = Chaos::TDataRewindHistory<DataType>;

	FORCEINLINE TNetRewindHistory(const int32 FrameCount, const bool bIsHistoryLocal) :
		Super(FrameCount, bIsHistoryLocal)
	{
	}

	FORCEINLINE TNetRewindHistory(const int32 FrameCount) :
		Super(FrameCount)
	{
	}

	FORCEINLINE virtual ~TNetRewindHistory() {}

	virtual TUniquePtr<Chaos::FBaseRewindHistory> CreateNew() const
	{
		TUniquePtr<TNetRewindHistory> Copy = MakeUnique<TNetRewindHistory>(0, Super::bIsLocalHistory);

		return Copy;
	}

	virtual TUniquePtr<Chaos::FBaseRewindHistory> Clone() const
	{
		return MakeUnique<TNetRewindHistory>(*this);
	}

	virtual void ValidateDataInHistory(const void* ActorComponent) override
	{
		const UActorComponent* NetworkComponent = static_cast<const UActorComponent*>(ActorComponent);
		for (int32 FrameIndex = 0; FrameIndex < Super::NumFrames; ++FrameIndex)
		{
			DataType& FrameData = Super::DataHistory[FrameIndex];
			FrameData.ValidateData(NetworkComponent);
		}
	}

	virtual int32 CountValidData(const uint32 StartFrame, const uint32 EndFrame, const bool bIncludeUnimportant = true, const bool bIncludeImportant = false)
	{
		// Find how many entries are valid in frame range
		DataType FrameData;
		int32 Count = 0;
		for (uint32 CopyFrame = StartFrame; CopyFrame <= EndFrame; ++CopyFrame)
		{
			const int32 CopyIndex = Super::GetFrameIndex(CopyFrame);
			if (CopyFrame == Super::DataHistory[CopyIndex].LocalFrame)
			{
				FrameData = Super::DataHistory[CopyIndex];

				// Check if we should include unimportant and/or important data
				if ((!FrameData.bImportant && bIncludeUnimportant) || (FrameData.bImportant && bIncludeImportant))
				{
					Count++;
				}
			}
		}
		return Count;
	}

	virtual void SetImportant(const bool bImportant, const int32 Frame = INDEX_NONE) override
	{
		if (Frame > INDEX_NONE)
		{
			if (Super::EvalData(Frame))
			{
				// Set importance on specified frame
				Super::DataHistory[Super::CurrentIndex].bImportant = bImportant;
			}
		}
		else
		{
			// Set importance on all frames
			for (int32 CopyIndex = 0; CopyIndex < Super::NumFrames; ++CopyIndex)
			{
				Super::DataHistory[CopyIndex].bImportant = bImportant;
			}
		}

	}

	virtual void ApplyDataRange(const int32 FromFrame, const int32 ToFrame, void* ActorComponent, const bool bOnlyImportant = false) override
	{
		UActorComponent* NetworkComponent = static_cast<UActorComponent*>(ActorComponent);

		for (int32 ApplyFrame = FromFrame; ApplyFrame <= ToFrame; ++ApplyFrame)
		{
			const int32 ApplyIndex = Super::GetFrameIndex(ApplyFrame);
			DataType& FrameData = Super::DataHistory[ApplyIndex];
			if (ApplyFrame == FrameData.LocalFrame && (!bOnlyImportant || FrameData.bImportant))
			{
				FrameData.ApplyData(NetworkComponent);
			}
		}
	}

	virtual bool CopyAllData(Chaos::FBaseRewindHistory& OutHistory, bool bIncludeUnimportant = true, bool bIncludeImportant = false) override
	{
		TNetRewindHistory& OutNetHistory = static_cast<TNetRewindHistory&>(OutHistory);
		bool bHasCopiedData = false;

		DataType FrameData;
		for (int32 CopyIndex = 0; CopyIndex < Super::NumFrames; ++CopyIndex)
		{
			FrameData = Super::DataHistory[CopyIndex];

			// Check if we should include unimportant and/or important data
			if ((!FrameData.bImportant && bIncludeUnimportant) || (FrameData.bImportant && bIncludeImportant))
			{
				OutNetHistory.RecordData(FrameData.LocalFrame, &FrameData);
				bHasCopiedData = true;
			}
		}
		return bHasCopiedData;

	}

	virtual bool CopyData(Chaos::FBaseRewindHistory& OutHistory, const uint32 StartFrame, const uint32 EndFrame, bool bIncludeUnimportant = true, bool bIncludeImportant = false) override
	{
		TNetRewindHistory& OutNetHistory = static_cast<TNetRewindHistory&>(OutHistory);
		bool bHasCopiedData = false;

		DataType FrameData;
		for (uint32 CopyFrame = StartFrame; CopyFrame <= EndFrame; ++CopyFrame)
		{
			const int32 CopyIndex = Super::GetFrameIndex(CopyFrame);
			if (CopyFrame == Super::DataHistory[CopyIndex].LocalFrame)
			{
				FrameData = Super::DataHistory[CopyIndex];

				// Check if we should include unimportant and/or important data
				if ((!FrameData.bImportant && bIncludeUnimportant) || (FrameData.bImportant && bIncludeImportant))
				{
					OutNetHistory.RecordData(CopyFrame, &FrameData);
					bHasCopiedData = true;
				}
			}
		}
		return bHasCopiedData;
	}

	virtual TUniquePtr<Chaos::FBaseRewindHistory> CopyFramesWithOffset(const uint32 StartFrame, const uint32 EndFrame, const int32 FrameOffset) override
	{
		uint32 FramesCount = (uint32)Super::NumValidData(StartFrame, EndFrame);

		TUniquePtr<TNetRewindHistory> Copy = MakeUnique<TNetRewindHistory>(FramesCount, Super::bIsLocalHistory);

		DataType FrameData;
		for (uint32 CopyFrame = StartFrame; CopyFrame < EndFrame; ++CopyFrame)
		{
			const int32 CopyIndex = Super::GetFrameIndex(CopyFrame);
			if (CopyFrame == Super::DataHistory[CopyIndex].LocalFrame)
			{
				FrameData = Super::DataHistory[CopyIndex];
				FrameData.ServerFrame = FrameData.LocalFrame + FrameOffset;
				PRAGMA_DISABLE_DEPRECATION_WARNINGS // TODO: Change to RecordData() in UE 5.6 and remove deprecation pragma
				Copy->RecordDatas(CopyFrame, &FrameData);
				PRAGMA_ENABLE_DEPRECATION_WARNINGS
			}
		}

		return Copy;
	}

	virtual int32 ReceiveNewData(Chaos::FBaseRewindHistory& NewData, const int32 FrameOffset, bool CompareDataForRewind = false, const bool bImportant = false) override
	{
		TNetRewindHistory& NetNewData = static_cast<TNetRewindHistory&>(NewData);

		int32 RewindFrame = INDEX_NONE;
		if (NetNewData.NumFrames > 0)
		{
			for (int32 FrameIndex = 0; FrameIndex < NetNewData.NumFrames; ++FrameIndex)
			{
				DataType& FrameData = NetNewData.DataHistory[FrameIndex];
				FrameData.bImportant = bImportant;
				FrameData.bReceivedData = true; // Received data is marked to differentiate from locally predicted data

				FrameData.LocalFrame = FrameData.ServerFrame - FrameOffset;

				if (ShouldRecordReceivedDataOnFrame(FrameData))
				{
					if (CompareDataForRewind && FrameData.LocalFrame > RewindFrame && TriggerRewindFromNewData(FrameData))
					{
						RewindFrame = FrameData.LocalFrame;
					}

					PRAGMA_DISABLE_DEPRECATION_WARNINGS // TODO: Change to RecordData() in UE 5.6 and remove deprecation pragma
					Super::RecordDatas(FrameData.LocalFrame, &FrameData);
					PRAGMA_ENABLE_DEPRECATION_WARNINGS
				}
			}
		}
		return RewindFrame;
	}

	/** Check if we should record received data into history.
	* Can for example block received data from client from overriding server authoritative data */
	virtual bool ShouldRecordReceivedDataOnFrame(const DataType& ReceivedData)
	{
		if (ReceivedData.LocalFrame < 0)
		{
			return false;
		}

		// Allow received data to get recorded if it's for a newer frame than already stored at the history index or if the data stored is not marked as received (i.e. it's locally predicted)
		Super::LoadData(ReceivedData.LocalFrame);
		return (Super::DataHistory[Super::CurrentIndex].LocalFrame < ReceivedData.LocalFrame || (!Super::DataHistory[Super::CurrentIndex].bReceivedData && Super::DataHistory[Super::CurrentIndex].LocalFrame == ReceivedData.LocalFrame));
	}

	/** Compares new received data with local predicted data and returns true if they differ enough to trigger a resimulation  */
	virtual bool TriggerRewindFromNewData(DataType& NewData)
	{
		if (Super::EvalData(NewData.LocalFrame) && !Super::DataHistory[Super::CurrentIndex].bReceivedData)
		{
			return !NewData.CompareData(Super::DataHistory[Super::CurrentIndex]);
		}

		return false;
	}

	virtual void NetSerialize(FArchive& Ar, UPackageMap* InPackageMap) override
	{
		Ar << Super::NumFrames;
		
		if (Super::NumFrames > GetMaxArraySize())
		{
			UE_LOG(LogTemp, Warning, TEXT("TNetRewindHistory: serialized array of size %d exceeds maximum size %d."), Super::NumFrames, GetMaxArraySize());
			Ar.SetError();
			return;
		}

		if (Ar.IsLoading())
		{
			Super::DataHistory.SetNum(Super::NumFrames);
		}

		for (DataType& Data : Super::DataHistory)
		{
			NetSerializeData(Data, Ar, InPackageMap);
		}
	}

	/** Debug the data from the archive */
	FORCEINLINE virtual void DebugData(const Chaos::FBaseRewindHistory& DebugHistory, TArray<int32>& LocalFrames, TArray<int32>& ServerFrames, TArray<int32>& InputFrames) override
	{
		const TNetRewindHistory& NetDebugHistory = static_cast<const TNetRewindHistory&>(DebugHistory);

		if(NetDebugHistory.NumFrames >= 0)
		{
			LocalFrames.SetNum(NetDebugHistory.NumFrames);
			ServerFrames.SetNum(NetDebugHistory.NumFrames);
			InputFrames.SetNum(NetDebugHistory.NumFrames);

			DataType FrameData;
			for (int32 FrameIndex = 0; FrameIndex < NetDebugHistory.NumFrames; ++FrameIndex)
			{
				FrameData = NetDebugHistory.DataHistory[FrameIndex];
				LocalFrames[FrameIndex] = FrameData.LocalFrame;
				ServerFrames[FrameIndex] = FrameData.ServerFrame;
				InputFrames[FrameIndex] = FrameData.InputFrame;
			}
		}
	}

	/** Print custom string along with values for each entry in history */
	FORCEINLINE virtual void DebugData(const FString& DebugText) override
	{
		UE_LOG(LogChaos, Log, TEXT("%s"), *DebugText);
		UE_LOG(LogChaos, Log, TEXT("	NumFrames in data collection: %d"), Super::NumFrames);

		if (Super::NumFrames >= 0)
		{
			for (int32 FrameIndex = 0; FrameIndex < Super::NumFrames; ++FrameIndex)
			{
				UE_LOG(LogChaos, Log, TEXT("		Index: %d || LocalFrame = %d || ServerFrame = %d || InputFrame = %d || bReceivedData = %d || bImportant = %d  ||  Data: %s")
				, FrameIndex
				, Super::DataHistory[FrameIndex].LocalFrame
				, Super::DataHistory[FrameIndex].ServerFrame
				, Super::DataHistory[FrameIndex].InputFrame
				, Super::DataHistory[FrameIndex].bReceivedData
				, Super::DataHistory[FrameIndex].bImportant
				, *Super::DataHistory[FrameIndex].DebugData());
			}
		}
	}

private :

	/** Serialized array size limit to guard against invalid network data */
	static int32 GetMaxArraySize()
	{
		static int32 MaxArraySize = UPhysicsSettings::Get()->GetPhysicsHistoryCount() * 4;
		return MaxArraySize;
	}

	/** Use net serialize path to serialize data  */
	FORCEINLINE bool NetSerializeData(DataType& FrameData, FArchive& Ar, UPackageMap* PackageMap) const 
	{
		bool bOutSuccess = false;
		UScriptStruct* ScriptStruct = DataType::StaticStruct();
		if (ScriptStruct->StructFlags & STRUCT_NetSerializeNative)
		{
			ScriptStruct->GetCppStructOps()->NetSerialize(Ar, PackageMap, bOutSuccess, &FrameData);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("TNetRewindHistory::NetSerializeData called on data struct %s without a native NetSerialize"), *ScriptStruct->GetName());

			// Not working for now since the packagemap could be null
			// UNetConnection* Connection = CastChecked<UPackageMapClient>(PackageMap)->GetConnection();
			// UNetDriver* NetDriver = Connection ? Connection->GetDriver() : nullptr;
			// TSharedPtr<FRepLayout> RepLayout = NetDriver ? NetDriver->GetStructRepLayout(ScriptStruct) : nullptr;
			//
			// if (RepLayout.IsValid())
			// {
			// 	bool bHasUnmapped = false;
			// 	RepLayout->SerializePropertiesForStruct(ScriptStruct, Ar, PackageMap, &FrameData, bHasUnmapped);
			//
			// 	bOutSuccess = true;
			// }
		}
		return bOutSuccess;
	}
};

/**
 * Base struct for replicated rewind history properties
 */
USTRUCT()
struct FNetworkPhysicsRewindDataProxy
{
	GENERATED_BODY()

	FNetworkPhysicsRewindDataProxy& operator=(const FNetworkPhysicsRewindDataProxy& Other);

	/** Causes the history to be serialized every time. If implemented, would prevent serializing if the history hasn't changed. */
	bool operator==(const FNetworkPhysicsRewindDataProxy& Other) const { return false; }

protected:
	bool NetSerializeBase(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess, TUniqueFunction<TUniquePtr<Chaos::FBaseRewindHistory>()> CreateHistoryFunction);

public:
	/** The history to be serialized */
	TUniquePtr<Chaos::FBaseRewindHistory> History;

	/** Component that utilizes this data */
	UPROPERTY()
	TObjectPtr<UNetworkPhysicsComponent> Owner = nullptr;
};

/**
 * Struct suitable for use as a replicated property to replicate input rewind history
 */
USTRUCT()
struct FNetworkPhysicsRewindDataInputProxy : public FNetworkPhysicsRewindDataProxy
{
	GENERATED_BODY()
		
	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess);
};

template<>
struct TStructOpsTypeTraits<FNetworkPhysicsRewindDataInputProxy> : public TStructOpsTypeTraitsBase2<FNetworkPhysicsRewindDataInputProxy>
{
	enum
	{
		WithNetSerializer = true,
		WithIdenticalViaEquality = true
	};
};

/**
 * Struct suitable for use as a replicated property to replicate state rewind history
 */
USTRUCT()
struct FNetworkPhysicsRewindDataStateProxy : public FNetworkPhysicsRewindDataProxy
{
	GENERATED_BODY()

	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess);
};

template<>
struct TStructOpsTypeTraits<FNetworkPhysicsRewindDataStateProxy> : public TStructOpsTypeTraitsBase2<FNetworkPhysicsRewindDataStateProxy>
{
	enum
	{
		WithNetSerializer = true,
		WithIdenticalViaEquality = true
	};
};
/**
 * Struct suitable for use as a replicated property to replicate input rewind history
 */
USTRUCT()
struct FNetworkPhysicsRewindDataImportantInputProxy : public FNetworkPhysicsRewindDataProxy
{
	GENERATED_BODY()
		
	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess);
};

template<>
struct TStructOpsTypeTraits<FNetworkPhysicsRewindDataImportantInputProxy> : public TStructOpsTypeTraitsBase2<FNetworkPhysicsRewindDataImportantInputProxy>
{
	enum
	{
		WithNetSerializer = true,
		WithIdenticalViaEquality = true
	};
};

/**
 * Struct suitable for use as a replicated property to replicate state rewind history
 */
USTRUCT()
struct FNetworkPhysicsRewindDataImportantStateProxy : public FNetworkPhysicsRewindDataProxy
{
	GENERATED_BODY()

	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess);
};

template<>
struct TStructOpsTypeTraits<FNetworkPhysicsRewindDataImportantStateProxy> : public TStructOpsTypeTraitsBase2<FNetworkPhysicsRewindDataImportantStateProxy>
{
	enum
	{
		WithNetSerializer = true,
		WithIdenticalViaEquality = true
	};
};


/**
 * Network physics rewind callback to manage all the sim callbacks rewind functionalities
 */
struct FNetworkPhysicsCallback : public Chaos::IRewindCallback
{
	FNetworkPhysicsCallback(UWorld* InWorld) : World(InWorld) 
	{ }

	// Delegate on the internal inputs process
	FOnPreProcessInputsInternal PreProcessInputsInternal;
	FOnPostProcessInputsInternal PostProcessInputsInternal;
	// Bind to this for additional processing on the GT during InjectInputs_External()
	FOnInjectInputsExternal InjectInputsExternal;

	// Rewind API
	virtual void InjectInputs_External(int32 PhysicsStep, int32 NumSteps) override;
	virtual void ProcessInputs_External(int32 PhysicsStep, const TArray<Chaos::FSimCallbackInputAndObject>& SimCallbackInputs);
	virtual void ProcessInputs_Internal(int32 PhysicsStep, const TArray<Chaos::FSimCallbackInputAndObject>& SimCallbackInputs) override;
	virtual void PreResimStep_Internal(int32 PhysicsStep, bool bFirst) override;
	virtual void PostResimStep_Internal(int32 PhysicsStep) override;
	virtual int32 TriggerRewindIfNeeded_Internal(int32 LatestStepCompleted) override;
	virtual void RegisterRewindableSimCallback_Internal(Chaos::ISimCallbackObject* SimCallbackObject) override
	{
		if (SimCallbackObject && SimCallbackObject->HasOption(Chaos::ESimCallbackOptions::Rewind))
		{
			RewindableCallbackObjects.Add(SimCallbackObject);
		}
	}

	virtual void UnregisterRewindableSimCallback_Internal(Chaos::ISimCallbackObject* SimCallbackObject) override
	{
		RewindableCallbackObjects.Remove(SimCallbackObject);
	}

	// Update client player on GT
	UE_DEPRECATED(5.4, "Physics frame offset is handled by the PlayerController automatically, it's recommended to use APlayerController::GetAsyncPhysicsTimestamp() to get the ServerFrame and LocalFrame on both client and server. Also disable the deprecated flow by setting p.net.CmdOffsetEnabled = 0")
	void UpdateClientPlayer_External(int32 PhysicsStep);

	// Update server player on GT
	UE_DEPRECATED(5.4, "Physics frame offset is handled by the PlayerController automatically, it's recommended to use APlayerController::GetAsyncPhysicsTimestamp() to get the ServerFrame and LocalFrame on both client and server. Also disable the deprecated flow by setting p.net.CmdOffsetEnabled = 0")
	void UpdateServerPlayer_External(int32 PhysicsStep);

	// World owning that callback
	UWorld* World = nullptr;

	// List of rewindable sim callback objects
	TArray<Chaos::ISimCallbackObject*> RewindableCallbackObjects;
};


/**
 * Network physics manager to initialize data required for rewind/resim
 */
UCLASS(MinimalAPI)
class UNetworkPhysicsSystem : public UWorldSubsystem
{
public:

	GENERATED_BODY()
	ENGINE_API UNetworkPhysicsSystem();

	friend struct FNetworkPhysicsCallback;

	// Subsystem Init/Deinit
	ENGINE_API virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	ENGINE_API virtual void Deinitialize() override;

	// Delegate at world init 
	ENGINE_API void OnWorldPostInit(UWorld* World, const UWorld::InitializationValues);
};


/**
 * Base network physics data that will be used by physics
 */
USTRUCT()
struct FNetworkPhysicsData
{
	GENERATED_USTRUCT_BODY()

	virtual ~FNetworkPhysicsData() = default;

	// Server frame at which this data has been generated
	UPROPERTY()
	int32 ServerFrame = INDEX_NONE;

	// Local frame at which this data has been generated
	UPROPERTY()
	int32 LocalFrame = INDEX_NONE;

	// Input frame used to generate the network data
	UPROPERTY()
	int32 InputFrame = INDEX_NONE;

	// If this data was received over the network or locally predicted
	bool bReceivedData = false;

	// If this data is marked as important (replicated reliably)
	bool bImportant = false;

	// Serialize the data into/from the archive
	void SerializeFrames(FArchive& Ar)
	{
		Ar << ServerFrame;
		Ar << LocalFrame;
		Ar << InputFrame;
	}

	void PrepareFrame(int32 CurrentFrame, bool bIsServer, int32 ClientFrameOffset)
	{
		LocalFrame = CurrentFrame;
		ServerFrame = bIsServer ? CurrentFrame : CurrentFrame + ClientFrameOffset;
		InputFrame = CurrentFrame;
		bReceivedData = false;
		bImportant = false;
	}

	/** Set if this data is important(replicated reliably) or unimportant(replicated unreliably)
	* NOTE: Default is to handle all inputs as unimportant, while one time events can be marked as important. */
	void SetImportant(bool bIsImportant)
	{
		bImportant = bIsImportant;
	}

	// Apply the data onto the network physics component
	virtual void ApplyData(UActorComponent* NetworkComponent) const
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS // TODO: Remove deprecation pragma and deprecated function call in UE 5.6
		ApplyDatas(NetworkComponent);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	// Build the data from the network physics component
	virtual void BuildData(const UActorComponent* NetworkComponent)
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS // TODO: Remove deprecation pragma and deprecated function call in UE 5.6
		BuildDatas(NetworkComponent);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}
	
	/** Define how to interpolate between two data points if we have a gap between known data.
	* @param MinData is data from a previous frame.
	* @param MaxData is data from a future frame.
	* EXAMPLE: We have input data for frame 1 and 4 and we need to interpolate data for frame 2 and 3 based on frame 1 as MinData and frame 4 as MaxData.
	*/
	virtual void InterpolateData(const FNetworkPhysicsData& MinData, const FNetworkPhysicsData& MaxData) { }

	/** Use to decay desired data during resimulation if data is forward predicted.
	* @param DecayAmount = Total amount of decay as a multiplier. 10% decay = 0.1.
	* NOTE: Decay is not accumulated, the data will be in its original state each time DecayData is called. DecayAmount will increase each time the input is predicted (reused).
	* EXAMPLE: Use to decay steering inputs to make resimulation not predict too much with a high steering value. Use DecayAmount of 0.1 to turn a steering value of 0.5 into 0.45 for example.
	*/ 
	virtual void DecayData(float DecayAmount) { }
	
	/** Define how to merge data together
	* @param FromData is data from a previous frame that is getting merged into the current data.
	* EXAMPLE: Simulated proxies might receive two inputs at the same time after having used the same input twice, to not miss any important inputs we need to take both inputs into account 
	* and to not get behind in simulation we need to apply them both at the same simulation tick meaning we merge the two new inputs to one input.
	*/
	virtual void MergeData(const FNetworkPhysicsData& FromData) { }

	/** Validate data received on the server from clients
	* EXAMPLE: Validate incoming inputs from clients and correct any invalid input commands.
	* NOTE: Changes to the data in this callback will be sent from server to clients.
	*/
	virtual void ValidateData(const UActorComponent* NetworkComponent) { }

	/** Define how to compare client and server data for the same frame, returning false means the data differ enough to trigger a resimulation.
	* @param PredictedData is data predicted on the client to compare with the current data received from the server.
	* NOTE: To use this function, CVars np2.Resim.CompareStateToTriggerRewind and/or np2.Resim.CompareInputToTriggerRewind needs to be set to true
	* or the equivalent settings overridden on the actor via UNetworkPhysicsSettingsComponent. 
	*/
	virtual bool CompareData(const FNetworkPhysicsData& PredictedData) { return true; }

	UE_DEPRECATED(5.4, "Deprecated, use ApplyData instead")
	virtual void ApplyDatas(UActorComponent* NetworkComponent) const { }
	UE_DEPRECATED(5.4, "Deprecated, use BuildData instead")
	virtual void BuildDatas(const UActorComponent* NetworkComponent) { }
	UE_DEPRECATED(5.4, "Deprecated, use InterpolateData instead")
	virtual void InterpolateDatas(const FNetworkPhysicsData& MinData, const FNetworkPhysicsData& MaxData) { InterpolateData(MinData, MaxData); }
	
	/** Return string with custom debug data */
	virtual const FString DebugData() { return FString(" - DebugData() not implemented - "); }

	bool operator==(const FNetworkPhysicsData& Other) const
	{
		return ServerFrame == Other.ServerFrame && LocalFrame == Other.LocalFrame && InputFrame == Other.InputFrame;
	}

	friend UNetworkPhysicsComponent;
};

USTRUCT()
struct UE_DEPRECATED(5.4, "Deprecated, use FNetworkPhysicsData instead") FNetworkPhysicsDatas : public FNetworkPhysicsData
{
	GENERATED_USTRUCT_BODY()
	virtual ~FNetworkPhysicsDatas() = default;

	void SerializeFrames(FArchive & Ar) { FNetworkPhysicsData::SerializeFrames(Ar); }
	
	UE_DEPRECATED(5.4, "FNetworkPhysicsDatas is deprecated, use FNetworkPhysicsData instead. InterpolateDatas is also changed from a Static Polymorphic function to a Dynamic Polymorphic virtual function named InterpolateData().")
	virtual void InterpolateDatas(const FNetworkPhysicsData& MinData, const FNetworkPhysicsData& MaxData) override
	{
		ensureMsgf(false, TEXT("FNetworkPhysicsDatas is deprecated from UE 5.4, use FNetworkPhysicsData instead where InterpolateDatas() has changed from a Static Polymorphic function to a Dynamic Polymorphic virtual function named InterpolateData()."));
		FNetworkPhysicsData::InterpolateData(MinData, MaxData);
	}
};

/** Base for helper, to create data and data history */
struct FNetworkPhysicsDataHelper
{
	virtual ~FNetworkPhysicsDataHelper() = default;
	
	virtual TUniquePtr<FNetworkPhysicsDataHelper> Clone() const = 0;
	virtual TUniquePtr<FNetworkPhysicsData> CreateUniqueData() const = 0;
	virtual TUniquePtr<Chaos::FBaseRewindHistory> CreateUniqueRewindHistory(const int32 Size) const = 0;
};

/** Helper for the creation of state / input data and history with correct derived type */
template<typename DataType>
struct TNetworkPhysicsDataHelper : FNetworkPhysicsDataHelper
{
	virtual ~TNetworkPhysicsDataHelper() = default;

	virtual TUniquePtr<FNetworkPhysicsDataHelper> Clone() const { return MakeUnique<TNetworkPhysicsDataHelper>(*this); }
	virtual TUniquePtr<FNetworkPhysicsData> CreateUniqueData() const { return MakeUnique<DataType>(); }
	virtual TUniquePtr<Chaos::FBaseRewindHistory> CreateUniqueRewindHistory(const int32 Size) const { return MakeUnique<TNetRewindHistory<DataType>>(Size); }
};

/**
 * Network physics component to add to actors or pawns that control their physic simulation through applying inputs,
 * and should support networking through physics resimulation.
 */
UCLASS(BlueprintType, MinimalAPI)
class UNetworkPhysicsComponent : public UActorComponent
{
	GENERATED_UCLASS_BODY()
public:
	ENGINE_API UNetworkPhysicsComponent();

	// Get the player controller
	ENGINE_API virtual APlayerController* GetPlayerController() const;

	// Init the network physics component 
	ENGINE_API void InitPhysics();

	// Called every frame
	ENGINE_API virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// Function to init the replicated properties
	ENGINE_API virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifeTimeProps) const override;

	// Used to create any physics engine information for this component 
	ENGINE_API virtual void BeginPlay() override;

	// Register the component into the network manager
	ENGINE_API virtual void InitializeComponent() override;

	// Unregister the component from the network manager
	ENGINE_API virtual void UninitializeComponent() override;

	// Remove state/input history from rewind data
	ENGINE_API void RemoveDataHistory();

	// Add state/input history to rewind data
	ENGINE_API void AddDataHistory();

	// Get the GameThread state history (not guaranteed to be the exact data used in physics, for that use GetStateHistory_Internal on PhysicsThread)
	ENGINE_API TSharedPtr<Chaos::FBaseRewindHistory>& GetStateHistory_External() { return StateHistory; }
	
	// Get the PhysicsThread state history (if there is none it returns the GameThread history)
	ENGINE_API TSharedPtr<Chaos::FBaseRewindHistory>& GetStateHistory_Internal();
	
	// Get the GameThread input history (not guaranteed to be the exact data used in physics, for that use GetInputHistory_Internal on PhysicsThread)
	ENGINE_API TSharedPtr<Chaos::FBaseRewindHistory>& GetInputHistory_External() { return InputHistory; }
	
	// Get the PhysicsThread input history (if there is none it returns the GameThread history)
	ENGINE_API TSharedPtr<Chaos::FBaseRewindHistory>& GetInputHistory_Internal();

	// Check if the world is on server
	ENGINE_API bool HasServerWorld() const;
	
	// Check if this is controlled locally through relayed inputs or an existing local player controller
	ENGINE_API bool IsLocallyControlled() const;

	/** Mark this as controlled through locally relayed inputs rather than controlled as a pawn through a player controller.
	* Set if NetworkPhysicsComponent is implemented on an AActor instead of APawn and it's currently being fed inputs, or if this is controlled by the server. 
	* NOTE: The actor for this NetworkPhysicsComponent also needs to be owned by the local client if this is used client-side. */
	ENGINE_API void SetIsRelayingLocalInputs(bool bInRelayingLocalInputs);

	/** Stop relaying local inputs after next network send.
	* Deferred version of SetIsRelayingLocalInputs(false) to ensure that the last replicated data gets sent.
	* This does not work on locally controlled APawns, see SetIsRelayingLocalInputs() for description. */
	ENGINE_API void StopRelayingLocalInputsDeferred()
	{
		if (bIsRelayingLocalInputs)
		{
			bStopRelayingLocalInputsDeferred = true;
		}
	}

	/** Check if this is controlled locally through relayed inputs from autonomous proxy. It's recommended to use IsLocallyControlled() when checking if this is locally controlled. */
	ENGINE_API const bool GetIsRelayingLocalInputs() const { return bIsRelayingLocalInputs; }

	/** Get FAsyncNetworkPhysicsComponent on the Physics Thread */
	FAsyncNetworkPhysicsComponent* GetNetworkPhysicsComponent_Internal() { return NetworkPhysicsComponent_Internal; }

protected : 

	// Server RPC to receive inputs from client
	UFUNCTION(Server, unreliable)
	ENGINE_API void ServerReceiveInputData(const FNetworkPhysicsRewindDataInputProxy& ClientInputs);

	// Server RPC to receive important inputs from client
	UFUNCTION(Server, reliable)
	ENGINE_API void ServerReceiveImportantInputData(const FNetworkPhysicsRewindDataImportantInputProxy& ClientInputs);

	// Client RPC to receive important inputs from server
	UFUNCTION(NetMulticast, reliable)
	ENGINE_API void MulticastReceiveImportantInputData(const FNetworkPhysicsRewindDataImportantInputProxy& ServerInputs);

	// Client RPC to receive important states from server
	UFUNCTION(NetMulticast, reliable)
	ENGINE_API void MulticastReceiveImportantStateData(const FNetworkPhysicsRewindDataImportantStateProxy& ServerStates);

	// replicated important physics input
	UPROPERTY(Transient)
	FNetworkPhysicsRewindDataImportantInputProxy ReplicatedImportantInput;

	// replicated important physics state
	UPROPERTY(Transient)
	FNetworkPhysicsRewindDataImportantStateProxy ReplicatedImportantState;

	// repnotify for the inputs on the client
	UFUNCTION()
	ENGINE_API void OnRep_SetReplicatedInputs();

	// repnotify for the states on the client
	UFUNCTION()
	ENGINE_API void OnRep_SetReplicatedStates();

	// replicated physics inputs
	UPROPERTY(Transient, ReplicatedUsing = OnRep_SetReplicatedInputs)
	FNetworkPhysicsRewindDataInputProxy ReplicatedInputs;

	// replicated physics states 
	UPROPERTY(Transient, ReplicatedUsing = OnRep_SetReplicatedStates)
	FNetworkPhysicsRewindDataStateProxy ReplicatedStates;

private:
	/** Send network data from marshaled data */
	void NetworkMarshaledData();

	/** Update the async component with GT properties */
	void UpdateAsyncComponent(const bool bFullUpdate);

	/** Setup data type and history in async input and trigger the creation of data history in the async component */
	ENGINE_API void CreateAsyncDataHistory();

	/** Set number of inputs to send over the network with each message, clamped to 1 as minimum */
	void SetNumberOfInputsToNetwork(uint16 RedundantInputs)
	{
		InputsToNetwork = FMath::Max((uint16)(1 + RedundantInputs), (uint16)1);
	}

	/** Set number of states to send over the network with each message, clamped to 1 as minimum */
	void SetNumberOfStatesToNetwork(uint16 RedundantStates)
	{
		StatesToNetwork = FMath::Max((uint16)(1 + RedundantStates), (uint16)1);
	}

public:
	/** Register and create both state and input to be both networked and cached in history */
	template<typename PhysicsTraits>
	void CreateDataHistory(UActorComponent* HistoryComponent);

	/**  Register and create input history
	* Please use CreateDataHistory() if both input and custom state are supposed to be networked and cached in history.
	* NOTE: Registering Input without State requires networking push-model to be enabled to take advantage of all the CPU and Network Bandwidth savings, CVar: Net.IsPushModelEnabled 1 */
	template<class InputsType>
	void CreateInputHistory(UActorComponent* HistoryComponent);

private:

	friend FNetworkPhysicsCallback;
	friend struct FNetworkPhysicsRewindDataInputProxy;
	friend struct FNetworkPhysicsRewindDataStateProxy;
	friend struct FNetworkPhysicsRewindDataImportantInputProxy;
	friend struct FNetworkPhysicsRewindDataImportantStateProxy;

	// Network Physics Component data internal to the physics thread
	FAsyncNetworkPhysicsComponent* NetworkPhysicsComponent_Internal;

	// States history on GameThread
	TSharedPtr<Chaos::FBaseRewindHistory> StateHistory;

	// Inputs history on GameThread
	TSharedPtr<Chaos::FBaseRewindHistory> InputHistory;

	// Helper for the creation of input data and history with correct derived type
	TUniquePtr<FNetworkPhysicsDataHelper> InputHelper;

	// Helper for the creation of state data and history with correct derived type
	TUniquePtr<FNetworkPhysicsDataHelper> StateHelper;

	// Send last N number of inputs each replication call to patch up holes due to packet loss
	uint16 InputsToNetwork = 3;

	// Send last N number of states each replication call to patch up holes due to packet loss
	uint16 StatesToNetwork = 1;

	// Actor component that will be used to fill the histories
	TObjectPtr<UActorComponent> ActorComponent;

	// Root components physics object
	Chaos::FPhysicsObjectHandle RootPhysicsObject;

	// Locally relayed inputs makes this component act as if it's a locally controlled pawn.
	bool bIsRelayingLocalInputs = false;
	
	// If we are currently relaying inputs and will stop after next network send.
	bool bStopRelayingLocalInputsDeferred = false;

	// ToDo, retrieve from NetworkPhysicsSettingsComponent so changes at runtime gets picked up
	bool bEnableUnreliableFlow = true;
	bool bEnableReliableFlow = false;
	bool bValidateDataOnGameThread = false;


	/** ----- Deprecated API ----- */
public:
	UE_DEPRECATED(5.4, "Deprecated, use ServerReceiveInputData instead")
		UFUNCTION(Server, unreliable, meta = (DeprecatedFunction, DeprecationMessage = "ServerReceiveInputsDatas has been deprecated. Use ServerReceiveInputData instead."))
		ENGINE_API void ServerReceiveInputsDatas(const FNetworkPhysicsRewindDataInputProxy& ClientInputs);

	// Replicate input data 
	UE_DEPRECATED(5.5, "Deprecated, inputs are sent from PT marshaled data and can't be manually triggered on GT.")
		ENGINE_API void SendInputData() {}

	UE_DEPRECATED(5.4, "Deprecated, use SendInputData() instead")
		ENGINE_API void SendLocalInputsDatas()
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
			SendInputData();
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	// Replicate state data
	UE_DEPRECATED(5.5, "Deprecated, states are sent from PT marshaled data and can't be manually triggered on GT.")
		ENGINE_API void SendStateData() {}

	UE_DEPRECATED(5.4, "Deprecated, use SendStateData() instead")
		ENGINE_API void SendLocalStatesDatas()
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
			SendStateData();
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	// Delegate linked to the physics rewind callback to send record local inputs/states
	UE_DEPRECATED(5.5, "Deprecated, Logic moved to async flow through FAsyncNetworkPhysicsComponent::OnPreProcessInputs_Internal instead")
		ENGINE_API void OnPreProcessInputsInternal(const int32 PhysicsStep) {};

	// Delegate linked to the physics rewind callback to send record local inputs/states
	UE_DEPRECATED(5.5, "Deprecated, Logic moved to async flow through FAsyncNetworkPhysicsComponent::OnPostProcessInputs_Internal instead")
		ENGINE_API void OnPostProcessInputsInternal(const int32 PhysicsStep) {};

	// Correct the player controller Server to local offset based on the received replicated states
	UE_DEPRECATED(5.4, "Physics frame offset is handled by the PlayerController automatically, it's recommended to use APlayerController::GetAsyncPhysicsTimestamp() to get the ServerFrame and LocalFrame on both client and server. Also disable the deprecated flow by setting p.net.CmdOffsetEnabled = 0")
		ENGINE_API void CorrectServerToLocalOffset(const int32 LocalToServerOffset);

	UE_DEPRECATED(5.4, "Deprecated, use RemoveDataHistory() instead")
		ENGINE_API void RemoveDatasHistory() { RemoveDataHistory(); }

	UE_DEPRECATED(5.4, "Deprecated, use AddDataHistory() instead")
		ENGINE_API void AddDatasHistory() { AddDataHistory(); }

	// Enable RewindData history caching and return the history size
	UE_DEPRECATED(5.5, "Deprecated, don't setup rewind data from GameThread")
		ENGINE_API int32 SetupRewindData() { return 0; }

	UE_DEPRECATED(5.5, "Deprecated, use GetStateHistory_Internal() or GetStateHistory_External() instead")
		TSharedPtr<Chaos::FBaseRewindHistory>& GetStateHistory() { return GetStateHistory_Internal(); }

	UE_DEPRECATED(5.4, "Deprecated, use GetStateHistory() instead")
		TSharedPtr<Chaos::FBaseRewindHistory>& GetStatesHistory()
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		return GetStateHistory();
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	UE_DEPRECATED(5.5, "Deprecated, use GetInputHistory_Internal() or GetInputHistory_External() instead")
		TSharedPtr<Chaos::FBaseRewindHistory>& GetInputHistory() { return GetInputHistory_Internal(); }
	
	UE_DEPRECATED(5.4, "Deprecated, use GetInputHistory() instead")
		TSharedPtr<Chaos::FBaseRewindHistory>& GetInputsHistory()
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		return GetInputHistory();
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}

	// Check if the player controller exists and is local
	UE_DEPRECATED(5.4, "Deprecated, use IsLocallyControlled() which takes both local player controlled and local relayed inputs into account.")
		ENGINE_API bool HasLocalController() const;

	template<typename PhysicsTraits>
	UE_DEPRECATED(5.4, "Deprecated, use CreateDataHistory() instead")
		void CreateDatasHistory(UActorComponent* HistoryComponent);

	/** Returns the current amount of input decay during resimulation as a magnitude from 0.0 to 1.0. Returns 0 if not currently resimulating. */
	UE_DEPRECATED(5.5, "Deprecated, use FAsyncNetworkPhysicsComponent::GetCurrentInputDecay instead.")
		ENGINE_API const float GetCurrentInputDecay(FNetworkPhysicsData* PhysicsData);
};

/** DEPRECATED UE 5.4 */
template<typename PhysicsTraits>
FORCEINLINE void UNetworkPhysicsComponent::CreateDatasHistory(UActorComponent* HistoryComponent)
{
	CreateDataHistory<PhysicsTraits>(HistoryComponent);
}

template<typename PhysicsTraits>
FORCEINLINE void UNetworkPhysicsComponent::CreateDataHistory(UActorComponent* HistoryComponent)
{
	InputHelper = MakeUnique<TNetworkPhysicsDataHelper<typename PhysicsTraits::InputsType>>();
	StateHelper = MakeUnique<TNetworkPhysicsDataHelper<typename PhysicsTraits::StatesType>>();

	ReplicatedInputs.History = MakeUnique<TNetRewindHistory<typename PhysicsTraits::InputsType>>(InputsToNetwork);
	ReplicatedInputs.Owner = this;

	ReplicatedStates.History = MakeUnique<TNetRewindHistory<typename PhysicsTraits::StatesType>>(StatesToNetwork);
	ReplicatedStates.Owner = this;

	ReplicatedImportantInput.History = MakeUnique<TNetRewindHistory<typename PhysicsTraits::InputsType>>(1);
	ReplicatedImportantInput.Owner = this;

	ReplicatedImportantState.History = MakeUnique<TNetRewindHistory<typename PhysicsTraits::StatesType>>(1);
	ReplicatedImportantState.Owner = this;

	ActorComponent = HistoryComponent;

	CreateAsyncDataHistory();
}

template<class InputsType>
FORCEINLINE void UNetworkPhysicsComponent::CreateInputHistory(UActorComponent* HistoryComponent)
{
	InputHelper = MakeUnique<TNetworkPhysicsDataHelper<InputsType>>();

	ReplicatedInputs.History = MakeUnique<TNetRewindHistory<InputsType>>(InputsToNetwork);
	ReplicatedInputs.Owner = this;

	ReplicatedImportantInput.History = MakeUnique<TNetRewindHistory<InputsType>>(1);
	ReplicatedImportantInput.Owner = this;

	ActorComponent = HistoryComponent;

	CreateAsyncDataHistory();
}


// --------------------------- PhysicsThread Network Physics Component ---------------------------

struct FAsyncNetworkPhysicsComponentInput : public Chaos::FSimCallbackInput
{
	TOptional<bool> bIsLocallyControlled;
	TOptional<ENetMode> NetMode;
	TOptional<ENetRole> NetRole;
	TOptional<int32> NetworkPhysicsTickOffset;
	TOptional<EPhysicsReplicationMode> PhysicsReplicationMode;
	TOptional<TObjectPtr<UActorComponent>> ActorComponent;
	TOptional<FString> ActorName;
	TOptional<TUniquePtr<FNetworkPhysicsDataHelper>> InputHelper;
	TOptional<TUniquePtr<FNetworkPhysicsDataHelper>> StateHelper;
	TOptional<bool> bRegisterDataHistoryInRewindData;
	TOptional<bool> bUnregisterDataHistoryFromRewindData;

	TUniquePtr<Chaos::FBaseRewindHistory> InputData;
	TUniquePtr<Chaos::FBaseRewindHistory> StateData;

	TArray<TUniquePtr<Chaos::FBaseRewindHistory>> InputDataImportant;
	TArray<TUniquePtr<Chaos::FBaseRewindHistory>> StateDataImportant;

	void Reset()
	{
		bIsLocallyControlled.Reset();
		NetMode.Reset();
		NetRole.Reset();
		NetworkPhysicsTickOffset.Reset();
		PhysicsReplicationMode.Reset();
		ActorComponent.Reset();
		ActorName.Reset();
		InputHelper.Reset();
		StateHelper.Reset();
		bRegisterDataHistoryInRewindData.Reset();
		bUnregisterDataHistoryFromRewindData.Reset();

		if (InputData)
		{
			InputData->ResetFast();
		}
		if (StateData)
		{
			StateData->ResetFast();
		}

		// Todo, optimize
		InputDataImportant.Reset();
		StateDataImportant.Reset();
	}
};

struct FAsyncNetworkPhysicsComponentOutput : public Chaos::FSimCallbackOutput
{
	TUniquePtr<Chaos::FBaseRewindHistory> InputData;
	TUniquePtr<Chaos::FBaseRewindHistory> StateData;

	TArray<TUniquePtr<Chaos::FBaseRewindHistory>> InputDataImportant;
	TArray<TUniquePtr<Chaos::FBaseRewindHistory>> StateDataImportant;

	void Reset()
	{
		if (InputData)
		{
			InputData->ResetFast();
		}
		if (StateData)
		{
			StateData->ResetFast();
		}

		// Todo, optimize
		InputDataImportant.Reset();
		StateDataImportant.Reset();
	}
};

class FAsyncNetworkPhysicsComponent : public Chaos::TSimCallbackObject<
	FAsyncNetworkPhysicsComponentInput,
	FAsyncNetworkPhysicsComponentOutput,
	Chaos::ESimCallbackOptions::None>
{

public:
	FAsyncNetworkPhysicsComponent();
	~FAsyncNetworkPhysicsComponent();

	// Get reference to async output for current internal frame and initialize it if not already done
	FAsyncNetworkPhysicsComponentOutput& GetAsyncOutput_Internal();

	// If this network physics component is locally controlled, can be either server or autonomous proxy
	const bool IsLocallyControlled() const { return bIsLocallyControlled; }

	// If we are on the server
	const bool IsServer() const { return (NetMode == ENetMode::NM_DedicatedServer || NetMode == ENetMode::NM_ListenServer); }

	// Get the ENetRole
	const ENetRole GetNetRole() const { return NetRole; }

	// Get actor name
	const FString GetActorName() const { return ActorName; }

	// Get the physics tick offset (add to clients physics tick to get the servers corresponding physics tick)
	const int32 GetNetworkPhysicsTickOffset() const { return NetworkPhysicsTickOffset; }

	// Get the physics replication mode used
	const EPhysicsReplicationMode GetPhysicsReplicationMode() const { return PhysicsReplicationMode; }

	// Add state/input history to rewind data
	void RegisterDataHistoryInRewindData();

	// Remove state/input history from rewind data
	void UnregisterDataHistoryFromRewindData();

	// Enable RewindData history caching and return the history size
	const int32 SetupRewindData();

private:
	// Initialize, bind delegates etc.
	virtual void OnPostInitialize_Internal() override;

	// Delegate ran at the start of FNetworkPhysicsCallback::ProcessInputs_Internal, used to receive and apply inputs and states if needed.
	void OnPreProcessInputs_Internal(const int32 PhysicsStep);

	// Delegate ran at the end of FNetworkPhysicsCallback::ProcessInputs_Internal, used to record and send inputs and states if needed.
	void OnPostProcessInputs_Internal(const int32 PhysicsStep);

	// Consume data from async input
	void ConsumeAsyncInput(const int32 PhysicsStep);

	/** Get the rigid solver */
	Chaos::FPBDRigidsSolver* GetRigidSolver();

	/** Get the rigid solver evolution */
	Chaos::FPBDRigidsEvolution* GetEvolution();

	/** Get the settings for this NetworkPhysicsComponent */
	const FNetworkPhysicsSettingsNetworkPhysicsComponent& GetComponentSettings();

	/** Trigger a resimulation on frame */
	void TriggerResimulation(int32 ResimFrame);

	/** Returns the current amount of input decay during resimulation as a magnitude from 0.0 to 1.0. Returns 0 if not currently resimulating. */
	const float GetCurrentInputDecay(const FNetworkPhysicsData* PhysicsData);

	/** Populate input data in AsyncOutput to send over the network */
	void SendInputData_Internal(FAsyncNetworkPhysicsComponentOutput& AsyncOutput, const int32 PhysicsStep);

	/** Populate state data in AsyncOutput to send over the network */
	void SendStateData_Internal(FAsyncNetworkPhysicsComponentOutput& AsyncOutput, const int32 PhysicsStep);

private:
	friend UNetworkPhysicsComponent;

	bool bIsLocallyControlled;
	ENetMode NetMode;
	ENetRole NetRole;
	int32 NetworkPhysicsTickOffset;
	EPhysicsReplicationMode PhysicsReplicationMode;
	FString ActorName;

	int32 LastInputSendFrame = INDEX_NONE;
	int32 LastStateSendFrame = INDEX_NONE;
	int32 NewImportantInputFrame = INT_MAX;

	// Component settings
	FNetworkPhysicsSettingsComponentAsync* SettingsComponent;
	static const FNetworkPhysicsSettingsNetworkPhysicsComponent SettingsNetworkPhysicsComponent_Default;

	// Actor component that will be used to fill the histories
	TWeakObjectPtr<UActorComponent> ActorComponent;

	// Root components physics object
	Chaos::FPhysicsObjectHandle RootPhysicsObject;

	// States history uses to rewind simulation 
	TSharedPtr<Chaos::FBaseRewindHistory> StateHistory;

	// Inputs history used during simulation
	TSharedPtr<Chaos::FBaseRewindHistory> InputHistory;

	// Local temporary inputs data used by pre/post process inputs functions
	TUniquePtr<FNetworkPhysicsData> InputData;
	
	// Local temporary inputs data used by ConsumeAsyncInput
	TUniquePtr<FNetworkPhysicsData> LatestInputReceiveData;

	// Local temporary states data used by pre/post process inputs functions
	TUniquePtr<FNetworkPhysicsData> StateData;

	// Send last N number of inputs each replication call to patch up holes due to packet loss
	uint16 InputsToNetwork = 3;

	// Send last N number of states each replication call to patch up holes due to packet loss
	uint16 StatesToNetwork = 1;

	FDelegateHandle DelegateOnPreProcessInputs_Internal;
	FDelegateHandle DelegateOnPostProcessInputs_Internal;
};
