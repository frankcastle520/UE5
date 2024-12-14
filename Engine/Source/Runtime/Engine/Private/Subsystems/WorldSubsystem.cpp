// Copyright Epic Games, Inc. All Rights Reserved.

#include "Subsystems/WorldSubsystem.h"
#include "Engine/World.h"
#include "Subsystems/Subsystem.h"
#include "Streaming/StreamingWorldSubsystemInterface.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(WorldSubsystem)

// ----------------------------------------------------------------------------------

UWorldSubsystem::UWorldSubsystem()
	: USubsystem()
{

}

UWorld& UWorldSubsystem::GetWorldRef() const
{
	return *CastChecked<UWorld>(GetOuter(), ECastCheckedType::NullChecked);
}

UWorld* UWorldSubsystem::GetWorld() const
{
	return Cast<UWorld>(GetOuter());
}

bool UWorldSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}

	UWorld* World = Cast<UWorld>(Outer);
	check(World);
	return DoesSupportWorldType(World->WorldType);
}

bool UWorldSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::Editor || WorldType == EWorldType::PIE;
}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
void UWorldSubsystem::UpdateStreamingState()
{
	if (IStreamingWorldSubsystemInterface* StreamingWorldSubsystem = Cast<IStreamingWorldSubsystemInterface>(this))
	{
		StreamingWorldSubsystem->OnUpdateStreamingState();
	}
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

// ----------------------------------------------------------------------------------

UTickableWorldSubsystem::UTickableWorldSubsystem()
{

}

UWorld* UTickableWorldSubsystem::GetTickableGameObjectWorld() const
{
	return GetWorld();
}

ETickableTickType UTickableWorldSubsystem::GetTickableTickType() const 
{
	// If this is a template or has not been initialized yet, set to never tick and it will be enabled when it is initialized
	if (IsTemplate() || !bInitialized)
	{
		return ETickableTickType::Never;
	}

	// Otherwise default to conditional
	return ETickableTickType::Conditional;
}

bool UTickableWorldSubsystem::IsAllowedToTick() const
{
	// This function is now deprecated and subclasses should implement IsTickable instead
	// This should never be false because Initialize should always be called before the first tick and Deinitialize cancels the tick
	ensureMsgf(bInitialized, TEXT("Tickable subsystem %s tried to tick when not initialized! Check for missing Super call"), *GetFullName());

	return bInitialized;
}

void UTickableWorldSubsystem::Tick(float DeltaTime)
{
	checkf(IsInitialized(), TEXT("Ticking should have been disabled for an uninitialized subsystem!"));
}

void UTickableWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	check(!bInitialized);
	bInitialized = true;

	// Refresh the tick type after initialization
	SetTickableTickType(GetTickableTickType());
}

void UTickableWorldSubsystem::Deinitialize()
{
	check(bInitialized);
	bInitialized = false;

	// Always cancel tick as this is about to be destroyed
	SetTickableTickType(ETickableTickType::Never);
}

void UTickableWorldSubsystem::BeginDestroy()
{
	Super::BeginDestroy();

	ensureMsgf(!bInitialized, TEXT("Tickable subsystem %s was destroyed while still initialized! Check for missing Super call"), *GetFullName());
}
