// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "AnimNextRigVMAssetEntry.generated.h"

class UAnimNextRigVMAssetEditorData;
enum class ERigVMGraphNotifType : uint8;
enum class EAnimNextEditorDataNotifType : uint8;
class URigVMGraph;

namespace UE::AnimNext::Editor
{
	class SRigVMAssetView;
	class SRigVMAssetViewRow;
	struct FRigVMAssetViewEntry;
	class FVariableCustomization;
}

/** Base class that defines an entry in a module, e.g. a parameter or a graph */
UCLASS(MinimalAPI, BlueprintType, Abstract)
class UAnimNextRigVMAssetEntry : public UObject
{
	GENERATED_BODY()
public:
	// Binds delegates to owning editor data
	virtual void Initialize(UAnimNextRigVMAssetEditorData* InEditorData);

	// Allow entries the opportunity to handle RigVM modification events
	virtual void HandleRigVMGraphModifiedEvent(ERigVMGraphNotifType InNotifType, URigVMGraph* InGraph, UObject* InSubject) {}

	// Get this entry's name
	virtual FName GetEntryName() const PURE_VIRTUAL(UAnimNextRigVMAssetEntry::GetEntryName, return NAME_None;)

	// Set this entry's name
	virtual void SetEntryName(FName InName, bool bSetupUndoRedo = true) {}

	// Get the name to be displayed in the UI for this entry
	virtual FText GetDisplayName() const { return FText::FromName(GetEntryName()); }

	// Get the tooltip to be displayed for the name in the UI for this entry
	virtual FText GetDisplayNameTooltip() const { return FText::FromName(GetEntryName()); }

	// Set whether this entry is hidden in the outliner
	void SetHiddenInOutliner(bool bInHide) { bHiddenInOutliner = bInHide; }

	// Get whether this entry is hidden in the outliner
	bool IsHiddenInOutliner() const { return bHiddenInOutliner; }

	// UObject interface
	virtual bool IsAsset() const override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostTransacted(const FTransactionObjectEvent& TransactionEvent) override;
#endif

protected:
	ANIMNEXTUNCOOKEDONLY_API void BroadcastModified(EAnimNextEditorDataNotifType InType);

	friend class UE::AnimNext::Editor::FVariableCustomization;
	friend class UE::AnimNext::Editor::SRigVMAssetView;

	UPROPERTY()
	uint32 bHiddenInOutliner : 1 = false;
};