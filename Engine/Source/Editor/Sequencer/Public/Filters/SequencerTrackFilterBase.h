// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "Filters/FilterBase.h"
#include "Filters/ISequencerTrackFilters.h"
#include "Misc/IFilter.h"
#include "MovieSceneTrack.h"
#include "MovieSceneTrackEditor.h"
#include "SequencerFilterData.h"
#include "MVVM/ViewModelPtr.h"
#include "Textures/SlateIcon.h"

class ISequencer;
class FMenuBuilder;
class UMovieScene;
class UMovieSceneSequence;
struct FSequencerFilterData;

namespace UE::Sequencer
{
	class IOutlinerExtension;

	namespace ExtensionHooks
	{
		static FName Hierarchy(TEXT("Hierarchy"));
		static FName Show(TEXT("Show"));
	}
}

using FSequencerTrackFilterType = UE::Sequencer::FViewModelPtr;

//////////////////////////////////////////////////////////////////////////
//

class FSequencerTrackFilter : public FFilterBase<FSequencerTrackFilterType>
	, public TSharedFromThis<FSequencerTrackFilter>
{
public:
	SEQUENCER_API static bool SupportsLevelSequence(UMovieSceneSequence* const InSequence);
	SEQUENCER_API static bool SupportsUMGSequence(UMovieSceneSequence* const InSequence);

	template<typename InTrackClassType>
	static bool IsSequenceTrackSupported(UMovieSceneSequence* const InSequence)
	{
		const ETrackSupport Support = IsValid(InSequence)
			? InSequence->IsTrackSupported(InTrackClassType::StaticClass())
			: ETrackSupport::NotSupported;
		return Support == ETrackSupport::Supported;
	}

	SEQUENCER_API static FText BuildTooltipTextForCommand(const FText& InBaseText, const TSharedPtr<FUICommandInfo>& InCommand);

	SEQUENCER_API FSequencerTrackFilter(ISequencerTrackFilters& InOutFilterInterface, TSharedPtr<FFilterCategory>&& InCategory = nullptr);

	//~ Begin IFilter
	SEQUENCER_API virtual bool PassesFilter(FSequencerTrackFilterType InItem) const override { return true; };
	//~ End IFilter

	//~ Begin FFilterBase

	virtual FText GetDisplayName() const override { return FText::GetEmpty(); }
	SEQUENCER_API virtual FText GetToolTipText() const override;
	virtual FLinearColor GetColor() const override { return FLinearColor::White; }
	virtual bool IsInverseFilter() const override { return false; }

	virtual void ActiveStateChanged(bool bActive) override {}

	virtual void ModifyContextMenu(FMenuBuilder& MenuBuilder) override {}

	virtual void SaveSettings(const FString& IniFilename, const FString& IniSection, const FString& SettingsString) const override {}
	virtual void LoadSettings(const FString& IniFilename, const FString& IniSection, const FString& SettingsString) override {}

	//~ End FFilterBase

	//~ Begin FSequencerTrackFilter

	SEQUENCER_API virtual FText GetDefaultToolTipText() const { return FText(); }
	SEQUENCER_API virtual FSlateIcon GetIcon() const { return FSlateIcon(); }

	SEQUENCER_API virtual bool IsCustomTextFilter() const { return false; }

	/** Returns whether this filter needs reevaluating any time track values have been modified, not just tree changes */
	SEQUENCER_API virtual bool ShouldUpdateOnTrackValueChanged() const { return false; }

	/** Returns whether the filter supports the sequence type */
	SEQUENCER_API virtual bool SupportsSequence(UMovieSceneSequence* const InSequence) const;

	SEQUENCER_API virtual void BindCommands();

	SEQUENCER_API virtual TSharedPtr<FUICommandInfo> GetToggleCommand() const { return nullptr; }

	virtual TSubclassOf<UMovieSceneTrack> GetTrackClass() const { return nullptr; };

	//~ End FSequencerTrackFilter

	SEQUENCER_API ISequencerTrackFilters& GetFilterInterface() const;
	SEQUENCER_API ISequencer& GetSequencer() const;

	bool CanToggleFilter() const;
	void ToggleFilter();

	SEQUENCER_API void MapToggleAction(const TSharedPtr<FUICommandInfo>& InCommand);

protected:
	UMovieSceneSequence* GetFocusedMovieSceneSequence() const;
	UMovieScene* GetFocusedGetMovieScene() const;

	ISequencerTrackFilters& FilterInterface;

private:
	//~ Begin IFilter
	/** Hide behind private to force use of GetIcon() instead */
	virtual FName GetIconName() const override { return FName(); }
	//~ End IFilter
};

//////////////////////////////////////////////////////////////////////////
//

/** Base filter for filtering Sequencer tracks based track model */
template<typename InModelType>
class FSequencerTrackFilter_ModelType : public FSequencerTrackFilter
{
public:
	FSequencerTrackFilter_ModelType(ISequencerTrackFilters& InOutFilterInterface, TSharedPtr<FFilterCategory> InCategory)
		: FSequencerTrackFilter(InOutFilterInterface, MoveTemp(InCategory))
	{}

	//~ Begin IFilter
	virtual bool PassesFilter(FSequencerTrackFilterType InItem) const override
	{
		const UE::Sequencer::TViewModelPtr<InModelType> Model = InItem->FindAncestorOfType<InModelType>();
		return Model.IsValid(); // show child tracks
	}
	//~ End IFilter
};

//////////////////////////////////////////////////////////////////////////
//

/** Base filter for filtering Sequencer tracks based object class type */
template<typename InClassType>
class FSequencerTrackFilter_ClassType : public FSequencerTrackFilter
{
public:
	FSequencerTrackFilter_ClassType(ISequencerTrackFilters& InOutFilterInterface, TSharedPtr<FFilterCategory> InCategory)
		: FSequencerTrackFilter(InOutFilterInterface, MoveTemp(InCategory))
	{}

	//~ Begin IFilter
	virtual bool PassesFilter(FSequencerTrackFilterType InItem) const override
	{
		FSequencerFilterData& FilterData = FilterInterface.GetFilterData();
		const UMovieSceneTrack* const TrackObject = FilterData.ResolveMovieSceneTrackObject(InItem);
		return IsValid(TrackObject) && TrackObject->IsA(InClassType::StaticClass());
	}
	//~ End IFilter

	//~ Begin FSequencerTrackFilter
	TSubclassOf<UMovieSceneTrack> GetTrackClass() const override
	{
		return InClassType::StaticClass();
	}
	//~ End FSequencerTrackFilter
};

//////////////////////////////////////////////////////////////////////////
//

/** Base filter for filtering Sequencer tracks based object component type */
template<typename InComponentType>
class FSequencerTrackFilter_ComponentType : public FSequencerTrackFilter
{
public:
	FSequencerTrackFilter_ComponentType(ISequencerTrackFilters& InOutFilterInterface, TSharedPtr<FFilterCategory> InCategory)
		: FSequencerTrackFilter(InOutFilterInterface, MoveTemp(InCategory))
	{}

	//~ Begin IFilter
	virtual bool PassesFilter(FSequencerTrackFilterType InItem) const override
	{
		FSequencerFilterData& FilterData = FilterInterface.GetFilterData();

		const UMovieSceneTrack* const TrackObject = FilterData.ResolveMovieSceneTrackObject(InItem);
		if (IsValid(TrackObject) && TrackObject->IsA(InComponentType::StaticClass()))
		{
			return true;
		}

		const TWeakObjectPtr<> BoundObject = FilterData.ResolveTrackBoundObject(GetSequencer(), InItem);
		if (BoundObject.IsValid() && BoundObject->IsA(InComponentType::StaticClass()))
		{
			return true;
		}

		const AActor* const Actor = Cast<const AActor>(BoundObject.Get());
		if (IsValid(Actor) && Actor->FindComponentByClass(InComponentType::StaticClass()))
		{
			return true;
		}

		return false;
	}
	//~ End IFilter

	//~ Begin FSequencerTrackFilter
	TSubclassOf<UMovieSceneTrack> GetTrackClass() const override
	{
		return InComponentType::StaticClass();
	}
	//~ End FSequencerTrackFilter
};
