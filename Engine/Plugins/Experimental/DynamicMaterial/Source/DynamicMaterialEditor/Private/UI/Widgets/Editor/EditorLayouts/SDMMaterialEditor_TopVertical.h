// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UI/Widgets/Editor/EditorLayouts/SDMMaterialEditor_TopBase.h"

class SDMMaterialEditor_TopVertical : public SDMMaterialEditor_TopBase
{
	SLATE_BEGIN_ARGS(SDMMaterialEditor_TopVertical)
		: _MaterialModelBase(nullptr)
		, _MaterialProperty(TOptional<FDMObjectMaterialProperty>())
		{}
		SLATE_ARGUMENT(UDynamicMaterialModelBase*, MaterialModelBase)
		SLATE_ARGUMENT(TOptional<FDMObjectMaterialProperty>, MaterialProperty)
	SLATE_END_ARGS()

public:
	SDMMaterialEditor_TopVertical();

	virtual ~SDMMaterialEditor_TopVertical() override = default;

	void Construct(const FArguments& InArgs, const TSharedRef<SDMMaterialDesigner>& InDesignerWidget);

protected:
	FSlotBase* SplitterSlot_Top;

	void OnTopSplitterResized();

	//~ Begin SDMMaterialEditor_TopBase
	virtual TSharedRef<SWidget> CreateSlot_Top() override;
	//~ End SDMMaterialEditor_TopBase

	//~ Begin SDMMaterialEditor
	virtual TSharedRef<SWidget> CreateSlot_Main() override;
	virtual TSharedRef<SDMMaterialPropertySelector> CreateSlot_PropertySelector_Impl() override;
	//~ End SDMMaterialEditor
};
