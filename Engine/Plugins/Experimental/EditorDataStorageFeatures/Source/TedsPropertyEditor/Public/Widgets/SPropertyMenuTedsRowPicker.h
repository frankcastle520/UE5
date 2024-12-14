// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TedsRowPickingMode.h"
#include "Widgets/SCompoundWidget.h"

class TEDSPROPERTYEDITOR_API SPropertyMenuTedsRowPicker : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SPropertyMenuTedsRowPicker)
		: _AllowClear(true)
		, _ElementFilter()
	{}
		SLATE_ARGUMENT(bool, AllowClear)
		SLATE_ARGUMENT(UE::Editor::DataStorage::FQueryDescription, QueryFilter)
		SLATE_ARGUMENT(FOnShouldFilterTedsRow, ElementFilter)
		SLATE_EVENT(FOnTedsRowSelected, OnSet)
		SLATE_EVENT(FSimpleDelegate, OnClose)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:

	void OnClear();

	void OnElementSelected(UE::Editor::DataStorage::RowHandle RowHandle);

	void SetValue(UE::Editor::DataStorage::RowHandle RowHandle);

private:
	bool bAllowClear;

	UE::Editor::DataStorage::FQueryDescription QueryFilter;

	FOnShouldFilterTedsRow ElementFilter;

	FOnTedsRowSelected OnSet;

	FSimpleDelegate OnClose;

	FSimpleDelegate OnUseSelected;
};