// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraDataInterfaceDetails.h"

class IDetailLayoutBuilder;
class FNiagaraDetailSourcedArrayBuilder;
class UNiagaraDataInterfaceSocketReader;

class FNiagaraDataInterfaceSocketReaderDetails : public IDetailCustomization//FNiagaraDataInterfaceDetailsBase
{
public:
	~FNiagaraDataInterfaceSocketReaderDetails();
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
	static TSharedRef<IDetailCustomization> MakeInstance();

private:
	void OnDataChanged();

	TArray<TSharedPtr<FName>> GetSocketNames() const;

private:
	TWeakObjectPtr<UNiagaraDataInterfaceSocketReader>	WeakDataInterface;
	TSharedPtr<FNiagaraDetailSourcedArrayBuilder>		SocketArrayBuilder;
};
