// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/SparseDelegate.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/AssetUserData.h"
#include "Rigs/RigHierarchyDefines.h"
#include "ControlRig.h"
#include "Styling/SlateTypes.h"
#include "ISequencerPropertyKeyedStatus.h"
#include "AnimLayers.generated.h"

class ULevelSequence;
class UControlRig;
class UMovieSceneTrack;
class UMovieSceneSection;
class ISequencer;
class UMovieSceneControlRigParameterTrack;
class UMovieSceneControlRigParameterSection;
struct FBakingAnimationKeySettings;
struct FMovieSceneFloatChannel;
class IPropertyHandle;

USTRUCT(BlueprintType)
// Name of a property and control and the specific channels that make up the layer
struct FAnimLayerPropertyAndChannels
{
	GENERATED_BODY()

	FAnimLayerPropertyAndChannels() : Name(NAME_None) , Channels((uint32)EControlRigContextChannelToKey::AllTransform) {};
	
	//Name of the property or control
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Data")
	FName Name; 

	//Mask of channels for that property/control, currently not used
	uint32 Channels;
};

//Bound object/control rig and the properties/channels it is made of
//A layer will consist of a bunch of these
USTRUCT(BlueprintType)
struct FAnimLayerSelectionSet
{
	GENERATED_BODY()
	FAnimLayerSelectionSet() :BoundObject(nullptr) {};

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Data")
	TWeakObjectPtr<UObject> BoundObject; //bound object is either a CR or a bound sequencer object

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Data")
	TMap<FName, FAnimLayerPropertyAndChannels> Names; //property/control name and channels

	bool MergeWithAnotherSelection(const FAnimLayerSelectionSet& Selection);
};

//The set with it's section
USTRUCT(BlueprintType)
struct FAnimLayerSectionItem
{
	GENERATED_BODY()
	FAnimLayerSectionItem() : Section(nullptr) {};

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Data")
	FAnimLayerSelectionSet AnimLayerSet;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Data")
	TWeakObjectPtr<UMovieSceneSection> Section;
};

//individual layer items that make up the layer
USTRUCT(BlueprintType)
struct FAnimLayerItem
{
	GENERATED_BODY()
	FAnimLayerItem()  {};

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Data")
	TArray< FAnimLayerSectionItem>  SectionItems;

	//find section that matches based upon incoming section (same type and property)
	FAnimLayerSectionItem* FindMatchingSectionItem(UMovieSceneSection* InMovieSceneSection);
};

UENUM()
enum EAnimLayerType : uint32
{
	Base = 0x0,
	Additive = 0x1,
	Override = 0x2,
};
ENUM_CLASS_FLAGS(EAnimLayerType)

//state and properties of a layer
USTRUCT(BlueprintType)
struct FAnimLayerState
{

	GENERATED_BODY()

	FAnimLayerState();
	FText AnimLayerTypeToText()const;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Data")
	mutable ECheckBoxState bKeyed = ECheckBoxState::Unchecked;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Data")
	mutable ECheckBoxState bSelected = ECheckBoxState::Unchecked;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Data")
	mutable bool bActive;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Data")
	mutable bool bLock;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Data")
	FText Name;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Data")
	mutable double Weight;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Data", meta = (Bitmask, BitmaskEnum = "/Script/ControlRigEditor.EAnimLayerType"))
	mutable int32 Type;

};

USTRUCT(BlueprintType)
struct FAnimLayerControlRigObject
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Data")
	TWeakObjectPtr<UControlRig> ControlRig;
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Data")
	TArray<FName>  ControlNames;
};

USTRUCT(BlueprintType)
struct FAnimLayerSceneObject
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Data")
	TWeakObjectPtr<UObject> SceneObjectOrComponent;
	
	//just doing transform for now
	//UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Data")
	//TArray<FName>  PropertyNames;
};

USTRUCT(BlueprintType)
struct FAnimLayerObjects
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Data")
	TArray<FAnimLayerControlRigObject> ControlRigObjects;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Data")
	TArray<FAnimLayerSceneObject> SceneObjects;
};

UCLASS(EditInlineNew, CollapseCategories, HideDropdown)
class UAnimLayerWeightProxy : public UObject
{
	GENERATED_BODY()
public:

	UPROPERTY(EditAnywhere, Interp, DisplayName = "", Category = "NoCategory", meta = (SliderExponent = "1.0"))
	double Weight = 1.0;

	//UObect overrides for setting values when weight changes
	virtual void PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent) override;
#if WITH_EDITOR
	virtual void PostEditUndo() override;
#endif
};

//for friend access to State variables directly
class UAnimLayers;
struct FAnimLayerSourceUIEntry;

UCLASS(BlueprintType, MinimalAPI)
class UAnimLayer : public UObject
{
	GENERATED_UCLASS_BODY()
public:
	UAnimLayer() {};

	void GetAnimLayerObjects(FAnimLayerObjects& InLayerObjects) const;

	UFUNCTION(BlueprintCallable, Category = "State")
	ECheckBoxState GetKeyed() const;
	UFUNCTION(BlueprintCallable, Category = "State")
	void SetKeyed();

	UFUNCTION(BlueprintCallable, Category = "State")
	ECheckBoxState GetSelected() const;
	UFUNCTION(BlueprintCallable, Category = "State")
	void SetSelected(bool bInSelected, bool bClearSelection);

	UFUNCTION(BlueprintCallable, Category = "State")
	bool GetActive() const;
	UFUNCTION(BlueprintCallable, Category = "State")
	void SetActive(bool bInActive);

	UFUNCTION(BlueprintCallable, Category = "State")
	bool GetLock() const;
	UFUNCTION(BlueprintCallable, Category = "State")
	void SetLock(bool bInLock);

	UFUNCTION(BlueprintCallable, Category = "State")
	FText GetName() const;
	UFUNCTION(BlueprintCallable, Category = "State")
	void SetName(const FText& InName);

	UFUNCTION(BlueprintCallable, Category = "State")
	double GetWeight() const;
	UFUNCTION(BlueprintCallable, Category = "State")
	void SetWeight(double InWeight);

	UFUNCTION(BlueprintCallable, Category = "State")
	EAnimLayerType GetType() const;
	UFUNCTION(BlueprintCallable, Category = "State")
	void SetType(EAnimLayerType LayerType);

	UFUNCTION(BlueprintCallable, Category = "State")
	bool AddSelectedInSequencer();
	UFUNCTION(BlueprintCallable, Category = "State")
	bool RemoveSelectedInSequencer();

	bool RemoveAnimLayerItem(UObject* InObject);
	void SetSelectedInList(bool bInValue) { bIsSelectedInList = bInValue; }
	bool GetSelectedInList() const { return bIsSelectedInList; }

	//for key property status for weights
	void SetKey(TSharedPtr<ISequencer>& Sequencer, const IPropertyHandle& KeyedPropertyHandle);
	EPropertyKeyedStatus GetPropertyKeyedStatus(TSharedPtr<ISequencer>& Sequencer, const IPropertyHandle& PropertyHandle);
private:
	UPROPERTY(VisibleAnywhere, Category = "Data")
	TMap<TWeakObjectPtr<UObject>, FAnimLayerItem> AnimLayerItems;

	UPROPERTY(VisibleAnywhere, Category = "Data")
	FAnimLayerState State;

	UPROPERTY(Transient)
	TObjectPtr<UAnimLayerWeightProxy> WeightProxy;
	
	void SetSectionToKey() const;

	//static helper functions
	static bool IsAccepableNonControlRigObject(UObject* InObject);

	bool bIsSelectedInList = false;

	friend class UAnimLayers;
	friend struct FAnimLayerSourceUIEntry;
};

/** Link To Set of Anim Sequences that we may belinked to.*/
UCLASS(BlueprintType, MinimalAPI)
class UAnimLayers : public UAssetUserData
{
	GENERATED_UCLASS_BODY()

public:
#if WITH_EDITOR
	virtual void PostEditUndo() override;
#endif
	virtual bool IsEditorOnly() const override { return true; }

	UPROPERTY(BlueprintReadWrite, Category = Layers)
	TArray<TObjectPtr<UAnimLayer>> AnimLayers;

	int32 GetAnimLayerIndex(UAnimLayer* InAnimLayer) const;
	bool DeleteAnimLayer(ISequencer* InSequencer, int32 Index);
	int32 DuplicateAnimLayer(ISequencer* InSequencer, int32 Index);
	int32 AddAnimLayerFromSelection(ISequencer* InSequencer);
	void GetAnimLayerStates(TArray<FAnimLayerState>& OutStates);
	bool MergeAnimLayers(ISequencer* InSequencer, const TArray<int32>& Indices, const FBakingAnimationKeySettings* InSettings);
	bool SetPassthroughKey(ISequencer* InSequencer, int32 Index);

	//will always blend to base fo rnow
	bool AdjustmentBlendLayers(ISequencer* InSequencer, int32 LayerIndex);

	bool IsTrackOnSelectedLayer(const UMovieSceneTrack* InTrack)const; 
	TArray<UMovieSceneSection*> GetSelectedLayerSections() const;

	//Get
	static UAnimLayers* GetAnimLayers(ISequencer* InSequencer);
	static UAnimLayers* GetAnimLayers(ULevelSequence* InLevelSequence);
	static 	TSharedPtr<ISequencer> GetSequencerFromAsset();
public:
	DECLARE_EVENT_OneParam(UAnimLayers, FAnimLayerListChanged, UAnimLayers*);

	FAnimLayerListChanged& AnimLayerListChanged() { return OnAnimLayerListChanged; };

private:
	void AnimLayerListChangedBroadcast();
	FAnimLayerListChanged OnAnimLayerListChanged;
	void AddBaseLayer();
public:
	void SetUpBaseLayerSections();

	static void SetUpSectionDefaults(ISequencer* SequencerPtr, UAnimLayer* Layer, UMovieSceneTrack* InTrack, UMovieSceneSection* InSection, FMovieSceneFloatChannel* FloatChannel);
	static void SetUpControlRigSection(UMovieSceneControlRigParameterSection* InSection, TArray<FName>& ControlNames);

};




