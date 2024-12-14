// Copyright Epic Games, Inc. All Rights Reserved.

#pragma  once

#include "Modules/ModuleInterface.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "Delegates/IDelegateInstance.h"

class IMassEntityEditor;
struct FGraphPanelNodeFactory;
struct FGraphNodeClassHelper;
class UWorld;

/**
* The public interface to this module
*/
class MASSENTITYEDITOR_API FMassEntityEditorModule : public IModuleInterface, public IHasMenuExtensibility, public IHasToolBarExtensibility
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	virtual TSharedPtr<FExtensibilityManager> GetMenuExtensibilityManager() override { return MenuExtensibilityManager; }
	virtual TSharedPtr<FExtensibilityManager> GetToolBarExtensibilityManager() override { return ToolBarExtensibilityManager; }

	UE_DEPRECATED(5.3, "Please use GetProcessorClassCache instead")
	TSharedPtr<FGraphNodeClassHelper> GetProcassorClassCache() { return ProcessorClassCache; }
	TSharedPtr<FGraphNodeClassHelper> GetProcessorClassCache() { return ProcessorClassCache; }

protected:
#if WITH_UNREAL_DEVELOPER_TOOLS
	static void OnWorldCleanup(UWorld* /*World*/, bool /*bSessionEnded*/, bool /*bCleanupResources*/);
	FDelegateHandle OnWorldCleanupHandle;
#endif // WITH_UNREAL_DEVELOPER_TOOLS

	TSharedPtr<FGraphNodeClassHelper> ProcessorClassCache;

	TSharedPtr<FExtensibilityManager> MenuExtensibilityManager;
	TSharedPtr<FExtensibilityManager> ToolBarExtensibilityManager;
};

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_3
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "AssetTypeCategories.h"
#include "Toolkits/IToolkitHost.h"
#endif
