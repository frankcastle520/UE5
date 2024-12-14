// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraCommon.h"
#include "Customizations/NiagaraDataInterfaceSimCacheVisualizer.h"

/*
 * Provides a custom widget to show the array DI data in the sim cache in a table.
 */
class FNiagaraSimpleCounterSimCacheVisualizer : public INiagaraDataInterfaceSimCacheVisualizer
{
public:
	FNiagaraSimpleCounterSimCacheVisualizer() {}
	virtual ~FNiagaraSimpleCounterSimCacheVisualizer() override = default;
	
	virtual TSharedPtr<SWidget> CreateWidgetFor(const UObject* CachedData, TSharedPtr<FNiagaraSimCacheViewModel> ViewModel) override;
};
