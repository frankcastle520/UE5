// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/ISubmitToolService.h"

template<class E>
concept DerivedFromISubmitToolService = std::is_base_of<ISubmitToolService, E>::value;

class FSubmitToolServiceProvider final
{

public:
	template<DerivedFromISubmitToolService T>
	TSharedPtr<T> GetService()
	{
		FString TypeIdx = TNameOf<T>::GetName();
		if(Services.Contains(TypeIdx))
		{
			return StaticCastSharedPtr<T>(Services[TypeIdx]);
		}

		return nullptr; 
	}

	template<DerivedFromISubmitToolService T>
	void RegisterService(TSharedRef<T> InService)
	{
		Services.FindOrAdd(TNameOf<T>::GetName(), StaticCastSharedPtr<ISubmitToolService>(InService.ToSharedPtr()));
	}

private:
	TMap<FString, TSharedPtr<ISubmitToolService>> Services;
};