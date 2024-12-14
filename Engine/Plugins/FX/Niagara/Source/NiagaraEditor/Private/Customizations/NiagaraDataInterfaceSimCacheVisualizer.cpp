// Copyright Epic Games, Inc. All Rights Reserved.


#include "Customizations/NiagaraDataInterfaceSimCacheVisualizer.h"
#include "ViewModels/NiagaraSimCacheViewModel.h"


TSharedPtr<SWidget> INiagaraDataInterfaceSimCacheVisualizer::CreateWidgetFor(const UObject*, TSharedPtr<FNiagaraSimCacheViewModel>)
{
	return SNullWidget::NullWidget;
}
