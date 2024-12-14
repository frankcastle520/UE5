// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "EditorSubsystem.h"

#include "MovieSceneTimeUnit.h"
#include "Containers/SortedMap.h"
#include "UObject/StructOnScope.h"
#include "UniversalObjectLocator.h"
#include "UniversalObjectLocatorResolveParams.h"
#include "Misc/NotifyHook.h"
#include "MovieSceneTrack.h"
#include "LevelSequenceEditorSubsystem.generated.h"

class FUICommandList;
class ISequencer;
class UMovieSceneTrack;
struct FMovieSceneBindingProxy;
struct FMovieScenePasteBindingsParams;
struct FMovieScenePasteFoldersParams;
struct FMovieScenePasteSectionsParams;
struct FMovieScenePasteTracksParams;
struct FBakingAnimationKeySettings;
struct FSequencerChangeBindingInfo;
struct FMovieScenePossessable;

DECLARE_LOG_CATEGORY_EXTERN(LogLevelSequenceEditor, Log, All);

class ACineCameraActor;
class FExtender;
class FMenuBuilder;
class UMovieSceneCompiledDataManager;
class UMovieSceneFolder;
class UMovieSceneSection;
class UMovieSceneSequence;
class USequencerModuleScriptingLayer;
class IDetailsView;
class USequencerCurveEditorObject;
class UMovieSceneCustomBinding;
class IMenu;
class UMovieSceneTrackRowMetadataHelper;

USTRUCT(BlueprintType)
struct FMovieSceneScriptingParams
{
	GENERATED_BODY()
		
	FMovieSceneScriptingParams() {}

	UPROPERTY(BlueprintReadWrite, Category = "Movie Scene")
	EMovieSceneTimeUnit TimeUnit = EMovieSceneTimeUnit::DisplayRate;
};

// Helper struct for Binding Properties UI for locators.
USTRUCT()
struct FMovieSceneBindingPropertyInfo
{
	GENERATED_BODY()

	// Locator for the entry
	UPROPERTY(EditAnywhere, Category = "Default", meta=(AllowedLocators="Actor, UsdPrim", DisplayName="Actor"))
	FUniversalObjectLocator Locator;

	// Flags for how to resolve the locator
	UPROPERTY()
	ELocatorResolveFlags ResolveFlags = ELocatorResolveFlags::None;

	UPROPERTY(Instanced, VisibleAnywhere, Category = "Default", meta=(EditInline, AllowEditInlineCustomization, DisplayName="Custom Binding Type"))
	TObjectPtr<UMovieSceneCustomBinding> CustomBinding = nullptr;
};

// Helper UObject for editing arrays of locators for object bindings. A UObject instead of a UStruct because we need to support instanced sub objects
UCLASS()
class UMovieSceneBindingPropertyInfoList : public UObject
{
	GENERATED_BODY()
public:
	// List of locator info for a particular binding
	UPROPERTY(EditAnywhere, Category = "Binding Properties")
	TArray<FMovieSceneBindingPropertyInfo> Bindings;
};

/**
* ULevelSequenceEditorSubsystem
* Subsystem for level sequence editor related utilities to scripts
*/
UCLASS()
class LEVELSEQUENCEEDITOR_API ULevelSequenceEditorSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	void OnSequencerCreated(TSharedRef<ISequencer> InSequencer);

	void OnSequencerClosed(TSharedRef<ISequencer> InSequencer);

	/** Retrieve the scripting layer */
	UFUNCTION(BlueprintPure, Category = "Level Sequence Editor")
	USequencerModuleScriptingLayer* GetScriptingLayer();

	/** Retrieve the curve editor */
	UFUNCTION(BlueprintPure, Category = "Level Sequence Editor")
	USequencerCurveEditorObject* GetCurveEditor();

	/** Add existing actors to Sequencer. Tracks will be automatically added based on default track settings. */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	TArray<FMovieSceneBindingProxy> AddActors(const TArray<AActor*>& Actors);

	/** Create a cine camera actor and add it to Sequencer */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	FMovieSceneBindingProxy CreateCamera(bool bSpawnable, ACineCameraActor*& OutActor);

	/** 
	* Convert to spawnable. If there are multiple objects assigned to the possessable, multiple spawnables will be created. 
	* For level sequences, the bindings created will be custom bindings of type UMovieSceneSpawnableActorBinding.
	*/
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	TArray<FMovieSceneBindingProxy> ConvertToSpawnable(const FMovieSceneBindingProxy& ObjectBinding);

	/** Convert to possessable. If there are multiple objects assigned to the spawnable. */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	FMovieSceneBindingProxy ConvertToPossessable(const FMovieSceneBindingProxy& ObjectBinding);

	/** Convert to a custom binding of the given binding type*/
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	FMovieSceneBindingProxy ConvertToCustomBinding(const FMovieSceneBindingProxy& ObjectBinding, UPARAM(meta = (AllowAbstract = "false")) TSubclassOf<UMovieSceneCustomBinding> BindingType);

	/** In the case that the given binding proxy holds custom bindings, returns an array of the binding objects so properties can be accessed. */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	TArray<UMovieSceneCustomBinding*> GetCustomBindingObjects(const FMovieSceneBindingProxy& ObjectBinding);

	/** Returns all of the bindings in the sequence of the given custom type. */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	TArray<FMovieSceneBindingProxy> GetCustomBindingsOfType(TSubclassOf<UMovieSceneCustomBinding> CustomBindingType);

	/* Returns the custom binding type for the given binding, or nullptr for possessables*/
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	TSubclassOf<UMovieSceneCustomBinding> GetCustomBindingType(const FMovieSceneBindingProxy& ObjectBinding);

	/* Sets the actor class for the spawnable or replaceable template, in the case those binding types support templates. */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	bool ChangeActorTemplateClass(const FMovieSceneBindingProxy& ObjectBinding, TSubclassOf<AActor> ActorClass);

	/* Save the default state of the spawnable. */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	void SaveDefaultSpawnableState(const FMovieSceneBindingProxy& ObjectBinding);

	/** 
	 * Copy folders 
	 * The copied folders will be saved to the clipboard as well as assigned to the ExportedText string. 
	 * The ExportedTest string can be used in conjunction with PasteFolders if, for example, pasting copy/pasting multiple 
	 * folders without relying on a single clipboard. 
	 */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	void CopyFolders(const TArray<UMovieSceneFolder*>& Folders, FString& ExportedText);

	/** 
	 * Paste folders 
	 * Paste folders from the given TextToImport string (used in conjunction with CopyFolders). 
	 * If TextToImport is empty, the contents of the clipboard will be used.
	 */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	bool PasteFolders(const FString& TextToImport, FMovieScenePasteFoldersParams PasteFoldersParams, TArray<UMovieSceneFolder*>& OutFolders);

	/**
	 * Copy sections
	 * The copied sections will be saved to the clipboard as well as assigned to the ExportedText string.
	 * The ExportedTest string can be used in conjunction with PasteSections if, for example, pasting copy/pasting multiple
	 * sections without relying on a single clipboard.
	 */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	void CopySections(const TArray<UMovieSceneSection*>& Sections, FString& ExportedText);

	/**
	 * Paste sections
	 * Paste sections from the given TextToImport string (used in conjunction with CopySections).
	 * If TextToImport is empty, the contents of the clipboard will be used.
	 */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	bool PasteSections(const FString& TextToImport, FMovieScenePasteSectionsParams PasteSectionsParams, TArray<UMovieSceneSection*>& OutSections);

	/**
	 * Copy tracks
	 * The copied tracks will be saved to the clipboard as well as assigned to the ExportedText string.
	 * The ExportedTest string can be used in conjunction with PasteTracks if, for example, pasting copy/pasting multiple
	 * tracks without relying on a single clipboard.
	 */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	void CopyTracks(const TArray<UMovieSceneTrack*>& Tracks, FString& ExportedText);

	/**
	 * Paste tracks
	 * Paste tracks from the given TextToImport string (used in conjunction with CopyTracks).
	 * If TextToImport is empty, the contents of the clipboard will be used.
	 */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	bool PasteTracks(const FString& TextToImport, FMovieScenePasteTracksParams PasteTracksParams, TArray<UMovieSceneTrack*>& OutTracks);

	/**
	 * Copy bindings
	 * The copied bindings will be saved to the clipboard as well as assigned to the ExportedText string.
	 * The ExportedTest string can be used in conjunction with PasteBindings if, for example, pasting copy/pasting multiple
	 * bindings without relying on a single clipboard.
	 */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	void CopyBindings(const TArray<FMovieSceneBindingProxy>& Bindings, FString& ExportedText);

	/**
	 * Paste bindings
	 * Paste bindings from the given TextToImport string (used in conjunction with CopyBindings).
	 * If TextToImport is empty, the contents of the clipboard will be used.
	 */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	bool PasteBindings(const FString& TextToImport, FMovieScenePasteBindingsParams PasteBindingsParams, TArray<FMovieSceneBindingProxy>& OutObjectBindings);

	/** Snap sections to timeline using source timecode */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	void SnapSectionsToTimelineUsingSourceTimecode(const TArray<UMovieSceneSection*>& Sections);

	/** Sync section using source timecode */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	void SyncSectionsUsingSourceTimecode(const TArray<UMovieSceneSection*>& Sections);

	/** Bake transform */
	UE_DEPRECATED(5.3, "Use ULevelSequenceEditorSubsystem::BakeTransformWithSettings instead")
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	void BakeTransform(const TArray<FMovieSceneBindingProxy>& ObjectBindings, const FFrameTime& BakeInTime, const FFrameTime& BakeOutTime, const FFrameTime& BakeInterval, const FMovieSceneScriptingParams& Params = FMovieSceneScriptingParams());

	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	bool BakeTransformWithSettings(const TArray<FMovieSceneBindingProxy>& ObjectBindings, const FBakingAnimationKeySettings& InSettings, const FMovieSceneScriptingParams& Params = FMovieSceneScriptingParams());

	/** Attempts to automatically fix up broken actor references in the current scene */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	void FixActorReferences();

	/** Assigns the given actors to the binding */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	void AddActorsToBinding(const TArray<AActor*>& Actors, const FMovieSceneBindingProxy& ObjectBinding);

	/** Replaces the binding with the given actors */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	void ReplaceBindingWithActors(const TArray<AActor*>& Actors, const FMovieSceneBindingProxy& ObjectBinding);

	/** Removes the given actors from the binding */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	void RemoveActorsFromBinding(const TArray<AActor*>& Actors, const FMovieSceneBindingProxy& ObjectBinding);

	/** Remove all bound actors from this track */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	void RemoveAllBindings(const FMovieSceneBindingProxy& ObjectBinding);

	/** Remove missing objects bound to this track */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	void RemoveInvalidBindings(const FMovieSceneBindingProxy& ObjectBinding);

	/** Rebind the component binding to the requested component */
	UFUNCTION(BlueprintCallable, Category = "Level Sequence Editor")
	void RebindComponent(const TArray<FMovieSceneBindingProxy>& ComponentBindings, const FName& ComponentName);

	// Refreshes the binding details when the bindings change in the menu
	void RefreshBindingDetails(IDetailsView* DetailsView, FGuid ObjectBindingID);

	// Refreshes the track row metadata details when the track row metadata changes in the menu
	void RefreshTrackRowMetadataDetails(IDetailsView* DetailsView);

private:
	/** Used by Baking transforms*/
	struct FBakeData
	{
		TArray<FVector> Locations;
		TArray<FRotator> Rotations;
		TArray<FVector> Scales;
		TSortedMap<FFrameNumber,FFrameNumber> KeyTimes;
	};
	void CalculateFramesPerGuid(TSharedPtr<ISequencer>& Sequencer, const FBakingAnimationKeySettings& InSettings, TMap<FGuid, FBakeData>& OutBakeDataMa,
		TSortedMap<FFrameNumber, FFrameNumber>&  OutFrameMap);

	// Used by binding properties menu
	struct FBindingPropertiesNotifyHook : FNotifyHook
	{
		UMovieSceneSequence* ObjectToModify = nullptr;
		FBindingPropertiesNotifyHook() {}

		FBindingPropertiesNotifyHook(UMovieSceneSequence* InObjectToModify) : ObjectToModify(InObjectToModify) {}

		virtual void NotifyPreChange(FProperty* PropertyAboutToChange) override;
		virtual void NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged) override;
	};

	UPROPERTY()
	TObjectPtr<UMovieSceneBindingPropertyInfoList> BindingPropertyInfoList = nullptr;

	UPROPERTY()
	TArray<TObjectPtr<UMovieSceneTrackRowMetadataHelper>> TrackRowMetadataHelperList;
	
	FBindingPropertiesNotifyHook NotifyHook;

private:

	void AddBindingDetailCustomizations(TSharedRef<IDetailsView> DetailsView, TSharedPtr<ISequencer> ActiveSequencer, FGuid BindingGuid);
	void AddTrackRowMetadataCustomizations(TSharedRef<IDetailsView> DetailsView, TSharedPtr<ISequencer> ActiveSequencer, UMovieSceneSequence* Sequence);
	void OnBindingPropertyMenuBeingDestroyed(const TSharedRef<IMenu>& Menu, TSharedRef<IDetailsView> DetailsView);
	void OnTrackRowMetadataMenuBeingDestroyed(const TSharedRef<IMenu>& Menu, TSharedRef<IDetailsView> DetailsView);

	TSharedPtr<ISequencer> GetActiveSequencer();
	
	void SnapSectionsToTimelineUsingSourceTimecodeInternal();
	void SyncSectionsUsingSourceTimecodeInternal();
	void BakeTransformInternal();
	void AddActorsToBindingInternal();
	void ReplaceBindingWithActorsInternal();
	void RemoveActorsFromBindingInternal();
	void RemoveAllBindingsInternal();
	void RemoveInvalidBindingsInternal();
	void RebindComponentInternal(const FName& ComponentName);

	void AddAssignActorMenu(FMenuBuilder& MenuBuilder);
	void AddBindingPropertiesMenu(FMenuBuilder& MenuBuilder);
	void AddConvertBindingsMenu(FMenuBuilder& MenuBuilder);

	
	void FillDirectorBlueprintBindingSubMenu(FMenuBuilder& MenuBuilder, TSharedRef<ISequencer> Sequencer, const TArray<FSequencerChangeBindingInfo>& BindingsToChange, bool bConvert, TFunction<void()> OnBindingChanged, const TSubclassOf<UMovieSceneCustomBinding>& CustomBindingType);
	void PopulateQuickBindSubMenu(FMenuBuilder& MenuBuilder, TSharedRef<ISequencer> Sequencer, const TArray<FSequencerChangeBindingInfo>& BindingsToChange, bool bConvert, TFunction<void()> OnBindingChanged, const TSubclassOf<UMovieSceneCustomBinding>& CustomBindingType);
	void FillBindingClassSubMenu(FMenuBuilder& MenuBuilder, TSharedRef<ISequencer> Sequencer, const TArray<FSequencerChangeBindingInfo>& BindingsToChange, bool bConvert, TFunction<void()> OnBindingChanged, const TArray<const TSubclassOf<UMovieSceneCustomBinding>>& UserCustomBindingTypes);
	void ChangeBindingTypes(const TSharedRef<ISequencer>& InSequencer
		, const TArray<FSequencerChangeBindingInfo>& InBindingsToChange
		, TFunction<FMovieScenePossessable* (FGuid, int32)> InDoChangeType
		, TFunction<void()> InOnBindingChanged);

	void AddTrackRowMetadataMenu(FMenuBuilder& MenuBuilder);
public:
	void AddBindingPropertiesSidebar(FMenuBuilder& MenuBuilder); 
	
	/* Creates a menu for changing or converting a binding type. If bConvert is true, it will only show types that state they are able to be converted to from the passed in bindings
	and will attempt to convert them. If bConvert is false, it will change the binding type and reset to a default binding of that type.*/
	void AddChangeBindingTypeMenu(FMenuBuilder& MenuBuilder, TSharedRef<ISequencer> Sequencer, const TArray<FSequencerChangeBindingInfo>& BindingsToChange, bool bConvert, TFunction<void()> OnBindingChanged);

private:
	void OnFinishedChangingLocators(const FPropertyChangedEvent& PropertyChangedEvent, TSharedRef<IDetailsView> DetailsView, FGuid ObjectBindingID);

	void OnFinishedChangingTrackRowMetadata(const FPropertyChangedEvent& PropertyChangedEvent, TSharedRef<IDetailsView> DetailsView);

	void GetRebindComponentNames(TArray<FName>& OutComponentNames);
	void RebindComponentMenu(FMenuBuilder& MenuBuilder);

	bool IsSelectedBindingRootPossessable();

	FDelegateHandle OnSequencerCreatedHandle;

	/* List of sequencers that have been created */
	TArray<TWeakPtr<ISequencer>> Sequencers;

	/* Map of curve editors with their sequencers*/
	TMap<TWeakPtr<ISequencer>, TObjectPtr<USequencerCurveEditorObject>> CurveEditorObjects;
	/* property array of the curve editors*/
	UPROPERTY()
	TArray<TObjectPtr<USequencerCurveEditorObject>> CurveEditorArray;

	TSharedPtr<FUICommandList> CommandList;

	TSharedPtr<FExtender> TransformMenuExtender;
	TSharedPtr<FExtender> FixActorReferencesMenuExtender;

	TSharedPtr<FExtender> AssignActorMenuExtender;
	TSharedPtr<FExtender> BindingPropertiesMenuExtender;
	TSharedPtr<FExtender> RebindComponentMenuExtender;
	TSharedPtr<FExtender> SidebarMenuExtender;

	friend class FMovieSceneBindingPropertyInfoListCustomization;
};

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_2
#include "CoreMinimal.h"
#include "ISequencer.h"
#include "MovieSceneBindingProxy.h"
#include "SequencerUtilities.h"
#endif
