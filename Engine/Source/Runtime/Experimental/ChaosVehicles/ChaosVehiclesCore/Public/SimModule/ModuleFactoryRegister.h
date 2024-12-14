// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "SimulationModuleBase.h"
#include "Templates/SharedPointer.h"
#include "Hash/CityHash.h"

namespace Chaos
{

	struct FModuleNetData;
	class IFactoryModule;

	class CHAOSVEHICLESCORE_API FModuleFactoryRegister
	{
	public:
		static FModuleFactoryRegister& Get()
		{
			static FModuleFactoryRegister Instance;
			return Instance;
		}
		
		void RegisterFactory(const FName TypeName, TWeakPtr<IFactoryModule> InFactory);
		void RegisterFactory(const uint32 TypeNameHash, TWeakPtr<IFactoryModule> InFactory);
		void RemoveFactory(TWeakPtr<IFactoryModule> InFactory);
		void Reset();
		bool ContainsFactory(const FName TypeName) const;
		bool ContainsFactory(const uint32 TypeNameHash) const;
		TSharedPtr<Chaos::FModuleNetData> GenerateNetData(const uint32 TypeNameHash, const int32 SimArrayIndex);

		static uint32 GetModuleHash(const FName TypeName)
		{
			uint32 Hash = CityHash32((const char*)TypeName.ToString().GetCharArray().GetData(), sizeof(FString::ElementType) * TypeName.GetStringLength());
			return Hash;
		}
	protected:

		FModuleFactoryRegister() = default;
		TMap<int32, TWeakPtr<IFactoryModule>> RegisteredFactoriesByName;
	};
	
	template<typename _To, typename ..._Rest>
	class TSimulationModuleTypeable;
	//Static helper function to create and register a factory of the correct type. The returned Factory MUST be stored somewhere by the caller.
	template<typename T, typename... Args>
	static bool RegisterFactoryHelper(Args... args)
	{
		FName SimTypeName = T::StaticSimType();
		if(SimTypeName.IsValid() == false)
		{
			return false;
		}
		uint32 SimTypeNameHash = FModuleFactoryRegister::GetModuleHash(SimTypeName);
		if(FModuleFactoryRegister::Get().ContainsFactory(SimTypeNameHash))
		{
			return true;
		}
		static TSharedPtr<T> SharedFactory = MakeShared<T>(args...);
		if (SharedFactory.IsValid())
		{
			FModuleFactoryRegister::Get().RegisterFactory(SimTypeNameHash, SharedFactory);
			return true;
		}
		return false;
	}
} // namespace Chaos
