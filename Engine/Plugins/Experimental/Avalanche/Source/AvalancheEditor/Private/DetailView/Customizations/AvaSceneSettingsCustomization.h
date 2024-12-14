// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDetailCustomization.h"
#include "Templates/SharedPointerFwd.h"

class IAvaEditor;
class IDetailLayoutBuilder;

class FAvaSceneSettingsCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeDefaultInstance();

	static TSharedRef<IDetailCustomization> MakeInstance(TWeakPtr<IAvaEditor> InEditorWeak);

	explicit FAvaSceneSettingsCustomization(const TWeakPtr<IAvaEditor>& InEditorWeak);

	//~ Begin IDetailCustomization
	virtual void CustomizeDetails(IDetailLayoutBuilder& InDetailBuilder) override;
	//~ End IDetailCustomization

private:
	TWeakPtr<IAvaEditor> EditorWeak;
};
