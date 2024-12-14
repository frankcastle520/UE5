// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UI/Widgets/SDMMaterialEditor.h"

class SDMMaterialEditor_LeftBase : public SDMMaterialEditor
{
	SLATE_BEGIN_ARGS(SDMMaterialEditor_LeftBase)
		: _MaterialModelBase(nullptr)
		, _MaterialProperty(TOptional<FDMObjectMaterialProperty>())
		{}
		SLATE_ARGUMENT(UDynamicMaterialModelBase*, MaterialModelBase)
		SLATE_ARGUMENT(TOptional<FDMObjectMaterialProperty>, MaterialProperty)
	SLATE_END_ARGS()

public:
	virtual ~SDMMaterialEditor_LeftBase() override = default;

	void Construct(const FArguments& InArgs, const TSharedRef<SDMMaterialDesigner>& InDesignerWidget);

	//~ Begin SDMMaterialEditor
	virtual void EditSlot(UDMMaterialSlot* InSlot, bool bInForceRefresh = false) override;
	virtual void EditComponent(UDMMaterialComponent* InComponent, bool bInForceRefresh = false) override;
	virtual void EditGlobalSettings(bool bInForceRefresh = false) override;
	virtual void EditProperties(bool bInForceRefresh = false) override;
	//~ End SDMMaterialEditor

protected:
	TDMWidgetSlot<SWidget> LeftSlot;
	TDMWidgetSlot<SWidget> RightSlot;

	virtual TSharedRef<SWidget> CreateSlot_Left() = 0;

	TSharedRef<SWidget> CreateSlot_Right();

	TSharedRef<SWidget> CreateSlot_Right_GlobalSettings();

	TSharedRef<SWidget> CreateSlot_Right_PropertyPreviews();

	TSharedRef<SWidget> CreateSlot_Right_EditSlot();

	//~ Begin SDMMaterialEditor
	virtual void ValidateSlots_Main() override;
	virtual void ClearSlots_Main() override;
	virtual TSharedRef<SWidget> CreateSlot_Main() override;
	//~ End SDMMaterialEditor
};
