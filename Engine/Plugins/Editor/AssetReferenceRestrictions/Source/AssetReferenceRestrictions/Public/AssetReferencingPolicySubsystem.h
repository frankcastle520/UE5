// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AssetRegistry/AssetData.h"
#include "EditorSubsystem.h"
#include "Templates/ValueOrError.h"

#include "AssetReferencingPolicySubsystem.generated.h"

struct FAssetReferenceFilterContext;
class IAssetReferenceFilter;
struct FAssetData;
struct FDomainDatabase;

enum class EAssetReferenceErrorType
{
	DoesNotExist,
	Illegal
};

struct FAssetReferenceError
{
	EAssetReferenceErrorType Type;
	FAssetData ReferencedAsset;
	FText Message;
};

/** Subsystem to register the domain-based asset referencing policy restrictions with the editor */
UCLASS()
class ASSETREFERENCERESTRICTIONS_API UAssetReferencingPolicySubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	//~UEditorSubsystem interface
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~End of UEditorSubsystem interface

	// Returns whether the given asset's outgoing references are restricted in any way and should be individually validated
	bool ShouldValidateAssetReferences(const FAssetData& Asset) const;

	// Check the outgoing references of the given asset according to the asset registry and return details of any errors 
	TValueOrError<void, TArray<FAssetReferenceError>> ValidateAssetReferences(const FAssetData& Asset) const;

	TSharedPtr<FDomainDatabase> GetDomainDB() const;
private:
	void UpdateDBIfNecessary() const;

	TSharedPtr<IAssetReferenceFilter> HandleMakeAssetReferenceFilter(const FAssetReferenceFilterContext& Context);

	TSharedPtr<FDomainDatabase> DomainDB;
};
