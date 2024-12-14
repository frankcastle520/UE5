// Copyright Epic Games, Inc. All Rights Reserved.

#include "Stateless/Modules/NiagaraStatelessModule_ShapeLocation.h"
#include "Stateless/NiagaraStatelessEmitterDataBuildContext.h"
#include "Stateless/NiagaraStatelessModuleShaderParameters.h"
#include "Stateless/NiagaraStatelessParticleSimContext.h"
#include "Stateless/NiagaraStatelessDrawDebugContext.h"

namespace NSMShapeLocationPrivate
{
	struct FModuleBuiltData
	{
		FUintVector4	Mode		= FUintVector4::ZeroValue;
		FVector4f		Parameters0	= FVector4f::Zero();
		FVector4f		Parameters1 = FVector4f::Zero();

		int32			PositionVariableOffset = INDEX_NONE;
		int32			PreviousPositionVariableOffset = INDEX_NONE;
	};

	FVector3f ShapeLocation_GetLocation(const NiagaraStateless::FParticleSimulationContext& ParticleSimulationContext, const FModuleBuiltData* ModuleBuiltData, uint32 iInstance)
	{
		// ENSM_ShapePrimitive::Box | ENSM_ShapePrimitive::Plane
		if ( ModuleBuiltData->Mode.X == 0 )
		{
			const FVector3f BoxScale = FVector3f(ModuleBuiltData->Parameters0);
			const FVector3f BoxBias = FVector3f(ModuleBuiltData->Parameters1);
			const bool bOnSurface = ModuleBuiltData->Mode.Y == 1;
			const float SurfaceScale = ModuleBuiltData->Parameters0.W;
			const float SurfaceBias = ModuleBuiltData->Parameters1.W;
	
			const FVector3f P0 = ParticleSimulationContext.RandomFloat3(iInstance, 0);
			if (bOnSurface)
			{
				//-TODO: This isn't quite what we want
				const FVector3f L0(FMath::RoundToFloat(P0.X), FMath::RoundToFloat(P0.Y), FMath::RoundToFloat(P0.Z));
				const uint32 S = ParticleSimulationContext.RandomUInt(iInstance, 1) % 3;
				FVector3f Location;
				Location.X = S != 0 ? P0.X : L0.X;
				Location.Y = S != 1 ? P0.Y : L0.Y;
				Location.Z = S != 2 ? P0.Z : L0.Z;
				return (Location * BoxScale + BoxBias) + (P0 * SurfaceScale + SurfaceBias);
			}
			else
			{
				return P0 * BoxScale + BoxBias;
			}
		}
	
		// ENSM_ShapePrimitive::Cylinder:
		if ( ModuleBuiltData->Mode.X == 1 )
		{
			const FVector4f Random = ParticleSimulationContext.RandomFloat4(iInstance, 0);
			const float HeightScale = ModuleBuiltData->Parameters0.X;
			const float HeightBias = ModuleBuiltData->Parameters0.Y;
			const float Radius = ModuleBuiltData->Parameters0.Z;
	
			const FVector2f UnitVec = ParticleSimulationContext.SafeNormalize(FVector2f(Random.X - 0.5f, Random.Y - 0.5f));
			return FVector3f(
				UnitVec.X * Radius * Random.Z,
				UnitVec.Y * Radius * Random.Z,
				Random.W * HeightScale + HeightBias
			);
		}
	
		// ENSM_ShapePrimitive::Ring:
		if ( ModuleBuiltData->Mode.X == 2 )
		{
			const float RadiusScale = ModuleBuiltData->Parameters0.X;
			const float RadiusBias = ModuleBuiltData->Parameters0.Y;
			const float UDistributionScale = ModuleBuiltData->Parameters0.Z;
			const float UDistributionBias = ModuleBuiltData->Parameters0.W;
	
			const float Radius = ParticleSimulationContext.RandomScaleBiasFloat(iInstance, 0, RadiusScale, RadiusBias);
			const float U = ParticleSimulationContext.RandomScaleBiasFloat(iInstance, 1, UDistributionScale, UDistributionBias);
	
			return FVector3f(cosf(U) * Radius, sinf(U) * Radius, 0.0f);
		}
	
		// ENSM_ShapePrimitive::Sphere:
		{
			const float SphereScale	= ModuleBuiltData->Parameters0.X;
			const float SphereBias	= ModuleBuiltData->Parameters0.Y;
	
			const FVector3f Vector = ParticleSimulationContext.RandomUnitFloat3(iInstance, 0);
			return Vector * ParticleSimulationContext.RandomScaleBiasFloat(iInstance, 1, SphereScale, SphereBias);
		}
	}

	static void ParticleSimulate(const NiagaraStateless::FParticleSimulationContext& ParticleSimulationContext)
	{
		using namespace NiagaraStateless;

		const FModuleBuiltData* ModuleBuiltData = ParticleSimulationContext.ReadBuiltData<FModuleBuiltData>();

		for (uint32 i = 0; i < ParticleSimulationContext.GetNumInstances(); ++i)
		{
			const FVector3f ShapeLocation = ShapeLocation_GetLocation(ParticleSimulationContext, ModuleBuiltData, i);
			const FVector3f Position = ParticleSimulationContext.ReadParticleVariable(ModuleBuiltData->PositionVariableOffset, i, FVector3f::ZeroVector);
			const FVector3f PreviousPosition = ParticleSimulationContext.ReadParticleVariable(ModuleBuiltData->PreviousPositionVariableOffset, i, FVector3f::ZeroVector);

			ParticleSimulationContext.WriteParticleVariable(ModuleBuiltData->PositionVariableOffset, i, Position + ShapeLocation);
			ParticleSimulationContext.WriteParticleVariable(ModuleBuiltData->PreviousPositionVariableOffset, i, PreviousPosition + ShapeLocation);
		}
	}
}

void UNiagaraStatelessModule_ShapeLocation::BuildEmitterData(const FNiagaraStatelessEmitterDataBuildContext& BuildContext) const
{
	using namespace NSMShapeLocationPrivate;

	FModuleBuiltData* BuiltData = BuildContext.AllocateBuiltData<FModuleBuiltData>();
	if (!IsModuleEnabled())
	{
		return;
	}

	const FNiagaraStatelessGlobals& StatelessGlobals = FNiagaraStatelessGlobals::Get();
	BuiltData->PositionVariableOffset			= BuildContext.FindParticleVariableIndex(StatelessGlobals.PositionVariable);
	BuiltData->PreviousPositionVariableOffset	= BuildContext.FindParticleVariableIndex(StatelessGlobals.PreviousPositionVariable);
	if (BuiltData->PositionVariableOffset == INDEX_NONE && BuiltData->PreviousPositionVariableOffset == INDEX_NONE)
	{
		return;
	}

	//-TODO: Build Parameters
	switch (ShapePrimitive)
	{
		case ENSM_ShapePrimitive::Box:
		{
			BuiltData->Mode.X		= 0;
			BuiltData->Mode.Y		= bBoxSurfaceOnly ? 1 : 0;
			BuiltData->Parameters0	= FVector4f(BoxSize, BoxSurfaceThicknessMax - BoxSurfaceThicknessMin);
			BuiltData->Parameters1	= FVector4f(BoxSize * -0.5f, BoxSurfaceThicknessMin);
			break;
		}
		case ENSM_ShapePrimitive::Plane:
		{
			BuiltData->Mode.X		= 0;
			BuiltData->Parameters0	= FVector4f(PlaneSize.X, PlaneSize.Y, 0.0f, 0.0f);
			BuiltData->Parameters1	= FVector4f(-PlaneSize.X * 0.5f, -PlaneSize.Y * 0.5f, 0.0f, 0.0f);
			break;
		}
		case ENSM_ShapePrimitive::Cylinder:
		{
			BuiltData->Mode.X		= 1;
			BuiltData->Parameters0.X = CylinderHeight;
			BuiltData->Parameters0.Y = CylinderHeight * -CylinderHeightMidpoint;
			BuiltData->Parameters0.Z = CylinderRadius;
			break;
		}
		case ENSM_ShapePrimitive::Ring:
		{
			const float DC = FMath::Clamp(1.0f - DiscCoverage, 0.0f, 1.0f);
			const float SDC = DC > 0.0f ? FMath::Sqrt(DC) : 0.0f;

			BuiltData->Mode.X		= 2;
			BuiltData->Parameters0.X = RingRadius * (1.0f - SDC);
			BuiltData->Parameters0.Y = RingRadius * SDC;
			BuiltData->Parameters0.Z = -UE_TWO_PI * (1.0f - RingUDistribution);
			BuiltData->Parameters0.W = 0.0f;
			break;
		}
		case ENSM_ShapePrimitive::Sphere:
		{
			BuiltData->Mode.X		= 3;
			BuiltData->Parameters0.X = SphereMax - SphereMin;
			BuiltData->Parameters0.Y = SphereMin;
			break;
		}
		default:
		{
			BuiltData->Mode.X = 0;
			BuiltData->Parameters0 = FVector4f::Zero();
			BuiltData->Parameters1 = FVector4f::Zero();
			checkNoEntry();
		}
	}

	BuildContext.AddParticleSimulationExecSimulate(&ParticleSimulate);
}

void UNiagaraStatelessModule_ShapeLocation::BuildShaderParameters(FNiagaraStatelessShaderParametersBuilder& ShaderParametersBuilder) const
{
	ShaderParametersBuilder.AddParameterNestedStruct<FParameters>();
}

void UNiagaraStatelessModule_ShapeLocation::SetShaderParameters(const FNiagaraStatelessSetShaderParameterContext& SetShaderParameterContext) const
{
	using namespace NSMShapeLocationPrivate;

	FParameters* Parameters = SetShaderParameterContext.GetParameterNestedStruct<FParameters>();
	const FModuleBuiltData* ModuleBuiltData = SetShaderParameterContext.ReadBuiltData<FModuleBuiltData>();

	Parameters->ShapeLocation_Mode			= ModuleBuiltData->Mode;
	Parameters->ShapeLocation_Parameters0	= ModuleBuiltData->Parameters0;
	Parameters->ShapeLocation_Parameters1	= ModuleBuiltData->Parameters1;
}

#if WITH_EDITOR
void UNiagaraStatelessModule_ShapeLocation::DrawDebug(const FNiagaraStatelessDrawDebugContext& DrawDebugContext) const
{
	switch (ShapePrimitive)
	{
		case ENSM_ShapePrimitive::Box:
		{
			DrawDebugContext.DrawBox(FVector::ZeroVector, FVector(BoxSize * 0.5f));
			break;
		}
		case ENSM_ShapePrimitive::Plane:
		{
			DrawDebugContext.DrawBox(FVector::ZeroVector, FVector(PlaneSize.X * 0.5f, PlaneSize.Y * 0.5f, 0.0f));
			break;
		}
		case ENSM_ShapePrimitive::Cylinder:
		{
			DrawDebugContext.DrawCylinder(CylinderHeight, CylinderRadius, CylinderHeightMidpoint);
			break;
		}
		case ENSM_ShapePrimitive::Ring:
		{
			const float DC = FMath::Clamp(1.0f - DiscCoverage, 0.0f, 1.0f);
			const float SDC = DC > 0.0f ? FMath::Sqrt(DC) : 0.0f;
			DrawDebugContext.DrawCircle(FVector::ZeroVector, RingRadius);
			DrawDebugContext.DrawCircle(FVector::ZeroVector, RingRadius * SDC);
			break;
		}
		case ENSM_ShapePrimitive::Sphere:
		{
			DrawDebugContext.DrawSphere(FVector::ZeroVector, SphereMin);
			DrawDebugContext.DrawSphere(FVector::ZeroVector, SphereMax);
			break;
		}
	}
}
#endif
