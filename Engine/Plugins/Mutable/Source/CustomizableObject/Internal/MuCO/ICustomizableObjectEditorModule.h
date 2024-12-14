// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"
#include "Templates/SharedPointer.h"

class FCustomizableObjectEditorLogger;
class ICustomizableObjectDebugger;
class ICustomizableObjectEditor;
class ICustomizableObjectInstanceEditor;
class IToolkitHost;
class UCustomizableObject;
class UCustomizableObjectPrivate;
class UCustomizableObjectInstance;
class USkeletalMesh;
class FExtensibilityManager;
class FBakeOperationCompletedDelegate;
class UEdGraph;
struct FBakingConfiguration;
struct FCompilationRequest;
struct FCompilationOptions;

extern const FName CustomizableObjectEditorAppIdentifier;
extern const FName CustomizableObjectInstanceEditorAppIdentifier;
extern const FName CustomizableObjectPopulationEditorAppIdentifier;
extern const FName CustomizableObjectPopulationClassEditorAppIdentifier;
extern const FName CustomizableObjectDebuggerAppIdentifier;

/**
 * Customizable object editor module interface
 */
class CUSTOMIZABLEOBJECT_API ICustomizableObjectEditorModule : public IModuleInterface
{
public:
	static ICustomizableObjectEditorModule* Get()
	{
		return FModuleManager::LoadModulePtr<ICustomizableObjectEditorModule>("CustomizableObjectEditor");
	}
	
	static ICustomizableObjectEditorModule& GetChecked()
	{
		return FModuleManager::LoadModuleChecked<ICustomizableObjectEditorModule>("CustomizableObjectEditor");
	}

	virtual TSharedPtr<FExtensibilityManager> GetCustomizableObjectEditorToolBarExtensibilityManager() { return nullptr; }
	virtual TSharedPtr<FExtensibilityManager> GetCustomizableObjectEditorMenuExtensibilityManager() { return nullptr; }
	
	/** Returns the module logger. */
	virtual FCustomizableObjectEditorLogger& GetLogger() = 0;
	
	/** Return if the CO is not compiled or the ParticipatingObjects system has detected a change (participating objects dirty or re-saved since last compilation).
	  * @param Object object to check.
	  * @param bSkipIndirectReferences if true, do not check for added/removed indirect references.
	  * @param OutOfDatePackages list of out of date packages.
	  * @param AddedPackages list of added packages since the last compilation.
	  * @param RemovedPackages list of removed packages since the last compilation.
	  * @param bReleaseVersionDiff true if the Release Version has changed since the last compilation.
	  * @return true if the compilation is out of date. */
	virtual bool IsCompilationOutOfDate(const UCustomizableObject& Object, bool bSkipIndirectReferences, TArray<FName>& OutOfDatePackages, TArray<FName>& AddedPackages, TArray<FName>& RemovedPackages, bool& bReleaseVersionDiff) const = 0;

	/** See GraphTraversal::IsRootObject(...) */
	virtual bool IsRootObject(const UCustomizableObject& Object) const = 0;

	/** Get the current Release Version for the given Object. 
	  * @return Current version as string. */
	virtual FString GetCurrentReleaseVersionForObject(const UCustomizableObject& Object) const = 0;

	/** See GraphTraversal::GetRootObject(...) */
	virtual UCustomizableObject* GetRootObject(UCustomizableObject* ChildObject) const = 0;

	/** See GraphTraversal::GetRootObject(...) */
	virtual const UCustomizableObject* GetRootObject(const UCustomizableObject* ChildObject) const = 0;
	
	/**
	 * Execute this method in order to bake the provided instance. It will schedule a special type of instance update before proceeding with the bake itself.
	 * @param InTargetInstance The instance we want to bake
	 * @param InBakingConfig Structure containing the configuration to be used for the baking
	 */
	virtual void BakeCustomizableObjectInstance(UCustomizableObjectInstance* InTargetInstance, const FBakingConfiguration& InBakingConfig) = 0;

	/** Request for a given customizable object to be compiled. Async compile requests will be queued and processed sequentially.
	 * @param InCompilationRequest - Request to compile an object.
	 * @param bForceRequest - Queue request even if already in the pending list. */
	virtual void CompileCustomizableObject(const TSharedRef<FCompilationRequest>& InCompilationRequest, bool bForceRequest = false) = 0;
	virtual void CompileCustomizableObjects(const TArray<TSharedRef<FCompilationRequest>>& InCompilationRequests, bool bForceRequests = false) = 0;

	virtual int32 Tick(bool bBlocking) = 0;

	/** Force finish current compile request and cancels all pending requests */
	virtual void CancelCompileRequests() = 0;

	/** Return the number of pending compilation requests. Ongoing requests included. */
	virtual int32 GetNumCompileRequests() = 0;

	virtual USkeletalMesh* GetReferenceSkeletalMesh(const UCustomizableObject& Object, const FName& Component) const = 0;
	
	/** Perform a fast compilation pass to get all participating objects.
	 *  @param bLoadObjects Load any object. If false, no objects will load. If true, only objects strictly required to get the full list of participating objects will load. */
	virtual TMap<FName, FGuid> GetParticipatingObjects(const UCustomizableObject* Object, bool bLoadObjects, const FCompilationOptions* Options = nullptr) const = 0;

	virtual void BackwardsCompatibleFixup(UEdGraph& Graph, int32 CustomizableObjectCustomVersion) = 0;
	
	virtual void PostBackwardsCompatibleFixup(UEdGraph& Graph) = 0;
};
 
