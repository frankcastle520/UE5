// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EdMode.h"
#include "ISequencer.h"

class FSubTrackEditorMode : public FEdMode
{
public:
	static FName ModeName;
	
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnOriginValueChanged, FVector, FRotator)
	
	FSubTrackEditorMode();
	virtual ~FSubTrackEditorMode() override;

	// FEdMode interface
	virtual void Initialize() override;
	virtual bool InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale) override;
	virtual bool UsesTransformWidget() const override;
	virtual bool UsesTransformWidget(UE::Widget::EWidgetMode CheckMode) const override;
	virtual FVector GetWidgetLocation() const override;
	virtual bool ShouldDrawWidget() const override;
	virtual bool GetPivotForOrbit(FVector& OutPivot) const override;
	virtual bool GetCustomDrawingCoordinateSystem(FMatrix& OutMatrix, void* InData) override;
	virtual bool GetCustomInputCoordinateSystem(FMatrix& OutMatrix, void* InData) override;
	virtual bool IsCompatibleWith(FEditorModeID OtherModeID) const override;

	void SetSequencer(const TSharedPtr<ISequencer>& InSequencer) { WeakSequencer = InSequencer; }
	FOnOriginValueChanged& GetOnOriginValueChanged() { return OnOriginValueChanged; };

private:
	// Returns true if there are any sub tracks with active transform origin overrides.
	static bool DoesSubSectionHaveTransformOverrides(const UMovieSceneSubSection& SubSection);

	// Gets the sequence ID from the context of the subsection in the current hierarchy.
	TOptional<FMovieSceneSequenceID> GetSequenceIDForSubSection(const UMovieSceneSubSection* InSubSection) const;
	// Gets the sequence ID of the currently focused sequence.
	TOptional<FMovieSceneSequenceID> GetFocusedSequenceID() const;
	// Gets the transform origin of the provided section, after all parent transforms have been applied.
	FTransform GetFinalTransformOriginForSubSection(const UMovieSceneSubSection* SubSection) const;
	// Gets the transform origin corresponding to the sequence in the current hierarchy matching the provided sequence ID.
	FTransform GetTransformOriginForSequence(TOptional<FMovieSceneSequenceID> InSequenceID) const;
	// returns true if any actors are selected in the level editor. This is used to prevent this editor mode from being active when actors are selected.
	bool AreAnyActorsSelected() const;

	// Returns the currently selected 
	UMovieSceneSubSection* GetSelectedSection() const;

	/** Sequencer that owns this editor mode */
	TWeakPtr<ISequencer> WeakSequencer;

	/** Delegate called when the origin is modified from the editor gizmo */
	FOnOriginValueChanged OnOriginValueChanged;

	/** Used to tell if the gizmo as moved, and if the editor hit proxies need to be invalidated as a result */
	mutable TOptional<FVector> CachedLocation;

	const TArray<FName> IncompatibleEditorModes = TArray<FName>({ "EditMode.ControlRig", "EM_Landscape" });
};
