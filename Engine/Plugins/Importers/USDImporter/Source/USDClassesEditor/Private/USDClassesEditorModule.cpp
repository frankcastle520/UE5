// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDClassesEditorModule.h"

#include "USDAssetCache2.h"

#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "USDClassesEditorModule"

UUsdAssetCache2* IUsdClassesEditorModule::ShowMissingDefaultAssetCacheDialog()
{
	return nullptr;
}

void IUsdClassesEditorModule::ShowMissingDefaultAssetCacheDialog(UUsdAssetCache2*& OutCreatedCache, bool& bOutUserAccepted)
{
}

EDefaultAssetCacheDialogOption IUsdClassesEditorModule::ShowMissingDefaultAssetCacheDialog(UUsdAssetCache2*& OutCreatedCache)
{
	return EDefaultAssetCacheDialogOption::Cancel;
}

class FUsdClassesEditorModule : public IUsdClassesEditorModule
{
public:
	virtual void StartupModule() override
	{
	}

	virtual void ShutdownModule() override
	{
	}
};

IMPLEMENT_MODULE(FUsdClassesEditorModule, USDClassesEditor);

#undef LOCTEXT_NAMESPACE
