// Copyright Epic Games, Inc. All Rights Reserved.

#include "Sequencer/ControlRigSequencerFilter.h"
#include "ControlRig.h"
#include "ControlRigBlueprint.h"
#include "Filters/ISequencerTrackFilters.h"
#include "Filters/SequencerTrackFilterBase.h"
#include "Framework/Commands/Commands.h"
#include "Framework/Commands/UICommandInfo.h"
#include "MVVM/Extensions/IOutlinerExtension.h"
#include "MVVM/ViewModels/ChannelModel.h"
#include "Sequencer/MovieSceneControlRigParameterTrack.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateIconFinder.h"

using namespace UE::Sequencer;

#include UE_INLINE_GENERATED_CPP_BY_NAME(ControlRigSequencerFilter)

#define LOCTEXT_NAMESPACE "ControlRigSequencerTrackFilters"

//////////////////////////////////////////////////////////////////////////
//

class FSequencerTrackFilter_ControlRigControlsCommands
	: public TCommands<FSequencerTrackFilter_ControlRigControlsCommands>
{
public:
	FSequencerTrackFilter_ControlRigControlsCommands()
		: TCommands<FSequencerTrackFilter_ControlRigControlsCommands>(
			TEXT("FSequencerTrackFilter_ControlRigControls"),
			LOCTEXT("FSequencerTrackFilter_ControlRigControls", "Control Rig Filters"),
			NAME_None,
			FAppStyle::GetAppStyleSetName())
	{}

	/** Toggle the control rig controls filter */
	TSharedPtr< FUICommandInfo > ToggleFilter_ControlRigControls;

	/** Initialize commands */
	virtual void RegisterCommands() override
	{
		UI_COMMAND(ToggleFilter_ControlRigControls, "Control Rig Controls", "Toggle the filter for Control Rig Controls.", EUserInterfaceActionType::ToggleButton, FInputChord(EKeys::F9));
	}
};

//////////////////////////////////////////////////////////////////////////
//

class FSequencerTrackFilter_ControlRigControls : public FSequencerTrackFilter
{
public:
	FSequencerTrackFilter_ControlRigControls(ISequencerTrackFilters& InOutFilterInterface, TSharedPtr<FFilterCategory> InCategory = nullptr)
		: FSequencerTrackFilter(InOutFilterInterface, MoveTemp(InCategory))
		, BindingCount(0)
	{
		FSequencerTrackFilter_ControlRigControlsCommands::Register();
	}

	virtual ~FSequencerTrackFilter_ControlRigControls() override
	{
		BindingCount--;
		if (BindingCount < 1)
		{
			FSequencerTrackFilter_ControlRigControlsCommands::Unregister();
		}
	}

	//~ Begin IFilter

	virtual FString GetName() const override { return TEXT("ControlRigControl"); }

	virtual bool PassesFilter(FSequencerTrackFilterType InItem) const override
	{
		FSequencerFilterData& FilterData = FilterInterface.GetFilterData();
		const UMovieSceneTrack* const TrackObject = FilterData.ResolveMovieSceneTrackObject(InItem);
		const UMovieSceneControlRigParameterTrack* const Track = Cast<UMovieSceneControlRigParameterTrack>(TrackObject);
		return IsValid(Track);
	}

	//~ End IFilter

	//~ Begin FFilterBase
	virtual FText GetDisplayName() const override { return LOCTEXT("SequenceTrackFilter_ControlRigControl", "Control Rig Control"); }
	virtual FSlateIcon GetIcon() const override { return FSlateIconFinder::FindIconForClass(UControlRigBlueprint::StaticClass()); }
	//~ End FFilterBase

	//~ Begin FSequencerTrackFilter

	virtual FText GetDefaultToolTipText() const override
	{
		return LOCTEXT("SequencerTrackFilter_ControlRigControlsTip", "Show only Control Rig Control tracks");
	}

	virtual TSharedPtr<FUICommandInfo> GetToggleCommand() const override
	{
		return FSequencerTrackFilter_ControlRigControlsCommands::Get().ToggleFilter_ControlRigControls;
	}

	//~ End FSequencerTrackFilter

private:
	mutable uint32 BindingCount;
};

//////////////////////////////////////////////////////////////////////////
//

class FSequencerTrackFilter_ControlRigSelectedControlsCommands
	: public TCommands<FSequencerTrackFilter_ControlRigSelectedControlsCommands>
{
public:
	FSequencerTrackFilter_ControlRigSelectedControlsCommands()
		: TCommands<FSequencerTrackFilter_ControlRigSelectedControlsCommands>(
			TEXT("FSequencerTrackFilter_ControlRigSelectedControls"),
			LOCTEXT("FSequencerTrackFilter_ControlRigSelectedControls", "Control Rig Selected Control Filters"),
			NAME_None,
			FAppStyle::GetAppStyleSetName())
	{}

	/** Toggle the control rig selected controls filter */
	TSharedPtr<FUICommandInfo> ToggleFilter_ControlRigSelectedControls;

	/** Initialize commands */
	virtual void RegisterCommands() override
	{
		UI_COMMAND(ToggleFilter_ControlRigSelectedControls, "Control Rig Selected Controls", "Toggle the filter for Control Rig Selected Controls.", EUserInterfaceActionType::ToggleButton, FInputChord(EKeys::F10));
	}
};

//////////////////////////////////////////////////////////////////////////
//

class FSequencerTrackFilter_ControlRigSelectedControls : public FSequencerTrackFilter
{
public:
	FSequencerTrackFilter_ControlRigSelectedControls(ISequencerTrackFilters& InOutFilterInterface, TSharedPtr<FFilterCategory> InCategory = nullptr)
		:  FSequencerTrackFilter(InOutFilterInterface, MoveTemp(InCategory))
		, BindingCount(0)
	{
		FSequencerTrackFilter_ControlRigSelectedControlsCommands::Register();
	}

	virtual ~FSequencerTrackFilter_ControlRigSelectedControls() override
	{
		BindingCount--;
		if (BindingCount < 1)
		{
			FSequencerTrackFilter_ControlRigSelectedControlsCommands::Unregister();
		}
	}

	//~ Begin IFilter

	virtual FString GetName() const override { return TEXT("SelectedControlRigControl"); }

	virtual bool PassesFilter(FSequencerTrackFilterType InItem) const override
	{
		FSequencerFilterData& FilterData = FilterInterface.GetFilterData();

		UMovieSceneTrack* const TrackObject = FilterData.ResolveMovieSceneTrackObject(InItem);
		URigHierarchy* const ControlRigHierarchy = GetControlRigHierarchyFromTrackObject(TrackObject);

		if (!IsValid(ControlRigHierarchy))
		{
			return false;
		}

		const TViewModelPtr<IOutlinerExtension> OutlinerExtension = InItem.AsModel()->FindAncestorOfType<IOutlinerExtension>();
		if (!OutlinerExtension.IsValid())
		{
			return false;
		}

		const FString ControlTrackLabel = OutlinerExtension->GetLabel().ToString();

		const TArray<const FRigBaseElement*> BaseRigElements = ControlRigHierarchy->GetSelectedElements(ERigElementType::Control);
		for (const FRigBaseElement* const BaseRigElement : BaseRigElements)
		{
			const FString ElementDisplayName = ControlRigHierarchy->GetDisplayNameForUI(BaseRigElement).ToString();
			if (ElementDisplayName.Equals(ControlTrackLabel))
			{
				return true;
			}

			if (const FRigControlElement* const ControlElement = Cast<FRigControlElement>(BaseRigElement))
			{
				if (ControlElement->CanDriveControls())
				{
					const TArray<FRigElementKey>& DrivenControls = ControlElement->Settings.DrivenControls;
					for (const FRigElementKey& DrivenKey : DrivenControls)
					{
						if (const FRigBaseElement* const DrivenKeyElement = ControlRigHierarchy->Find(DrivenKey))
						{
							const FString DrivenKeyElementDisplayName = ControlRigHierarchy->GetDisplayNameForUI(DrivenKeyElement).ToString();
							if (DrivenKeyElementDisplayName.Equals(ControlTrackLabel))
							{
								return true;
							}
						}
					}
				}
			}
		}

		return false;
	}

	//~ End IFilter

	//~ Begin FFilterBase
	virtual FText GetDisplayName() const override { return LOCTEXT("SequenceTrackFilter_ControlRigSelectedControl", "Selected Control Rig Control"); }
	virtual FSlateIcon GetIcon() const override { return FSlateIconFinder::FindIconForClass(UControlRigBlueprint::StaticClass()); }
	//~ End FFilterBase

	//~ Begin FSequencerTrackFilter

	virtual FText GetDefaultToolTipText() const override
	{
		return LOCTEXT("SequencerTrackFilter_ControlRigSelectedControlsTip", "Show Only Selected Control Rig Controls.");
	}

	virtual TSharedPtr<FUICommandInfo> GetToggleCommand() const override
	{
		return FSequencerTrackFilter_ControlRigSelectedControlsCommands::Get().ToggleFilter_ControlRigSelectedControls;
	}

	//~ End FSequencerTrackFilter

	static URigHierarchy* GetControlRigHierarchyFromTrackObject(UMovieSceneTrack* const InTrackObject)
	{
		const UMovieSceneControlRigParameterTrack* const Track = Cast<UMovieSceneControlRigParameterTrack>(InTrackObject);
		if (!IsValid(Track))
		{
			return nullptr;
		}

		UControlRig* const ControlRig = Track->GetControlRig();
		if (!IsValid(ControlRig))
		{
			return nullptr;
		}

		return ControlRig->GetHierarchy();
	}

private:
	mutable uint32 BindingCount;
};

/*

	Bool,
	Float,
	Vector2D,
	Position,
	Scale,
	Rotator,
	Transform
*/

//////////////////////////////////////////////////////////////////////////
//

void UControlRigTrackFilter::AddTrackFilterExtensions(ISequencerTrackFilters& InOutFilterInterface
	, const TSharedRef<FFilterCategory>& InPreferredCategory
	, TArray<TSharedRef<FSequencerTrackFilter>>& InOutFilterList) const
{
	InOutFilterList.Add(MakeShared<FSequencerTrackFilter_ControlRigControls>(InOutFilterInterface, InPreferredCategory));
	InOutFilterList.Add(MakeShared<FSequencerTrackFilter_ControlRigSelectedControls>(InOutFilterInterface, InPreferredCategory));
}

#undef LOCTEXT_NAMESPACE
