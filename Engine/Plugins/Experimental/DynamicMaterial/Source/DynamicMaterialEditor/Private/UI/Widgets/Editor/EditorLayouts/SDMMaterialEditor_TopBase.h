// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UI/Widgets/SDMMaterialEditor.h"

class SDMMaterialEditor_TopBase : public SDMMaterialEditor
{
	SLATE_BEGIN_ARGS(SDMMaterialEditor_TopBase)
		: _MaterialModelBase(nullptr)
		, _MaterialProperty(TOptional<FDMObjectMaterialProperty>())
		{}
		SLATE_ARGUMENT(UDynamicMaterialModelBase*, MaterialModelBase)
		SLATE_ARGUMENT(TOptional<FDMObjectMaterialProperty>, MaterialProperty)
	SLATE_END_ARGS()

public:
	virtual ~SDMMaterialEditor_TopBase() override = default;

	void Construct(const FArguments& InArgs, const TSharedRef<SDMMaterialDesigner>& InDesignerWidget);

	//~ Begin SDMMaterialEditor
	virtual void EditSlot(UDMMaterialSlot* InSlot, bool bInForceRefresh = false) override;
	virtual void EditComponent(UDMMaterialComponent* InComponent, bool bInForceRefresh = false) override;
	virtual void EditGlobalSettings(bool bInForceRefresh = false) override;
	virtual void EditProperties(bool bInForceRefresh = false) override;
	//~ End SDMMaterialEditor

protected:
	TDMWidgetSlot<SWidget> TopSlot;
	TDMWidgetSlot<SWidget> BottomSlot;

	virtual TSharedRef<SWidget> CreateSlot_Top() = 0;

	TSharedRef<SWidget> CreateSlot_Bottom();

	TSharedRef<SWidget> CreateSlot_Bottom_GlobalSettings();

	TSharedRef<SWidget> CreateSlot_Bottom_PropertyPreviews();

	TSharedRef<SWidget> CreateSlot_Bottom_EditSlot();

	//~ Begin SDMMaterialEditor
	virtual void ValidateSlots_Main() override;
	virtual void ClearSlots_Main() override;
	virtual TSharedRef<SWidget> CreateSlot_Main() override;
	//~ End SDMMaterialEditor
};
