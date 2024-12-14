// Copyright Epic Games, Inc. All Rights Reserved.

#include "Actors/DaySequenceModifierVolume.h"

#include "DaySequenceActor.h"
#include "DaySequenceModifierComponent.h"
#include "DaySequenceSubsystem.h"

#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "GameDelegates.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"

ADaySequenceModifierVolume::ADaySequenceModifierVolume(const FObjectInitializer& Init)
: Super(Init)
{
	PrimaryActorTick.bCanEverTick = true;
	
	DaySequenceModifier = CreateDefaultSubobject<UDaySequenceModifierComponent>(TEXT("DaySequenceModifier"));
	DaySequenceModifier->SetupAttachment(RootComponent);

	DefaultBox = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	DefaultBox->SetupAttachment(DaySequenceModifier);
	DefaultBox->SetLineThickness(10.f);
	DefaultBox->SetBoxExtent(FVector(500.f));

	FComponentReference DefaultBoxReference;
	DefaultBoxReference.ComponentProperty = TEXT("DefaultBox");
	DaySequenceModifier->AddVolumeShapeComponent(DefaultBoxReference);
}

void ADaySequenceModifierVolume::SetBlendTarget(APlayerController* InPC)
{
	if (!IsValid(InPC) || InPC == CurrentBlendTarget)
	{
		return;
	}

	CurrentBlendTarget = InPC;

	DaySequenceModifier->SetBlendTarget(InPC);
	DaySequenceModifier->SetUserBlendWeight(1.f);
}

void ADaySequenceModifierVolume::BeginPlay()	
{
	Super::BeginPlay();

	Initialize();

	if (const UWorld* World = GetWorld(); World && World->IsPlayingReplay())
	{
		ReplayScrubbedHandle = FNetworkReplayDelegates::OnReplayScrubComplete.AddWeakLambda(this, [this](const UWorld* InWorld)
		{
			if (InWorld == GetWorld())
			{
				DaySequenceActorSetup();
			}
		});
	}
}

void ADaySequenceModifierVolume::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	
	Initialize();
}

void ADaySequenceModifierVolume::Initialize()
{
	if (IsTemplate())
	{
		return;
	}
	
	// This actor should only initialize on the client.
	if (GetNetMode() == NM_DedicatedServer)
	{
		SetActorEnableCollision(false);
		return;
	}

#if WITH_EDITOR
	const UWorld* World = GetWorld();
	if (World && World->WorldType == EWorldType::Editor)
	{
		DaySequenceActor = nullptr;
		if (IsValid(DaySequenceModifier))
		{
			DaySequenceModifier->UnbindFromDaySequenceActor();
		}
	}
#endif

	DaySequenceActorSetup();
}

void ADaySequenceModifierVolume::PlayerControllerSetup()
{
#if WITH_EDITOR
	UWorld* World = GetWorld();
	if (World && World->WorldType != EWorldType::Editor)
	{
#endif
	CachePlayerController();
#if WITH_EDITOR
	}
#endif
}

void ADaySequenceModifierVolume::CachePlayerController()
{
	if (const UWorld* World = GetWorld())
	{
		for (FConstPlayerControllerIterator Iterator = World->GetPlayerControllerIterator(); Iterator; ++Iterator)
		{
			if (APlayerController* PlayerController = Iterator->Get())
			{
				if (PlayerController->IsLocalPlayerController())
				{
					CachedPlayerController = PlayerController;
					break;
				}
			}
		}
	}

	// If no local player controller found, queue this function for next tick.
	if (!IsValid(CachedPlayerController))
	{
		QueuePlayerControllerQuery();
	}
	else
	{
		SetBlendTarget(CachedPlayerController);
	}
}

void ADaySequenceModifierVolume::QueuePlayerControllerQuery()
{
	if (!IsValid(this))
	{
		return;
	}

	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimerForNextTick([this]()
		{
			CachePlayerController();
		});
	}
}

void ADaySequenceModifierVolume::DaySequenceActorSetup()
{
	SetupDaySequenceSubsystemCallbacks();
	BindToDaySequenceActor();
}

void ADaySequenceModifierVolume::BindToDaySequenceActor()
{
	bool bNewActorSet = false;
	
	if (const UWorld* World = GetWorld())
	{
		if (const UDaySequenceSubsystem* DaySequenceSubsystem = World->GetSubsystem<UDaySequenceSubsystem>())
		{
			if (ADaySequenceActor* NewActor = DaySequenceSubsystem->GetDaySequenceActor())
			{
				if (NewActor != DaySequenceActor)
				{
					DaySequenceActor = NewActor;
					bNewActorSet = true;
				}
			}
		}
	}
	
	if (bNewActorSet)
	{
		DaySequenceModifier->BindToDaySequenceActor(DaySequenceActor);

		PlayerControllerSetup();

		OnDaySequenceActorBound(DaySequenceActor);
	}
}

void ADaySequenceModifierVolume::SetupDaySequenceSubsystemCallbacks()
{
	if (const UWorld* World = GetWorld())
	{
		if (UDaySequenceSubsystem* DaySequenceSubsystem = World->GetSubsystem<UDaySequenceSubsystem>())
		{
			// Prevent consecutive calls to this function from adding redundant lambdas to invocation list.
			if (!DaySequenceSubsystem->OnDaySequenceActorSetEvent.IsBoundToObject(this))
			{
				DaySequenceSubsystem->OnDaySequenceActorSetEvent.AddWeakLambda(this, [this](ADaySequenceActor* InActor)
				{
					BindToDaySequenceActor();
				});
			}
		}
	}
}