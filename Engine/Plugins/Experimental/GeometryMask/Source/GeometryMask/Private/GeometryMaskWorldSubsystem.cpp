// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryMaskWorldSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GeometryMaskSVE.h"
#include "SceneViewExtension.h"

void UGeometryMaskWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	UWorld* World = GetWorld();

	GeometryMaskSceneViewExtension = FSceneViewExtensions::NewExtension<FGeometryMaskSceneViewExtension>(World);
}

void UGeometryMaskWorldSubsystem::Deinitialize()
{
	Super::Deinitialize();

	for (const TPair<TWeakObjectPtr<const ULevel>, FGeometryMaskLevelState>& LevelState : LevelStates)
	{
		for (const TPair<FName, TObjectPtr<UGeometryMaskCanvas>>& NamedCanvas : LevelState.Value.NamedCanvases)
		{
			if (IsValid(NamedCanvas.Value))
			{
				NamedCanvas.Value->Free();	
			}
		}
	}

	LevelStates.Empty();

	if (UGeometryMaskSubsystem* EngineSubsystem = GEngine->GetEngineSubsystem<UGeometryMaskSubsystem>())
	{
		EngineSubsystem->OnWorldDestroyed(GetWorld());
	}
}

const FGeometryMaskLevelState* UGeometryMaskWorldSubsystem::FindLevelState(const ULevel* InLevel) const
{
	if (IsValid(InLevel))
	{
		return LevelStates.Find(InLevel);
	}
	return nullptr;
}

FGeometryMaskLevelState& UGeometryMaskWorldSubsystem::FindOrAddLevelState(const ULevel* InLevel)
{
	check(IsValid(InLevel));
	return LevelStates.FindOrAdd(InLevel);
}

UGeometryMaskCanvas* UGeometryMaskWorldSubsystem::GetNamedCanvas(const ULevel* InLevel, FName InName)
{
	if (!IsValid(InLevel))
	{
		return nullptr;
	}

	UGeometryMaskSubsystem* EngineSubsystem = GEngine->GetEngineSubsystem<UGeometryMaskSubsystem>();
	if (!ensureAlwaysMsgf(EngineSubsystem, TEXT("UGeometryMaskSubsystem not resolved.")))
	{
		return nullptr;
	}

	if (InName.IsNone())
	{
		return EngineSubsystem->GetDefaultCanvas();
	}

	if (const FGeometryMaskLevelState* LevelState = FindLevelState(InLevel))
	{
		if (const TObjectPtr<UGeometryMaskCanvas>* FoundCanvas = LevelState->NamedCanvases.Find(InName))
		{
			return *FoundCanvas;
		}
	}

	const FName ObjectName = MakeUniqueObjectName(this, UGeometryMaskCanvas::StaticClass(), FName(FString::Printf(TEXT("GeometryMaskCanvas_%s_"), *InName.ToString())));

	FGeometryMaskLevelState& LevelState = FindOrAddLevelState(InLevel);
	const TObjectPtr<UGeometryMaskCanvas>& NewCanvas = LevelState.NamedCanvases.Emplace(InName, NewObject<UGeometryMaskCanvas>(this, ObjectName));

	NewCanvas->Initialize(InLevel, InName);
	EngineSubsystem->AssignResourceToCanvas(NewCanvas);

	NewCanvas->OnActivated().BindUObject(this, &UGeometryMaskWorldSubsystem::OnCanvasActivated, NewCanvas.Get());
	NewCanvas->OnDeactivated().BindUObject(this, &UGeometryMaskWorldSubsystem::OnCanvasDeactivated, NewCanvas.Get());
	
	OnGeometryMaskCanvasCreatedDelegate.Broadcast(NewCanvas);

	return NewCanvas;
}

TArray<FName> UGeometryMaskWorldSubsystem::GetCanvasNames(const ULevel* InLevel)
{
	TArray<FName> CanvasNames;
	if (const FGeometryMaskLevelState* LevelState = FindLevelState(InLevel))
	{
		LevelState->NamedCanvases.GenerateKeyArray(CanvasNames);
	}
	return CanvasNames;
}

int32 UGeometryMaskWorldSubsystem::RemoveWithoutWriters()
{
	int32 NumRemoved = 0;
	
	TMap<FName, TObjectPtr<UGeometryMaskCanvas>> UsedCanvases;
	UsedCanvases.Reserve(LevelStates.Num());

	for (decltype(LevelStates)::TIterator LevelStateIter(LevelStates); LevelStateIter; ++LevelStateIter)
	{
		FGeometryMaskLevelState& LevelState = LevelStateIter.Value();

		for (decltype(LevelState.NamedCanvases)::TIterator CanvasIter(LevelState.NamedCanvases); CanvasIter; ++CanvasIter)
		{
			TObjectPtr<UGeometryMaskCanvas> NamedCanvas = CanvasIter.Value();
			if (IsValid(NamedCanvas))
			{
				if (NamedCanvas->GetWriters().IsEmpty())
				{
					OnGeometryMaskCanvasDestroyed().Broadcast(NamedCanvas->GetCanvasId());
					NamedCanvas->FreeResource();
				}

				CanvasIter.RemoveCurrent();
				++NumRemoved;
			}
		}

		if (LevelState.NamedCanvases.IsEmpty())
		{
			LevelStateIter.RemoveCurrent();
		}
	}

	return NumRemoved;
}

void UGeometryMaskWorldSubsystem::OnCanvasActivated(UGeometryMaskCanvas* InCanvas)
{
	check(InCanvas);
	
	if (InCanvas->IsDefaultCanvas())
	{
		return;
	}

	// Already has a resource
	if (InCanvas->GetResource())
	{
		return;
	}

	UGeometryMaskSubsystem* EngineSubsystem = GEngine->GetEngineSubsystem<UGeometryMaskSubsystem>();
	if (!ensureAlwaysMsgf(EngineSubsystem, TEXT("UGeometryMaskSubsystem not resolved.")))
	{
		return;
	}

	// Provide a new resource for the canvas to write to
	EngineSubsystem->AssignResourceToCanvas(InCanvas);
}

void UGeometryMaskWorldSubsystem::OnCanvasDeactivated(UGeometryMaskCanvas* InCanvas)
{
	if (!InCanvas || !IsValid(InCanvas) || InCanvas->IsDefaultCanvas())
	{
		return;
	}

	if (const UGeometryMaskCanvasResource* CanvasResource = InCanvas->GetResource())
	{
		// Resource assigned, so free it up
		OnGeometryMaskCanvasDestroyed().Broadcast(InCanvas->GetCanvasId());
		InCanvas->FreeResource();
	}
}
