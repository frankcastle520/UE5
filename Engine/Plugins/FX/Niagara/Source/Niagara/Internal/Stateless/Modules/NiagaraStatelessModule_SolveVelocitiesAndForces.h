// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Stateless/NiagaraStatelessModule.h"
#include "Stateless/NiagaraStatelessEmitterDataBuildContext.h"
#include "Stateless/NiagaraStatelessModuleShaderParameters.h"
#include "Stateless/NiagaraStatelessParticleSimContext.h"
#include "Stateless/Modules/NiagaraStatelessModuleCommon.h"

#include "VectorField/VectorField.h"
#include "VectorField/VectorFieldStatic.h"
#include "RHIStaticStates.h"

#include "NiagaraStatelessModule_SolveVelocitiesAndForces.generated.h"

// Integrates all the forces applying them to position
UCLASS(MinimalAPI, EditInlineNew, meta = (DisplayName = "Solve Forces And Velocity"))
class UNiagaraStatelessModule_SolveVelocitiesAndForces : public UNiagaraStatelessModule
{
	GENERATED_BODY()

	struct FModuleBuiltData
	{
		NiagaraStateless::FPhysicsBuildData	PhysicsData;
		int32								PositionVariableOffset = INDEX_NONE;
		int32								VelocityVariableOffset = INDEX_NONE;
		int32								PreviousPositionVariableOffset = INDEX_NONE;
		int32								PreviousVelocityVariableOffset = INDEX_NONE;
	};

public:
	using FParameters = NiagaraStateless::FSolveVelocitiesAndForcesModule_ShaderParameters;

	virtual void BuildEmitterData(const FNiagaraStatelessEmitterDataBuildContext& BuildContext) const override
	{
		NiagaraStateless::FPhysicsBuildData& PhysicsBuildData = BuildContext.GetTransientBuildData<NiagaraStateless::FPhysicsBuildData>();

		FModuleBuiltData* BuiltData				= BuildContext.AllocateBuiltData<FModuleBuiltData>();
		BuiltData->PhysicsData					= PhysicsBuildData;
		BuiltData->PhysicsData.DragRange.Min	= FMath::Max(PhysicsBuildData.DragRange.Min, 0.01f);
		BuiltData->PhysicsData.DragRange.Max	= FMath::Max(PhysicsBuildData.DragRange.Max, 0.01f);

		const FNiagaraStatelessGlobals& StatelessGlobals = FNiagaraStatelessGlobals::Get();
		BuiltData->PositionVariableOffset				= BuildContext.FindParticleVariableIndex(StatelessGlobals.PositionVariable);
		BuiltData->VelocityVariableOffset				= BuildContext.FindParticleVariableIndex(StatelessGlobals.VelocityVariable);
		BuiltData->PreviousPositionVariableOffset		= BuildContext.FindParticleVariableIndex(StatelessGlobals.PreviousPositionVariable);
		BuiltData->PreviousVelocityVariableOffset		= BuildContext.FindParticleVariableIndex(StatelessGlobals.PreviousVelocityVariable);
		
		const bool bAttributesUsed =
			BuiltData->PositionVariableOffset != INDEX_NONE ||
			BuiltData->VelocityVariableOffset != INDEX_NONE ||
			BuiltData->PreviousPositionVariableOffset != INDEX_NONE ||
			BuiltData->PreviousVelocityVariableOffset != INDEX_NONE;

		if (bAttributesUsed)
		{
			BuildContext.AddParticleSimulationExecSimulate(&UNiagaraStatelessModule_SolveVelocitiesAndForces::ParticleSimulate);
		}
	}

	virtual void BuildShaderParameters(FNiagaraStatelessShaderParametersBuilder& ShaderParametersBuilder) const override
	{
		ShaderParametersBuilder.AddParameterNestedStruct<FParameters>();
	}

	virtual void SetShaderParameters(const FNiagaraStatelessSetShaderParameterContext& SetShaderParameterContext) const override
	{
		const FModuleBuiltData* ModuleBuiltData = SetShaderParameterContext.ReadBuiltData<FModuleBuiltData>();
		const NiagaraStateless::FPhysicsBuildData& PhysicsData = ModuleBuiltData->PhysicsData;

		const FNiagaraStatelessSpaceTransforms& SpaceTransforms = SetShaderParameterContext.GetSpaceTransforms();

		FParameters* Parameters = SetShaderParameterContext.GetParameterNestedStruct<FParameters>();
		Parameters->SolveVelocitiesAndForces_MassScale				= PhysicsData.MassRange.GetScale();
		Parameters->SolveVelocitiesAndForces_MassBias				= PhysicsData.MassRange.Min;
		Parameters->SolveVelocitiesAndForces_DragScale				= PhysicsData.DragRange.GetScale();
		Parameters->SolveVelocitiesAndForces_DragBias				= PhysicsData.DragRange.Min;
		Parameters->SolveVelocitiesAndForces_VelocityScale			= SpaceTransforms.TransformVector(PhysicsData.VelocityCoordinateSpace, PhysicsData.VelocityRange.GetScale());
		Parameters->SolveVelocitiesAndForces_VelocityBias			= SpaceTransforms.TransformVector(PhysicsData.VelocityCoordinateSpace, PhysicsData.VelocityRange.Min);
		Parameters->SolveVelocitiesAndForces_WindScale				= SpaceTransforms.TransformVector(PhysicsData.WindCoordinateSpace, PhysicsData.WindRange.GetScale());
		Parameters->SolveVelocitiesAndForces_WindBias				= SpaceTransforms.TransformVector(PhysicsData.WindCoordinateSpace, PhysicsData.WindRange.Min);
		Parameters->SolveVelocitiesAndForces_AccelerationScale		= SpaceTransforms.TransformVector(PhysicsData.AccelerationCoordinateSpace, PhysicsData.AccelerationRange.GetScale());
		Parameters->SolveVelocitiesAndForces_AccelerationBias		= SpaceTransforms.TransformVector(PhysicsData.AccelerationCoordinateSpace, PhysicsData.AccelerationRange.Min);
		Parameters->SolveVelocitiesAndForces_AccelerationScale		+= SpaceTransforms.TransformVector(ENiagaraCoordinateSpace::World, PhysicsData.GravityRange.GetScale());
		Parameters->SolveVelocitiesAndForces_AccelerationBias		+= SpaceTransforms.TransformVector(ENiagaraCoordinateSpace::World, PhysicsData.GravityRange.Min);

		Parameters->SolveVelocitiesAndForces_ConeVelocityEnabled	= PhysicsData.bConeVelocity ? 1 : 0;
		Parameters->SolveVelocitiesAndForces_ConeQuat				= SpaceTransforms.TransformRotation(PhysicsData.ConeCoordinateSpace, PhysicsData.ConeQuat);
		Parameters->SolveVelocitiesAndForces_ConeVelocityScale		= PhysicsData.ConeVelocityRange.GetScale();
		Parameters->SolveVelocitiesAndForces_ConeVelocityBias		= PhysicsData.ConeVelocityRange.Min;
		Parameters->SolveVelocitiesAndForces_ConeAngleScale			= (PhysicsData.ConeOuterAngle - PhysicsData.ConeInnerAngle) * (UE_PI / 360.0f);
		Parameters->SolveVelocitiesAndForces_ConeAngleBias			= PhysicsData.ConeInnerAngle * (UE_PI / 360.0f);
		Parameters->SolveVelocitiesAndForces_ConeVelocityFalloff	= PhysicsData.ConeVelocityFalloff;

		Parameters->SolveVelocitiesAndForces_PontVelocityEnabled	= PhysicsData.bPointVelocity ? 1 : 0;
		Parameters->SolveVelocitiesAndForces_PointVelocityScale		= PhysicsData.PointVelocityRange.GetScale();
		Parameters->SolveVelocitiesAndForces_PointVelocityBias		= PhysicsData.PointVelocityRange.Min;
		Parameters->SolveVelocitiesAndForces_PointOrigin			= SpaceTransforms.TransformPosition(PhysicsData.PointCoordinateSpace, PhysicsData.PointOrigin);

		Parameters->SolveVelocitiesAndForces_NoiseEnabled			= PhysicsData.bNoiseEnabled ? 1 : 0;
		Parameters->SolveVelocitiesAndForces_NoiseAmplitude			= PhysicsData.NoiseAmplitude;
		Parameters->SolveVelocitiesAndForces_NoiseFrequency			= FVector3f(PhysicsData.NoiseFrequency, PhysicsData.NoiseFrequency, PhysicsData.NoiseFrequency);
		//SetShaderParameterContext.SetTextureResource(&Parameters->SolveVelocitiesAndForces_NoiseTexture, ModuleBuildData->NoiseTexture);
		Parameters->SolveVelocitiesAndForces_NoiseMode				= PhysicsData.NoiseMode;
		Parameters->SolveVelocitiesAndForces_NoiseLUTOffset			= PhysicsData.NoiseLUTOffset;
		Parameters->SolveVelocitiesAndForces_NoiseLUTNumChannel		= PhysicsData.NoiseLUTNumChannel;
		Parameters->SolveVelocitiesAndForces_NoiseLUTChannelWidth	= PhysicsData.NoiseLUTChannelWidth;

		FVectorFieldTextureAccessor TextureAccessor(Cast<UVectorField>(PhysicsData.NoiseTexture));

		ENQUEUE_RENDER_COMMAND(FNaughtyTest)(
			[Parameters, TextureAccessor](FRHICommandListImmediate& RHICmdList)
			{
				FRHITexture* NoiseTextureRHI = TextureAccessor.GetTexture();
				Parameters->SolveVelocitiesAndForces_NoiseTexture = NoiseTextureRHI;
				Parameters->SolveVelocitiesAndForces_NoiseSampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();

				if (Parameters->SolveVelocitiesAndForces_NoiseMode < 2)
				{
					const FIntVector TextureSize = NoiseTextureRHI ? NoiseTextureRHI->GetSizeXYZ() : FIntVector(1, 1, 1);
					Parameters->SolveVelocitiesAndForces_NoiseFrequency.X *= 1.0f / float(TextureSize.X);
					Parameters->SolveVelocitiesAndForces_NoiseFrequency.Y *= 1.0f / float(TextureSize.Y);
					Parameters->SolveVelocitiesAndForces_NoiseFrequency.Z *= 1.0f / float(TextureSize.Z);
				}
			}
		);
	}

	static FVector3f IntegratePosition(float Age, float Mass, float Drag, const FVector3f& Velocity, const FVector3f& Wind, const FVector3f& Acceleration)
	{
		const FVector3f IntVelocity = (Velocity - Wind) + (Wind * Age * Age);
		const float LambdaDragMass = FMath::Max(Drag * (1.0f / Mass), 0.0001f);
		const float LambdaAge = (1.0f - FMath::Exp(-(LambdaDragMass * Age))) / LambdaDragMass;
		FVector3f Position = IntVelocity * LambdaAge;
		Position += (Age - LambdaAge) * (Acceleration / LambdaDragMass);
		return Position;
	}

	static void ParticleSimulate(const NiagaraStateless::FParticleSimulationContext& ParticleSimulationContext)
	{
		const FModuleBuiltData* ModuleBuiltData = ParticleSimulationContext.ReadBuiltData<FModuleBuiltData>();
		const FParameters* Parameters = ParticleSimulationContext.ReadParameterNestedStruct<FParameters>();

		const float* AgeData			= ParticleSimulationContext.GetParticleAge();
		const float* PreviousAgeData	= ParticleSimulationContext.GetParticlePreviousAge();
		const float InvDeltaTime		= ParticleSimulationContext.GetInvDeltaTime();

		for (uint32 i = 0; i < ParticleSimulationContext.GetNumInstances(); ++i)
		{
			const float Mass				= ParticleSimulationContext.RandomScaleBiasFloat(i, 0, Parameters->SolveVelocitiesAndForces_MassScale, Parameters->SolveVelocitiesAndForces_MassBias);
			const float Drag				= ParticleSimulationContext.RandomScaleBiasFloat(i, 1, Parameters->SolveVelocitiesAndForces_DragScale, Parameters->SolveVelocitiesAndForces_DragBias);
			FVector3f InitialVelocity		= ParticleSimulationContext.RandomScaleBiasFloat(i, 2, Parameters->SolveVelocitiesAndForces_VelocityScale, Parameters->SolveVelocitiesAndForces_VelocityBias);
			const FVector3f Wind			= ParticleSimulationContext.RandomScaleBiasFloat(i, 3, Parameters->SolveVelocitiesAndForces_WindScale, Parameters->SolveVelocitiesAndForces_WindBias);
			const FVector3f Acceleration	= ParticleSimulationContext.RandomScaleBiasFloat(i, 4, Parameters->SolveVelocitiesAndForces_AccelerationScale, Parameters->SolveVelocitiesAndForces_AccelerationBias);

			FVector3f Position			= ParticleSimulationContext.ReadParticleVariable(ModuleBuiltData->PositionVariableOffset, i, FVector3f::ZeroVector);
			FVector3f PreviousPosition	= ParticleSimulationContext.ReadParticleVariable(ModuleBuiltData->PreviousPositionVariableOffset, i, FVector3f::ZeroVector);

			if (ModuleBuiltData->PhysicsData.bConeVelocity)
			{
				const float ConeAngle = ParticleSimulationContext.RandomScaleBiasFloat(i, 5, Parameters->SolveVelocitiesAndForces_ConeAngleScale, Parameters->SolveVelocitiesAndForces_ConeAngleBias);
				const float ConeRotation = ParticleSimulationContext.RandomFloat(i, 6) * UE_TWO_PI;
				const FVector2f scAng = FVector2f(FMath::Sin(ConeAngle), FMath::Cos(ConeAngle));
				const FVector2f scRot = FVector2f(FMath::Sin(ConeRotation), FMath::Cos(ConeRotation));
				const FVector3f Direction = FVector3f(scRot.X * scAng.X, scRot.Y * scAng.X, scAng.Y);

				float VelocityScale = ParticleSimulationContext.RandomScaleBiasFloat(i, 7, Parameters->SolveVelocitiesAndForces_ConeVelocityScale, Parameters->SolveVelocitiesAndForces_ConeVelocityBias);
				if (ModuleBuiltData->PhysicsData.bConeVelocity)
				{
					const float pf = FMath::Pow(FMath::Clamp(scAng.Y, 0.0f, 1.0f), Parameters->SolveVelocitiesAndForces_ConeVelocityFalloff * 10.0f);
					VelocityScale *= FMath::Lerp(1.0f, pf, Parameters->SolveVelocitiesAndForces_ConeVelocityFalloff);
				}

				InitialVelocity += ModuleBuiltData->PhysicsData.ConeQuat.RotateVector(Direction) * VelocityScale;
			}

			if (ModuleBuiltData->PhysicsData.bPointVelocity)
			{
				const FVector3f FallbackDir	= ParticleSimulationContext.RandomUnitFloat3(i, 8);
				const FVector3f Delta		= Position - Parameters->SolveVelocitiesAndForces_PointOrigin;
				const FVector3f Direction	= ParticleSimulationContext.SafeNormalize(Delta, FallbackDir);
				const float		VelocityScale = ParticleSimulationContext.RandomScaleBiasFloat(i, 9, Parameters->SolveVelocitiesAndForces_PointVelocityScale, Parameters->SolveVelocitiesAndForces_PointVelocityBias);

				InitialVelocity += Direction * VelocityScale;
			}

			if (ModuleBuiltData->PhysicsData.bNoiseEnabled)
			{
				//-TODO:
			}

			Position += IntegratePosition(AgeData[i], Mass, Drag, InitialVelocity, Wind, Acceleration);
			PreviousPosition += IntegratePosition(PreviousAgeData[i], Mass, Drag, InitialVelocity, Wind, Acceleration);

			ParticleSimulationContext.WriteParticleVariable(ModuleBuiltData->PositionVariableOffset, i, Position);
			ParticleSimulationContext.WriteParticleVariable(ModuleBuiltData->PreviousPositionVariableOffset, i, PreviousPosition);

			const FVector3f Velocity = (Position - PreviousPosition) * InvDeltaTime;
			ParticleSimulationContext.WriteParticleVariable(ModuleBuiltData->VelocityVariableOffset, i, Velocity);
			ParticleSimulationContext.WriteParticleVariable(ModuleBuiltData->PreviousVelocityVariableOffset, i, Velocity);
		}
	}

#if WITH_EDITORONLY_DATA
	virtual void GetOutputVariables(TArray<FNiagaraVariableBase>& OutVariables) const override
	{
		const FNiagaraStatelessGlobals& StatelessGlobals = FNiagaraStatelessGlobals::Get();
		OutVariables.AddUnique(StatelessGlobals.PositionVariable);
		OutVariables.AddUnique(StatelessGlobals.VelocityVariable);
		OutVariables.AddUnique(StatelessGlobals.PreviousPositionVariable);
		OutVariables.AddUnique(StatelessGlobals.PreviousVelocityVariable);
	}
#endif
};
