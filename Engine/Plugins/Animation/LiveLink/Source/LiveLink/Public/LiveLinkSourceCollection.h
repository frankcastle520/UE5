// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Async/Async.h"
#include "Async/RecursiveMutex.h"
#include "ILiveLinkClient.h"
#include "LiveLinkSubject.h"
#include "LiveLinkSubjectSettings.h"
#include "LiveLinkVirtualSubject.h"
#include "UObject/GCObject.h"
#include "UObject/StrongObjectPtr.h"

class FLiveLinkSourceCollection;

class FLiveLinkSubject;
class ILiveLinkSource;
class ILiveLinkSubject;

class ULiveLinkSourceSettings;
class ULiveLinkSubjectSettings;
class ULiveLinkVirtualSubject;

struct LIVELINK_API FLiveLinkCollectionSourceItem
{
	FLiveLinkCollectionSourceItem() = default;
	FLiveLinkCollectionSourceItem(FLiveLinkCollectionSourceItem&&) = default;
	FLiveLinkCollectionSourceItem& operator=(FLiveLinkCollectionSourceItem&&) = default;

	FLiveLinkCollectionSourceItem(const FLiveLinkCollectionSourceItem&) = delete;
	FLiveLinkCollectionSourceItem& operator=(const FLiveLinkCollectionSourceItem&) = delete;

	FGuid Guid;
	TStrongObjectPtr<ULiveLinkSourceSettings> Setting;
	TSharedPtr<ILiveLinkSource> Source;
	TSharedPtr<FLiveLinkTimedDataInput> TimedData;
	bool bPendingKill = false;
	bool bIsVirtualSource = false;

public:
	bool IsVirtualSource() const;
};

struct LIVELINK_API FLiveLinkCollectionSubjectItem
{
	FLiveLinkCollectionSubjectItem(FLiveLinkSubjectKey InKey, TUniquePtr<FLiveLinkSubject> InLiveSubject, ULiveLinkSubjectSettings* InSettings, bool bInEnabled);
	FLiveLinkCollectionSubjectItem(FLiveLinkSubjectKey InKey, ULiveLinkVirtualSubject* InVirtualSubject, bool bInEnabled);

public:
	FLiveLinkSubjectKey Key;
	bool bEnabled;
	bool bPendingKill;

	// Todo: These methods should be revisited because they may not be safe to access when LiveLinkHub is ticked outside of  the game thread.
	// ie. Calling methods on a subject that is about to be removed will not keep the underlying livelink subject alive.
	ILiveLinkSubject* GetSubject() { return VirtualSubject ? static_cast<ILiveLinkSubject*>(VirtualSubject.Get()) : static_cast<ILiveLinkSubject*>(LiveSubject.Get()); }
	ULiveLinkVirtualSubject* GetVirtualSubject() { return VirtualSubject.Get(); }
	ILiveLinkSubject* GetSubject() const { return VirtualSubject ? VirtualSubject.Get() : static_cast<ILiveLinkSubject*>(LiveSubject.Get()); }
	ULiveLinkVirtualSubject* GetVirtualSubject() const { return VirtualSubject.Get(); }
	UObject* GetSettings() const { return VirtualSubject ? static_cast<UObject*>(VirtualSubject.Get()) : static_cast<UObject*>(Setting.Get()); }
	ULiveLinkSubjectSettings* GetLinkSettings() const { return Setting.Get(); }
	FLiveLinkSubject* GetLiveSubject() const { return LiveSubject.Get(); }


private:
	TStrongObjectPtr<ULiveLinkSubjectSettings> Setting;
	TUniquePtr<FLiveLinkSubject> LiveSubject;
	TStrongObjectPtr<ULiveLinkVirtualSubject> VirtualSubject;

public:
	FLiveLinkCollectionSubjectItem(const FLiveLinkCollectionSubjectItem&) = delete;
	FLiveLinkCollectionSubjectItem& operator=(const FLiveLinkCollectionSubjectItem&) = delete;
	FLiveLinkCollectionSubjectItem(FLiveLinkCollectionSubjectItem&&) = default;
	FLiveLinkCollectionSubjectItem& operator=(FLiveLinkCollectionSubjectItem&&) = default;

	friend FLiveLinkSourceCollection;
};


class LIVELINK_API FLiveLinkSourceCollection
{
public:
	// "source guid" for virtual subjects
	static const FGuid DefaultVirtualSubjectGuid;
	FLiveLinkSourceCollection();

	UE_NONCOPYABLE(FLiveLinkSourceCollection)

public:

	UE_DEPRECATED(5.5, "Use ForEachSource instead.")
	TArray<FLiveLinkCollectionSourceItem>& GetSources() { return Sources; }

	UE_DEPRECATED(5.5, "Use ForEachSource instead.")
	const TArray<FLiveLinkCollectionSourceItem>& GetSources() const { return Sources; }

	UE_DEPRECATED(5.5, "Use ForEachSubject instead.")
	const TArray<FLiveLinkCollectionSubjectItem>& GetSubjects() const { return Subjects; }

	void AddSource(FLiveLinkCollectionSourceItem Source);
	void RemoveSource(FGuid SourceGuid);
	void RemoveAllSources();
	FLiveLinkCollectionSourceItem* FindSource(TSharedPtr<ILiveLinkSource> Source);
	const FLiveLinkCollectionSourceItem* FindSource(TSharedPtr<ILiveLinkSource> Source) const;
	FLiveLinkCollectionSourceItem* FindSource(FGuid SourceGuid);
	const FLiveLinkCollectionSourceItem* FindSource(FGuid SourceGuid) const;
	FLiveLinkCollectionSourceItem* FindVirtualSource(FName VirtualSourceName);
	const FLiveLinkCollectionSourceItem* FindVirtualSource(FName VirtualSourceName) const;
	/** Get the number of sources in the collection. */
	int32 NumSources() const;

	void AddSubject(FLiveLinkCollectionSubjectItem Subject);
	void RemoveSubject(FLiveLinkSubjectKey SubjectKey);
	FLiveLinkCollectionSubjectItem* FindSubject(FLiveLinkSubjectKey SubjectKey);
	const FLiveLinkCollectionSubjectItem* FindSubject(FLiveLinkSubjectKey SubjectKey) const;
	const FLiveLinkCollectionSubjectItem* FindSubject(FLiveLinkSubjectName SubjectName) const;
	const FLiveLinkCollectionSubjectItem* FindEnabledSubject(FLiveLinkSubjectName SubjectName) const;
	/** Get the number of subjects in the collection. */
	int32 NumSubjects() const;

	bool IsSubjectEnabled(FLiveLinkSubjectKey SubjectKey) const;
	void SetSubjectEnabled(FLiveLinkSubjectKey SubjectKey, bool bEnabled);

	void RemovePendingKill();
	bool RequestShutdown();

	/**
	 * Thread safe way to apply a method over every subject.
	 */
	void ForEachSubject(TFunctionRef<void(FLiveLinkCollectionSourceItem& /*SourceItem*/, FLiveLinkCollectionSubjectItem& /*SubjectItem*/)> VisitorFunc);

	/**
	 * Thread safe way to apply a method over every subject.
	 */
	void ForEachSubject(TFunctionRef<void(const FLiveLinkCollectionSourceItem&, const FLiveLinkCollectionSubjectItem&)> VisitorFunc) const;

	/**
	 *  Thread safe way to apply a method over every source.
	 */
	void ForEachSource(TFunctionRef<void(FLiveLinkCollectionSourceItem& /*SourceItem*/)> VisitorFunc);

	/**
	 * Thread safe way to apply a method over every source.
	 */
	void ForEachSource(TFunctionRef<void(const FLiveLinkCollectionSourceItem&)> VisitorFunc) const;

	FSimpleMulticastDelegate& OnLiveLinkSourcesChanged() { return OnLiveLinkSourcesChangedDelegate; }
	FSimpleMulticastDelegate& OnLiveLinkSubjectsChanged() { return OnLiveLinkSubjectsChangedDelegate; }

	FOnLiveLinkSourceChangedDelegate& OnLiveLinkSourceAdded() { return OnLiveLinkSourceAddedDelegate; }
	FOnLiveLinkSourceChangedDelegate& OnLiveLinkSourceRemoved() { return OnLiveLinkSourceRemovedDelegate; }
	FOnLiveLinkSubjectChangedDelegate& OnLiveLinkSubjectAdded() { return OnLiveLinkSubjectAddedDelegate; }
	FOnLiveLinkSubjectChangedDelegate& OnLiveLinkSubjectRemoved() { return OnLiveLinkSubjectRemovedDelegate; }

	/** Utility method used to broadcast delegates on the game thread if this function is called on a different thread. */
	template <typename DelegateType, typename... ArgTypes>
	void BroadcastOnGameThread(DelegateType& InDelegate, ArgTypes&&... InArgs)
	{
		if (IsInGameThread())
		{
			InDelegate.Broadcast(Forward<ArgTypes>(InArgs)...);
		}
		else
		{
			AsyncTask(ENamedThreads::GameThread, [&InDelegate, ...Args = InArgs] () mutable
			{
				InDelegate.Broadcast(MoveTemp(Args)...);
			});
		}
	}

private:
	TArray<FLiveLinkCollectionSourceItem> Sources;

	TArray<FLiveLinkCollectionSubjectItem> Subjects;

	/** Notify when the client sources list has changed */
	FSimpleMulticastDelegate OnLiveLinkSourcesChangedDelegate;

	/** Notify when a client subjects list has changed */
	FSimpleMulticastDelegate OnLiveLinkSubjectsChangedDelegate;

	/** Notify when a client source's is added */
	FOnLiveLinkSourceChangedDelegate OnLiveLinkSourceAddedDelegate;

	/** Notify when a client source's is removed */
	FOnLiveLinkSourceChangedDelegate OnLiveLinkSourceRemovedDelegate;

	/** Notify when a client subject's is added */
	FOnLiveLinkSubjectChangedDelegate OnLiveLinkSubjectAddedDelegate;

	/** Notify when a client subject's is removed */
	FOnLiveLinkSubjectChangedDelegate OnLiveLinkSubjectRemovedDelegate;

	/** Lock to stop multiple threads accessing the Subjects from the collection at the same time */
	mutable UE::FRecursiveMutex SubjectsLock;

	/** Lock to stop multiple threads accessing the Sources from the collection at the same time */
	mutable UE::FRecursiveMutex SourcesLock;
};
