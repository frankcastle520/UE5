// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"

class FNiagaraSimCacheViewModel;
struct FNiagaraSimCacheDebugDataFrame;
namespace UE::Niagara::SimCache::DebugDataUI
{
	class SParameterStoreListView;
};

class SNiagaraSimCacheDebugDataView : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNiagaraSimCacheDebugDataView) {}
		SLATE_ARGUMENT(TSharedPtr<FNiagaraSimCacheViewModel>, SimCacheViewModel)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	const FNiagaraSimCacheDebugDataFrame* GetCurrentFrameData() const;
	TSharedRef<SWidget> GetParameterStoreSelectionMenu();

	void RefreshContents();
	void RefreshContents(bool);

private:
	TSharedPtr<FNiagaraSimCacheViewModel> SimCacheViewModel;
	TSharedPtr<UE::Niagara::SimCache::DebugDataUI::SParameterStoreListView> OverrideParametersWidget;
	FString	SelectedParameterStoreName;
};
