// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
namespace UE::MetaHuman
{
class IMetaHumanProjectUtilitiesAutomationHandler;
class IMetaHumanBulkImportHandler;
struct FMetaHumanAssetImportDescription;

class METAHUMANSDKEDITOR_API FMetaHumanImport
{
public:
	void ImportAsset(const FMetaHumanAssetImportDescription& AssetImportDescription);
	void SetAutomationHandler(IMetaHumanProjectUtilitiesAutomationHandler* Handler);
	void SetBulkImportHandler(IMetaHumanBulkImportHandler* Handler);

	static TSharedPtr<FMetaHumanImport> Get();

private:
	FMetaHumanImport() = default;
	IMetaHumanProjectUtilitiesAutomationHandler* AutomationHandler{nullptr};
	IMetaHumanBulkImportHandler* BulkImportHandler{nullptr};
	static TSharedPtr<FMetaHumanImport> MetaHumanImportInst;
};
}
