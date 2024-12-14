// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

#include "Containers/ContainersFwd.h"
#include "Delegates/Delegate.h"
#include "Templates/SharedPointerFwd.h"

class AActor;
class UClass;
class UDynamicMaterialInstance;
class UDynamicMaterialModelBase;
class UObject;
struct FDMObjectMaterialProperty;
struct IDMOnWizardCompleteCallback;

DECLARE_DELEGATE_RetVal_OneParam(TArray<FDMObjectMaterialProperty>, FDMGetObjectMaterialPropertiesDelegate, UObject* InObject)

/** Material Designer - Build your own materials in a slimline editor! */
class IDynamicMaterialEditorModule : public IModuleInterface
{
protected:
	static constexpr const TCHAR* ModuleName = TEXT("DynamicMaterialEditor");

public:
	static bool IsLoaded()
	{
		return FModuleManager::Get().IsModuleLoaded(ModuleName);
	}

	static IDynamicMaterialEditorModule& Get()
	{
		return FModuleManager::LoadModuleChecked<IDynamicMaterialEditorModule>(ModuleName);
	}

	virtual void RegisterCustomMaterialPropertyGenerator(UClass* InClass, FDMGetObjectMaterialPropertiesDelegate InGenerator) = 0;

	virtual void RegisterMaterialModelCreatedCallback(const TSharedRef<IDMOnWizardCompleteCallback> InCallback) = 0;

	virtual void UnregisterMaterialModelCreatedCallback(const TSharedRef<IDMOnWizardCompleteCallback> InCallback) = 0;

	template<typename InCallbackType, typename... InArgsType
		UE_REQUIRES(std::is_base_of_v<IDMOnWizardCompleteCallback, InCallbackType>)>
	TSharedRef<InCallbackType> RegisterMaterialModelCreatedCallback(InArgsType&&... InArgs)
	{
		TSharedRef<InCallbackType> NewCallback = MakeShared<InCallbackType>(Forward<InArgsType>(InArgs)...);
		RegisterMaterialModelCreatedCallback(NewCallback);
		return NewCallback;
	}

	virtual void OpenEditor(UWorld* InWorld) const = 0;

	virtual UDynamicMaterialModelBase* GetOpenedMaterialModel(UWorld* InWorld) const = 0;

	virtual void OpenMaterialModel(UDynamicMaterialModelBase* InMaterialModel, UWorld* InWorld, bool bInInvokeTab) const = 0;

	virtual void OpenMaterialObjectProperty(const FDMObjectMaterialProperty& InObjectProperty, UWorld* InWorld, bool bInInvokeTab) const = 0;

	virtual void OpenMaterial(UDynamicMaterialInstance* InInstance, UWorld* InWorld, bool bInInvokeTab) const = 0;

	virtual void OnActorSelected(AActor* InActor, UWorld* InWorld, bool bInInvokeTab) const = 0;

	virtual void ClearDynamicMaterialModel(UWorld* InWorld) const = 0;
};
