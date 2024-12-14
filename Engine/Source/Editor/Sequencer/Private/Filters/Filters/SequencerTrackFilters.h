// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Camera/CameraComponent.h"
#include "Components/LightComponentBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Filters/SequencerTrackFilterBase.h"
#include "Filters/SequencerTrackFilterCommands.h"
#include "Misc/IFilter.h"
#include "MovieSceneFolder.h"
#include "MVVM/ViewModels/FolderModel.h"
#include "Particles/ParticleSystem.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateIconFinder.h"
#include "Tracks/MovieSceneAudioTrack.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "Tracks/MovieSceneCinematicShotTrack.h"
#include "Tracks/MovieSceneDataLayerTrack.h"
#include "Tracks/MovieSceneEventTrack.h"
#include "Tracks/MovieSceneFadeTrack.h"
#include "Tracks/MovieSceneLevelVisibilityTrack.h"
#include "Tracks/MovieSceneParticleTrack.h"
#include "Tracks/MovieSceneSlomoTrack.h"
#include "Tracks/MovieSceneSubTrack.h"

#define LOCTEXT_NAMESPACE "SequencerTrackFilters"

class FSequencerTrackFilter_Audio : public FSequencerTrackFilter_ClassType<UMovieSceneAudioTrack>
{
public:
	FSequencerTrackFilter_Audio(ISequencerTrackFilters& InFilterInterface, TSharedPtr<FFilterCategory> InCategory = nullptr)
		: FSequencerTrackFilter_ClassType<UMovieSceneAudioTrack>(InFilterInterface, InCategory)
	{}

	//~ Begin IFilter
	virtual FString GetName() const override { return TEXT("Audio"); }
	//~ End IFilter

	//~ Begin FFilterBase
	virtual FText GetDisplayName() const override { return LOCTEXT("SequencerTrackFilter_Audio", "Audio"); }
	virtual FSlateIcon GetIcon() const override { return FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Sequencer.Tracks.Audio")); }
	//~ End FFilterBase

	//~ Begin FSequencerTrackFilter

	virtual FText GetDefaultToolTipText() const override
	{
		return LOCTEXT("SequencerTrackFilter_AudioToolTip", "Show only Audio tracks");
	}

	virtual TSharedPtr<FUICommandInfo> GetToggleCommand() const override
	{
		return FSequencerTrackFilterCommands::Get().ToggleFilter_Audio;
	}

	virtual bool SupportsSequence(UMovieSceneSequence* const InSequence) const override
	{
		return IsSequenceTrackSupported<UMovieSceneAudioTrack>(InSequence)
			|| SupportsLevelSequence(InSequence)
			|| SupportsUMGSequence(InSequence);
	}

	//~ End FSequencerTrackFilter
};

//////////////////////////////////////////////////////////////////////////
//

class FSequencerTrackFilter_Event : public FSequencerTrackFilter_ClassType<UMovieSceneEventTrack>
{
public:
	FSequencerTrackFilter_Event(ISequencerTrackFilters& InFilterInterface, TSharedPtr<FFilterCategory> InCategory = nullptr)
		: FSequencerTrackFilter_ClassType<UMovieSceneEventTrack>(InFilterInterface, InCategory)
	{}

	//~ Begin IFilter
	virtual FString GetName() const override { return TEXT("Event"); }
	//~ End IFilter

	//~ Begin FFilterBase
	virtual FText GetDisplayName() const override { return LOCTEXT("SequencerTrackFilter_Event", "Event"); }
	virtual FSlateIcon GetIcon() const override { return FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Sequencer.Tracks.Event")); }
	//~ End FFilterBase

	//~ Begin FSequencerTrackFilter

	virtual FText GetDefaultToolTipText() const override
	{
		return LOCTEXT("SequencerTrackFilter_EventToolTip", "Show only Event tracks");
	}

	virtual TSharedPtr<FUICommandInfo> GetToggleCommand() const override
	{
		return FSequencerTrackFilterCommands::Get().ToggleFilter_Event;
	}

	virtual bool SupportsSequence(UMovieSceneSequence* const InSequence) const override
	{
		return FSequencerTrackFilter::SupportsSequence(InSequence) || SupportsUMGSequence(InSequence);
	}

	//~ End FSequencerTrackFilter
};

//////////////////////////////////////////////////////////////////////////
//

class FSequencerTrackFilter_LevelVisibility : public FSequencerTrackFilter_ClassType<UMovieSceneLevelVisibilityTrack>
{
public:
	FSequencerTrackFilter_LevelVisibility(ISequencerTrackFilters& InFilterInterface, TSharedPtr<FFilterCategory> InCategory = nullptr)
		: FSequencerTrackFilter_ClassType<UMovieSceneLevelVisibilityTrack>(InFilterInterface, InCategory)
	{}

	//~ Begin IFilter
	virtual FString GetName() const override { return TEXT("LevelVisibility"); }
	//~ End IFilter

	//~ Begin FFilterBase
	virtual FText GetDisplayName() const override { return LOCTEXT("SequencerTrackFilter_LevelVisibility", "Level Visibility"); }
	virtual FSlateIcon GetIcon() const override { return FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Sequencer.Tracks.LevelVisibility")); }
	//~ End FFilterBase

	//~ Begin FSequencerTrackFilter

	virtual FText GetDefaultToolTipText() const override
	{
		return LOCTEXT("SequencerTrackFilter_LevelVisibilityToolTip", "Show only Level Visibility tracks");
	}

	virtual TSharedPtr<FUICommandInfo> GetToggleCommand() const override
	{
		return FSequencerTrackFilterCommands::Get().ToggleFilter_LevelVisibility;
	}

	//~ End FSequencerTrackFilter
};

//////////////////////////////////////////////////////////////////////////
//

class FSequencerTrackFilter_Particle : public FSequencerTrackFilter_ClassType<UMovieSceneParticleTrack>
{
public:
	FSequencerTrackFilter_Particle(ISequencerTrackFilters& InFilterInterface, TSharedPtr<FFilterCategory> InCategory = nullptr)
		: FSequencerTrackFilter_ClassType<UMovieSceneParticleTrack>(InFilterInterface, InCategory)
	{}

	//~ Begin IFilter
	virtual FString GetName() const override { return TEXT("ParticleSystem"); }
	//~ End IFilter

	//~ Begin FFilterBase
	virtual FText GetDisplayName() const override { return LOCTEXT("SequencerTrackFilter_Particle", "Particle System"); }
	virtual FSlateIcon GetIcon() const override { return FSlateIconFinder::FindIconForClass(UParticleSystem::StaticClass()); }
	//~ End FFilterBase

	//~ Begin FSequencerTrackFilter

	virtual FText GetDefaultToolTipText() const override
	{
		return LOCTEXT("SequencerTrackFilter_ParticleToolTip", "Show only Particle System tracks");
	}

	virtual TSharedPtr<FUICommandInfo> GetToggleCommand() const override
	{
		return FSequencerTrackFilterCommands::Get().ToggleFilter_Particle;
	}

	//~ End FSequencerTrackFilter
};

//////////////////////////////////////////////////////////////////////////
//

class FSequencerTrackFilter_CinematicShot : public FSequencerTrackFilter_ClassType<UMovieSceneCinematicShotTrack>
{
public:
	FSequencerTrackFilter_CinematicShot(ISequencerTrackFilters& InFilterInterface, TSharedPtr<FFilterCategory> InCategory = nullptr)
		: FSequencerTrackFilter_ClassType<UMovieSceneCinematicShotTrack>(InFilterInterface, InCategory)
	{}

	//~ Begin IFilter
	virtual FString GetName() const override { return TEXT("CinematicShot"); }
	//~ End IFilter

	//~ Begin FFilterBase
	virtual FText GetDisplayName() const override { return LOCTEXT("SequencerTrackFilter_CinematicShot", "Shot"); }
	virtual FSlateIcon GetIcon() const override { return FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Sequencer.Tracks.CinematicShot")); }
	//~ End FFilterBase

	//~ Begin FSequencerTrackFilter

	virtual FText GetDefaultToolTipText() const override
	{
		return LOCTEXT("SequencerTrackFilter_CinematicShotToolTip", "Show only Shot tracks");
	}

	virtual TSharedPtr<FUICommandInfo> GetToggleCommand() const override
	{
		return FSequencerTrackFilterCommands::Get().ToggleFilter_CinematicShot;
	}

	//~ End FSequencerTrackFilter
};

//////////////////////////////////////////////////////////////////////////
//

class FSequencerTrackFilter_Subsequence : public FSequencerTrackFilter_ClassType<UMovieSceneSubTrack>
{
public:
	FSequencerTrackFilter_Subsequence(ISequencerTrackFilters& InFilterInterface, TSharedPtr<FFilterCategory> InCategory = nullptr)
		: FSequencerTrackFilter_ClassType<UMovieSceneSubTrack>(InFilterInterface, InCategory)
	{}

	//~ Begin IFilter

	virtual FString GetName() const override { return TEXT("SubSequence"); }

	virtual bool PassesFilter(FSequencerTrackFilterType InItem) const override
	{
		FSequencerFilterData& FilterData = FilterInterface.GetFilterData();
		const UMovieSceneTrack* const TrackObject = FilterData.ResolveMovieSceneTrackObject(InItem);
		return IsValid(TrackObject)
			&& TrackObject->IsA(UMovieSceneSubTrack::StaticClass())
			&& !TrackObject->IsA(UMovieSceneCinematicShotTrack::StaticClass());
	}

	//~ End IFilter

	//~ Begin FFilterBase
	virtual FText GetDisplayName() const override { return LOCTEXT("SequencerTrackFilter_Subsequence", "Subsequence"); }
	virtual FSlateIcon GetIcon() const override { return FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Sequencer.Tracks.Sub")); }
	//~ End FFilterBase

	//~ Begin FSequencerTrackFilter

	virtual FText GetDefaultToolTipText() const override
	{
		return LOCTEXT("SequencerTrackFilter_SubsequenceToolTip", "Show only Subsequence tracks");
	}

	virtual TSharedPtr<FUICommandInfo> GetToggleCommand() const override
	{
		return FSequencerTrackFilterCommands::Get().ToggleFilter_Subsequence;
	}

	//~ End FSequencerTrackFilter
};

//////////////////////////////////////////////////////////////////////////
//

class FSequencerTrackFilter_SkeletalMesh : public FSequencerTrackFilter_ComponentType<USkeletalMeshComponent>
{
public:
	FSequencerTrackFilter_SkeletalMesh(ISequencerTrackFilters& InFilterInterface, TSharedPtr<FFilterCategory> InCategory = nullptr)
		: FSequencerTrackFilter_ComponentType<USkeletalMeshComponent>(InFilterInterface, InCategory)
	{}

	//~ Begin IFilter
	virtual FString GetName() const override { return TEXT("SkeletalMesh"); }
	//~ End IFilter

	//~ Begin FFilterBase
	virtual FText GetDisplayName() const override { return LOCTEXT("SequencerTrackFilter_SkeletalMesh", "Skeletal Mesh"); }
	virtual FSlateIcon GetIcon() const override { return FSlateIconFinder::FindIconForClass(USkeletalMeshComponent::StaticClass()); }
	//~ End FFilterBase

	//~ Begin FSequencerTrackFilter

	virtual FText GetDefaultToolTipText() const override
	{
		return LOCTEXT("SequencerTrackFilter_SkeletalMeshToolTip", "Show only Skeletal Mesh tracks");
	}

	virtual TSharedPtr<FUICommandInfo> GetToggleCommand() const override
	{
		return FSequencerTrackFilterCommands::Get().ToggleFilter_SkeletalMesh;
	}

	//~ End FSequencerTrackFilter
};

//////////////////////////////////////////////////////////////////////////
//

class FSequencerTrackFilter_Camera : public FSequencerTrackFilter_ComponentType<UCameraComponent>
{
public:
	FSequencerTrackFilter_Camera(ISequencerTrackFilters& InFilterInterface, TSharedPtr<FFilterCategory> InCategory = nullptr)
		: FSequencerTrackFilter_ComponentType<UCameraComponent>(InFilterInterface, InCategory)
	{}

	//~ Begin IFilter
	virtual FString GetName() const override { return TEXT("Camera"); }
	//~ End IFilter

	//~ Begin FFilterBase
	virtual FText GetDisplayName() const override { return LOCTEXT("SequencerTrackFilter_Camera", "Camera"); }
	virtual FSlateIcon GetIcon() const override { return FSlateIconFinder::FindIconForClass(UCameraComponent::StaticClass()); }
	//~ End FFilterBase

	//~ Begin FSequencerTrackFilter

	virtual FText GetDefaultToolTipText() const override
	{
		return LOCTEXT("SequencerTrackFilter_CameraToolTip", "Show only Camera tracks");
	}

	virtual TSharedPtr<FUICommandInfo> GetToggleCommand() const override
	{
		return FSequencerTrackFilterCommands::Get().ToggleFilter_Camera;
	}

	//~ End FSequencerTrackFilter
};

//////////////////////////////////////////////////////////////////////////
//

class FSequencerTrackFilter_Light : public FSequencerTrackFilter_ComponentType<ULightComponentBase>
{
public:
	FSequencerTrackFilter_Light(ISequencerTrackFilters& InFilterInterface, TSharedPtr<FFilterCategory> InCategory = nullptr)
		: FSequencerTrackFilter_ComponentType<ULightComponentBase>(InFilterInterface, InCategory)
	{}

	//~ Begin IFilter
	virtual FString GetName() const override { return TEXT("Light"); }
	//~ End IFilter

	//~ Begin FFilterBase
	virtual FText GetDisplayName() const override { return LOCTEXT("SequencerTrackFilter_Light", "Light"); }
	virtual FSlateIcon GetIcon() const override { return FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("ClassIcon.Light")); }
	//~ End FFilterBase

	//~ Begin FSequencerTrackFilter

	virtual FText GetDefaultToolTipText() const override
	{
		return LOCTEXT("SequencerTrackFilter_LightToolTip", "Show only Light tracks");
	}

	virtual TSharedPtr<FUICommandInfo> GetToggleCommand() const override
	{
		return FSequencerTrackFilterCommands::Get().ToggleFilter_Light;
	}

	//~ End FSequencerTrackFilter
};

//////////////////////////////////////////////////////////////////////////
//

class FSequencerTrackFilter_CameraCut : public FSequencerTrackFilter_ClassType<UMovieSceneCameraCutTrack>
{
public:
	FSequencerTrackFilter_CameraCut(ISequencerTrackFilters& InFilterInterface, TSharedPtr<FFilterCategory> InCategory = nullptr)
		: FSequencerTrackFilter_ClassType<UMovieSceneCameraCutTrack>(InFilterInterface, InCategory)
	{}

	//~ Begin IFilter
	virtual FString GetName() const override { return TEXT("CameraCut"); }
	//~ End IFilter

	//~ Begin FFilterBase
	virtual FText GetDisplayName() const override { return LOCTEXT("SequencerTrackFilter_CameraCut", "Camera Cut"); }
	virtual FSlateIcon GetIcon() const override { return FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Sequencer.Tracks.CameraCut")); }
	//~ End FFilterBase

	//~ Begin FSequencerTrackFilter

	virtual FText GetDefaultToolTipText() const override
	{
		return LOCTEXT("SequencerTrackFilter_CameraCutToolTip", "Show only Camera Cut tracks");
	}

	virtual TSharedPtr<FUICommandInfo> GetToggleCommand() const override
	{
		return FSequencerTrackFilterCommands::Get().ToggleFilter_CameraCut;
	}

	//~ End FSequencerTrackFilter
};

//////////////////////////////////////////////////////////////////////////
//

class FSequencerTrackFilter_Fade : public FSequencerTrackFilter_ClassType<UMovieSceneFadeTrack>
{
public:
	FSequencerTrackFilter_Fade(ISequencerTrackFilters& InFilterInterface, TSharedPtr<FFilterCategory> InCategory = nullptr)
		: FSequencerTrackFilter_ClassType<UMovieSceneFadeTrack>(InFilterInterface, InCategory)
	{}

	//~ Begin IFilter
	virtual FString GetName() const override { return TEXT("Fade"); }
	//~ End IFilter

	//~ Begin FFilterBase
	virtual FText GetDisplayName() const override { return LOCTEXT("SequencerTrackFilter_Fade", "Fade"); }
	virtual FSlateIcon GetIcon() const override { return FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Sequencer.Tracks.Fade")); }
	//~ End FFilterBase

	//~ Begin FSequencerTrackFilter

	virtual FText GetDefaultToolTipText() const override
	{
		return LOCTEXT("SequencerTrackFilter_FadeToolTip", "Show only Fade tracks");
	}

	virtual TSharedPtr<FUICommandInfo> GetToggleCommand() const override
	{
		return FSequencerTrackFilterCommands::Get().ToggleFilter_Fade;
	}

	//~ End FSequencerTrackFilter
};

//////////////////////////////////////////////////////////////////////////
//

class FSequencerTrackFilter_DataLayer : public FSequencerTrackFilter_ClassType<UMovieSceneDataLayerTrack>
{
public:
	FSequencerTrackFilter_DataLayer(ISequencerTrackFilters& InFilterInterface, TSharedPtr<FFilterCategory> InCategory = nullptr)
		: FSequencerTrackFilter_ClassType<UMovieSceneDataLayerTrack>(InFilterInterface, InCategory)
	{}

	//~ Begin IFilter
	virtual FString GetName() const override { return TEXT("DataLayer"); }
	//~ End IFilter

	//~ Begin FFilterBase
	virtual FText GetDisplayName() const override { return LOCTEXT("SequencerTrackFilter_DataLayer", "Data Layer"); }
	virtual FSlateIcon GetIcon() const override { return FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Sequencer.Tracks.DataLayer")); }
	//~ End FFilterBase

	//~ Begin FSequencerTrackFilter

	virtual FText GetDefaultToolTipText() const override
	{
		return LOCTEXT("SequencerTrackFilter_DataLayerToolTip", "Show only Data Layer tracks");
	}

	virtual TSharedPtr<FUICommandInfo> GetToggleCommand() const override
	{
		return FSequencerTrackFilterCommands::Get().ToggleFilter_DataLayer;
	}

	//~ End FSequencerTrackFilter
};

//////////////////////////////////////////////////////////////////////////
//

class FSequencerTrackFilter_TimeDilation : public FSequencerTrackFilter_ClassType<UMovieSceneSlomoTrack>
{
public:
	FSequencerTrackFilter_TimeDilation(ISequencerTrackFilters& InFilterInterface, TSharedPtr<FFilterCategory> InCategory = nullptr)
		: FSequencerTrackFilter_ClassType<UMovieSceneSlomoTrack>(InFilterInterface, InCategory)
	{}

	//~ Begin IFilter
	virtual FString GetName() const override { return TEXT("TimeDilation"); }
	//~ End IFilter

	//~ Begin FFilterBase
	virtual FText GetDisplayName() const override { return LOCTEXT("SequencerTrackFilter_TimeDilation", "Time Dilation"); }
	virtual FSlateIcon GetIcon() const override { return FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Sequencer.Tracks.Slomo")); }
	//~ End FFilterBase

	//~ Begin FSequencerTrackFilter

	virtual FText GetDefaultToolTipText() const override
	{
		return LOCTEXT("SequencerTrackFilter_TimeDilationToolTip", "Show only Time Dilation tracks");
	}

	virtual TSharedPtr<FUICommandInfo> GetToggleCommand() const override
	{
		return FSequencerTrackFilterCommands::Get().ToggleFilter_TimeDilation;
	}

	//~ End FSequencerTrackFilter
};

//////////////////////////////////////////////////////////////////////////
//

class FSequencerTrackFilter_Folder : public FSequencerTrackFilter_ModelType<UE::Sequencer::FFolderModel>
{
public:
	FSequencerTrackFilter_Folder(ISequencerTrackFilters& InFilterInterface, TSharedPtr<FFilterCategory> InCategory = nullptr)
		: FSequencerTrackFilter_ModelType<UE::Sequencer::FFolderModel>(InFilterInterface, InCategory)
	{}

	//~ Begin IFilter
	virtual FString GetName() const override { return TEXT("Folder"); }
	//~ End IFilter

	//~ Begin FFilterBase
	virtual FText GetDisplayName() const override { return LOCTEXT("SequencerTrackFilter_Folder", "Folder"); }
	virtual FSlateIcon GetIcon() const override { return FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("ContentBrowser.AssetTreeFolderClosed")); }
	//~ End FFilterBase

	//~ Begin FSequencerTrackFilter

	virtual FText GetDefaultToolTipText() const override
	{
		return LOCTEXT("SequencerTrackFilter_FolderToolTip", "Show only Folder tracks");
	}

	virtual TSharedPtr<FUICommandInfo> GetToggleCommand() const override
	{
		return FSequencerTrackFilterCommands::Get().ToggleFilter_Folder;
	}

	TSubclassOf<UMovieSceneTrack> GetTrackClass() const override
	{
		return UMovieSceneFolder::StaticClass();
	}

	virtual bool SupportsSequence(UMovieSceneSequence* const InSequence) const override
	{
		return true;
	}

	//~ End FSequencerTrackFilter
};

#undef LOCTEXT_NAMESPACE
