// Copyright Epic Games, Inc. All Rights Reserved.

#include "PoseSearchInteractionAssetFactory.h"
#include "PoseSearch/PoseSearchInteractionAsset.h"

#define LOCTEXT_NAMESPACE "PoseSearchInteractionAssetFactory"

UPoseSearchInteractionAssetFactory::UPoseSearchInteractionAssetFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UPoseSearchInteractionAsset::StaticClass();
}

UObject* UPoseSearchInteractionAssetFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<UPoseSearchInteractionAsset>(InParent, Class, Name, Flags);
}

FString UPoseSearchInteractionAssetFactory::GetDefaultNewAssetName() const
{
	return FString(TEXT("NewPoseSearchInteractionAsset"));
}

#undef LOCTEXT_NAMESPACE