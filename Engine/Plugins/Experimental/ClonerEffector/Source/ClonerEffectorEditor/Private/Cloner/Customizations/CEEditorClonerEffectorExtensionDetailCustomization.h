// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDetailCustomization.h"
#include "Templates/SharedPointer.h"

class AActor;
class IDetailCategoryBuilder;
class IPropertyHandle;

/** Used to customize cloner effector extension properties in details panel */
class FCEEditorClonerEffectorExtensionDetailCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance()
	{
		return MakeShared<FCEEditorClonerEffectorExtensionDetailCustomization>();
	}

	//~ Begin IDetailCustomization
	virtual void CustomizeDetails(IDetailLayoutBuilder& InDetailBuilder) override;
	//~ End IDetailCustomization

protected:
	static void CustomizeEffectorsProperty(TSharedRef<IPropertyHandle> InProperty, IDetailCategoryBuilder& InCategoryBuilder);
	static bool OnFilterEffectorActor(const AActor* InActor);
};
