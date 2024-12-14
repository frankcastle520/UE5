// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once 

template<typename TBaseInnerStep>
struct FInnerStepArrayBuilder
{
	FInnerStepArrayBuilder()
	{}

	template<typename T, typename... TArguments>
	FInnerStepArrayBuilder&& EmplaceInnerStep(TArguments&&... Args)
	{
		Steps.Emplace(new T(std::forward<TArguments>(Args)...));	
		return std::move(*this);
	}

	TArray<TUniquePtr<TBaseInnerStep>> Steps;
};
