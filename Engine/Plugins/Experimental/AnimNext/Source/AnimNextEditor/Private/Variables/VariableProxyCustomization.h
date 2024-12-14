// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDetailCustomization.h"
#include "IPropertyTypeCustomization.h"
#include "PropertyEditorModule.h"
#include "StructUtils/PropertyBag.h"

class UAnimNextRigVMAssetEditorData;
enum class EAnimNextEditorDataNotifType : uint8;

namespace UE::AnimNext::Editor
{

class FVariableProxyCustomization : public IDetailCustomization
{
private:
	// IDetailCustomization interface
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
	virtual void CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder) override;

	void HandleModified(UAnimNextRigVMAssetEditorData* InEditorData, EAnimNextEditorDataNotifType InType, UObject* InSubject);

	void HandleGlobalRowExtension(const FOnGenerateGlobalRowExtensionArgs& InArgs, TArray<FPropertyRowExtensionButton>& OutExtensions);

	TSharedPtr<IPropertyHandle> PropertyHandle;
	FInstancedPropertyBag InternalPropertyBag;
	TWeakPtr<IDetailLayoutBuilder> WeakDetailBuilder;
};

}
