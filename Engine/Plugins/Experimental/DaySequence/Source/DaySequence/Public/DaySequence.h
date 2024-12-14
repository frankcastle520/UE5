// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Interfaces/Interface_AssetUserData.h"
#include "MovieSceneSequence.h"
#include "DaySequenceBindingReference.h"
#include "DaySequence.generated.h"

class UAssetUserData;
class UBlueprint;
struct FMovieSceneSequenceID;

UCLASS(BlueprintType, HideCategories=(Object))
class DAYSEQUENCE_API UDaySequence
	: public UMovieSceneSequence
	, public IInterface_AssetUserData
{
	GENERATED_BODY()
public:
	/** Pointer to the movie scene that controls this animation. */
	UPROPERTY()
	TObjectPtr<UMovieScene> MovieScene;

public:
	UDaySequence(const FObjectInitializer&);
	
	/** Initialize this sequence. */
	virtual void Initialize();
	virtual void Initialize(EObjectFlags Flags);

	void AddDefaultBinding(const FGuid& PossessableGuid);

	// UMovieSceneSequence interface
	virtual void BindPossessableObject(const FGuid& ObjectId, UObject& PossessedObject, UObject* Context) override;
	virtual bool CanPossessObject(UObject& Object, UObject* InPlaybackContext) const override;
	virtual void LocateBoundObjects(const FGuid& ObjectId, UObject* Context, TArray<UObject*, TInlineAllocator<1>>& OutObjects) const override;
	virtual FGuid FindBindingFromObject(UObject* InObject, TSharedRef<const FSharedPlaybackState> SharedPlaybackState) const override;
	virtual void GatherExpiredObjects(const FMovieSceneObjectCache& InObjectCache, TArray<FGuid>& OutInvalidIDs) const override;
	virtual UMovieScene* GetMovieScene() const override;
	virtual UObject* GetParentObject(UObject* Object) const override;
	virtual void UnbindPossessableObjects(const FGuid& ObjectId) override;
	virtual void UnbindObjects(const FGuid& ObjectId, const TArray<UObject*>& InObjects, UObject* InContext) override;
	virtual void UnbindInvalidObjects(const FGuid& ObjectId, UObject* InContext) override;
	virtual bool AllowsSpawnableObjects() const override;
	virtual bool CanRebindPossessable(const FMovieScenePossessable& InPossessable) const override;
	virtual UObject* MakeSpawnableTemplateFromInstance(UObject& InSourceObject, FName ObjectName) override;
	virtual bool CanAnimateObject(UObject& InObject) const override;
	virtual UObject* CreateDirectorInstance(TSharedRef<const FSharedPlaybackState> SharedPlaybackState, FMovieSceneSequenceID SequenceID) override;
	virtual void PostLoad() override;
	virtual void PostInitProperties() override;
	virtual bool Rename(const TCHAR* NewName = nullptr, UObject* NewOuter = nullptr, ERenameFlags Flags = REN_None) override;

	//~ Begin IInterface_AssetUserData Interface
	virtual void AddAssetUserData(UAssetUserData* InUserData) override;
	virtual void RemoveUserDataOfClass(TSubclassOf<UAssetUserData> InUserDataClass) override;
	virtual UAssetUserData* GetAssetUserDataOfClass(TSubclassOf<UAssetUserData> InUserDataClass) override;
	virtual const TArray<UAssetUserData*>* GetAssetUserDataArray() const override;
	//~ End IInterface_AssetUserData Interface

#if WITH_EDITOR
	virtual ETrackSupport IsTrackSupportedImpl(TSubclassOf<class UMovieSceneTrack> InTrackClass) const override;
	virtual void GetAssetRegistryTags(FAssetRegistryTagsContext Context) const override;

	DECLARE_DELEGATE_RetVal_OneParam(void, FPostDuplicateEvent, UDaySequence*);
	static FPostDuplicateEvent PostDuplicateEvent;
#endif

	virtual void PostDuplicate(bool bDuplicateForPIE) override;

#if WITH_EDITOR


public:

	/**
	 * Assign a new director blueprint to this sequence. The specified blueprint *must* be contained within this object.?	 */
	void SetDirectorBlueprint(UBlueprint* NewDirectorBlueprint);

	/**
	 * Retrieve the currently assigned director blueprint for this sequence
	 */
	UBlueprint* GetDirectorBlueprint() const;

	FString GetDirectorBlueprintName() const;

protected:

	virtual FGuid CreatePossessable(UObject* ObjectToPossess) override;
	virtual FGuid CreateSpawnable(UObject* ObjectToSpawn) override;

	FGuid FindOrAddBinding(UObject* ObjectToPossess);

	/**
	 * Invoked when this sequence's director blueprint has been recompiled
	 */
	void OnDirectorRecompiled(UBlueprint*);

#endif // WITH_EDITOR

protected:
	/** References to bound objects. */
	UPROPERTY()
	FDaySequenceBindingReferences BindingReferences;

#if WITH_EDITORONLY_DATA
	/** A pointer to the director blueprint that generates this sequence's DirectorClass. */
	UPROPERTY()
	TObjectPtr<UBlueprint> DirectorBlueprint;
#endif

	/**
	 * The class that is used to spawn this sequence's director instance.
	 * Director instances are allocated on-demand one per sequence during evaluation and are used by event tracks for triggering events.
	 */
	UPROPERTY()
	TObjectPtr<UClass> DirectorClass;

protected:
	/** Array of user data stored with the asset */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Instanced, Category = Animation)
	TArray<TObjectPtr<UAssetUserData>> AssetUserData;
};
