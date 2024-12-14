// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EditorValidatorBase.h"

#include "PackageFileValidator.generated.h"

struct FPackageFileSummary;

/**
 * This validator checks the format of the package on disk to make sure that is has not become corrupted since it was last saved.
 * 
 * To disable the validator entirely, set ini:Editor:[/Script/DataValidation.PackageFileValidator]:bIsConfigDisabled=true
 * To disable validation of payload hashes (which is much slower than the rest of the validation) set 
 * ini:Editor:[/Script/DataValidation.PackageFileValidator]:bValidatePayloadHashes=true
 */
UCLASS()
class DATAVALIDATION_API UPackageFileValidator : public UEditorValidatorBase
{
	GENERATED_BODY()

protected:

	virtual bool CanValidateAsset_Implementation(const FAssetData& AssetData, UObject* Asset, FDataValidationContext& Context) const override;
	virtual EDataValidationResult ValidateLoadedAsset_Implementation(const FAssetData& AssetData, UObject* Asset, FDataValidationContext& Context) override;

private:

	bool ValidatePackageSummary(FName PackageName, FArchive& Ar, FDataValidationContext& Context, FPackageFileSummary& OutSummary);
	bool ValidatePackageTrailer(FName PackageName, FArchive& Ar, FDataValidationContext& Context);

	bool TryResolvePackagePath(FName PackageName, FPackagePath& OutPackagePath) const;

	UPROPERTY(Config)
	bool bValidatePayloadHashes = true;
};
