// Copyright Epic Games, Inc. All Rights Reserved.

#include "IUMGWidgetPreviewModule.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "MVVMWidgetPreviewExtension.h"

#define LOCTEXT_NAMESPACE "ModelViewViewModelPreview"

class FMVVMPreviewModule
	: public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		IUMGWidgetPreviewModule& WidgetPreviewModule = FModuleManager::LoadModuleChecked<IUMGWidgetPreviewModule>(WidgetPreviewModuleName);
		WidgetPreviewExtension = MakeShared<UE::MVVM::Private::FMVVMWidgetPreviewExtension>();
		WidgetPreviewExtension->Register(WidgetPreviewModule);
	}

	virtual void ShutdownModule() override
	{
		if (IUMGWidgetPreviewModule* WidgetPreviewModule = FModuleManager::GetModulePtr<IUMGWidgetPreviewModule>(WidgetPreviewModuleName))
		{
			WidgetPreviewExtension->Unregister(WidgetPreviewModule);
		}
	}

private:
	const FName WidgetPreviewModuleName = "UMGWidgetPreview";

	TSharedPtr<UE::MVVM::Private::FMVVMWidgetPreviewExtension> WidgetPreviewExtension;
};

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMVVMPreviewModule, ModelViewViewModelPreview)
