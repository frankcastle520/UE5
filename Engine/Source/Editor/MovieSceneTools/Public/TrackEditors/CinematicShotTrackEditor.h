// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"
#include "Templates/SubclassOf.h"
#include "Widgets/SWidget.h"
#include "ISequencer.h"
#include "ISequencerSection.h"
#include "MovieSceneTrack.h"
#include "SubTrackEditor.h"
#include "ISequencerTrackEditor.h"

class AActor;
class FMenuBuilder;
class FTrackEditorThumbnailPool;
class UMovieSceneCinematicShotSection;
class UMovieSceneCinematicShotTrack;
class UMovieSceneSubSection;

/**
 * Tools for cinematic shots.
 */
class MOVIESCENETOOLS_API FCinematicShotTrackEditor : public FSubTrackEditor
{
public:

	/**
	 * Constructor
	 *
	 * @param InSequencer The sequencer instance to be used by this tool.
	 */
	FCinematicShotTrackEditor(TSharedRef<ISequencer> InSequencer);

	/** Virtual destructor. */
	virtual ~FCinematicShotTrackEditor() { }

	/**
	 * Creates an instance of this class.  Called by a sequencer .
	 *
	 * @param OwningSequencer The sequencer instance to be used by this tool.
	 * @return The new instance of this class.
	 */
	static TSharedRef<ISequencerTrackEditor> CreateTrackEditor(TSharedRef<ISequencer> OwningSequencer);

	UE_DEPRECATED(5.5, "Use FCameraCutPlaybackCapability::LastViewTargetCamera instead.")
	TWeakObjectPtr<AActor> GetCinematicShotCamera() const;

public:

	// ISequencerTrackEditor interface
	virtual TSharedPtr<SWidget> BuildOutlinerColumnWidget(const FBuildColumnWidgetParams& Params, const FName& ColumnName) override;
	virtual TSharedRef<ISequencerSection> MakeSectionInterface(UMovieSceneSection& SectionObject, UMovieSceneTrack& Track, FGuid ObjectBinding) override;
	virtual bool SupportsSequence(UMovieSceneSequence* InSequence) const override;
	virtual void Tick(float DeltaTime) override;
	virtual void BuildTrackContextMenu(FMenuBuilder& MenuBuilder, UMovieSceneTrack* Track) override;

	/*
	 * Render shots. 
	 *
	 * @param Sections The sections to render
	 */
	void RenderShots(const TArray<UMovieSceneCinematicShotSection*>& Sections);

public:

	// FSubTrackEditor interface
	virtual FText GetSubTrackName() const override;
	virtual FText GetSubTrackToolTip() const override;
	virtual FName GetSubTrackBrushName() const override;
	virtual FString GetSubSectionDisplayName(const UMovieSceneSubSection* Section) const override;
	virtual FString GetDefaultSubsequenceName() const override;
	virtual FString GetDefaultSubsequenceDirectory() const override;
	virtual TSubclassOf<UMovieSceneSubTrack> GetSubTrackClass() const;

protected:

	virtual bool HandleAddSubTrackMenuEntryCanExecute() const override;
	virtual bool CanHandleAssetAdded(UMovieSceneSequence* Sequence) const override;

private:

	/** Delegate for shots button lock state */
	ECheckBoxState AreShotsLocked() const;

	/** Delegate for locked shots button */
	void OnLockShotsClicked(ECheckBoxState CheckBoxState);
	
	/** Delegate for shots button lock tooltip */
	FText GetLockShotsToolTip() const;

	/** Callback for ImportEDL. */
	void ImportEDL();
	
	/** Callback for ExportEDL. */
	void ExportEDL();

	/** Callback for ImportFCPXML. */
	void ImportFCPXML();

	/** Callback for ExportFCPXML. */
	void ExportFCPXML();

private:

	/** The Thumbnail pool which draws all the viewport thumbnails for the shot track. */
	TSharedPtr<FTrackEditorThumbnailPool> ThumbnailPool;
};
