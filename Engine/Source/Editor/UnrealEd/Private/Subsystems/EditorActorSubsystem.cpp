// Copyright Epic Games, Inc. All Rights Reserved.

#include "Subsystems/EditorActorSubsystem.h"

#include "ActorEditorUtils.h"
#include "ActorFactories/ActorFactory.h"
#include "ActorFactories/ActorFactoryBlueprint.h"
#include "ActorGroupingUtils.h"
#include "Animation/SkeletalMeshActor.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetSelection.h"
#include "BSPOps.h"
#include "Components/LightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Editor.h"
#include "Editor/GroupActor.h"
#include "Editor/UnrealEdEngine.h"
#include "EditorScriptingHelpers.h"
#include "Elements/Framework/EngineElementsLibrary.h"
#include "Elements/Framework/TypedElementHandle.h"
#include "Engine/Blueprint.h"
#include "Engine/Brush.h"
#include "Engine/Light.h"
#include "Engine/Selection.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "GameFramework/Actor.h"
#include "InteractiveFoliageActor.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Layers/LayersSubsystem.h"
#include "LevelEditorViewport.h"
#include "Logging/MessageLog.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FeedbackContext.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Model.h"
#include "Rendering/ColorVertexBuffer.h"
#include "SCreateAssetFromObject.h"
#include "ScopedTransaction.h"
#include "Serialization/ArchiveReplaceObjectRef.h"
#include "Settings/LevelEditorMiscSettings.h"
#include "Settings/LevelEditorViewportSettings.h"
#include "StaticMeshComponentLODInfo.h"
#include "Subsystems/EditorElementSubsystem.h"
#include "Subsystems/UnrealEditorSubsystem.h"
#include "UObject/Stack.h"
#include "UObject/UObjectIterator.h"
#include "UnrealEdGlobals.h"
#include "Utils.h"
#include "WorldPartition/ContentBundle/ContentBundleActivationScope.h"

#define LOCTEXT_NAMESPACE "EditorActorUtilities"

namespace InternalActorUtilitiesSubsystemLibrary
{
	template<class T>
	bool IsEditorLevelActor(T* Actor)
	{
		bool bResult = false;
		if (Actor && IsValidChecked(Actor))
		{
			UWorld* World = Actor->GetWorld();
			if (World && World->WorldType == EWorldType::Editor)
			{
				bResult = true;
			}
		}
		return bResult;
	}

	template<class T>
	TArray<T*> GetAllLoadedObjects()
	{
		TArray<T*> Result;

		if (!EditorScriptingHelpers::CheckIfInEditorAndPIE())
		{
			return Result;
		}

		const EObjectFlags ExcludeFlags = RF_ClassDefaultObject;
		for (TObjectIterator<T> It(ExcludeFlags, true, EInternalObjectFlags::Garbage); It; ++It)
		{
			T* Obj = *It;
			if (InternalActorUtilitiesSubsystemLibrary::IsEditorLevelActor(Obj))
			{
				Result.Add(Obj);
			}
		}

		return Result;
	}

	AActor* SpawnActor(const TCHAR* MessageName, UObject* ObjToUse, FVector Location, FRotator Rotation, bool bTransient)
	{
		UUnrealEditorSubsystem* UnrealEditorSubsystem = GEditor->GetEditorSubsystem<UUnrealEditorSubsystem>();

		if (!UnrealEditorSubsystem || !EditorScriptingHelpers::CheckIfInEditorAndPIE())
		{
			return nullptr;
		}

		if (!ObjToUse)
		{
			UE_LOG(LogUtils, Error, TEXT("%s. ObjToUse is not valid."), MessageName);
			return nullptr;
		}

		UWorld* World = UnrealEditorSubsystem->GetEditorWorld();
		if (!World)
		{
			UE_LOG(LogUtils, Error, TEXT("%s. Can't spawn the actor because there is no world."), MessageName);
			return nullptr;
		}

		ULevel* DesiredLevel = World->GetCurrentLevel();
		if (!DesiredLevel)
		{
			UE_LOG(LogUtils, Error, TEXT("%s. Can't spawn the actor because there is no Level."), MessageName);
			return nullptr;
		}

		GEditor->ClickLocation = Location;
		GEditor->ClickPlane = FPlane(Location, FVector::UpVector);

		EObjectFlags NewObjectFlags = RF_Transactional;

		if (bTransient)
		{
			NewObjectFlags |= RF_Transient;
		}

		UActorFactory* FactoryToUse = nullptr;
		bool bSelectActors = false;
		TArray<AActor*> Actors = FLevelEditorViewportClient::TryPlacingActorFromObject(DesiredLevel, ObjToUse, bSelectActors, NewObjectFlags, FactoryToUse);

		if (Actors.Num() == 0 || Actors[0] == nullptr)
		{
			UE_LOG(LogUtils, Warning, TEXT("%s. No actor was spawned."), MessageName);
			return nullptr;
		}

		for (AActor* Actor : Actors)
		{
			if (Actor)
			{
				Actor->SetActorLocationAndRotation(Location, Rotation, false, nullptr, ETeleportType::TeleportPhysics);
			}
		}

		return Actors[0];
	}
}

void UEditorActorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	FEditorDelegates::OnNewActorsDropped.AddUObject(this, &UEditorActorSubsystem::BroadcastEditNewActorsDropped);
	FEditorDelegates::OnNewActorsPlaced.AddUObject(this, &UEditorActorSubsystem::BroadcastEditNewActorsPlaced);

	FEditorDelegates::OnEditCutActorsBegin.AddUObject(this, &UEditorActorSubsystem::BroadcastEditCutActorsBegin);
	FEditorDelegates::OnEditCutActorsEnd.AddUObject(this, &UEditorActorSubsystem::BroadcastEditCutActorsEnd);

	FEditorDelegates::OnEditCopyActorsBegin.AddUObject(this, &UEditorActorSubsystem::BroadcastEditCopyActorsBegin);
	FEditorDelegates::OnEditCopyActorsEnd.AddUObject(this, &UEditorActorSubsystem::BroadcastEditCopyActorsEnd);

	FEditorDelegates::OnEditPasteActorsBegin.AddUObject(this, &UEditorActorSubsystem::BroadcastEditPasteActorsBegin);
	FEditorDelegates::OnEditPasteActorsEnd.AddUObject(this, &UEditorActorSubsystem::BroadcastEditPasteActorsEnd);

	FEditorDelegates::OnDuplicateActorsBegin.AddUObject(this, &UEditorActorSubsystem::BroadcastDuplicateActorsBegin);
	FEditorDelegates::OnDuplicateActorsEnd.AddUObject(this, &UEditorActorSubsystem::BroadcastDuplicateActorsEnd);

	FEditorDelegates::OnDeleteActorsBegin.AddUObject(this, &UEditorActorSubsystem::BroadcastDeleteActorsBegin);
	FEditorDelegates::OnDeleteActorsEnd.AddUObject(this, &UEditorActorSubsystem::BroadcastDeleteActorsEnd);

	FCoreDelegates::OnActorLabelChanged.AddUObject(this, &UEditorActorSubsystem::BroadcastActorLabelChanged);
}

void UEditorActorSubsystem::Deinitialize()
{
	FEditorDelegates::OnNewActorsDropped.RemoveAll(this);
	FEditorDelegates::OnNewActorsPlaced.RemoveAll(this);
	FEditorDelegates::OnEditCutActorsBegin.RemoveAll(this);
	FEditorDelegates::OnEditCutActorsEnd.RemoveAll(this);
	FEditorDelegates::OnEditCopyActorsBegin.RemoveAll(this);
	FEditorDelegates::OnEditCopyActorsEnd.RemoveAll(this);
	FEditorDelegates::OnEditPasteActorsBegin.RemoveAll(this);
	FEditorDelegates::OnEditPasteActorsEnd.RemoveAll(this);
	FEditorDelegates::OnDuplicateActorsBegin.RemoveAll(this);
	FEditorDelegates::OnDuplicateActorsEnd.RemoveAll(this);
	FEditorDelegates::OnDeleteActorsBegin.RemoveAll(this);
	FEditorDelegates::OnDeleteActorsEnd.RemoveAll(this);
	FCoreDelegates::OnActorLabelChanged.RemoveAll(this);
}

/** To fire before an Actor is Dropped */
void UEditorActorSubsystem::BroadcastEditNewActorsDropped(const TArray<UObject*>& DroppedObjects, const TArray<AActor*>& DroppedActors)
{
	OnNewActorsDropped.Broadcast(DroppedObjects, DroppedActors);
}

/** To fire before an Actor is Placed */
void UEditorActorSubsystem::BroadcastEditNewActorsPlaced(UObject* ObjToUse, const TArray<AActor*>& PlacedActors)
{
	OnNewActorsPlaced.Broadcast(ObjToUse, PlacedActors);
}

/** To fire before an Actor is Cut */
void UEditorActorSubsystem::BroadcastEditCutActorsBegin()
{
	OnEditCutActorsBegin.Broadcast();
}

/** To fire after an Actor is Cut */
void UEditorActorSubsystem::BroadcastEditCutActorsEnd()
{
	OnEditCutActorsEnd.Broadcast();
}

/** To fire before an Actor is Copied */
void UEditorActorSubsystem::BroadcastEditCopyActorsBegin()
{
	OnEditCopyActorsBegin.Broadcast();
}

/** To fire after an Actor is Copied */
void UEditorActorSubsystem::BroadcastEditCopyActorsEnd()
{
	OnEditCopyActorsEnd.Broadcast();
}

/** To fire before an Actor is Pasted */
void UEditorActorSubsystem::BroadcastEditPasteActorsBegin()
{
	OnEditPasteActorsBegin.Broadcast();
}

/** To fire after an Actor is Pasted */
void UEditorActorSubsystem::BroadcastEditPasteActorsEnd()
{
	OnEditPasteActorsEnd.Broadcast();
}

/** To fire before an Actor is duplicated */
void UEditorActorSubsystem::BroadcastDuplicateActorsBegin()
{
	OnDuplicateActorsBegin.Broadcast();
}

/** To fire after an Actor is duplicated */
void UEditorActorSubsystem::BroadcastDuplicateActorsEnd()
{
	OnDuplicateActorsEnd.Broadcast();
}

/** To fire before an Actor is Deleted */
void UEditorActorSubsystem::BroadcastDeleteActorsBegin()
{
	OnDeleteActorsBegin.Broadcast();
}

/** To fire after an Actor is Deleted */
void UEditorActorSubsystem::BroadcastDeleteActorsEnd()
{
	OnDeleteActorsEnd.Broadcast();
}

/** To fire after an Actor has changed label */
void UEditorActorSubsystem::BroadcastActorLabelChanged(AActor* Actor)
{
	OnActorLabelChanged.Broadcast(Actor);
}

void UEditorActorSubsystem::DuplicateSelectedActors(UWorld* InWorld)
{
	if (!GEditor || !InWorld)
	{
		return;
	}

	bool bComponentsSelected = GEditor->GetSelectedComponentCount() > 0;
	//@todo locked levels - if all actor levels are locked, cancel the transaction
	const FScopedTransaction Transaction(bComponentsSelected ? NSLOCTEXT("UnrealEd", "DuplicateComponents", "Duplicate Components") : NSLOCTEXT("UnrealEd", "DuplicateActors", "Duplicate Actors"));

	FEditorDelegates::OnDuplicateActorsBegin.Broadcast();

	// duplicate selected
	ABrush::SetSuppressBSPRegeneration(true);
	GEditor->edactDuplicateSelected(InWorld->GetCurrentLevel(), GetDefault<ULevelEditorViewportSettings>()->GridEnabled);
	ABrush::SetSuppressBSPRegeneration(false);

	// Find out if any of the selected actors will change the BSP.
	// and only then rebuild BSP as this is expensive. 
	const FSelectedActorInfo& SelectedActors = AssetSelectionUtils::GetSelectedActorInfo();
	if (SelectedActors.bHaveBrush)
	{
		GEditor->RebuildAlteredBSP(); // Update the Bsp of any levels containing a modified brush
	}

	FEditorDelegates::OnDuplicateActorsEnd.Broadcast();

	GEditor->RedrawLevelEditingViewports();
}

void UEditorActorSubsystem::DeleteSelectedActors(UWorld* InWorld)
{
	if (!GEditor || !InWorld)
	{
		return;
	}

	bool bComponentsSelected = GEditor->GetSelectedComponentCount() > 0;

	const FScopedTransaction Transaction(bComponentsSelected ? NSLOCTEXT("UnrealEd", "DeleteComponents", "Delete Components") : NSLOCTEXT("UnrealEd", "DeleteActors", "Delete Actors"));

	FEditorDelegates::OnDeleteActorsBegin.Broadcast();
	const bool bCheckRef = GetDefault<ULevelEditorMiscSettings>()->bCheckReferencesOnDelete;
	GEditor->edactDeleteSelected(InWorld, true, bCheckRef, bCheckRef);
	FEditorDelegates::OnDeleteActorsEnd.Broadcast();
}

void UEditorActorSubsystem::InvertSelection(UWorld* InWorld)
{
	if (!GUnrealEd || !InWorld)
	{
		return;
	}

	const FScopedTransaction Transaction(NSLOCTEXT("UnrealEd", "SelectInvert", "Select Invert"));
	GUnrealEd->edactSelectInvert(InWorld);
}

void UEditorActorSubsystem::SelectAll(UWorld* InWorld)
{
	if (!GUnrealEd || !InWorld)
	{
		return;
	}

	const FScopedTransaction Transaction(NSLOCTEXT("UnrealEd", "SelectAll", "Select All"));
	GUnrealEd->edactSelectAll(InWorld);
}

void UEditorActorSubsystem::SelectAllChildren(bool bRecurseChildren)
{
	if (!GUnrealEd)
	{
		return;
	}

	FText TransactionLabel;
	if (bRecurseChildren)
	{
		TransactionLabel = NSLOCTEXT("UnrealEd", "SelectAllDescendants", "Select All Descendants");
	}
	else
	{
		TransactionLabel = NSLOCTEXT("UnrealEd", "SelectAllChildren", "Select All Children");
	}

	const FScopedTransaction Transaction(TransactionLabel);
	GUnrealEd->edactSelectAllChildren(bRecurseChildren);
}

TArray<AActor*> UEditorActorSubsystem::GetAllLevelActors()
{
	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);
	TArray<AActor*> Result;

	UUnrealEditorSubsystem* UnrealEditorSubsystem = GEditor->GetEditorSubsystem<UUnrealEditorSubsystem>();

	if (UnrealEditorSubsystem && EditorScriptingHelpers::CheckIfInEditorAndPIE())
	{
		//Default iterator only iterates over active levels.
		const EActorIteratorFlags Flags = EActorIteratorFlags::SkipPendingKill;
		for (TActorIterator<AActor> It(UnrealEditorSubsystem->GetEditorWorld(), AActor::StaticClass(), Flags); It; ++It)
		{
			AActor* Actor = *It;
			if (Actor->IsEditable() &&
				Actor->IsListedInSceneOutliner() &&					// Only add actors that are allowed to be selected and drawn in editor
				!Actor->IsTemplate() &&								// Should never happen, but we never want CDOs
				!Actor->HasAnyFlags(RF_Transient) &&				// Don't add transient actors in non-play worlds
				!FActorEditorUtils::IsABuilderBrush(Actor) &&		// Don't add the builder brush
				!Actor->IsA(AWorldSettings::StaticClass()))			// Don't add the WorldSettings actor, even though it is technically editable
			{
				Result.Add(*It);
			}
		}
	}

	return Result;
}

TArray<UActorComponent*> UEditorActorSubsystem::GetAllLevelActorsComponents()
{
	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

	if (!EditorScriptingHelpers::CheckIfInEditorAndPIE())
	{
		return TArray<UActorComponent*>();
	}

	return InternalActorUtilitiesSubsystemLibrary::GetAllLoadedObjects<UActorComponent>();
}

TArray<AActor*> UEditorActorSubsystem::GetSelectedLevelActors()
{
	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

	TArray<AActor*> Result;

	if (!EditorScriptingHelpers::CheckIfInEditorAndPIE())
	{
		return Result;
	}

	for (FSelectionIterator Iter(*GEditor->GetSelectedActors()); Iter; ++Iter)
	{
		AActor* Actor = Cast<AActor>(*Iter);
		if (InternalActorUtilitiesSubsystemLibrary::IsEditorLevelActor(Actor))
		{
			Result.Add(Actor);
		}
	}

	return Result;
}

void UEditorActorSubsystem::SetSelectedLevelActors(const TArray<class AActor*>& ActorsToSelect)
{
	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

	TArray<AActor*> Result;

	if (!EditorScriptingHelpers::CheckIfInEditorAndPIE())
	{
		return;
	}

	if (GEdSelectionLock)
	{
		UE_LOG(LogUtils, Warning, TEXT("SetSelectedLevelActors. The editor selection is currently locked."));
		return;
	}

	USelection* SelectedActors = GEditor->GetSelectedActors();
	SelectedActors->Modify();
	if (ActorsToSelect.Num() > 0)
	{
		SelectedActors->BeginBatchSelectOperation();
		GEditor->SelectNone(false, true, false);
		for (AActor* Actor : ActorsToSelect)
		{
			if (InternalActorUtilitiesSubsystemLibrary::IsEditorLevelActor(Actor))
			{
				if (!GEditor->CanSelectActor(Actor, true))
				{
					UE_LOG(LogUtils, Warning, TEXT("SetSelectedLevelActors. Can't select actor '%s'."), *Actor->GetName());
					continue;
				}
				GEditor->SelectActor(Actor, true, false);
			}
		}
		SelectedActors->EndBatchSelectOperation();
		GEditor->NoteSelectionChange();
	}
	else
	{
		GEditor->SelectNone(true, true, false);
	}
}

void UEditorActorSubsystem::ClearActorSelectionSet()
{
	GEditor->GetSelectedActors()->Modify();
	GEditor->GetSelectedActors()->DeselectAll();
	GEditor->NoteSelectionChange();
}

void UEditorActorSubsystem::SelectNothing()
{
	GEditor->GetSelectedActors()->Modify();
	GEditor->SelectNone(true, true, false);
}

void UEditorActorSubsystem::SetActorSelectionState(AActor* Actor, bool bShouldBeSelected)
{
	GEditor->GetSelectedActors()->Modify();
	GEditor->SelectActor(Actor, bShouldBeSelected, /*bNotify=*/ false);
	GEditor->NoteSelectionChange();
}

AActor* UEditorActorSubsystem::GetActorReference(FString PathToActor)
{
	return Cast<AActor>(StaticFindObject(AActor::StaticClass(), GEditor->GetEditorWorldContext().World(), *PathToActor, false));
}


AActor* UEditorActorSubsystem::SpawnActorFromObject(UObject* ObjToUse, FVector Location, FRotator Rotation, bool bTransient)
{
	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

	if (!EditorScriptingHelpers::CheckIfInEditorAndPIE())
	{
		return nullptr;
	}

	if (!ObjToUse)
	{
		UE_LOG(LogUtils, Error, TEXT("SpawnActorFromObject. ObjToUse is not valid."));
		return nullptr;
	}

	return InternalActorUtilitiesSubsystemLibrary::SpawnActor(TEXT("SpawnActorFromObject"), ObjToUse, Location, Rotation, bTransient);
}

AActor* UEditorActorSubsystem::SpawnActorFromClass(TSubclassOf<class AActor> ActorClass, FVector Location, FRotator Rotation, bool bTransient)
{
	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

	if (!EditorScriptingHelpers::CheckIfInEditorAndPIE())
	{
		return nullptr;
	}

	if (!ActorClass.Get())
	{
		FFrame::KismetExecutionMessage(TEXT("SpawnActorFromClass. ActorClass is not valid."), ELogVerbosity::Error);
		return nullptr;
	}

	return InternalActorUtilitiesSubsystemLibrary::SpawnActor(TEXT("SpawnActorFromClass"), ActorClass.Get(), Location, Rotation, bTransient);
}

bool UEditorActorSubsystem::DestroyActor(AActor* ActorToDestroy)
{
	TArray<AActor*> ActorsToDestroy;
	ActorsToDestroy.Add(ActorToDestroy);
	return DestroyActors(ActorsToDestroy);
}

bool UEditorActorSubsystem::DestroyActors(const TArray<AActor*>& ActorsToDestroy)
{
	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

	const FScopedTransaction Transaction(LOCTEXT("DeleteActors", "Delete Actors"));
	
	if (!EditorScriptingHelpers::CheckIfInEditorAndPIE())
	{
		return false;
	}

	for (AActor* ActorToDestroy : ActorsToDestroy)
	{
		if (!ActorToDestroy)
		{
			UE_LOG(LogUtils, Error, TEXT("DestroyActors. An actor to destroy is invalid."));
			return false;
		}
		if (!InternalActorUtilitiesSubsystemLibrary::IsEditorLevelActor(ActorToDestroy))
		{  
			UE_LOG(LogUtils, Error, TEXT("DestroyActors. An actor to destroy is not part of the world editor."));
			return false;
		}
	}

	UUnrealEditorSubsystem* UnrealEditorSubsystem = GEditor->GetEditorSubsystem<UUnrealEditorSubsystem>();

	if (!UnrealEditorSubsystem)
	{
		return false;
	}

	UWorld* World = UnrealEditorSubsystem->GetEditorWorld();
	if (!World)
	{
		UE_LOG(LogUtils, Error, TEXT("DestroyActors. Can't destroy actors because there is no world."));
		return false;
	}

	FEditorDelegates::OnDeleteActorsBegin.Broadcast();
	
	// Make sure these actors are no longer selected
	USelection* ActorSelection = GEditor->GetSelectedActors();
	if (UTypedElementSelectionSet* SelectionSet = ActorSelection->GetElementSelectionSet())
	{
		TArray<FTypedElementHandle> ActorHandles;
		ActorHandles.Reserve(ActorsToDestroy.Num());
		for (AActor* ActorToDestroy : ActorsToDestroy)
		{
			if (FTypedElementHandle ActorHandle = UEngineElementsLibrary::AcquireEditorActorElementHandle(ActorToDestroy, /*bAllowCreate*/false))
			{
				ActorHandles.Add(ActorHandle);
			}
		}
	
		const FTypedElementSelectionOptions SelectionOptions = FTypedElementSelectionOptions()
			.SetAllowHidden(true)
			.SetAllowGroups(false)
			.SetWarnIfLocked(false)
			.SetChildElementInclusionMethod(ETypedElementChildInclusionMethod::Recursive);

		SelectionSet->DeselectElements(ActorHandles, SelectionOptions);
	}

	ULayersSubsystem* Layers = GEditor->GetEditorSubsystem<ULayersSubsystem>();
	Layers->DisassociateActorsFromLayers(ActorsToDestroy);

	bool SuccessfullyDestroyedAll = true;
	
	for (AActor* ActorToDestroy : ActorsToDestroy)
	{
		if (!World->EditorDestroyActor(ActorToDestroy, true))
		{
			SuccessfullyDestroyedAll = false;
		}
	}

	FEditorDelegates::OnDeleteActorsEnd.Broadcast();
	
	return SuccessfullyDestroyedAll;
}

TArray<class AActor*> UEditorActorSubsystem::ConvertActors(const TArray<class AActor*>& Actors, TSubclassOf<class AActor> ActorClass, const FString& StaticMeshPackagePath)
{
	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

	TArray<class AActor*> Result;

	if (!EditorScriptingHelpers::CheckIfInEditorAndPIE())
	{
		return Result;
	}

	if (ActorClass.Get() == nullptr)
	{
		UE_LOG(LogUtils, Error, TEXT("ConvertActorWith. The ActorClass is not valid."));
		return Result;
	}

	UUnrealEditorSubsystem* UnrealEditorSubsystem = GEditor->GetEditorSubsystem<UUnrealEditorSubsystem>();

	if (!UnrealEditorSubsystem)
	{
		return Result;
	}

	FString PackagePath = StaticMeshPackagePath;
	if (!PackagePath.IsEmpty())
	{
		FString FailureReason;
		PackagePath = EditorScriptingHelpers::ConvertAnyPathToLongPackagePath(PackagePath, FailureReason);
		if (PackagePath.IsEmpty())
		{
			UE_LOG(LogUtils, Error, TEXT("ConvertActorWith. %s"), *FailureReason);
			return Result;
		}
	}

	TArray<class AActor*> ActorToConvert;
	ActorToConvert.Reserve(Actors.Num());
	for (AActor* Actor : Actors)
	{
		if (Actor == nullptr || !IsValidChecked(Actor))
		{
			continue;
		}

		UWorld* ActorWorld = Actor->GetWorld();
		if (ActorWorld == nullptr)
		{
			UE_LOG(LogUtils, Warning, TEXT("ConvertActorWith. %s is not in a world. The actor will be skipped."), *Actor->GetActorLabel());
			continue;
		}
		if (ActorWorld->WorldType != EWorldType::Editor)
		{
			UE_LOG(LogUtils, Warning, TEXT("ConvertActorWith. %s is not in an editor world. The actor will be skipped."), *Actor->GetActorLabel());
			continue;
		}

		ULevel* CurrentLevel = Actor->GetLevel();
		if (CurrentLevel == nullptr)
		{
			UE_LOG(LogUtils, Warning, TEXT("ConvertActorWith. %s must be in a valid level. The actor will be skipped."), *Actor->GetActorLabel());
			continue;
		}

		if (Cast<ABrush>(Actor) && PackagePath.Len() == 0)
		{
			UE_LOG(LogUtils, Warning, TEXT("ConvertActorWith. %s is a Brush and not package path was provided. The actor will be skipped."), *Actor->GetActorLabel());
			continue;
		}

		ActorToConvert.Add(Actor);
	}

	if (ActorToConvert.Num() != 0)
	{
		const bool bUseSpecialCases = false; // Don't use special cases, they are a bit too exhaustive and create dialog
		DoConvertActors(ActorToConvert, ActorClass.Get(), TSet<FString>(), bUseSpecialCases, StaticMeshPackagePath);
		Result.Reserve(GEditor->GetSelectedActorCount());
		for (auto Itt = GEditor->GetSelectedActorIterator(); Itt; ++Itt)
		{
			Result.Add(CastChecked<AActor>(*Itt));
		}
	}

	UE_LOG(LogUtils, Log, TEXT("ConvertActorWith. %d conversions occurred."), Result.Num());
	return Result;
}

namespace UE::EditorActorSubsystem::Private
{

/**
 * Internal helper function to copy component properties from one actor to another. Only copies properties
 * from components if the source actor, source actor class default object, and destination actor all contain
 * a component of the same name (specified by parameter) and all three of those components share a common base
 * class, at which point properties from the common base are copied. Component template names are used instead of
 * component classes because an actor could potentially have multiple components of the same class.
 *
 * @param	InSourceActor		Actor to copy component properties from
 * @param	InDestActor		Actor to copy component properties to
 * @param	InComponentNames	Set of component template names to attempt to copy
 */
void CopyActorComponentProperties(const AActor* InSourceActor, AActor* InDestActor, const TSet<FString>& InComponentNames)
{
	// Don't attempt to copy anything if the user didn't specify component names to copy
	if (InComponentNames.Num() > 0)
	{
		check(InSourceActor && InDestActor);
		const AActor* SrcActorDefaultActor = InSourceActor->GetClass()->GetDefaultObject<AActor>();
		check(SrcActorDefaultActor);

		// Construct a mapping from the default actor of its relevant component names to its actual components. Here
		// relevant component names are those that match a name provided as a parameter.
		TMap<FString, const UActorComponent*> NameToDefaultComponentMap;
		for (UActorComponent* CurComp : SrcActorDefaultActor->GetComponents())
		{
			if (CurComp)
			{
				FString CurCompName = CurComp->GetName();
				if (InComponentNames.Contains(CurCompName))
				{
					NameToDefaultComponentMap.Add(MoveTemp(CurCompName), CurComp);
				}
			}
		}

		// Construct a mapping from the source actor of its relevant component names to its actual components. Here
		// relevant component names are those that match a name provided as a parameter.
		TMap<FString, const UActorComponent*> NameToSourceComponentMap;
		for (UActorComponent* CurComp : InSourceActor->GetComponents())
		{
			if (CurComp)
			{
				FString CurCompName = CurComp->GetName();
				if (InComponentNames.Contains(CurCompName))
				{
					NameToSourceComponentMap.Add(MoveTemp(CurCompName), CurComp);
				}
			}
		}

		bool bCopiedAnyProperty = false;

		TInlineComponentArray<UActorComponent*> DestComponents;
		InDestActor->GetComponents(DestComponents);

		// Iterate through all of the destination actor's components to find the ones which should have properties copied into them.
		for (TInlineComponentArray<UActorComponent*>::TIterator DestCompIter(DestComponents); DestCompIter; ++DestCompIter)
		{
			UActorComponent* CurComp = *DestCompIter;
			check(CurComp);

			const FString CurCompName = CurComp->GetName();

			// Check if the component is one that the user wanted to copy properties into
			if (InComponentNames.Contains(CurCompName))
			{
				const UActorComponent** DefaultComponent = NameToDefaultComponentMap.Find(CurCompName);
				const UActorComponent** SourceComponent = NameToSourceComponentMap.Find(CurCompName);

				// Make sure that both the default actor and the source actor had a component of the same name
				if (DefaultComponent && SourceComponent)
				{
					const UClass* CommonBaseClass = nullptr;
					const UClass* DefaultCompClass = (*DefaultComponent)->GetClass();
					const UClass* SourceCompClass = (*SourceComponent)->GetClass();

					// Handle the unlikely case of the default component and the source actor component not being the exact same class by finding
					// the common base class across all three components (default, source, and destination)
					if (DefaultCompClass != SourceCompClass)
					{
						const UClass* CommonBaseClassWithDefault = CurComp->FindNearestCommonBaseClass(DefaultCompClass);
						const UClass* CommonBaseClassWithSource = CurComp->FindNearestCommonBaseClass(SourceCompClass);
						if (CommonBaseClassWithDefault && CommonBaseClassWithSource)
						{
							// If both components yielded the same common base, then that's the common base of all three
							if (CommonBaseClassWithDefault == CommonBaseClassWithSource)
							{
								CommonBaseClass = CommonBaseClassWithDefault;
							}
							// If not, find a common base across all three components
							else
							{
								CommonBaseClass = const_cast<UClass*>(CommonBaseClassWithDefault)
													  ->GetDefaultObject()
													  ->FindNearestCommonBaseClass(CommonBaseClassWithSource);
							}
						}
					}
					else
					{
						CommonBaseClass = CurComp->FindNearestCommonBaseClass(DefaultCompClass);
					}

					// If all three components have a base class in common, copy the properties from that base class
					// from the source actor component to the destination
					if (CommonBaseClass)
					{
						// Iterate through the properties, only copying those which are non-native, non-transient,
						// non-component, and not identical to the values in the default component
						for (FProperty* Property = CommonBaseClass->PropertyLink; Property != nullptr;
							 Property = Property->PropertyLinkNext)
						{
							const bool bIsTransient = !!(Property->PropertyFlags & CPF_Transient);
							const bool bIsIdentical = Property->Identical_InContainer(*SourceComponent, *DefaultComponent);
							const bool bIsComponent =
								!!(Property->PropertyFlags & (CPF_InstancedReference | CPF_ContainsInstancedReference));

							if (!bIsTransient && !bIsIdentical && !bIsComponent)
							{
								bCopiedAnyProperty = true;
								Property->CopyCompleteValue_InContainer(CurComp, *SourceComponent);
							}
						}
					}
				}
			}
		}

		// If any properties were copied at all, alert the actor to the changes
		if (bCopiedAnyProperty)
		{
			InDestActor->PostEditChange();
		}
	}
}

namespace ReattachActorsHelper
{
/** Holds the actor and socket name for attaching. */
struct FActorAttachmentInfo
{
	AActor* Actor;

	FName SocketName;
};

/** Used to cache the attachment info for an actor. */
struct FActorAttachmentCache
{
public:
	/** The post-conversion actor. */
	AActor* NewActor;

	/** The parent actor and socket. */
	FActorAttachmentInfo ParentActor;

	/** Children actors and the sockets they were attached to. */
	TArray<FActorAttachmentInfo> AttachedActors;
};

/**
 * Caches the attachment info for the actors being converted.
 *
 * @param InActorsToReattach			List of actors to reattach.
 * @param InOutAttachmentInfo			List of attachment info for the list of actors.
 */
void CacheAttachments(const TArray<AActor*>& InActorsToReattach, TArray<FActorAttachmentCache>& InOutAttachmentInfo)
{
	for (int32 ActorIdx = 0; ActorIdx < InActorsToReattach.Num(); ++ActorIdx)
	{
		AActor* ActorToReattach = InActorsToReattach[ActorIdx];

		InOutAttachmentInfo.AddZeroed();

		FActorAttachmentCache& CurrentAttachmentInfo = InOutAttachmentInfo[ActorIdx];

		// Retrieve the list of attached actors.
		TArray<AActor*> AttachedActors;
		ActorToReattach->GetAttachedActors(AttachedActors);

		// Cache the parent actor and socket name.
		CurrentAttachmentInfo.ParentActor.Actor = ActorToReattach->GetAttachParentActor();
		CurrentAttachmentInfo.ParentActor.SocketName = ActorToReattach->GetAttachParentSocketName();

		// Required to restore attachments properly.
		for (int32 AttachedActorIdx = 0; AttachedActorIdx < AttachedActors.Num(); ++AttachedActorIdx)
		{
			// Store the attached actor and socket name in the cache.
			CurrentAttachmentInfo.AttachedActors.AddZeroed();
			CurrentAttachmentInfo.AttachedActors[AttachedActorIdx].Actor = AttachedActors[AttachedActorIdx];
			CurrentAttachmentInfo.AttachedActors[AttachedActorIdx].SocketName =
				AttachedActors[AttachedActorIdx]->GetAttachParentSocketName();

			AActor* ChildActor = CurrentAttachmentInfo.AttachedActors[AttachedActorIdx].Actor;
			ChildActor->Modify();
			ChildActor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		}

		// Modify the actor so undo will reattach it.
		ActorToReattach->Modify();
		ActorToReattach->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	}
}

/**
 * Caches the actor old/new information, mapping the old actor to the new version for easy look-up and matching.
 *
 * @param InOldActor			The old version of the actor.
 * @param InNewActor			The new version of the actor.
 * @param InOutReattachmentMap	Map object for placing these in.
 * @param InOutAttachmentInfo	Update the required attachment info to hold the Converted Actor.
 */
void CacheActorConvert(
	AActor* InOldActor, AActor* InNewActor, TMap<AActor*, AActor*>& InOutReattachmentMap, FActorAttachmentCache& InOutAttachmentInfo
)
{
	// Add mapping data for the old actor to the new actor.
	InOutReattachmentMap.Add(InOldActor, InNewActor);

	// Set the converted actor so re-attachment can occur.
	InOutAttachmentInfo.NewActor = InNewActor;
}

/**
 * Checks if two actors can be attached, creates Message Log messages if there are issues.
 *
 * @param InParentActor			The parent actor.
 * @param InChildActor			The child actor.
 *
 * @return Returns true if the actors can be attached, false if they cannot.
 */
bool CanParentActors(AActor* InParentActor, AActor* InChildActor)
{
	FText ReasonText;
	if (GEditor->CanParentActors(InParentActor, InChildActor, &ReasonText))
	{
		return true;
	}
	else
	{
		FMessageLog("EditorErrors").Error(ReasonText);
		return false;
	}
}

/**
 * Reattaches actors to maintain the hierarchy they had previously using a conversion map and an array of attachment
 * info. All errors displayed in Message Log along with notifications.
 *
 * @param InReattachmentMap			Used to find the corresponding new versions of actors using an old actor pointer.
 * @param InAttachmentInfo			Holds parent and child attachment data.
 */
void ReattachActors(TMap<AActor*, AActor*>& InReattachmentMap, TArray<FActorAttachmentCache>& InAttachmentInfo)
{
	// Holds the errors for the message log.
	FMessageLog EditorErrors("EditorErrors");
	EditorErrors.NewPage(LOCTEXT("AttachmentLogPage", "Actor Reattachment"));

	for (int32 ActorIdx = 0; ActorIdx < InAttachmentInfo.Num(); ++ActorIdx)
	{
		FActorAttachmentCache& CurrentAttachment = InAttachmentInfo[ActorIdx];

		// Need to reattach all of the actors that were previously attached.
		for (int32 AttachedIdx = 0; AttachedIdx < CurrentAttachment.AttachedActors.Num(); ++AttachedIdx)
		{
			// Check if the attached actor was converted. If it was it will be in the TMap.
			AActor** CheckIfConverted = InReattachmentMap.Find(CurrentAttachment.AttachedActors[AttachedIdx].Actor);
			if (CheckIfConverted)
			{
				// This should always be valid.
				if (*CheckIfConverted)
				{
					AActor* ParentActor = CurrentAttachment.NewActor;
					AActor* ChildActor = *CheckIfConverted;

					if (CanParentActors(ParentActor, ChildActor))
					{
						// Attach the previously attached and newly converted actor to the current converted actor.
						ChildActor->AttachToActor(
							ParentActor,
							FAttachmentTransformRules::KeepWorldTransform,
							CurrentAttachment.AttachedActors[AttachedIdx].SocketName
						);
					}
				}
			}
			else
			{
				AActor* ParentActor = CurrentAttachment.NewActor;
				AActor* ChildActor = CurrentAttachment.AttachedActors[AttachedIdx].Actor;

				if (CanParentActors(ParentActor, ChildActor))
				{
					// Since the actor was not converted, reattach the unconverted actor.
					ChildActor->AttachToActor(
						ParentActor,
						FAttachmentTransformRules::KeepWorldTransform,
						CurrentAttachment.AttachedActors[AttachedIdx].SocketName
					);
				}
			}
		}

		// Check if the parent was converted.
		AActor** CheckIfNewActor = InReattachmentMap.Find(CurrentAttachment.ParentActor.Actor);
		if (CheckIfNewActor)
		{
			// Since the actor was converted, attach the current actor to it.
			if (*CheckIfNewActor)
			{
				AActor* ParentActor = *CheckIfNewActor;
				AActor* ChildActor = CurrentAttachment.NewActor;

				if (CanParentActors(ParentActor, ChildActor))
				{
					ChildActor->AttachToActor(
						ParentActor, FAttachmentTransformRules::KeepWorldTransform, CurrentAttachment.ParentActor.SocketName
					);
				}
			}
		}
		else
		{
			AActor* ParentActor = CurrentAttachment.ParentActor.Actor;
			AActor* ChildActor = CurrentAttachment.NewActor;

			// Verify the parent is valid, the actor may not have actually been attached before.
			if (ParentActor && CanParentActors(ParentActor, ChildActor))
			{
				// The parent was not converted, attach to the unconverted parent.
				ChildActor->AttachToActor(
					ParentActor, FAttachmentTransformRules::KeepWorldTransform, CurrentAttachment.ParentActor.SocketName
				);
			}
		}
	}

	// Add the errors to the message log, notifications will also be displayed as needed.
	EditorErrors.Notify(NSLOCTEXT("ActorAttachmentError", "AttachmentsFailed", "Attachments Failed!"));
}

} // namespace ReattachActorsHelper

namespace ConvertHelpers
{
struct TConvertData
{
	const TArray<AActor*> ActorsToConvert;
	UClass* ConvertToClass;
	const TSet<FString> ComponentsToConsider;
	bool bUseSpecialCases;

	TConvertData(
		const TArray<AActor*>& InActorsToConvert,
		UClass* InConvertToClass,
		const TSet<FString>& InComponentsToConsider,
		bool bInUseSpecialCases
	)
		: ActorsToConvert(InActorsToConvert)
		, ConvertToClass(InConvertToClass)
		, ComponentsToConsider(InComponentsToConsider)
		, bUseSpecialCases(bInUseSpecialCases)
	{
	}
};

void OnBrushToStaticMeshNameCommitted(const FString& InSettingsPackageName, TConvertData InConvertData)
{
	UEditorActorSubsystem::DoConvertActors(
		InConvertData.ActorsToConvert,
		InConvertData.ConvertToClass,
		InConvertData.ComponentsToConsider,
		InConvertData.bUseSpecialCases,
		InSettingsPackageName
	);
}

void GetBrushList(
	const TArray<AActor*>& InActorsToConvert, const UClass* InConvertToClass, TArray<ABrush*>& OutBrushList, int32& OutBrushIndexForReattachment
)
{
	for (int32 ActorIdx = 0; ActorIdx < InActorsToConvert.Num(); ++ActorIdx)
	{
		AActor* ActorToConvert = InActorsToConvert[ActorIdx];
		if (IsValidChecked(ActorToConvert) && ActorToConvert->GetClass()->IsChildOf(ABrush::StaticClass())
			&& InConvertToClass == AStaticMeshActor::StaticClass())
		{
			GEditor->SelectActor(ActorToConvert, true, true);
			OutBrushList.Add(Cast<ABrush>(ActorToConvert));

			// If this is a single brush conversion then this index will be used for re-attachment.
			OutBrushIndexForReattachment = ActorIdx;
		}
	}
}

} // namespace ConvertHelpers

} // namespace UE::EditorActorSubsystem::Private

/* Gets the common components of a specific type between two actors so that they may be copied.
 *
 * @param InOldActor		The actor to copy component properties from
 * @param InNewActor		The actor to copy to
 */
static void CopyLightComponentProperties(const AActor& InOldActor, const AActor& InNewActor)
{
	// Since this is only being used for lights, make sure only the light component can be copied.
	const UClass* CopyableComponentClass = ULightComponent::StaticClass();

	// Get the light component from the default actor of source actors class.
	// This is so we can avoid copying properties that have not changed.
	// using ULightComponent::StaticClass()->GetDefaultObject() will not work since each light actor sets default component properties differently.
	ALight* OldActorDefaultObject = InOldActor.GetClass()->GetDefaultObject<ALight>();
	check(OldActorDefaultObject);
	UActorComponent* DefaultLightComponent = OldActorDefaultObject->GetLightComponent();
	check(DefaultLightComponent);

	// The component we are copying from class
	UClass* CompToCopyClass = nullptr;
	UActorComponent* LightComponentToCopy = nullptr;

	// Go through the old actor's components and look for a light component to copy.
	for (UActorComponent* Component : InOldActor.GetComponents())
	{
		if (Component && Component->IsRegistered() && Component->IsA(CopyableComponentClass))
		{
			// A light component has been found.
			CompToCopyClass = Component->GetClass();
			LightComponentToCopy = Component;
			break;
		}
	}

	// The light component from the new actor
	UActorComponent* NewActorLightComponent = nullptr;
	// The class of the new actors light component
	const UClass* CommonLightComponentClass = nullptr;

	// Don't do anything if there is no valid light component to copy from
	if (LightComponentToCopy)
	{
		// Find a light component to overwrite in the new actor
		for (UActorComponent* Component : InNewActor.GetComponents())
		{
			if (Component && Component->IsRegistered())
			{
				// Find a common component class between the new and old actor.
				// This needs to be done so we can copy as many properties as possible.
				// For example: if we are converting from a point light to a spotlight, the point light component will
				// be the common superclass. That way we can copy properties like light radius, which would have been
				// impossible if we just took the base LightComponent as the common class.
				const UClass* CommonSuperclass = Component->FindNearestCommonBaseClass(CompToCopyClass);

				if (CommonSuperclass->IsChildOf(CopyableComponentClass))
				{
					NewActorLightComponent = Component;
					CommonLightComponentClass = CommonSuperclass;
				}
			}
		}
	}

	// Don't do anything if there is no valid light component to copy to
	if (NewActorLightComponent)
	{
		bool bCopiedAnyProperty = false;

		// Find and copy the lightmass settings directly as they need to be examined and copied individually and not by the entire light mass settings struct
		const FString LightmassPropertyName = TEXT("LightmassSettings");

		FProperty* PropertyToCopy = nullptr;
		for (FProperty* Property = CompToCopyClass->PropertyLink; Property != nullptr; Property = Property->PropertyLinkNext)
		{
			if (Property->GetName() == LightmassPropertyName)
			{
				// Get the offset in the old actor where lightmass properties are stored.
				PropertyToCopy = Property;
				break;
			}
		}

		if (PropertyToCopy != nullptr)
		{
			void* PropertyToCopyBaseLightComponentToCopy =
				PropertyToCopy->ContainerPtrToValuePtr<void>(LightComponentToCopy);
			void* PropertyToCopyBaseDefaultLightComponent =
				PropertyToCopy->ContainerPtrToValuePtr<void>(DefaultLightComponent);
			// Find the location of the lightmass settings in the new actor (if any)
			for (FProperty* NewProperty = NewActorLightComponent->GetClass()->PropertyLink; NewProperty != nullptr;
				 NewProperty = NewProperty->PropertyLinkNext)
			{
				if (NewProperty->GetName() == LightmassPropertyName)
				{
					FStructProperty* OldLightmassProperty = CastField<FStructProperty>(PropertyToCopy);
					FStructProperty* NewLightmassProperty = CastField<FStructProperty>(NewProperty);

					void* NewPropertyBaseNewActorLightComponent =
						NewProperty->ContainerPtrToValuePtr<void>(NewActorLightComponent);
					// The lightmass settings are a struct property so the cast should never fail.
					check(OldLightmassProperty);
					check(NewLightmassProperty);

					// Iterate through each property field in the lightmass settings struct that we are copying from...
					for (TFieldIterator<FProperty> OldIt(OldLightmassProperty->Struct); OldIt; ++OldIt)
					{
						FProperty* OldLightmassField = *OldIt;

						// And search for the same field in the lightmass settings struct we are copying to.
						// We should only copy to fields that exist in both structs.
						// Even though their offsets match the structs may be different depending on what type of light we are converting to
						bool bPropertyFieldFound = false;
						for (TFieldIterator<FProperty> NewIt(NewLightmassProperty->Struct); NewIt; ++NewIt)
						{
							FProperty* NewLightmassField = *NewIt;
							if (OldLightmassField->GetName() == NewLightmassField->GetName())
							{
								// The field is in both structs.  Ok to copy
								bool bIsIdentical = OldLightmassField->Identical_InContainer(
									PropertyToCopyBaseLightComponentToCopy, PropertyToCopyBaseDefaultLightComponent
								);
								if (!bIsIdentical)
								{
									// Copy if the value has changed
									OldLightmassField->CopySingleValue(
										NewLightmassField->ContainerPtrToValuePtr<void>(NewPropertyBaseNewActorLightComponent),
										OldLightmassField->ContainerPtrToValuePtr<void>(PropertyToCopyBaseLightComponentToCopy)
									);
									bCopiedAnyProperty = true;
								}
								break;
							}
						}
					}
					// No need to continue once we have found the lightmass settings
					break;
				}
			}
		}

		// Now Copy the light component properties.
		for (FProperty* Property = CommonLightComponentClass->PropertyLink; Property != nullptr;
			 Property = Property->PropertyLinkNext)
		{
			bool bIsTransient =
				!!(Property->PropertyFlags & (CPF_Transient | CPF_DuplicateTransient | CPF_NonPIEDuplicateTransient));
			// Properties are identical if they have not changed from the light component on the default source actor
			bool bIsIdentical = Property->Identical_InContainer(LightComponentToCopy, DefaultLightComponent);
			bool bIsComponent = !!(Property->PropertyFlags & (CPF_InstancedReference | CPF_ContainsInstancedReference));

			if (!bIsTransient && !bIsIdentical && !bIsComponent && Property->GetName() != LightmassPropertyName)
			{
				bCopiedAnyProperty = true;
				// Copy only if not native, not transient, not identical, not a component (at this time don't copy
				// components within components) Also dont copy lightmass settings, those were examined and taken above
				Property->CopyCompleteValue_InContainer(NewActorLightComponent, LightComponentToCopy);
			}
		}

		if (bCopiedAnyProperty)
		{
			NewActorLightComponent->PostEditChange();
		}
	}
}

void UEditorActorSubsystem::ConvertLightActors(UClass* InConvertToClass)
{
	// Provide the option to abort the conversion
	if (GEditor->ShouldAbortActorDeletion())
	{
		return;
	}

	// List of actors to convert
	TArray<AActor*> ActorsToConvert;

	// Get a list of valid actors to convert.
	for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
	{
		AActor* ActorToConvert = static_cast<AActor*>(*It);
		// Prevent non light actors from being converted
		// Also prevent light actors from being converted if they are the same time as the new class
		if (ActorToConvert->IsA(ALight::StaticClass()) && ActorToConvert->GetClass() != InConvertToClass)
		{
			ActorsToConvert.Add(ActorToConvert);
		}
	}

	if (ActorsToConvert.Num())
	{
		GEditor->GetSelectedActors()->BeginBatchSelectOperation();

		// Undo/Redo support
		const FScopedTransaction Transaction(NSLOCTEXT("UnrealEd", "ConvertLights", "Convert Light"));

		int32 NumLightsConverted = 0;

		// Convert each light
		ULayersSubsystem* LayersSubsystem = GEditor->GetEditorSubsystem<ULayersSubsystem>();
		for (int32 ActorIdx = 0; ActorIdx < ActorsToConvert.Num(); ++ActorIdx)
		{
			AActor* ActorToConvert = ActorsToConvert[ActorIdx];

			check(ActorToConvert);
			// The class of the actor we are about to replace
			UClass* ClassToReplace = ActorToConvert->GetClass();

			// Set the current level to the level where the convertible actor resides
			UWorld* World = ActorToConvert->GetWorld();
			check(World);
			ULevel* ActorLevel = ActorToConvert->GetLevel();
			checkSlow(ActorLevel != nullptr);

			// Find a common superclass between the actors so we know what properties to copy
			const UClass* CommonSuperclass = ActorToConvert->FindNearestCommonBaseClass(InConvertToClass);
			check(CommonSuperclass);

			// spawn the new actor
			AActor* NewActor = nullptr;

			// Take the old actors location always, not rotation.  If rotation was changed on the source actor, it will be copied below.
			const FVector SpawnLoc = ActorToConvert->GetActorLocation();
			FActorSpawnParameters SpawnInfo;
			SpawnInfo.OverrideLevel = ActorLevel;
			NewActor = World->SpawnActor(InConvertToClass, &SpawnLoc, nullptr, SpawnInfo);
			// The new actor must exist
			check(NewActor);

			// Copy common light component properties
			CopyLightComponentProperties(*ActorToConvert, *NewActor);

			// Select the new actor
			GEditor->SelectActor(ActorToConvert, false, true);

			NewActor->InvalidateLightingCache();
			NewActor->PostEditChange();
			NewActor->PostEditMove(true);
			NewActor->Modify();
			LayersSubsystem->InitializeNewActorLayers(NewActor);

			// We have converted another light.
			++NumLightsConverted;

			UE_LOG(LogUtils, Log, TEXT("Converted: %s to %s"), *ActorToConvert->GetName(), *NewActor->GetName() );

			// Destroy the old actor.
			LayersSubsystem->DisassociateActorFromLayers(ActorToConvert);
			World->EditorDestroyActor(ActorToConvert, true);

			if (!IsValidChecked(NewActor) || NewActor->IsUnreachable())
			{
				UE_LOG(LogUtils, Log, TEXT("Newly converted actor ('%s') is pending kill"), *NewActor->GetName());
			}
			GEditor->SelectActor(NewActor, true, true);
		}

		GEditor->GetSelectedActors()->EndBatchSelectOperation();
		GEditor->RedrawLevelEditingViewports();

		ULevel::LevelDirtiedEvent.Broadcast();
	}
}

AActor* UEditorActorSubsystem::ConvertBrushesToStaticMesh(
	const FString& InStaticMeshPackageName, TArray<ABrush*>& InBrushesToConvert, const FVector& InPivotLocation
)
{
	AActor* NewActor(nullptr);

	FName ObjName = *FPackageName::GetLongPackageAssetName(InStaticMeshPackageName);

	UPackage* Pkg = CreatePackage(*InStaticMeshPackageName);
	check(Pkg != nullptr);

	FVector Location(0.0f, 0.0f, 0.0f);
	FRotator Rotation(0.0f, 0.0f, 0.0f);
	for (int32 BrushesIdx = 0; BrushesIdx < InBrushesToConvert.Num(); ++BrushesIdx)
	{
		// Cache the location and rotation.
		Location = InBrushesToConvert[BrushesIdx]->GetActorLocation();
		Rotation = InBrushesToConvert[BrushesIdx]->GetActorRotation();

		// Leave the actor's rotation but move it to origin so the Static Mesh will generate correctly.
		InBrushesToConvert[BrushesIdx]->TeleportTo(Location - InPivotLocation, Rotation, false, true);
	}

	GEditor->RebuildModelFromBrushes(GEditor->ConversionTempModel, true, true);
	GEditor->bspBuildFPolys(GEditor->ConversionTempModel, true, 0);

	if (0 < GEditor->ConversionTempModel->Polys->Element.Num())
	{
		UStaticMesh* NewMesh = CreateStaticMeshFromBrush(Pkg, ObjName, nullptr, GEditor->ConversionTempModel);
		NewActor = FActorFactoryAssetProxy::AddActorForAsset(NewMesh);

		NewActor->Modify();

		NewActor->InvalidateLightingCache();
		NewActor->PostEditChange();
		NewActor->PostEditMove(true);
		NewActor->Modify();
		ULayersSubsystem* LayersSubsystem = GEditor->GetEditorSubsystem<ULayersSubsystem>();
		LayersSubsystem->InitializeNewActorLayers(NewActor);

		// Teleport the new actor to the old location but not the old rotation. The static mesh is built to the rotation already.
		NewActor->TeleportTo(InPivotLocation, FRotator(0.0f, 0.0f, 0.0f), false, true);

		// Destroy the old brushes.
		for (int32 BrushIdx = 0; BrushIdx < InBrushesToConvert.Num(); ++BrushIdx)
		{
			LayersSubsystem->DisassociateActorFromLayers(InBrushesToConvert[BrushIdx]);
			GWorld->EditorDestroyActor(InBrushesToConvert[BrushIdx], true);
		}

		// Notify the asset registry
		IAssetRegistry::GetChecked().AssetCreated(NewMesh);
	}

	GEditor->ConversionTempModel->EmptyModel(1, 1);
	GEditor->RebuildAlteredBSP();
	GEditor->RedrawLevelEditingViewports();

	return NewActor;
}

void UEditorActorSubsystem::DoConvertActors(
	const TArray<AActor*>& InActorsToConvert,
	UClass* InConvertToClass,
	const TSet<FString>& InComponentsToConsider,
	bool bInUseSpecialCases,
	const FString& InStaticMeshPackageName
)
{
	// Early out if actor deletion is currently forbidden
	if (GEditor->ShouldAbortActorDeletion())
	{
		return;
	}

	GWarn->BeginSlowTask(NSLOCTEXT("UnrealEd", "ConvertingActors", "Converting Actors"), true);

	// Scope the transaction - we need it to end BEFORE we finish the slow task we just started
	{
		const FScopedTransaction Transaction(NSLOCTEXT("EditorEngine", "ConvertActors", "Convert Actors"));

		GEditor->GetSelectedActors()->BeginBatchSelectOperation();

		TArray<AActor*> ConvertedActors;
		int32 NumActorsToConvert = InActorsToConvert.Num();

		// Cache for attachment info of all actors being converted.
		TArray<UE::EditorActorSubsystem::Private::ReattachActorsHelper::FActorAttachmentCache> AttachmentInfo;

		// Maps actors from old to new for quick look-up.
		TMap<AActor*, AActor*> ConvertedMap;

		GEditor->SelectNone(true, true);
		UE::EditorActorSubsystem::Private::ReattachActorsHelper::CacheAttachments(InActorsToConvert, AttachmentInfo);

		// List of brushes being converted.
		TArray<ABrush*> BrushList;

		// The index of a brush, utilized for re-attachment purposes when a single brush is being converted.
		int32 BrushIndexForReattachment = 0;

		FVector CachePivotLocation = GEditor->GetPivotLocation();
		UE::EditorActorSubsystem::Private::ConvertHelpers::GetBrushList(
			InActorsToConvert, InConvertToClass, BrushList, BrushIndexForReattachment
		);

		if (BrushList.Num())
		{
			if (UEditorActorSubsystem* EditorActorSubsystem = GEditor->GetEditorSubsystem<UEditorActorSubsystem>())
			{
				AActor* ConvertedBrushActor = EditorActorSubsystem->ConvertBrushesToStaticMesh(
					InStaticMeshPackageName, BrushList, CachePivotLocation
				);
				ConvertedActors.Add(ConvertedBrushActor);

				// If only one brush is being converted, reattach it to whatever it was attached to before.
				// Multiple brushes become impossible to reattach due to the single actor returned.
				if (BrushList.Num() == 1)
				{
					UE::EditorActorSubsystem::Private::ReattachActorsHelper::CacheActorConvert(
						BrushList[0], ConvertedBrushActor, ConvertedMap, AttachmentInfo[BrushIndexForReattachment]
					);
				}
			}
		}

		ULayersSubsystem* LayersSubsystem = GEditor->GetEditorSubsystem<ULayersSubsystem>();
		for (int32 ActorIdx = 0; ActorIdx < InActorsToConvert.Num(); ++ActorIdx)
		{
			AActor* ActorToConvert = InActorsToConvert[ActorIdx];

			if (ActorToConvert->GetClass()->IsChildOf(ABrush::StaticClass())
				&& InConvertToClass == AStaticMeshActor::StaticClass())
			{
				// We already converted this actor in ConvertBrushesToStaticMesh above, and it has been marked as
				// pending kill (and hence is invalid) TODO: It would be good to refactor this function so there is a
				// single place where conversion happens
				ensure(!IsValid(ActorToConvert));
				continue;
			}

			if (!IsValidChecked(ActorToConvert))
			{
				UE_LOG(LogUtils, Error, TEXT("Actor '%s' is invalid and cannot be converted"), *ActorToConvert->GetFullName());
				continue;
			}

			// Source actor display label
			FString ActorLabel = ActorToConvert->GetActorLabel();

			// The class of the actor we are about to replace
			UClass* ClassToReplace = ActorToConvert->GetClass();

			AActor* NewActor = nullptr;

			ABrush* Brush = Cast<ABrush>(ActorToConvert);
			if ((Brush && FActorEditorUtils::IsABuilderBrush(Brush))
				|| (ClassToReplace->IsChildOf(ABrush::StaticClass()) && InConvertToClass == AStaticMeshActor::StaticClass()))
			{
				continue;
			}

			if (bInUseSpecialCases)
			{
				// Disable grouping temporarily as the following code assumes only one actor will be selected at any given time
				const bool bGroupingActiveSaved = UActorGroupingUtils::IsGroupingActive();

				UActorGroupingUtils::SetGroupingActive(false);

				GEditor->SelectNone(true, true);
				GEditor->SelectActor(ActorToConvert, true, true);

				// Each of the following 'special case' conversions will convert ActorToConvert to ConvertToClass if
				// possible. If it does it will mark the original for delete and select the new actor
				if (ClassToReplace->IsChildOf(ALight::StaticClass()))
				{
					UE_LOG(LogUtils, Log, TEXT("Converting light from %s to %s"), *ActorToConvert->GetFullName(), *InConvertToClass->GetName());
					ConvertLightActors(InConvertToClass);
				}
				else if (ClassToReplace->IsChildOf(ABrush::StaticClass()) && InConvertToClass->IsChildOf(AVolume::StaticClass()))
				{
					UE_LOG(LogUtils, Log, TEXT("Converting brush from %s to %s"), *ActorToConvert->GetFullName(), *InConvertToClass->GetName());
					ConvertSelectedBrushesToVolumes(InConvertToClass);
				}
				else
				{
					UE_LOG(LogUtils, Log, TEXT("Converting actor from %s to %s"), *ActorToConvert->GetFullName(), *InConvertToClass->GetName());
					ConvertActorsFromClass(ClassToReplace, InConvertToClass);
				}

				if (!IsValidChecked(ActorToConvert))
				{
					// Converted by one of the above
					check(1 == GEditor->GetSelectedActorCount());
					NewActor = Cast<AActor>(GEditor->GetSelectedActors()->GetSelectedObject(0));
					if (ensureMsgf(
							NewActor,
							TEXT("Actor conversion of %s to %s failed"),
							*ActorToConvert->GetFullName(),
							*InConvertToClass->GetName()
						))
					{
						// Caches information for finding the new actor using the pre-converted actor.
						UE::EditorActorSubsystem::Private::ReattachActorsHelper::CacheActorConvert(
							ActorToConvert, NewActor, ConvertedMap, AttachmentInfo[ActorIdx]
						);
					}
				}
				else
				{
					// Failed to convert, make sure the actor is unselected
					GEditor->SelectActor(ActorToConvert, false, true);
				}

				// Restore previous grouping setting
				UActorGroupingUtils::SetGroupingActive(bGroupingActiveSaved);
			}

			// Attempt normal spawning if a new actor hasn't been spawned yet via a special case
			if (!NewActor)
			{
				// Set the current level to the level where the convertible actor resides
				check(ActorToConvert);
				UWorld* World = ActorToConvert->GetWorld();
				ULevel* ActorLevel = ActorToConvert->GetLevel();
				check(World);
				checkSlow(ActorLevel);
				// Find a common base class between the actors so we know what properties to copy
				const UClass* CommonBaseClass = ActorToConvert->FindNearestCommonBaseClass(InConvertToClass);
				check(CommonBaseClass);

				const FTransform& SpawnTransform = ActorToConvert->GetActorTransform();
				{
					FActorSpawnParameters SpawnInfo;
					SpawnInfo.OverrideLevel = ActorLevel;
					SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
					SpawnInfo.bDeferConstruction = true;
					NewActor = World->SpawnActor(InConvertToClass, &SpawnTransform, SpawnInfo);

					if (NewActor)
					{
						// Deferred spawning and finishing with !bIsDefaultTransform results in scale being applied for
						// both native and simple construction script created root components
						constexpr bool bIsDefaultTransform = false;
						NewActor->FinishSpawning(SpawnTransform, bIsDefaultTransform);

						// Copy non component properties from the old actor to the new actor
						for (FProperty* Property = CommonBaseClass->PropertyLink; Property != nullptr;
							 Property = Property->PropertyLinkNext)
						{
							const bool bIsTransient = !!(Property->PropertyFlags & CPF_Transient);
							const bool bIsComponentProp =
								!!(Property->PropertyFlags & (CPF_InstancedReference | CPF_ContainsInstancedReference));
							const bool bIsIdentical =
								Property->Identical_InContainer(ActorToConvert, ClassToReplace->GetDefaultObject());

							if (!bIsTransient && !bIsIdentical && !bIsComponentProp && Property->GetName() != TEXT("Tag"))
							{
								// Copy only if not native, not transient, not identical, and not a component.
								// Copying components directly here is a bad idea because the next garbage collection will delete the component since we are deleting its outer.

								// Also do not copy the old actors tag.  That will always come up as not identical since
								// the default actor's Tag is "None" and SpawnActor uses the actor's class name The tag will be examined for changes later.
								Property->CopyCompleteValue_InContainer(NewActor, ActorToConvert);
							}
						}

						// Copy properties from actor components
						UE::EditorActorSubsystem::Private::CopyActorComponentProperties(
							ActorToConvert, NewActor, InComponentsToConsider
						);

						// Caches information for finding the new actor using the pre-converted actor.
						UE::EditorActorSubsystem::Private::ReattachActorsHelper::CacheActorConvert(
							ActorToConvert, NewActor, ConvertedMap, AttachmentInfo[ActorIdx]
						);

						NewActor->Modify();
						NewActor->InvalidateLightingCache();
						NewActor->PostEditChange();
						NewActor->PostEditMove(true);
						LayersSubsystem->InitializeNewActorLayers(NewActor);

						// Destroy the old actor.
						ActorToConvert->Modify();
						LayersSubsystem->DisassociateActorFromLayers(ActorToConvert);
						World->EditorDestroyActor(ActorToConvert, true);
					}
				}
			}

			if (NewActor)
			{
				// If the actor label isn't actually anything custom allow the name to be changed
				// to avoid cases like converting PointLight->SpotLight still being called PointLight after conversion
				FString ClassName = ClassToReplace->GetName();

				// Remove any number off the end of the label
				int32 Number = 0;
				if (!ActorLabel.StartsWith(ClassName) || !FParse::Value(*ActorLabel, *ClassName, Number))
				{
					NewActor->SetActorLabel(ActorLabel);
				}

				ConvertedActors.Add(NewActor);

				UE_LOG(LogUtils, Log, TEXT("Converted: %s to %s"), *ActorLabel, *NewActor->GetActorLabel() );

				FFormatNamedArguments Args;
				Args.Add(TEXT("OldActorName"), FText::FromString(ActorLabel));
				Args.Add(TEXT("NewActorName"), FText::FromString(NewActor->GetActorLabel()));
				const FText StatusUpdate = FText::Format(
					LOCTEXT("ConvertActorsTaskStatusUpdateMessageFormat", "Converted: {OldActorName} to {NewActorName}"), Args
				);

				GWarn->StatusUpdate(ConvertedActors.Num(), NumActorsToConvert, StatusUpdate);
			}
		}

		// Reattaches actors based on their previous parent child relationship.
		UE::EditorActorSubsystem::Private::ReattachActorsHelper::ReattachActors(ConvertedMap, AttachmentInfo);

		// Select the new actors
		GEditor->SelectNone(false, true);
		for (TArray<AActor*>::TConstIterator it(ConvertedActors); it; ++it)
		{
			GEditor->SelectActor(*it, true, true);
		}

		GEditor->GetSelectedActors()->EndBatchSelectOperation();

		GEditor->RedrawLevelEditingViewports();

		ULevel::LevelDirtiedEvent.Broadcast();

		// Clean up
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	}
	// End the slow task
	GWarn->EndSlowTask();
}

void UEditorActorSubsystem::ConvertActors(
	const TArray<AActor*>& InActorsToConvert, UClass* InConvertToClass, const TSet<FString>& InComponentsToConsider, bool bInUseSpecialCases
)
{
	// Early out if actor deletion is currently forbidden
	if (GEditor->ShouldAbortActorDeletion())
	{
		return;
	}

	GEditor->SelectNone(true, true);

	// List of brushes being converted.
	TArray<ABrush*> BrushList;
	int32 BrushIndexForReattachment;
	UE::EditorActorSubsystem::Private::ConvertHelpers::GetBrushList(
		InActorsToConvert, InConvertToClass, BrushList, BrushIndexForReattachment
	);

	if (BrushList.Num())
	{
		UE::EditorActorSubsystem::Private::ConvertHelpers::TConvertData ConvertData(
			InActorsToConvert, InConvertToClass, InComponentsToConsider, bInUseSpecialCases
		);

		TSharedPtr<SWindow> CreateAssetFromActorWindow =
			SNew(SWindow)
				.Title(LOCTEXT("SelectPath", "Select Path"))
				.ToolTipText(LOCTEXT("SelectPathTooltip", "Select the path where the static mesh will be created"))
				.ClientSize(FVector2D(400, 400));

		TSharedPtr<SCreateAssetFromObject> CreateAssetFromActorWidget;
		CreateAssetFromActorWindow->SetContent(
			SAssignNew(CreateAssetFromActorWidget, SCreateAssetFromObject, CreateAssetFromActorWindow)
				.AssetFilenameSuffix(TEXT("StaticMesh"))
				.HeadingText(LOCTEXT("ConvertBrushesToStaticMesh_Heading", "Static Mesh Name:"))
				.CreateButtonText(LOCTEXT("ConvertBrushesToStaticMesh_ButtonLabel", "Create Static Mesh"))
				.OnCreateAssetAction(FOnPathChosen::CreateStatic(
					UE::EditorActorSubsystem::Private::ConvertHelpers::OnBrushToStaticMeshNameCommitted, ConvertData
				))
		);

		TSharedPtr<SWindow> RootWindow = FGlobalTabmanager::Get()->GetRootWindow();
		if (RootWindow.IsValid())
		{
			FSlateApplication::Get().AddWindowAsNativeChild(
				CreateAssetFromActorWindow.ToSharedRef(), RootWindow.ToSharedRef()
			);
		}
		else
		{
			FSlateApplication::Get().AddWindow(CreateAssetFromActorWindow.ToSharedRef());
		}
	}
	else
	{
		DoConvertActors(InActorsToConvert, InConvertToClass, InComponentsToConsider, bInUseSpecialCases, TEXT(""));
	}
}

void UEditorActorSubsystem::ConvertSelectedBrushesToVolumes(UClass* InVolumeClass)
{
	TArray<ABrush*> BrushesToConvert;
	for (FSelectionIterator SelectedActorIter(GEditor->GetSelectedActorIterator()); SelectedActorIter; ++SelectedActorIter)
	{
		AActor* CurSelectedActor = Cast<AActor>(*SelectedActorIter);
		check(CurSelectedActor);
		ABrush* Brush = Cast<ABrush>(CurSelectedActor);
		if (Brush && !FActorEditorUtils::IsABuilderBrush(CurSelectedActor))
		{
			ABrush* CurBrushActor = CastChecked<ABrush>(CurSelectedActor);

			BrushesToConvert.Add(CurBrushActor);
		}
	}

	if (BrushesToConvert.Num())
	{
		GEditor->GetSelectedActors()->BeginBatchSelectOperation();

		checkSlow(InVolumeClass && InVolumeClass->IsChildOf(AVolume::StaticClass()));

		const FScopedTransaction Transaction(FText::Format(
			NSLOCTEXT("UnrealEd", "Transaction_ConvertToVolume", "Convert to Volume: {0}"),
			FText::FromString(InVolumeClass->GetName())
		));

		TArray<UWorld*> WorldsAffected;
		TArray<ULevel*> LevelsAffected;
		// Iterate over all selected actors, converting the brushes to volumes of the provided class
		for (int32 BrushIdx = 0; BrushIdx < BrushesToConvert.Num(); BrushIdx++)
		{
			ABrush* CurBrushActor = BrushesToConvert[BrushIdx];
			check(CurBrushActor);

			ULevel* CurActorLevel = CurBrushActor->GetLevel();
			check(CurActorLevel);
			LevelsAffected.AddUnique(CurActorLevel);

			// Cache the world and store in a list.
			UWorld* World = CurBrushActor->GetWorld();
			check(World);
			WorldsAffected.AddUnique(World);

			FActorSpawnParameters SpawnInfo;
			SpawnInfo.OverrideLevel = CurActorLevel;
			ABrush* NewVolume = World->SpawnActor<ABrush>(InVolumeClass, CurBrushActor->GetActorTransform(), SpawnInfo);
			if (NewVolume)
			{
				NewVolume->PreEditChange(nullptr);

				FBSPOps::csgCopyBrush(NewVolume, CurBrushActor, 0, RF_Transactional, true, true);

				// Set the texture on all polys to nullptr.  This stops invisible texture
				// dependencies from being formed on volumes.
				if (NewVolume->Brush)
				{
					for (TArray<FPoly>::TIterator PolyIter(NewVolume->Brush->Polys->Element); PolyIter; ++PolyIter)
					{
						FPoly& CurPoly = *PolyIter;
						CurPoly.Material = nullptr;
					}
				}

				// Select the new actor
				GEditor->SelectActor(CurBrushActor, false, true);
				GEditor->SelectActor(NewVolume, true, true);

				NewVolume->PostEditChange();
				NewVolume->PostEditMove(true);
				NewVolume->Modify(false);

				// Make the actor visible as the brush is hidden by default
				NewVolume->SetActorHiddenInGame(false);

				// Destroy the old actor.
				GEditor->GetEditorSubsystem<ULayersSubsystem>()->DisassociateActorFromLayers(CurBrushActor);
				World->EditorDestroyActor(CurBrushActor, true);
			}
		}

		GEditor->GetSelectedActors()->EndBatchSelectOperation();
		GEditor->RedrawLevelEditingViewports();

		// Broadcast a message that the levels in these worlds have changed
		for (UWorld* ChangedWorld : WorldsAffected)
		{
			ChangedWorld->BroadcastLevelsChanged();
		}

		// Rebuild BSP for any levels affected
		for (ULevel* ChangedLevel : LevelsAffected)
		{
			GEditor->RebuildLevel(*ChangedLevel);
		}
	}
}

/** Utility for copying properties that differ from defaults between mesh types. */
struct FConvertStaticMeshActorInfo
{
	/** The level the source actor belonged to, and into which the new actor is created. */
	ULevel* SourceLevel;

	// Actor properties.
	FVector Location;
	FRotator Rotation;
	FVector DrawScale3D;
	bool bHidden;
	AActor* Base;
	UPrimitiveComponent* BaseComponent;
	// End actor properties.

	/**
	 * Used to indicate if any of the above properties differ from defaults; if so, they're copied over.
	 * We don't want to simply copy all properties, because classes with different defaults will have
	 * their defaults hosed by other types.
	 */
	bool bActorPropsDifferFromDefaults[14];

	// Component properties.
	UStaticMesh* StaticMesh;
	USkeletalMesh* SkeletalMesh;
	TArray<UMaterialInterface*> OverrideMaterials;
	TArray<FGuid> IrrelevantLights;
	float CachedMaxDrawDistance;
	bool CastShadow;

	FBodyInstance BodyInstance;
	TArray<TArray<FColor>> OverrideVertexColors;

	// for skeletalmeshcomponent animation conversion
	// this is temporary until we have SkeletalMeshComponent.Animations
	UAnimationAsset* AnimAsset;
	bool bLooping;
	bool bPlaying;
	float Rate;
	float CurrentPos;

	// End component properties.

	/**
	 * Used to indicate if any of the above properties differ from defaults; if so, they're copied over.
	 * We don't want to simply copy all properties, because classes with different defaults will have
	 * their defaults hosed by other types.
	 */
	bool bComponentPropsDifferFromDefaults[7];

	AGroupActor* ActorGroup;

	static bool PropsDiffer(const TCHAR* InPropertyPath, const UObject* InObj)
	{
		const FProperty* PartsProp = FindFProperty<FProperty>(InPropertyPath);
		check(PartsProp);

		uint8* ClassDefaults = (uint8*)InObj->GetClass()->GetDefaultObject();
		check(ClassDefaults);

		for (int32 Index = 0; Index < PartsProp->ArrayDim; Index++)
		{
			const bool bMatches = PartsProp->Identical_InContainer(InObj, ClassDefaults, Index);
			if (!bMatches)
			{
				return true;
			}
		}
		return false;
	}

	void GetFromActor(AActor* InActor, UStaticMeshComponent* InMeshComp)
	{
		InternalGetFromActor(InActor);

		// Copy over component properties.
		StaticMesh = InMeshComp->GetStaticMesh();
		OverrideMaterials = InMeshComp->OverrideMaterials;
		CachedMaxDrawDistance = InMeshComp->CachedMaxDrawDistance;
		CastShadow = InMeshComp->CastShadow;

		BodyInstance.CopyBodyInstancePropertiesFrom(&InMeshComp->BodyInstance);

		// Loop over each LODInfo in the static mesh component, storing the override vertex colors
		// in each, if any
		bool bHasAnyVertexOverrideColors = false;
		for (int32 LODIndex = 0; LODIndex < InMeshComp->LODData.Num(); ++LODIndex)
		{
			const FStaticMeshComponentLODInfo& CurLODInfo = InMeshComp->LODData[LODIndex];
			const FColorVertexBuffer* CurVertexBuffer = CurLODInfo.OverrideVertexColors;

			OverrideVertexColors.Add(TArray<FColor>());

			// If the LODInfo has override vertex colors, store off each one
			if (CurVertexBuffer && CurVertexBuffer->GetNumVertices() > 0)
			{
				for (uint32 VertexIndex = 0; VertexIndex < CurVertexBuffer->GetNumVertices(); ++VertexIndex)
				{
					OverrideVertexColors[LODIndex].Add(CurVertexBuffer->VertexColor(VertexIndex));
				}
				bHasAnyVertexOverrideColors = true;
			}
		}

		// Record which component properties differ from their defaults.
		bComponentPropsDifferFromDefaults[0] = PropsDiffer(TEXT("Engine.StaticMeshComponent:StaticMesh"), InMeshComp);
		bComponentPropsDifferFromDefaults[1] = true; // Assume the materials array always differs.
		bComponentPropsDifferFromDefaults[2] =
			PropsDiffer(TEXT("Engine.PrimitiveComponent:CachedMaxDrawDistance"), InMeshComp);
		bComponentPropsDifferFromDefaults[3] = PropsDiffer(TEXT("Engine.PrimitiveComponent:CastShadow"), InMeshComp);
		bComponentPropsDifferFromDefaults[4] = PropsDiffer(TEXT("Engine.PrimitiveComponent:BodyInstance"), InMeshComp);
		bComponentPropsDifferFromDefaults[5] =
			bHasAnyVertexOverrideColors; // Differs from default if there are any vertex override colors
	}

	void SetToActor(AActor* InActor, UStaticMeshComponent* InMeshComp)
	{
		InternalSetToActor(InActor);

		// Set component properties.
		if (bComponentPropsDifferFromDefaults[0])
		{
			InMeshComp->SetStaticMesh(StaticMesh);
		}
		if (bComponentPropsDifferFromDefaults[1])
		{
			InMeshComp->OverrideMaterials = OverrideMaterials;
		}
		if (bComponentPropsDifferFromDefaults[2])
		{
			InMeshComp->CachedMaxDrawDistance = CachedMaxDrawDistance;
		}
		if (bComponentPropsDifferFromDefaults[3])
		{
			InMeshComp->CastShadow = CastShadow;
		}
		if (bComponentPropsDifferFromDefaults[4])
		{
			InMeshComp->BodyInstance.CopyBodyInstancePropertiesFrom(&BodyInstance);
		}
		if (bComponentPropsDifferFromDefaults[5])
		{
			// Ensure the LODInfo has the right number of entries
			InMeshComp->SetLODDataCount(OverrideVertexColors.Num(), InMeshComp->GetStaticMesh()->GetNumLODs());

			// Loop over each LODInfo to see if there are any vertex override colors to restore
			for (int32 LODIndex = 0; LODIndex < InMeshComp->LODData.Num(); ++LODIndex)
			{
				FStaticMeshComponentLODInfo& CurLODInfo = InMeshComp->LODData[LODIndex];

				// If there are override vertex colors specified for a particular LOD, set them in the LODInfo
				if (OverrideVertexColors.IsValidIndex(LODIndex) && OverrideVertexColors[LODIndex].Num() > 0)
				{
					const TArray<FColor>& OverrideColors = OverrideVertexColors[LODIndex];

					// Destroy the pre-existing override vertex buffer if it's not the same size as the override colors to be restored
					if (CurLODInfo.OverrideVertexColors
						&& CurLODInfo.OverrideVertexColors->GetNumVertices() != OverrideColors.Num())
					{
						CurLODInfo.ReleaseOverrideVertexColorsAndBlock();
					}

					// If there is a pre-existing color vertex buffer that is valid, release the render thread's hold on
					// it and modify it with the saved off colors
					if (CurLODInfo.OverrideVertexColors)
					{
						CurLODInfo.BeginReleaseOverrideVertexColors();
						FlushRenderingCommands();
						for (int32 VertexIndex = 0; VertexIndex < OverrideColors.Num(); ++VertexIndex)
						{
							CurLODInfo.OverrideVertexColors->VertexColor(VertexIndex) = OverrideColors[VertexIndex];
						}
					}

					// If there isn't a pre-existing color vertex buffer, create one and initialize it with the saved off colors
					else
					{
						CurLODInfo.OverrideVertexColors = new FColorVertexBuffer();
						CurLODInfo.OverrideVertexColors->InitFromColorArray(OverrideColors);
					}
					BeginInitResource(CurLODInfo.OverrideVertexColors);
				}
			}
		}
	}

	void GetFromActor(AActor* InActor, USkeletalMeshComponent* InMeshComp)
	{
		InternalGetFromActor(InActor);

		// Copy over component properties.
		SkeletalMesh = InMeshComp->GetSkeletalMeshAsset();
		OverrideMaterials = InMeshComp->OverrideMaterials;
		CachedMaxDrawDistance = InMeshComp->CachedMaxDrawDistance;
		CastShadow = InMeshComp->CastShadow;

		BodyInstance.CopyBodyInstancePropertiesFrom(&InMeshComp->BodyInstance);

		// Record which component properties differ from their defaults.
		bComponentPropsDifferFromDefaults[0] = PropsDiffer(TEXT("Engine.SkinnedMeshComponent:SkeletalMesh"), InMeshComp);
		bComponentPropsDifferFromDefaults[1] = true; // Assume the materials array always differs.
		bComponentPropsDifferFromDefaults[2] =
			PropsDiffer(TEXT("Engine.PrimitiveComponent:CachedMaxDrawDistance"), InMeshComp);
		bComponentPropsDifferFromDefaults[3] = PropsDiffer(TEXT("Engine.PrimitiveComponent:CastShadow"), InMeshComp);
		bComponentPropsDifferFromDefaults[4] = PropsDiffer(TEXT("Engine.PrimitiveComponent:BodyInstance"), InMeshComp);
		bComponentPropsDifferFromDefaults[5] = false; // Differs from default if there are any vertex override colors

		InternalGetAnimationData(InMeshComp);
	}

	void SetToActor(AActor* InActor, USkeletalMeshComponent* InMeshComp)
	{
		InternalSetToActor(InActor);

		// Set component properties.
		if (bComponentPropsDifferFromDefaults[0])
		{
			InMeshComp->SetSkeletalMeshAsset(SkeletalMesh);
		}
		if (bComponentPropsDifferFromDefaults[1])
		{
			InMeshComp->OverrideMaterials = OverrideMaterials;
		}
		if (bComponentPropsDifferFromDefaults[2])
		{
			InMeshComp->CachedMaxDrawDistance = CachedMaxDrawDistance;
		}
		if (bComponentPropsDifferFromDefaults[3])
		{
			InMeshComp->CastShadow = CastShadow;
		}
		if (bComponentPropsDifferFromDefaults[4])
		{
			InMeshComp->BodyInstance.CopyBodyInstancePropertiesFrom(&BodyInstance);
		}

		InternalSetAnimationData(InMeshComp);
	}

private:
	void InternalGetFromActor(const AActor* InActor)
	{
		SourceLevel = InActor->GetLevel();

		// Copy over actor properties.
		Location = InActor->GetActorLocation();
		Rotation = InActor->GetActorRotation();
		DrawScale3D = InActor->GetRootComponent() ? InActor->GetRootComponent()->GetRelativeScale3D()
												  : FVector(1.f, 1.f, 1.f);
		bHidden = InActor->IsHidden();

		// Record which actor properties differ from their defaults.
		// we don't have properties for location, rotation, scale3D, so copy all the time.
		bActorPropsDifferFromDefaults[0] = true;
		bActorPropsDifferFromDefaults[1] = true;
		bActorPropsDifferFromDefaults[2] = false;
		bActorPropsDifferFromDefaults[4] = true;
		bActorPropsDifferFromDefaults[5] = PropsDiffer(TEXT("Engine.Actor:bHidden"), InActor);
		bActorPropsDifferFromDefaults[7] = false;
		// used to point to Engine.Actor.bPathColliding
		bActorPropsDifferFromDefaults[9] = false;
	}

	void InternalSetToActor(AActor* InActor) const
	{
		if (InActor->GetLevel() != SourceLevel)
		{
			UE_LOG(LogUtils, Fatal, TEXT("Actor was converted into a different level."));
		}

		// Set actor properties.
		if (bActorPropsDifferFromDefaults[0])
		{
			InActor->SetActorLocation(Location, false);
		}
		if (bActorPropsDifferFromDefaults[1])
		{
			InActor->SetActorRotation(Rotation);
		}
		if (bActorPropsDifferFromDefaults[4])
		{
			if (InActor->GetRootComponent() != nullptr)
			{
				InActor->GetRootComponent()->SetRelativeScale3D(DrawScale3D);
			}
		}
		if (bActorPropsDifferFromDefaults[5])
		{
			InActor->SetHidden(bHidden);
		}
	}

	void InternalGetAnimationData(const USkeletalMeshComponent* InSkeletalComp)
	{
		AnimAsset = InSkeletalComp->AnimationData.AnimToPlay;
		bLooping = InSkeletalComp->AnimationData.bSavedLooping;
		bPlaying = InSkeletalComp->AnimationData.bSavedPlaying;
		Rate = InSkeletalComp->AnimationData.SavedPlayRate;
		CurrentPos = InSkeletalComp->AnimationData.SavedPosition;
	}

	void InternalSetAnimationData(USkeletalMeshComponent* InSkeletalComp)
	{
		if (!AnimAsset)
		{
			return;
		}

		UE_LOG(LogAnimation, Log, TEXT("Converting animation data for AnimAsset : (%s), bLooping(%d), bPlaying(%d), Rate(%0.2f), CurrentPos(%0.2f)"), 
			*AnimAsset->GetName(), bLooping, bPlaying, Rate, CurrentPos);

		InSkeletalComp->AnimationData.AnimToPlay = AnimAsset;
		InSkeletalComp->AnimationData.bSavedLooping = bLooping;
		InSkeletalComp->AnimationData.bSavedPlaying = bPlaying;
		InSkeletalComp->AnimationData.SavedPlayRate = Rate;
		InSkeletalComp->AnimationData.SavedPosition = CurrentPos;
		// we don't convert back to SkeletalMeshComponent.Animations - that will be gone soon
	}
};

void UEditorActorSubsystem::ConvertActorsFromClass(const UClass* InFromClass, UClass* InToClass)
{
	const bool bFromInteractiveFoliage = InFromClass == AInteractiveFoliageActor::StaticClass();
	// InteractiveFoliageActor derives from StaticMeshActor.  bFromStaticMesh should only convert static mesh actors that arent supported by some other conversion
	const bool bFromStaticMesh = !bFromInteractiveFoliage && InFromClass->IsChildOf(AStaticMeshActor::StaticClass());
	const bool bFromSkeletalMesh = InFromClass->IsChildOf(ASkeletalMeshActor::StaticClass());

	const bool bToInteractiveFoliage = InToClass == AInteractiveFoliageActor::StaticClass();
	const bool bToStaticMesh = InToClass->IsChildOf(AStaticMeshActor::StaticClass());
	const bool bToSkeletalMesh = InToClass->IsChildOf(ASkeletalMeshActor::StaticClass());

	const bool bFoundTarget = bToInteractiveFoliage || bToStaticMesh || bToSkeletalMesh;

	TArray<AActor*> SourceActors;
	TArray<FConvertStaticMeshActorInfo> ConvertInfo;

	// Provide the option to abort up-front.
	if (!bFoundTarget || (GUnrealEd && GUnrealEd->ShouldAbortActorDeletion()))
	{
		return;
	}

	const FScopedTransaction Transaction(NSLOCTEXT("UnrealEd", "ConvertMeshes", "Convert Meshes"));
	// Iterate over selected Actors.
	for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
	{
		AActor* Actor = static_cast<AActor*>(*It);
		checkSlow(Actor->IsA(AActor::StaticClass()));

		AStaticMeshActor* SMActor = bFromStaticMesh ? Cast<AStaticMeshActor>(Actor) : nullptr;
		AInteractiveFoliageActor* FoliageActor = bFromInteractiveFoliage ? Cast<AInteractiveFoliageActor>(Actor) : nullptr;
		ASkeletalMeshActor* SKMActor = bFromSkeletalMesh ? Cast<ASkeletalMeshActor>(Actor) : nullptr;

		const bool bFoundActorToConvert = SMActor || FoliageActor || SKMActor;
		if (bFoundActorToConvert)
		{
			// clear all transient properties before copying from
			Actor->UnregisterAllComponents();

			// If its the type we are converting 'from' copy its properties and remember it.
			FConvertStaticMeshActorInfo Info;
			FMemory::Memzero(&Info, sizeof(FConvertStaticMeshActorInfo));

			if (SMActor)
			{
				SourceActors.Add(Actor);
				Info.GetFromActor(SMActor, SMActor->GetStaticMeshComponent());
			}
			else if (FoliageActor)
			{
				SourceActors.Add(Actor);
				Info.GetFromActor(FoliageActor, FoliageActor->GetStaticMeshComponent());
			}
			else if (bFromSkeletalMesh)
			{
				SourceActors.Add(Actor);
				Info.GetFromActor(SKMActor, SKMActor->GetSkeletalMeshComponent());
			}

			// Get the actor group if any
			Info.ActorGroup = AGroupActor::GetParentForActor(Actor);

			ConvertInfo.Add(MoveTemp(Info));
		}
	}

	if (SourceActors.Num())
	{
		GEditor->GetSelectedActors()->BeginBatchSelectOperation();

		// Then clear selection, select and delete the source actors.
		GEditor->SelectNone(false, false);
		UWorld* World = nullptr;
		for (int32 ActorIndex = 0; ActorIndex < SourceActors.Num(); ++ActorIndex)
		{
			AActor* SourceActor = SourceActors[ActorIndex];
			GEditor->SelectActor(SourceActor, true, false);
			World = SourceActor->GetWorld();
		}

		if (World && GUnrealEd && GUnrealEd->edactDeleteSelected(World, false))
		{
			// Now we need to spawn some new actors at the desired locations.
			for (int32 i = 0; i < ConvertInfo.Num(); ++i)
			{
				FConvertStaticMeshActorInfo& Info = ConvertInfo[i];

				// Spawn correct type, and copy properties from intermediate struct.
				AActor* Actor = nullptr;

				// Cache the world pointer
				check(World == Info.SourceLevel->OwningWorld);

				FActorSpawnParameters SpawnInfo;
				SpawnInfo.OverrideLevel = Info.SourceLevel;
				SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				if (bToStaticMesh)
				{
					AStaticMeshActor* SMActor = CastChecked<AStaticMeshActor>(
						World->SpawnActor(InToClass, &Info.Location, &Info.Rotation, SpawnInfo)
					);
					SMActor->UnregisterAllComponents();
					Info.SetToActor(SMActor, SMActor->GetStaticMeshComponent());
					SMActor->RegisterAllComponents();
					GEditor->SelectActor(SMActor, true, false);
					Actor = SMActor;
				}
				else if (bToInteractiveFoliage)
				{
					AInteractiveFoliageActor* FoliageActor =
						World->SpawnActor<AInteractiveFoliageActor>(Info.Location, Info.Rotation, SpawnInfo);
					check(FoliageActor);
					FoliageActor->UnregisterAllComponents();
					Info.SetToActor(FoliageActor, FoliageActor->GetStaticMeshComponent());
					FoliageActor->RegisterAllComponents();
					GEditor->SelectActor(FoliageActor, true, false);
					Actor = FoliageActor;
				}
				else if (bToSkeletalMesh)
				{
					check(InToClass->IsChildOf(ASkeletalMeshActor::StaticClass()));
					// checked
					ASkeletalMeshActor* SkeletalMeshActor = CastChecked<ASkeletalMeshActor>(
						World->SpawnActor(InToClass, &Info.Location, &Info.Rotation, SpawnInfo)
					);
					SkeletalMeshActor->UnregisterAllComponents();
					Info.SetToActor(SkeletalMeshActor, SkeletalMeshActor->GetSkeletalMeshComponent());
					SkeletalMeshActor->RegisterAllComponents();
					GEditor->SelectActor(SkeletalMeshActor, true, false);
					Actor = SkeletalMeshActor;
				}

				// Fix up the actor group.
				if (Actor)
				{
					if (Info.ActorGroup)
					{
						Info.ActorGroup->Add(*Actor);
						Info.ActorGroup->Add(*Actor);
					}
				}
			}
		}

		GEditor->GetSelectedActors()->EndBatchSelectOperation();
	}
}
void UEditorActorSubsystem::ReplaceSelectedActors(UActorFactory* InFactory, const FAssetData& InAssetData, bool bInCopySourceProperties)
{
	// Provide the option to abort the delete
	if (GEditor->ShouldAbortActorDeletion())
	{
		return;
	}
	else if (InFactory != nullptr)
	{
		FText ActorErrorMsg;
		if (!InFactory->CanCreateActorFrom(InAssetData, ActorErrorMsg))
		{
			FMessageDialog::Open(EAppMsgType::Ok, ActorErrorMsg);
			return;
		}
	}
	else
	{
		UE_LOG(LogUtils, Error, TEXT("UEditorEngine::ReplaceSelectedActors() called with NULL parameters!"));
		return;
	}

	const FScopedTransaction Transaction(NSLOCTEXT("UnrealEd", "Replace Actors", "Replace Actor(s)"));

	// construct a list of Actors to replace in a separate pass so we can modify the selection set as we perform the replacement
	TArray<AActor*> ActorsToReplace;
	for (FSelectionIterator It = GEditor->GetSelectedActorIterator(); It; ++It)
	{
		AActor* Actor = Cast<AActor>(*It);
		if (Actor && Actor->IsUserManaged() && !FActorEditorUtils::IsABuilderBrush(Actor))
		{
			ActorsToReplace.Add(Actor);
		}
	}

	ReplaceActors(InFactory, InAssetData, ActorsToReplace, nullptr, bInCopySourceProperties);
}

void UEditorActorSubsystem::ReplaceActors(
	UActorFactory* InFactory,
	const FAssetData& InAssetData,
	const TArray<AActor*>& InActorsToReplace,
	TArray<AActor*>* InOutNewActors,
	bool bInCopySourceProperties
)
{
	FGuid InvalidGuid;
	FContentBundleActivationScope ActivationScope(InvalidGuid);

	// Cache for attachment info of all actors being converted.
	TArray<UE::EditorActorSubsystem::Private::ReattachActorsHelper::FActorAttachmentCache> AttachmentInfo;

	// Maps actors from old to new for quick look-up.
	TMap<AActor*, AActor*> ConvertedMap;

	// Cache the current attachment states.
	CacheAttachments(InActorsToReplace, AttachmentInfo);

	USelection* SelectedActors = GEditor->GetSelectedActors();
	SelectedActors->BeginBatchSelectOperation();
	SelectedActors->Modify();

	UObject* Asset = InAssetData.GetAsset();
	for (int32 ActorIdx = 0; ActorIdx < InActorsToReplace.Num(); ++ActorIdx)
	{
		AActor* OldActor = InActorsToReplace[ActorIdx]; //.Pop();
		check(OldActor);
		UWorld* World = OldActor->GetWorld();
		ULevel* Level = OldActor->GetLevel();
		AActor* NewActor = nullptr;

		// Destroy any non-native constructed components, but make sure we grab the transform first in case it has a
		// non-native root component. These will be reconstructed as part of the new actor when it's created/instanced.
		const FTransform OldTransform = OldActor->ActorToWorld();

		TOptional<FVector> OldRelativeScale3D;
		TOptional<EComponentMobility::Type> OldMobility;
		if (OldActor->GetRootComponent())
		{
			OldRelativeScale3D = OldActor->GetRootComponent()->GetRelativeScale3D();
			OldMobility = OldActor->GetRootComponent()->Mobility;
		}

		OldActor->DestroyConstructedComponents();

		// Unregister this actors components because we are effectively replacing it with an actor sharing the same ActorGuid.
		// This allows it to be unregistered before a new actor with the same guid gets registered avoiding conflicts.
		OldActor->UnregisterAllComponents();

		const FName OldActorName = OldActor->GetFName();
		const FName OldActorReplacedNamed = MakeUniqueObjectName(
			OldActor->GetOuter(), OldActor->GetClass(), *FString::Printf(TEXT("%s_REPLACED"), *OldActorName.ToString())
		);

		FActorSpawnParameters SpawnParams;
		SpawnParams.Name = OldActorName;
		SpawnParams.bCreateActorPackage = false;
		SpawnParams.OverridePackage = OldActor->GetExternalPackage();
		SpawnParams.OverrideActorGuid = OldActor->GetActorGuid();

		// Don't go through AActor::Rename here because we aren't changing outers (the actor's level). We just want to rename
		// that actor out of the way so we can spawn the new one in the exact same package, keeping the package name intact.
		OldActor->UObject::Rename(
			*OldActorReplacedNamed.ToString(), OldActor->GetOuter(), REN_DoNotDirty | REN_DontCreateRedirectors
		);

		// create the actor
		NewActor = InFactory->CreateActor(Asset, Level, OldTransform, SpawnParams);
		// For blueprints, try to copy over properties
		if (bInCopySourceProperties && InFactory->IsA(UActorFactoryBlueprint::StaticClass()))
		{
			UBlueprint* Blueprint = CastChecked<UBlueprint>(Asset);
			// Only try to copy properties if this blueprint is based on the actor
			UClass* OldActorClass = OldActor->GetClass();
			if (Blueprint->GeneratedClass->IsChildOf(OldActorClass) && NewActor != nullptr)
			{
				NewActor->UnregisterAllComponents();
				UEngine::FCopyPropertiesForUnrelatedObjectsParams Options;
				Options.bNotifyObjectReplacement = true;
				UEditorEngine::CopyPropertiesForUnrelatedObjects(OldActor, NewActor, Options);
				NewActor->RegisterAllComponents();
			}
		}

		if (NewActor)
		{
			// The new actor might not have a root component
			USceneComponent* const NewActorRootComponent = NewActor->GetRootComponent();
			if (NewActorRootComponent)
			{
				if (!GetDefault<ULevelEditorMiscSettings>()->bReplaceRespectsScale || !OldRelativeScale3D.IsSet())
				{
					NewActorRootComponent->SetRelativeScale3D(FVector(1.0f, 1.0f, 1.0f));
				}
				else
				{
					NewActorRootComponent->SetRelativeScale3D(OldRelativeScale3D.GetValue());
				}

				if (OldMobility.IsSet())
				{
					NewActorRootComponent->SetMobility(OldMobility.GetValue());
				}
			}

			NewActor->Layers.Empty();
			ULayersSubsystem* LayersSubsystem = GEditor->GetEditorSubsystem<ULayersSubsystem>();
			LayersSubsystem->AddActorToLayers(NewActor, OldActor->Layers);

			// Allow actor derived classes a chance to replace properties.
			NewActor->EditorReplacedActor(OldActor);

			// Caches information for finding the new actor using the pre-converted actor.
			UE::EditorActorSubsystem::Private::ReattachActorsHelper::CacheActorConvert(
				OldActor, NewActor, ConvertedMap, AttachmentInfo[ActorIdx]
			);

			if (SelectedActors->IsSelected(OldActor))
			{
				// Avoid notifications as we are in a Batch Select Operation
				const bool bNotify = false;
				GEditor->SelectActor(OldActor, false, bNotify);
				GEditor->SelectActor(NewActor, true, bNotify);
			}

			// Find compatible static mesh components and copy instance colors between them.
			UStaticMeshComponent* NewActorStaticMeshComponent = NewActor->FindComponentByClass<UStaticMeshComponent>();
			UStaticMeshComponent* OldActorStaticMeshComponent = OldActor->FindComponentByClass<UStaticMeshComponent>();
			if (NewActorStaticMeshComponent != nullptr && OldActorStaticMeshComponent != nullptr)
			{
				NewActorStaticMeshComponent->CopyInstanceVertexColorsIfCompatible(OldActorStaticMeshComponent);
			}

			NewActor->InvalidateLightingCache();
			NewActor->PostEditMove(true);
			NewActor->MarkPackageDirty();

			TSet<ULevel*> LevelsToRebuildBSP;
			ABrush* Brush = Cast<ABrush>(OldActor);
			if (Brush && !FActorEditorUtils::IsABuilderBrush(Brush)) // Track whether or not a brush actor was deleted.
			{
				ULevel* BrushLevel = OldActor->GetLevel();
				if (BrushLevel && !Brush->IsVolumeBrush())
				{
					BrushLevel->Model->Modify(false);
					LevelsToRebuildBSP.Add(BrushLevel);
				}
			}

			// Replace references in the level script Blueprint with the new Actor
			const bool bDontCreate = true;
			ULevelScriptBlueprint* LSB = NewActor->GetLevel()->GetLevelScriptBlueprint(bDontCreate);
			if (LSB)
			{
				// Only if the level script blueprint exists would there be references.
				FBlueprintEditorUtils::ReplaceAllActorRefrences(LSB, OldActor, NewActor);
			}

			LayersSubsystem->DisassociateActorFromLayers(OldActor);
			World->EditorDestroyActor(OldActor, true);

			// If any brush actors were modified, update the BSP in the appropriate levels
			if (LevelsToRebuildBSP.Num())
			{
				FlushRenderingCommands();

				for (ULevel* LevelToRebuild : LevelsToRebuildBSP)
				{
					GEditor->RebuildLevel(*LevelToRebuild);
				}
			}
		}
		else
		{
			// If creating the new Actor failed, put the old Actor's name back
			OldActor->UObject::Rename(
				*OldActorName.ToString(), OldActor->GetOuter(), REN_DoNotDirty | REN_DontCreateRedirectors
			);
			OldActor->RegisterAllComponents();
		}
	}

	const bool bNotify = true;
	SelectedActors->EndBatchSelectOperation(bNotify);

	// Reattaches actors based on their previous parent child relationship.
	ReattachActors(ConvertedMap, AttachmentInfo);

	// Output new actors and
	// Perform reference replacement on all Actors referenced by World
	TArray<UObject*> ReferencedLevels;
	if (InOutNewActors)
	{
		InOutNewActors->Reserve(ConvertedMap.Num());
	}
	for (const TPair<AActor*, AActor*>& ReplacedObj : ConvertedMap)
	{
		ReferencedLevels.AddUnique(ReplacedObj.Value->GetLevel());
		if (InOutNewActors)
		{
			InOutNewActors->Add(ReplacedObj.Value);
		}
	}

	for (UObject* Referencer : ReferencedLevels)
	{
		constexpr EArchiveReplaceObjectFlags ArFlags =
			(EArchiveReplaceObjectFlags::IgnoreOuterRef | EArchiveReplaceObjectFlags::TrackReplacedReferences);
		FArchiveReplaceObjectRef<AActor> Ar(Referencer, ConvertedMap, ArFlags);

		for (const TPair<UObject*, TArray<FProperty*>>& MapItem : Ar.GetReplacedReferences())
		{
			UObject* ModifiedObject = MapItem.Key;

			if (!ModifiedObject->HasAnyFlags(RF_Transient) && ModifiedObject->GetOutermost() != GetTransientPackage()
				&& !ModifiedObject->RootPackageHasAnyFlags(PKG_CompiledIn))
			{
				ModifiedObject->MarkPackageDirty();
			}

			for (FProperty* Property : MapItem.Value)
			{
				FPropertyChangedEvent PropertyEvent(Property);
				ModifiedObject->PostEditChangeProperty(PropertyEvent);
			}
		}
	}

	GEditor->RedrawLevelEditingViewports();

	ULevel::LevelDirtiedEvent.Broadcast();
}

AActor* UEditorActorSubsystem::DuplicateActor(AActor* ActorToDuplicate, UWorld* ToWorld/*= nullptr*/, FVector Offset/* = FVector::ZeroVector*/)
{
	return DuplicateActor(ActorToDuplicate, ToWorld, Offset, FActorDuplicateParameters());
}

AActor* UEditorActorSubsystem::DuplicateActor(AActor* ActorToDuplicate, UWorld* ToWorld, FVector Offset, const FActorDuplicateParameters& DuplicateParams)
{
	TArray<AActor*> Duplicate = DuplicateActors({ ActorToDuplicate }, ToWorld, Offset, DuplicateParams);
	return (Duplicate.Num() > 0) ? Duplicate[0] : nullptr;
}

TArray<AActor*> UEditorActorSubsystem::DuplicateActors(const TArray<AActor*>& ActorsToDuplicate, UWorld* ToWorld/*= nullptr*/, FVector Offset/* = FVector::ZeroVector*/)
{
	return DuplicateActors(ActorsToDuplicate, ToWorld, Offset, FActorDuplicateParameters());
}

TArray<AActor*> UEditorActorSubsystem::DuplicateActors(const TArray<AActor*>& ActorsToDuplicate, UWorld* InToWorld, FVector Offset, const FActorDuplicateParameters& DuplicateParams)
{
	TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

	FScopedTransaction Transaction(LOCTEXT("DuplicateActors", "Duplicate Actors"), DuplicateParams.bTransact);

	UUnrealEditorSubsystem* UnrealEditorSubsystem = GEditor->GetEditorSubsystem<UUnrealEditorSubsystem>();

	if (!UnrealEditorSubsystem || !EditorScriptingHelpers::CheckIfInEditorAndPIE())
	{
		return TArray<AActor*>();
	}

	UWorld* ToWorld = InToWorld ? InToWorld : UnrealEditorSubsystem->GetEditorWorld();
	if (!ToWorld)
	{
		return TArray<AActor*>();
	}

	ULevel* ToLevel = DuplicateParams.LevelOverride;
	if (!ToLevel || ToLevel->GetWorld() != ToWorld)
	{
		ToLevel = ToWorld->GetCurrentLevel();
	}

	FEditorDelegates::OnDuplicateActorsBegin.Broadcast();

	TArray<AActor*> NewActors;
	ABrush::SetSuppressBSPRegeneration(true);
	GUnrealEd->DuplicateActors(ActorsToDuplicate, NewActors, ToLevel, Offset);
	ABrush::SetSuppressBSPRegeneration(false);

	// Find out if any of the actors will change the BSP.
	// and only then rebuild BSP as this is expensive. 
	if (NewActors.FindItemByClass<ABrush>())
	{
		GEditor->RebuildAlteredBSP(); // Update the BSP of any levels containing a modified brush
	}

	FEditorDelegates::OnDuplicateActorsEnd.Broadcast();

	return NewActors;
}

bool UEditorActorSubsystem::SetActorTransform(AActor* InActor, const FTransform& InWorldTransform)
{
	if (!InActor)
	{
		FFrame::KismetExecutionMessage(TEXT("Cannot set the transfrom of a nullptr actor."), ELogVerbosity::Error);
		return false;
	}

	if (UEditorElementSubsystem* ElementSubsystem = GEditor->GetEditorSubsystem<UEditorElementSubsystem>())
	{
		if (FTypedElementHandle ActorElementHandle = UEngineElementsLibrary::AcquireEditorActorElementHandle(InActor))
		{
			return ElementSubsystem->SetElementTransform(ActorElementHandle, InWorldTransform);
		}
	}

	return false;
}

bool UEditorActorSubsystem::SetComponentTransform(USceneComponent* InSceneComponent, const FTransform& InWorldTransform)
{
	if (!InSceneComponent)
	{
		FFrame::KismetExecutionMessage(TEXT("Cannot set the transform of a nullptr SceneComponent."), ELogVerbosity::Error);
		return false;
	}

	if (UEditorElementSubsystem* ElementSubsystem = GEditor->GetEditorSubsystem<UEditorElementSubsystem>())
	{
		if (FTypedElementHandle ComponentElementHandle = UEngineElementsLibrary::AcquireEditorComponentElementHandle(InSceneComponent))
		{
			return ElementSubsystem->SetElementTransform(ComponentElementHandle, InWorldTransform);
		}
	}

	return false;
}

#undef LOCTEXT_NAMESPACE
