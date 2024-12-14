// Copyright Epic Games, Inc. All Rights Reserved.

#include "Helpers/PCGAttributeNiagaraTraits.h"

bool PCGAttributeNiagaraTraits::AreTypesCompatible(uint16 PCGType, const FNiagaraVariableBase& NiagaraVar, bool bPCGToNiagara)
{
	return CallbackWithNiagaraType(NiagaraVar, [PCGType, bPCGToNiagara]<typename T>(T) -> bool
	{
		if constexpr (std::is_same_v<T, FNiagaraSpawnInfo>)
		{
			// Not supported at the moment
			return false;
		}
		else
		{
			using NiagaraType = std::conditional_t<std::is_same_v<T, FLinearColor>, FVector4, T>;
			return bPCGToNiagara
				? PCG::Private::IsBroadcastableOrConstructible(PCGType, PCG::Private::MetadataTypes<NiagaraType>::Id)
				: PCG::Private::IsBroadcastableOrConstructible(PCG::Private::MetadataTypes<NiagaraType>::Id, PCGType);
		}
	});
}