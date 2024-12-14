// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ILiveLinkClient.h"

class ILiveLinkSubject;
struct FPropertyChangedEvent;
struct ILiveLinkProvider;

class ULiveLinkPreset;
class ULiveLinkSourceSettings;
class ULiveLinkSubjectBase;
class FLiveLinkSourceCollection;

// Live Link Log Category
DECLARE_LOG_CATEGORY_EXTERN(LogLiveLink, Log, All);

DECLARE_STATS_GROUP(TEXT("Live Link"), STATGROUP_LiveLink, STATCAT_Advanced);

struct FLiveLinkSubjectTimeSyncData
{
	bool bIsValid = false;
	FFrameTime OldestSampleTime;
	FFrameTime NewestSampleTime;
	FFrameRate SampleFrameRate;
};

PRAGMA_DISABLE_DEPRECATION_WARNINGS
struct FLiveLinkSkeletonStaticData;

class LIVELINK_API FLiveLinkClient_Base_DEPRECATED : public ILiveLinkClient
{
public:
	//~ Begin ILiveLinkClient implementation
	virtual void PushSubjectSkeleton(FGuid SourceGuid, FName SubjectName, const FLiveLinkRefSkeleton& RefSkeleton) override;
	virtual void PushSubjectData(FGuid SourceGuid, FName SubjectName, const FLiveLinkFrameData& FrameData) override;
	virtual void ClearSubject(FName SubjectName) override;
	virtual void GetSubjectNames(TArray<FName>& SubjectNames) override;
	virtual const FLiveLinkSubjectFrame* GetSubjectData(FName InSubjectName) override;
	virtual const FLiveLinkSubjectFrame* GetSubjectDataAtWorldTime(FName InSubjectName, double InWorldTime) override;
	virtual const FLiveLinkSubjectFrame* GetSubjectDataAtSceneTime(FName InSubjectName, const FTimecode& InSceneTime) override;
	virtual bool EvaluateFrameAtSceneTime_AnyThread(FLiveLinkSubjectName SubjectName, const FTimecode& SceneTime, TSubclassOf<ULiveLinkRole> DesiredRole, FLiveLinkSubjectFrameData& OutFrame) override;
	virtual const TArray<FLiveLinkFrame>* GetSubjectRawFrames(FName SubjectName) override;
	virtual void ClearSubjectsFrames(FName SubjectName) override;
	virtual void ClearAllSubjectsFrames() override;
	virtual TSubclassOf<ULiveLinkRole> GetSubjectRole(const FLiveLinkSubjectKey& SubjectKey) const override;
	virtual TSubclassOf<ULiveLinkRole> GetSubjectRole(FLiveLinkSubjectName SubjectName) const override;
	virtual bool DoesSubjectSupportsRole(const FLiveLinkSubjectKey& SubjectKey, TSubclassOf<ULiveLinkRole> SupportedRole) const override;
	virtual bool DoesSubjectSupportsRole(FLiveLinkSubjectName SubjectName, TSubclassOf<ULiveLinkRole> SupportedRole) const override;

	//~ End ILiveLinkClient implementation

protected:
	virtual void AquireLock_Deprecation() = 0;
	virtual void ReleaseLock_Deprecation() = 0;
	virtual void ClearFrames_Deprecation(const FLiveLinkSubjectKey& SubjectKey) = 0;
	virtual FLiveLinkSkeletonStaticData* GetSubjectAnimationStaticData_Deprecation(const FLiveLinkSubjectKey& SubjectKey) = 0;
};
PRAGMA_ENABLE_DEPRECATION_WARNINGS


class LIVELINK_API FLiveLinkClient : public FLiveLinkClient_Base_DEPRECATED
{
public:
	/** Default constructor that setups LiveLink to use the SamplingInput delegate to tick. */
	FLiveLinkClient();

	//~ Constructor that allow providing a custom delegate for ticking LiveLink.  
	FLiveLinkClient(FSimpleMulticastDelegate& InTickingDelegate);
	FLiveLinkClient(FTSSimpleMulticastDelegate& InTickingDelegate);
	

	virtual ~FLiveLinkClient();

	//~ Begin ILiveLinkClient implementation
	virtual FGuid AddSource(TSharedPtr<ILiveLinkSource> Source) override;
	virtual FGuid AddVirtualSubjectSource(FName SourceName) override;
	virtual bool CreateSource(const FLiveLinkSourcePreset& SourcePreset) override;
	virtual void RemoveSource(TSharedPtr<ILiveLinkSource> Source) override;
	virtual void RemoveSource(FGuid InEntryGuid) override;
	virtual bool HasSourceBeenAdded(TSharedPtr<ILiveLinkSource> Source) const override;
	virtual TArray<FGuid> GetSources(bool bEvenIfPendingKill = false) const override;
	virtual TArray<FGuid> GetVirtualSources(bool bEvenIfPendingKill = false) const override;
	virtual FLiveLinkSourcePreset GetSourcePreset(FGuid SourceGuid, UObject* DuplicatedObjectOuter) const override;
	virtual FText GetSourceType(FGuid EntryGuid) const override;
	virtual FText GetSourceStatus(FGuid EntryGuid) const override;
	virtual FText GetSourceMachineName(FGuid EntryGuid) const override;
	virtual bool IsSourceStillValid(FGuid EntryGuid) const override;

	virtual void PushSubjectStaticData_AnyThread(const FLiveLinkSubjectKey& SubjectKey, TSubclassOf<ULiveLinkRole> Role, FLiveLinkStaticDataStruct&& StaticData) override;
	virtual void PushSubjectFrameData_AnyThread(const FLiveLinkSubjectKey& SubjectKey, FLiveLinkFrameDataStruct&& FrameData) override;

	virtual bool CreateSubject(const FLiveLinkSubjectPreset& SubjectPreset) override;
	virtual bool AddVirtualSubject(const FLiveLinkSubjectKey& VirtualSubjectKey, TSubclassOf<ULiveLinkVirtualSubject> VirtualSubjectClass) override;
	virtual void RemoveVirtualSubject(const FLiveLinkSubjectKey& VirtualSubjectKey) override;
	virtual void RemoveSubject_AnyThread(const FLiveLinkSubjectKey& SubjectKey) override;
	virtual void ClearSubjectsFrames_AnyThread(FLiveLinkSubjectName SubjectName) override;
	virtual void ClearSubjectsFrames_AnyThread(const FLiveLinkSubjectKey& InSubjectKey) override;
	virtual void ClearAllSubjectsFrames_AnyThread() override;
	virtual TSubclassOf<ULiveLinkRole> GetSubjectRole_AnyThread(const FLiveLinkSubjectKey& SubjectKey) const override;
	virtual TSubclassOf<ULiveLinkRole> GetSubjectRole_AnyThread(FLiveLinkSubjectName SubjectName) const override;
	virtual bool DoesSubjectSupportsRole_AnyThread(const FLiveLinkSubjectKey& SubjectKey, TSubclassOf<ULiveLinkRole> SupportedRole) const override;
	virtual bool DoesSubjectSupportsRole_AnyThread(FLiveLinkSubjectName SubjectName, TSubclassOf<ULiveLinkRole> SupportedRole) const override;

	virtual FLiveLinkSubjectPreset GetSubjectPreset(const FLiveLinkSubjectKey& SubjectKey, UObject* DuplicatedObjectOuter) const override;
	virtual TArray<FLiveLinkSubjectKey> GetSubjects(bool bIncludeDisabledSubject, bool bIncludeVirtualSubject) const override;

	virtual bool IsSubjectValid(const FLiveLinkSubjectKey& SubjectKey) const override;
	virtual bool IsSubjectValid(FLiveLinkSubjectName SubjectName) const override;
	virtual bool IsSubjectEnabled(const FLiveLinkSubjectKey& SubjectKey, bool bForThisFrame) const override;
	virtual bool IsSubjectEnabled(FLiveLinkSubjectName SubjectName) const override;
	virtual void SetSubjectEnabled(const FLiveLinkSubjectKey& SubjectKey, bool bEnabled) override;
	virtual bool IsSubjectTimeSynchronized(const FLiveLinkSubjectKey& SubjectKey) const override;
	virtual bool IsSubjectTimeSynchronized(FLiveLinkSubjectName SubjectName) const override;
	virtual bool IsVirtualSubject(const FLiveLinkSubjectKey& SubjectKey) const override;
	virtual ELiveLinkSubjectState GetSubjectState(FLiveLinkSubjectName InSubjectName) const override;


	virtual TArray<FLiveLinkSubjectKey> GetSubjectsSupportingRole(TSubclassOf<ULiveLinkRole> SupportedRole, bool bIncludeDisabledSubject, bool bIncludeVirtualSubject) const override;
	virtual TArray<FLiveLinkTime> GetSubjectFrameTimes(const FLiveLinkSubjectKey& SubjectKey) const override;
	virtual TArray<FLiveLinkTime> GetSubjectFrameTimes(FLiveLinkSubjectName SubjectName) const override;
	virtual FText GetSourceNameOverride(const FLiveLinkSubjectKey& SubjectKey) const override;
	virtual FText GetSubjectDisplayName(const FLiveLinkSubjectKey& SubjectKey) const override;
	virtual ULiveLinkSourceSettings* GetSourceSettings(const FGuid& SourceGuid) const override;
	virtual UObject* GetSubjectSettings(const FLiveLinkSubjectKey& SubjectKey) const override;
	virtual const FLiveLinkStaticDataStruct* GetSubjectStaticData_AnyThread(const FLiveLinkSubjectKey& InSubjectKey, bool bGetOverrideData=true) const override;


	virtual bool EvaluateFrameFromSource_AnyThread(const FLiveLinkSubjectKey& SubjectKey, TSubclassOf<ULiveLinkRole> Role, FLiveLinkSubjectFrameData& OutFrame) override;
	virtual bool EvaluateFrame_AnyThread(FLiveLinkSubjectName SubjectName, TSubclassOf<ULiveLinkRole> Role, FLiveLinkSubjectFrameData& OutFrame) override;
	virtual bool EvaluateFrameAtWorldTime_AnyThread(FLiveLinkSubjectName SubjectName, double WorldTime, TSubclassOf<ULiveLinkRole> DesiredRole, FLiveLinkSubjectFrameData& OutFrame) override;
	virtual bool EvaluateFrameAtSceneTime_AnyThread(FLiveLinkSubjectName SubjectName, const FQualifiedFrameTime& FrameTime, TSubclassOf<ULiveLinkRole> DesiredRole, FLiveLinkSubjectFrameData& OutFrame) override;
	virtual void ForceTick() override;
	virtual bool HasPendingSubjectFrames() override;
	virtual void ClearOverrideStaticData_AnyThread(const FLiveLinkSubjectKey& InSubjectKey) override;

	virtual FSimpleMulticastDelegate& OnLiveLinkTicked() override;
	virtual FSimpleMulticastDelegate& OnLiveLinkSourcesChanged() override;
	virtual FSimpleMulticastDelegate& OnLiveLinkSubjectsChanged() override;
	virtual FOnLiveLinkSourceChangedDelegate& OnLiveLinkSourceAdded() override;
	virtual FOnLiveLinkSourceChangedDelegate& OnLiveLinkSourceRemoved() override;
	virtual FOnLiveLinkSubjectChangedDelegate& OnLiveLinkSubjectAdded() override;
	virtual FOnLiveLinkSubjectChangedDelegate& OnLiveLinkSubjectRemoved() override;
#if WITH_EDITOR
	virtual FOnLiveLinkSubjectEvaluated& OnLiveLinkSubjectEvaluated() override;
#endif

	virtual void RegisterForFrameDataReceived(const FLiveLinkSubjectKey& InSubjectKey, const FOnLiveLinkSubjectStaticDataReceived::FDelegate& OnStaticDataReceived_AnyThread, const FOnLiveLinkSubjectFrameDataReceived::FDelegate& OnFrameDataReceived_AnyThread, FDelegateHandle& OutStaticDataReceivedHandle, FDelegateHandle& OutFrameDataReceivedHandle) override;
	virtual void UnregisterForFrameDataReceived(const FLiveLinkSubjectKey& InSubjectKey, FDelegateHandle InStaticDataReceivedHandle, FDelegateHandle InFrameDataReceivedHandle) override;
	virtual bool RegisterForSubjectFrames(FLiveLinkSubjectName SubjectName, const FOnLiveLinkSubjectStaticDataAdded::FDelegate& OnStaticDataAdded, const FOnLiveLinkSubjectFrameDataAdded::FDelegate& OnFrameDataAdded, FDelegateHandle& OutStaticDataReceivedHandle, FDelegateHandle& OutFrameDataReceivedHandle, TSubclassOf<ULiveLinkRole>& OutSubjectRole, FLiveLinkStaticDataStruct* OutStaticData = nullptr) override;
	virtual void UnregisterSubjectFramesHandle(FLiveLinkSubjectName InSubjectName, FDelegateHandle InStaticDataReceivedHandle, FDelegateHandle InFrameDataReceivedHandle) override;
	//~ End ILiveLinkClient implementation

public:
	/** Struct that hold the pending static data that will be pushed next tick. */
	struct FPendingSubjectStatic
	{
		FLiveLinkSubjectKey SubjectKey;
		TSubclassOf<ULiveLinkRole> Role;
		FLiveLinkStaticDataStruct StaticData;
		TMap<FName, FString> ExtraMetadata;
	};

	/** Struct that hold the pending frame data that will be pushed next tick. */
	struct FPendingSubjectFrame
	{
		FLiveLinkSubjectKey SubjectKey;
		FLiveLinkFrameDataStruct FrameData;
	};


	/** The tick callback to update the pending work and clear the subject's snapshot*/
	void Tick();

	/** Remove all sources from the live link client */
	void RemoveAllSources();

	/**
	 * Remove all sources and wait for them to be removed. This is a blocking operation.
	 * @param InTimeout The timeout in seconds to wait.
	 * @return Whether all sources were removed successfully.
	 */
	bool RemoveAllSourcesWithTimeout(float InTimeout);

#if WITH_EDITOR
	/** Call initialize again on an existing virtual subject. Used for when a Blueprint Virtual Subject is compiled */
	void ReinitializeVirtualSubject(const FLiveLinkSubjectKey& SubjectKey);
#endif

	/** Callback when property changed for one of the source settings */
	void OnPropertyChanged(FGuid EntryGuid, const FPropertyChangedEvent& PropertyChangedEvent);

	/**
	 * Get all sources that can be displayed in the UI's source list.
	 * @param bIncludeVirtualSources Whether or not to include virtual sources in the returned list, since virtual sources are not displayed in the source list.
	 * @return the list of displayable sources.
	 */
	TArray<FGuid> GetDisplayableSources(bool bIncludeVirtualSources = false) const;

	FLiveLinkSubjectTimeSyncData GetTimeSyncData(FLiveLinkSubjectName SubjectName);

	/** Get the rebroadcast name for a given subject. (Defaults to the subject's subject name, but can be overriden. */
	FName GetRebroadcastName(const FLiveLinkSubjectKey& InSubjectKey) const;

	/** Push subject static data with additional metadata. */
	void PushPendingSubject_AnyThread(FPendingSubjectStatic&& PendingSubject);

	UE_DEPRECATED(4.23, "FLiveLinkClient::GetSourceTypeForEntry is deprecated. Please use GetSourceType instead!")
	FText GetSourceTypeForEntry(FGuid EntryGuid) const { return GetSourceType(EntryGuid); }
	UE_DEPRECATED(4.23, "FLiveLinkClient::GetMachineNameForEntry is deprecated. Please use GetSourceMachineName instead!")
	FText GetMachineNameForEntry(FGuid EntryGuid) const { return GetSourceMachineName(EntryGuid); }
	UE_DEPRECATED(4.23, "FLiveLinkClient::GetEntryStatusForEntry is deprecated. Please use GetSourceStatus instead!")
	FText GetEntryStatusForEntry(FGuid EntryGuid) const { return GetSourceStatus(EntryGuid); }
	UE_DEPRECATED(4.23, "FLiveLinkClient::GetSourceEntries is deprecated. Please use ILiveLinkClient::GetSources instead!")
	const TArray<FGuid>& GetSourceEntries() const;
	UE_DEPRECATED(4.23, "FLiveLinkClient::AddVirtualSubject(FName) is deprecated. Please use AddVirtualSubject(FName, TSubClass<ULiveLinkVirtualSubject>) instead!")
	void AddVirtualSubject(FName NewVirtualSubjectName);
	UE_DEPRECATED(4.25, "FLiveLinkClient::AddVirtualSubject(FLiveLinkSubjectName, TSubclassOf<ULiveLinkVirtualSubject>) is deprecated. Please use bool AddVirtualSubject(FLiveLinkSubjectKey, TSubClass<ULiveLinkVirtualSubject>) instead!")
	void AddVirtualSubject(FLiveLinkSubjectName VirtualSubjectName, TSubclassOf<ULiveLinkVirtualSubject> VirtualSubjectClass);
	UE_DEPRECATED(4.23, "FLiveLinkClient::OnLiveLinkSourcesChanged is deprecated. Please use OnLiveLinkSourceAdded instead!")
	FDelegateHandle RegisterSourcesChangedHandle(const FSimpleMulticastDelegate::FDelegate& SourcesChanged);
	UE_DEPRECATED(4.23, "FLiveLinkClient::OnLiveLinkSourcesChanged is deprecated. Please use OnLiveLinkSourceAdded instead!")
	void UnregisterSourcesChangedHandle(FDelegateHandle Handle);

protected:
	//~ Begin FLiveLinkClient_Base_DEPRECATED implementation
	using FLiveLinkClient_Base_DEPRECATED::EvaluateFrameAtSceneTime_AnyThread;
	virtual void AquireLock_Deprecation() override;
	virtual void ReleaseLock_Deprecation() override;
	virtual void ClearFrames_Deprecation(const FLiveLinkSubjectKey& SubjectKey) override;
	virtual FLiveLinkSkeletonStaticData* GetSubjectAnimationStaticData_Deprecation(const FLiveLinkSubjectKey& SubjectKey) override;
	//~ End FLiveLinkClient_Base_DEPRECATED implementation

	/**
	 * Add delegates that will be triggered for all subjects.
	 * @param InOnStaticDataAdded The delegate for when static data is added.
	 * @param InOnFrameDataAdded The delegate for when frame data is added.
	 * @param OutStaticDataAddedHandle [Out] The handle for adding static data.
	 * @param OutFrameDataAddedHandle [Out] The handle for adding frame data.
	 * @param bUseUnmappedData Whether to use raw, unmapped data. If false, then the data received may have a remapper applied.
	 */
	bool RegisterGlobalSubjectFramesDelegate(const FOnLiveLinkSubjectStaticDataAdded::FDelegate& InOnStaticDataAdded,
		const FOnLiveLinkSubjectFrameDataAdded::FDelegate& InOnFrameDataAdded, FDelegateHandle& OutStaticDataAddedHandle,
		FDelegateHandle& OutFrameDataAddedHandle, bool bUseUnmappedData);
	/**
	 * Remove the delegates that were triggered for all subjects.
	 * @param InStaticDataAddedHandle The static data handle to remove.
	 * @param InFrameDataAddedHandle The frame data handle to remove.
	 * @param bUseUnmappedData Whether this is for unmapped or remapped data.
	 */
	void UnregisterGlobalSubjectFramesDelegate(FDelegateHandle& InStaticDataAddedHandle, FDelegateHandle& InFrameDataAddedHandle, bool bUseUnmappedData);

private:
	/** Common initialization code for the different constructors. */
	void Initialize();

	/** Remove old sources & subject,  */
	void DoPendingWork();

	/** Update the added sources */
	void UpdateSources();

	/**
	 * Build subject data so that during the rest of the tick it can be read without
	 * thread locking or mem copying
	 */
	void BuildThisTicksSubjectSnapshot();

	/** Cache the game thread values to be reused on any thread */
	void CacheValues();

	void PushSubjectStaticData_Internal(FPendingSubjectStatic&& SubjectStaticData);
	void PushSubjectFrameData_Internal(FPendingSubjectFrame&& SubjectFrameData);

	/** Remove all sources. */
	void Shutdown();

	/** Process virtual subject for rebroadcast purpose */
	void HandleSubjectRebroadcast(ILiveLinkSubject* InSubject, const FLiveLinkFrameDataStruct& InFrameData);

	/** Called when a subject is removed. Used to remove rebroadcasted subjects */
	void OnSubjectRemovedCallback(FLiveLinkSubjectKey InSubjectKey);

	/** Removes a subject from the rebroadcast provider and resets it if there are no more subjects */
	void RemoveRebroadcastedSubject(FLiveLinkSubjectKey InSubjectKey);

protected:
	/** Broadcast out to the SubjectFrameAddedHandles a frame data update. */
	void BroadcastFrameDataUpdate(const FLiveLinkSubjectKey& InSubjectKey, const FLiveLinkFrameDataStruct& InFrameData);

	/** Method that can be overriden in child classes to provide their own rebroadcast provider. */
	virtual TSharedPtr<ILiveLinkProvider> GetRebroadcastLiveLinkProvider() const;

	/** The current collection used. */
	TUniquePtr<FLiveLinkSourceCollection> Collection;

	/** LiveLink Provider for rebroadcasting */
	TSharedPtr<ILiveLinkProvider> RebroadcastLiveLinkProvider;

	/** Lock to protect access on SubjectFrameToPush and SubjectStaticToPush. */
	mutable FCriticalSection PendingFramesCriticalSection;

private:
	/** Pending static info to add to a subject. */
	TArray<FPendingSubjectStatic> SubjectStaticToPush;

	/** Pending frame info to add to a subject. */
	TArray<FPendingSubjectFrame> SubjectFrameToPush;

	/** Key funcs for looking up a set of cached keys by its layout element */
	TMap<FLiveLinkSubjectName, FLiveLinkSubjectKey> EnabledSubjects;

	struct FSubjectFramesAddedHandles
	{
		FOnLiveLinkSubjectStaticDataAdded OnStaticDataAdded;
		FOnLiveLinkSubjectFrameDataAdded OnFrameDataAdded;
		/** Original data that hasn't been remapped. */
		FOnLiveLinkSubjectStaticDataAdded OnUnmappedStaticDataAdded;
		/** Original data that hasn't been remapped. */
		FOnLiveLinkSubjectFrameDataAdded OnUnmappedFrameDataAdded;
	};

	/** Map of delegates to notify interested parties when the client receives a static or data frame for each subject */
	TMap<FLiveLinkSubjectName, FSubjectFramesAddedHandles> SubjectFrameAddedHandles;

	struct FSubjectFramesReceivedHandles
	{
		FOnLiveLinkSubjectStaticDataReceived OnStaticDataReceived;
		FOnLiveLinkSubjectFrameDataReceived OnFrameDataReceived;
	};

	/** Delegate when LiveLinkClient received a subject static or frame data. */
	TMap<FLiveLinkSubjectKey, FSubjectFramesReceivedHandles> SubjectFrameReceivedHandles;

	/** Lock to to access SubjectFrameReceivedHandles */
	mutable FCriticalSection SubjectFrameReceivedHandleseCriticalSection;

	/** Delegate when LiveLinkClient has ticked. */
	FSimpleMulticastDelegate OnLiveLinkTickedDelegate;

	FString RebroadcastLiveLinkProviderName;
	TSet<FLiveLinkSubjectKey> RebroadcastedSubjects;

	/** Name token  used to register to all subject updates. */
	const FName ALL_SUBJECTS_DELEGATE_TOKEN = "__Internal_AllSubjects_Update";

	/** Whether to Preprocess frames before rebroadcasting them. */
	bool bPreProcessRebroadcastFrames = false;

	/** Whether to translate frames before rebroadcasting them. */
	bool bTranslateRebroadcastFrames = false;

	/** Whether or not parent subject support is enabled. Parent subjects allow resampling data to a different subject's rate before rebroadcasting it. */
	bool bEnableParentSubjects = false;

#if WITH_EDITOR
	/** Delegate when a subject is evaluated. */
	FOnLiveLinkSubjectEvaluated OnLiveLinkSubjectEvaluatedDelegate;

	/** Cached value of the engine timecode and frame rate*/
	double CachedEngineTime;
	TOptional<FQualifiedFrameTime> CachedEngineFrameTime;
#endif
};
