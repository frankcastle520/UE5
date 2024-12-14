// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCO/ICustomizableObjectEditorModule.h"
#include "MuCO/CustomizableObjectCompilerTypes.h"

#include "MuCOE/CustomizableObjectCompiler.h"
#include "MuCOE/CustomizableObjectEditorLogger.h"
#include "Toolkits/AssetEditorToolkit.h"

struct FBakingConfiguration;
class USkeletalMeshComponent;
class FPropertyEditorModule;
class FExtensibilityManager;
class ICustomizableObjectEditor;
class ICustomizableObjectInstanceEditor;
class ICustomizableObjectDebugger;
class FBakeOperationCompletedDelegate;


/** Get a list of packages that are used by the compilation but are not directly referenced.
  * List includes:
  * - Child UCustomizableObjects: Have inverted references.
  * - UDataTable: Data Tables used by Composite Data Tables are indirectly referenced by the UStruct and filtered by path. */
void GetReferencingPackages(const UCustomizableObject& Object, TArray<FAssetData>& ObjectNames);


/**
 * StaticMesh editor module
 */
class FCustomizableObjectEditorModule : public ICustomizableObjectEditorModule
{
public:
	// IModuleInterface interface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	// ICustomizableObjectEditorModule interface
	virtual FCustomizableObjectEditorLogger& GetLogger() override;
	virtual bool IsCompilationOutOfDate(const UCustomizableObject& Object, bool bSkipIndirectReferences, TArray<FName>& OutOfDatePackages, TArray<FName>& AddedPackages, TArray<FName>& RemovedPackages, bool& bVersionDiff) const override;
	virtual bool IsRootObject(const UCustomizableObject& Object) const override;
	virtual FString GetCurrentReleaseVersionForObject(const UCustomizableObject& Object) const override;
	virtual UCustomizableObject* GetRootObject(UCustomizableObject* ChildObject) const override;
	virtual const UCustomizableObject* GetRootObject(const UCustomizableObject* ChildObject) const override;
	virtual void BakeCustomizableObjectInstance(UCustomizableObjectInstance* InTargetInstance, const FBakingConfiguration& InBakingConfig) override;
	virtual USkeletalMesh* GetReferenceSkeletalMesh(const UCustomizableObject& Object, const FName& ComponentName) const override;
	virtual TMap<FName, FGuid> GetParticipatingObjects(const UCustomizableObject* Object, bool bLoadObjects, const FCompilationOptions* Options = nullptr) const override;
	virtual void BackwardsCompatibleFixup(UEdGraph& Graph, int32 CustomizableObjectCustomVersion) override;
	virtual void PostBackwardsCompatibleFixup(UEdGraph& Graph) override;

	/** Request for a given customizable object to be compiled. Async compile requests will be queued and processed sequentially. 
	 * @param InCompilationRequest - Request to compile an object. 
	 * @param bForceRequest - Queue request even if already in the pending list. */
	virtual void CompileCustomizableObject(const TSharedRef<FCompilationRequest>& InCompilationRequest, bool bForceRequest = false) override;
	virtual void CompileCustomizableObjects(const TArray<TSharedRef<FCompilationRequest>>& InCompilationRequests, bool bForceRequests = false) override;

	virtual int32 Tick(bool bBlocking) override;

	virtual void CancelCompileRequests() override;

	virtual int32 GetNumCompileRequests() override;

	virtual TSharedPtr<FExtensibilityManager> GetCustomizableObjectEditorToolBarExtensibilityManager() override { return CustomizableObjectEditor_ToolBarExtensibilityManager; }
	virtual TSharedPtr<FExtensibilityManager> GetCustomizableObjectEditorMenuExtensibilityManager() override { return CustomizableObjectEditor_MenuExtensibilityManager; }

private:	
	TSharedPtr<FExtensibilityManager> CustomizableObjectEditor_ToolBarExtensibilityManager;
	TSharedPtr<FExtensibilityManager> CustomizableObjectEditor_MenuExtensibilityManager;

	/** List of registered custom details to remove later. */
	TArray<FName> RegisteredCustomDetails;

	/** Register Custom details. Also adds them to RegisteredCustomDetails list. */
	void RegisterCustomDetails(FPropertyEditorModule& PropertyModule, const UClass* Class, FOnGetDetailCustomizationInstance DetailLayoutDelegate);

	FCustomizableObjectEditorLogger Logger;
	
	FCustomizableObjectCompiler Compiler;

	// Command to look for Customizable Object Instance in the player pawn of the current world and open its Customizable Object Instance Editor
	IConsoleCommand* LaunchCOIECommand = nullptr;

	FTSTicker::FDelegateHandle WarningsTickerHandle;
	
	static void OpenCOIE(const TArray<FString>& Arguments);

	// Used to ask the user if they want to recompile uncompiled PIE COs
	void OnPreBeginPIE(const bool bIsSimulatingInEditor);

	/** Register the COI factory */
	void RegisterFactory();

	bool HandleSettingsSaved();
	void RegisterSettings();
};