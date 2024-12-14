// Copyright Epic Games, Inc. All Rights Reserved.

#include "Stateless/NiagaraStatelessParticleSimExecData.h"

#include "NiagaraDataSetCompiledData.h"

namespace NiagaraStateless
{
	FParticleSimulationExecData::FParticleSimulationExecData(const FNiagaraDataSetCompiledData& ParticleDataSetCompiledData)
	{
		const int32 NumVariables = FMath::Min(ParticleDataSetCompiledData.Variables.Num(), ParticleDataSetCompiledData.VariableLayouts.Num());
		VariableComponentOffsets.AddDefaulted(NumVariables);

		for (int32 i = 0; i < NumVariables; ++i)
		{
			const FNiagaraVariableLayoutInfo& VariableLayout = ParticleDataSetCompiledData.VariableLayouts[i];
			const FNiagaraVariableBase& Variable = ParticleDataSetCompiledData.Variables[i];
			if (VariableLayout.GetNumFloatComponents() > 0)
			{
				check(VariableLayout.GetNumInt32Components() == 0 && VariableLayout.GetNumHalfComponents() == 0);
				VariableComponentOffsets[i].Type	= 0;
				VariableComponentOffsets[i].Offset	= VariableLayout.GetFloatComponentStart();
			}
			else if (VariableLayout.GetNumInt32Components() > 0)
			{
				check(VariableLayout.GetNumFloatComponents() == 0 && VariableLayout.GetNumHalfComponents() == 0);
				VariableComponentOffsets[i].Type	= 1;
				VariableComponentOffsets[i].Offset	= VariableLayout.GetInt32ComponentStart();

				if (Variable.GetName() == FNiagaraStatelessGlobals::Get().UniqueIDVariable.GetName())
				{
					UniqueIDIndex = i;
				}
			}
			else
			{
				// We don't support half components
				checkNoEntry();
			}
		}
	}

} //namespace NiagaraStateless
