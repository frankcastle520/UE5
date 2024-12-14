// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UI/Widgets/Editor/SDMObjectEditorWidgetBase.h"

class UDynamicMaterialModelBase;

class SDMMaterialGlobalSettingsEditor : public SDMObjectEditorWidgetBase
{
	SLATE_DECLARE_WIDGET(SDMMaterialGlobalSettingsEditor, SCompoundWidget)

	SLATE_BEGIN_ARGS(SDMMaterialGlobalSettingsEditor) {}
	SLATE_END_ARGS()

public:
	virtual ~SDMMaterialGlobalSettingsEditor() override = default;

	void Construct(const FArguments& InArgs, const TSharedRef<SDMMaterialEditor>& InEditorWidget, 
		UDynamicMaterialModelBase* InMaterialModelBase);

	UDynamicMaterialModelBase* GetMaterialModelBase() const;

protected:
	//~ Begin SDMObjectEditorWidgetBase
	virtual TArray<FDMPropertyHandle> GetPropertyRows() override;
	virtual void OnUndo() override;
	//~ End SDMObjectEditorWidgetBase
};
