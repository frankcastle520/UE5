// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/Framework/PhysicsProxyBase.h"
#include "Chaos/Framework/PhysicsSolverBase.h"

namespace RenderInterpolationCVars
{
	float RenderInterpErrorCorrectionDuration = 0.5f;
	static FAutoConsoleVariableRef CVarRenderInterpErrorCorrectionDuration(TEXT("p.RenderInterp.ErrorCorrectionDuration"), RenderInterpErrorCorrectionDuration, TEXT("How long in seconds to apply error correction over."));

	float RenderInterpMaximumErrorCorrectionBeforeSnapping = 250.0f;
	static FAutoConsoleVariableRef CVarRenderInterpErrorCorrectionMaximumError(TEXT("p.RenderInterp.MaximumErrorCorrectionBeforeSnapping"), RenderInterpMaximumErrorCorrectionBeforeSnapping, TEXT("Maximum error correction in cm before we stop interpolating and snap to target."));

	float RenderInterpErrorVelocitySmoothingDuration = 0.5f;
	static FAutoConsoleVariableRef CVarRenderInterpErrorVelocitySmoothingDuration(TEXT("p.RenderInterp.ErrorVelocitySmoothingDuration"), RenderInterpErrorVelocitySmoothingDuration, TEXT("How long in seconds to apply error velocity smoothing correction over, should be smaller than or equal to p.RenderInterp.ErrorCorrectionDuration. RENDERINTERPOLATION_VELOCITYSMOOTHING needs to be defined."));

	float RenderInterpErrorDirectionalDecayMultiplier = 0.0f;
	static FAutoConsoleVariableRef CVarRenderInterpErrorDirectionalDecayMultiplier(TEXT("p.RenderInterp.DirectionalDecayMultiplier"), RenderInterpErrorDirectionalDecayMultiplier, TEXT("Decay error offset in the direction that the physics object is moving, value is multiplier of projected offset direction, 0.25 means a 25% decay of the magnitude in the direction of physics travel. Deactivate by setting to 0."));

	bool bRenderInterpErrorVelocityCorrection = false;
	static FAutoConsoleVariableRef CVarRenderInterpErrorVelocityCorrection(TEXT("p.RenderInterp.ErrorVelocityCorrection"), bRenderInterpErrorVelocityCorrection, TEXT("EXPERIMENTAL - Take incoming velocity into consideration when performing render interpolation, the correction will be more organic but might result in clipping and it's heavier for memory and CPU."));

	bool bRenderInterpDebugDraw = false;
	static FAutoConsoleVariableRef CVarRenderInterpDebugDraw(TEXT("p.RenderInterp.DebugDraw"), bRenderInterpDebugDraw, TEXT("Draw debug lines for physics render interpolation, also needs p.Chaos.DebugDraw.Enabled set"));
	
	float RenderInterpDebugDrawZOffset = 0.0f;
	static FAutoConsoleVariableRef CVarRenderInterpDebugDrawZOffset(TEXT("p.RenderInterp.DebugDrawZOffset"), RenderInterpDebugDrawZOffset, TEXT("Add Z axis offset to DebugDraw calls for Render Interpolation."));
}

IPhysicsProxyBase::~IPhysicsProxyBase()
{
	if(GetSolver<Chaos::FPhysicsSolverBase>())
	{
		GetSolver<Chaos::FPhysicsSolverBase>()->RemoveDirtyProxy(this);
	}
}

int32 IPhysicsProxyBase::GetSolverSyncTimestamp_External() const
{
	if (Chaos::FPhysicsSolverBase* SolverBase = GetSolverBase())
	{
		return SolverBase->GetMarshallingManager().GetExternalTimestamp_External();
	}

	return INDEX_NONE;
}