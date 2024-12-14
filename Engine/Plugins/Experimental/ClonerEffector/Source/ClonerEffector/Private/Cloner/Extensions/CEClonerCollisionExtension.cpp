// Copyright Epic Games, Inc. All Rights Reserved.

#include "Cloner/Extensions/CEClonerCollisionExtension.h"

#include "Cloner/CEClonerComponent.h"
#include "NiagaraDataInterfaceArrayFloat.h"
#include "NiagaraMeshRendererProperties.h"
#include "NiagaraSystem.h"

void UCEClonerCollisionExtension::SetSurfaceCollisionEnabled(bool bInSurfaceCollisionEnabled)
{
	if (bSurfaceCollisionEnabled == bInSurfaceCollisionEnabled)
	{
		return;
	}

	bSurfaceCollisionEnabled = bInSurfaceCollisionEnabled;
	MarkExtensionDirty();
}

void UCEClonerCollisionExtension::SetParticleCollisionEnabled(bool bInParticleCollisionEnabled)
{
	if (bParticleCollisionEnabled == bInParticleCollisionEnabled)
	{
		return;
	}

	bParticleCollisionEnabled = bInParticleCollisionEnabled;
	MarkExtensionDirty();
}

void UCEClonerCollisionExtension::SetCollisionVelocityEnabled(bool bInCollisionVelocityEnabled)
{
	if (bCollisionVelocityEnabled == bInCollisionVelocityEnabled)
	{
		return;
	}

	bCollisionVelocityEnabled = bInCollisionVelocityEnabled;
	MarkExtensionDirty();
}

void UCEClonerCollisionExtension::SetCollisionIterations(int32 InCollisionIterations)
{
	InCollisionIterations = FMath::Max(InCollisionIterations, 1);
	if (CollisionIterations == InCollisionIterations)
	{
		return;
	}

	CollisionIterations = InCollisionIterations;
	MarkExtensionDirty();
}

void UCEClonerCollisionExtension::SetCollisionGridResolution(int32 InCollisionGridResolution)
{
	InCollisionGridResolution = FMath::Max(InCollisionGridResolution, 1);
	if (CollisionGridResolution == InCollisionGridResolution)
	{
		return;
	}

	CollisionGridResolution = InCollisionGridResolution;
	MarkExtensionDirty();
}

void UCEClonerCollisionExtension::SetCollisionGridSize(const FVector& InCollisionGridSize)
{
	const FVector NewCollisionGridSize = InCollisionGridSize.ComponentMax(FVector::ZeroVector);
	if (CollisionGridSize.Equals(NewCollisionGridSize))
	{
		return;
	}

	CollisionGridSize = NewCollisionGridSize;
	MarkExtensionDirty();
}

void UCEClonerCollisionExtension::SetCollisionRadiusMode(ECEClonerCollisionRadiusMode InMode)
{
	if (CollisionRadiusMode == InMode)
	{
		return;
	}

	CollisionRadiusMode = InMode;
	MarkExtensionDirty();
}

void UCEClonerCollisionExtension::SetMassMin(float InMassMin)
{
	InMassMin = FMath::Max(InMassMin, 1);
	if (FMath::IsNearlyEqual(MassMin, InMassMin))
	{
		return;
	}

	MassMin = InMassMin;
	MarkExtensionDirty();
}

void UCEClonerCollisionExtension::SetMassMax(float InMassMax)
{
	InMassMax = FMath::Max(InMassMax, 1);
	if (FMath::IsNearlyEqual(MassMax, InMassMax))
	{
		return;
	}

	MassMax = InMassMax;
	MarkExtensionDirty();
}

void UCEClonerCollisionExtension::OnExtensionParametersChanged(UCEClonerComponent* InComponent)
{
	Super::OnExtensionParametersChanged(InComponent);

	MassMin = FMath::Clamp(MassMin, 1, MassMax);
	MassMax = FMath::Max(MassMax, MassMin);

	InComponent->SetBoolParameter(TEXT("SurfaceCollisionEnabled"), bSurfaceCollisionEnabled);
	InComponent->SetIntParameter(TEXT("CollisionIterations"), bParticleCollisionEnabled ? CollisionIterations : 0);
	InComponent->SetBoolParameter(TEXT("CollisionVelocityEnabled"), bParticleCollisionEnabled ? bCollisionVelocityEnabled : false);
	InComponent->SetIntParameter(TEXT("CollisionGridResolution"), CollisionGridResolution);
	InComponent->SetVectorParameter(TEXT("CollisionGridSize"), CollisionGridSize);
	InComponent->SetFloatParameter(TEXT("MassMin"), MassMin);
	InComponent->SetFloatParameter(TEXT("MassMax"), MassMax);

	// Adjust size based on attached mesh count
	CollisionRadii.SetNum(InComponent->GetMeshCount());

	if (const UCEClonerLayoutBase* LayoutSystem = GetClonerLayout())
	{
		if (CollisionRadiusMode != ECEClonerCollisionRadiusMode::Manual)
		{
			UNiagaraMeshRendererProperties* MeshRenderer = LayoutSystem->GetMeshRenderer();

			for (int32 Idx = 0; Idx < CollisionRadii.Num(); Idx++)
			{
				const FNiagaraMeshRendererMeshProperties& MeshProperties = MeshRenderer->Meshes[Idx];
				FBoxSphereBounds MeshBounds(ForceInitToZero);
				const FTransform BoundTransform(MeshProperties.Rotation, MeshProperties.PivotOffset, MeshProperties.Scale);

				if (MeshProperties.Mesh)
				{
					MeshBounds = MeshProperties.Mesh->GetBounds().TransformBy(BoundTransform);
				}

				switch (CollisionRadiusMode)
				{
					case ECEClonerCollisionRadiusMode::MinExtent:
						CollisionRadii[Idx] = MeshBounds.BoxExtent.GetMin();
					break;
					case ECEClonerCollisionRadiusMode::MaxExtent:
						CollisionRadii[Idx] = MeshBounds.BoxExtent.GetMax();
					break;
					default:
					case ECEClonerCollisionRadiusMode::ExtentLength:
						CollisionRadii[Idx] = MeshBounds.SphereRadius;
					break;
				}
			}
		}

		const FNiagaraUserRedirectionParameterStore& ExposedParameters = InComponent->GetOverrideParameters();

		static const FNiagaraVariable CollisionRadiiVar(FNiagaraTypeDefinition(UNiagaraDataInterfaceArrayFloat::StaticClass()), TEXT("CollisionRadii"));

		if (UNiagaraDataInterfaceArrayFloat* CollisionRadiiDI = Cast<UNiagaraDataInterfaceArrayFloat>(ExposedParameters.GetDataInterface(CollisionRadiiVar)))
		{
			CollisionRadiiDI->GetArrayReference() = CollisionRadii;
		}
	}
}

void UCEClonerCollisionExtension::OnClonerMeshesUpdated()
{
	Super::OnClonerMeshesUpdated();

	MarkExtensionDirty();
}

#if WITH_EDITOR
const TCEPropertyChangeDispatcher<UCEClonerCollisionExtension> UCEClonerCollisionExtension::PropertyChangeDispatcher =
{
	/** Collision */
	{ GET_MEMBER_NAME_CHECKED(UCEClonerCollisionExtension, bSurfaceCollisionEnabled), &UCEClonerCollisionExtension::OnExtensionPropertyChanged },
	{ GET_MEMBER_NAME_CHECKED(UCEClonerCollisionExtension, bParticleCollisionEnabled), &UCEClonerCollisionExtension::OnExtensionPropertyChanged },
	{ GET_MEMBER_NAME_CHECKED(UCEClonerCollisionExtension, bCollisionVelocityEnabled), &UCEClonerCollisionExtension::OnExtensionPropertyChanged },
	{ GET_MEMBER_NAME_CHECKED(UCEClonerCollisionExtension, CollisionRadiusMode), &UCEClonerCollisionExtension::OnExtensionPropertyChanged },
	{ GET_MEMBER_NAME_CHECKED(UCEClonerCollisionExtension, CollisionRadii), &UCEClonerCollisionExtension::OnExtensionPropertyChanged },
	{ GET_MEMBER_NAME_CHECKED(UCEClonerCollisionExtension, CollisionIterations), &UCEClonerCollisionExtension::OnExtensionPropertyChanged },
	{ GET_MEMBER_NAME_CHECKED(UCEClonerCollisionExtension, CollisionGridResolution), &UCEClonerCollisionExtension::OnExtensionPropertyChanged },
	{ GET_MEMBER_NAME_CHECKED(UCEClonerCollisionExtension, CollisionGridSize), &UCEClonerCollisionExtension::OnExtensionPropertyChanged },
	{ GET_MEMBER_NAME_CHECKED(UCEClonerCollisionExtension, MassMin), &UCEClonerCollisionExtension::OnExtensionPropertyChanged },
	{ GET_MEMBER_NAME_CHECKED(UCEClonerCollisionExtension, MassMax), &UCEClonerCollisionExtension::OnExtensionPropertyChanged },
};

void UCEClonerCollisionExtension::PostEditChangeProperty(FPropertyChangedEvent& InPropertyChangedEvent)
{
	Super::PostEditChangeProperty(InPropertyChangedEvent);

	PropertyChangeDispatcher.OnPropertyChanged(this, InPropertyChangedEvent);
}
#endif