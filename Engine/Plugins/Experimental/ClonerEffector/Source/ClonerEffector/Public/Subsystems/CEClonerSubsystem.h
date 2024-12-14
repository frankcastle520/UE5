// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Cloner/Layouts/CEClonerLayoutBase.h"
#include "Subsystems/EngineSubsystem.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "CEClonerSubsystem.generated.h"

class AActor;
class UCEClonerExtensionBase;

UCLASS(MinimalAPI)
class UCEClonerSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()

	DECLARE_MULTICAST_DELEGATE(FOnSubsystemInitialized)
	DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnClonerSetEnabled, const UWorld* /** InWorld */, bool /** bInEnabled */, bool /** bInTransact */)

public:
	static FOnSubsystemInitialized::RegistrationType& OnSubsystemInitialized()
	{
		return OnSubsystemInitializedDelegate;
	}

	static FOnClonerSetEnabled::RegistrationType& OnClonerSetEnabled()
	{
		return OnClonerSetEnabledDelegate;
	}

	/** Get this subsystem instance */
	CLONEREFFECTOR_API static UCEClonerSubsystem* Get();

	CLONEREFFECTOR_API bool RegisterLayoutClass(UClass* InClonerLayoutClass);

	CLONEREFFECTOR_API bool UnregisterLayoutClass(UClass* InClonerLayoutClass);

	CLONEREFFECTOR_API bool IsLayoutClassRegistered(UClass* InClonerLayoutClass);

	/** Get available cloner layout names to use in dropdown */
	TSet<FName> GetLayoutNames() const;

	/** Get available cloner layout classes */
	TSet<TSubclassOf<UCEClonerLayoutBase>> GetLayoutClasses() const;

	/** Based on a layout class, find layout name */
	FName FindLayoutName(TSubclassOf<UCEClonerLayoutBase> InLayoutClass) const;

	/** Based on a layout name, find layout class */
	TSubclassOf<UCEClonerLayoutBase> FindLayoutClass(FName InLayoutName) const;

	/** Creates a new layout instance for a cloner */
	UCEClonerLayoutBase* CreateNewLayout(FName InLayoutName, UCEClonerComponent* InCloner);

	DECLARE_DELEGATE_RetVal_OneParam(TArray<AActor*> /** Children */, FOnGetOrderedActors, const AActor* /** InParent */)
	CLONEREFFECTOR_API void RegisterCustomActorResolver(FOnGetOrderedActors InCustomResolver);

	CLONEREFFECTOR_API void UnregisterCustomActorResolver();

	FOnGetOrderedActors& GetCustomActorResolver();

	CLONEREFFECTOR_API bool RegisterExtensionClass(UClass* InClass);

	CLONEREFFECTOR_API bool UnregisterExtensionClass(UClass* InClass);

	CLONEREFFECTOR_API bool IsExtensionClassRegistered(UClass* InClass) const;

	/** Get available cloner extension names to use */
	TSet<FName> GetExtensionNames() const;

	/** Get available cloner extension classes to use */
	TSet<TSubclassOf<UCEClonerExtensionBase>> GetExtensionClasses() const;

	/** Based on a extension class, find extension name */
	FName FindExtensionName(TSubclassOf<UCEClonerExtensionBase> InClass) const;

	/** Creates a new extension instance for a cloner */
	UCEClonerExtensionBase* CreateNewExtension(FName InExtensionName, UCEClonerComponent* InCloner);

	/** Set cloners state and optionally transact */
	CLONEREFFECTOR_API void SetClonersEnabled(const TSet<UCEClonerComponent*>& InCloners, bool bInEnable, bool bInShouldTransact);

	/** Set cloners state in world and optionally transact */
	CLONEREFFECTOR_API void SetLevelClonersEnabled(const UWorld* InWorld, bool bInEnable, bool bInShouldTransact);

#if WITH_EDITOR
	/** Converts cloners simulation to a mesh */
	CLONEREFFECTOR_API void ConvertCloners(const TSet<UCEClonerComponent*>& InCloners, ECEClonerMeshConversion InMeshConversion);

	/** Create cloners linked effector */
	CLONEREFFECTOR_API void CreateLinkedEffector(const TSet<UCEClonerComponent*>& InCloners);
#endif

	/** Creates a new cloner with actors attached */
	CLONEREFFECTOR_API AActor* CreateClonerWithActors(UWorld* InWorld, const TSet<AActor*>& InActors, bool bInShouldTransact);

protected:
	CLONEREFFECTOR_API static FOnSubsystemInitialized OnSubsystemInitializedDelegate;

	/** Delegate to change state of cloners in a world */
	static FOnClonerSetEnabled OnClonerSetEnabledDelegate;

	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	//~ End USubsystem

	void ScanForRegistrableClasses();

	/** Linking name to the layout class */
	UPROPERTY()
	TMap<FName, TSubclassOf<UCEClonerLayoutBase>> LayoutClasses;

	/** Linking name to the extension class */
	UPROPERTY()
	TMap<FName, TSubclassOf<UCEClonerExtensionBase>> ExtensionClasses;

	/** Used to gather ordered actors based on parent */
	FOnGetOrderedActors ActorResolver;
};