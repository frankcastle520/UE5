// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UI/Widgets/Editor/SDMObjectEditorWidgetBase.h"

enum class EDMUpdateType : uint8;
class UDMMaterialComponent;

/** Extends the object editor to provide component-specific events and properties. */
class SDMMaterialComponentEditor : public SDMObjectEditorWidgetBase
{
	SLATE_DECLARE_WIDGET(SDMMaterialComponentEditor, SCompoundWidget)

	SLATE_BEGIN_ARGS(SDMMaterialComponentEditor) {}
	SLATE_END_ARGS()

public:
	virtual ~SDMMaterialComponentEditor() override;

	void Construct(const FArguments& InArgs, const TSharedRef<SDMMaterialEditor>& InEditorWidget, UDMMaterialComponent* InMaterialComponent);

	UDMMaterialComponent* GetComponent() const;

protected:
	void OnComponentUpdated(UDMMaterialComponent* InComponent, UDMMaterialComponent* InSource, EDMUpdateType InUpdateType);

	//~ Begin SDMObjectEditorWidgetBase
	virtual TSharedRef<ICustomDetailsViewItem> GetDefaultCategory(const TSharedRef<ICustomDetailsView>& InDetailsView,
		const FCustomDetailsViewItemId& InRootId) override;
	virtual TArray<FDMPropertyHandle> GetPropertyRows() override;
	virtual void OnUndo() override;
	//~ End SDMObjectEditorWidgetBase
};
