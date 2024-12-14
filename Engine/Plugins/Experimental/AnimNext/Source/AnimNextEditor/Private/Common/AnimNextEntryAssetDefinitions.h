// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AssetDefinitionDefault.h"
#include "Entries/AnimNextEventGraphEntry.h"
#include "Entries/AnimNextVariableEntry.h"
#include "Entries/AnimNextAnimationGraphEntry.h"
#include "Entries/AnimNextDataInterfaceEntry.h"
#include "AnimNextEntryAssetDefinitions.generated.h"

#define LOCTEXT_NAMESPACE "AnimNextAssetDefinitions"

UCLASS()
class UAssetDefinition_AnimNextVariableEntry : public UAssetDefinitionDefault
{
	GENERATED_BODY()

public:
	// UAssetDefinition interface
	virtual FText GetAssetDisplayName() const override { return LOCTEXT("AnimNextVariable", "Variable"); }
	virtual FLinearColor GetAssetColor() const override { return FLinearColor(FColor(128,64,64)); }
	virtual TSoftClassPtr<UObject> GetAssetClass() const override { return UAnimNextVariableEntry::StaticClass(); }
	virtual FText GetObjectDisplayNameText(UObject* Object) const override;
};

UCLASS()
class UAssetDefinition_AnimNextAnimationGraphEntry : public UAssetDefinitionDefault
{
	GENERATED_BODY()

public:
	// UAssetDefinition interface
	virtual FText GetAssetDisplayName() const override { return LOCTEXT("AnimNextAnimationGraphEntry", "Animation Graph"); }
	virtual FLinearColor GetAssetColor() const override { return FLinearColor(FColor(128,64,64)); }
	virtual TSoftClassPtr<UObject> GetAssetClass() const override { return UAnimNextAnimationGraphEntry::StaticClass(); }
	virtual FText GetObjectDisplayNameText(UObject* Object) const override;
};

UCLASS()
class UAssetDefinition_AnimNextEventGraphEntry : public UAssetDefinitionDefault
{
	GENERATED_BODY()

public:
	// UAssetDefinition interface
	virtual FText GetAssetDisplayName() const override { return LOCTEXT("AnimNextEventGraphEntry", "Event Graph"); }
	virtual FLinearColor GetAssetColor() const override { return FLinearColor(FColor(128,64,64)); }
	virtual TSoftClassPtr<UObject> GetAssetClass() const override { return UAnimNextEventGraphEntry::StaticClass(); }
	virtual FText GetObjectDisplayNameText(UObject* Object) const override;
};

UCLASS()
class UAssetDefinition_AnimNextDataInterfaceEntry : public UAssetDefinitionDefault
{
	GENERATED_BODY()

public:
	// UAssetDefinition interface
	virtual FText GetAssetDisplayName() const override { return LOCTEXT("AnimNextDataInterfaceEntry", "Data Interface"); }
	virtual FLinearColor GetAssetColor() const override { return FLinearColor(FColor(128,64,64)); }
	virtual TSoftClassPtr<UObject> GetAssetClass() const override { return UAnimNextDataInterfaceEntry::StaticClass(); }
	virtual FText GetObjectDisplayNameText(UObject* Object) const override;
};

#undef LOCTEXT_NAMESPACE