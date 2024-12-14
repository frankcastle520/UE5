// Copyright Epic Games, Inc. All Rights Reserved.

#include "Cloner/Extensions/CEClonerMeshRendererExtension.h"

#include "Cloner/CEClonerComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraSystem.h"
#include "Settings/CEClonerEffectorSettings.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogCEClonerMeshRenderer, Log, All);

UCEClonerMeshRendererExtension::UCEClonerMeshRendererExtension()
	: UCEClonerExtensionBase(
		TEXT("MeshRenderer")
		, 1)
{
	// Default override material
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> DefaultMaterialFinder(UCEClonerEffectorSettings::DefaultMaterialPath);
	OverrideMaterial = DefaultMaterialFinder.Object;
}

void UCEClonerMeshRendererExtension::SetMeshRenderMode(ECEClonerMeshRenderMode InMode)
{
	if (InMode == MeshRenderMode)
	{
		return;
	}

	MeshRenderMode = InMode;
	MarkExtensionDirty();
}

void UCEClonerMeshRendererExtension::SetMeshFacingMode(ENiagaraMeshFacingMode InMode)
{
	if (MeshFacingMode == InMode)
	{
		return;
	}

	MeshFacingMode = InMode;
	OnOverrideMaterialOptionsChanged();
}

void UCEClonerMeshRendererExtension::SetMeshCastShadows(bool InbCastShadows)
{
	if (bMeshCastShadows == InbCastShadows)
	{
		return;
	}

	bMeshCastShadows = InbCastShadows;
	OnOverrideMaterialOptionsChanged();
}

void UCEClonerMeshRendererExtension::SetDefaultMeshes(const TArray<TObjectPtr<UStaticMesh>>& InMeshes)
{
	DefaultMeshes = InMeshes;
	OnOverrideMaterialOptionsChanged();
}

void UCEClonerMeshRendererExtension::SetDefaultMeshes(const TArray<UStaticMesh*>& InMeshes)
{
	DefaultMeshes.Empty(InMeshes.Num());

	Algo::Transform(InMeshes, DefaultMeshes, [](UStaticMesh* InMesh)->TObjectPtr<UStaticMesh>
	{
		return InMesh;
	});

	OnOverrideMaterialOptionsChanged();
}

void UCEClonerMeshRendererExtension::GetDefaultMeshes(TArray<UStaticMesh*>& OutMeshes) const
{
	OutMeshes.Empty(DefaultMeshes.Num());
	Algo::Transform(DefaultMeshes, OutMeshes, [](const TObjectPtr<UStaticMesh>& InMesh)->UStaticMesh*
	{
		return InMesh;
	});
}

void UCEClonerMeshRendererExtension::SetVisualizeEffectors(bool bInVisualize)
{
	if (bVisualizeEffectors == bInVisualize)
	{
		return;
	}

	bVisualizeEffectors = bInVisualize;
	OnOverrideMaterialOptionsChanged();
}

void UCEClonerMeshRendererExtension::SetUseOverrideMaterial(bool bInOverride)
{
	if (bUseOverrideMaterial == bInOverride)
	{
		return;
	}

	bUseOverrideMaterial = bInOverride;
	OnOverrideMaterialOptionsChanged();
}

void UCEClonerMeshRendererExtension::SetOverrideMaterial(UMaterialInterface* InMaterial)
{
	if (OverrideMaterial == InMaterial)
	{
		return;
	}

	OverrideMaterial = InMaterial;
	OnOverrideMaterialOptionsChanged();
}

void UCEClonerMeshRendererExtension::SetSortTranslucentParticles(bool bInSort)
{
	if (bSortTranslucentParticles == bInSort)
	{
		return;
	}

	bSortTranslucentParticles = bInSort;
	OnOverrideMaterialOptionsChanged();	
}

void UCEClonerMeshRendererExtension::OnExtensionParametersChanged(UCEClonerComponent* InComponent)
{
	Super::OnExtensionParametersChanged(InComponent);

	FNiagaraUserRedirectionParameterStore& ExposedParameters = InComponent->GetOverrideParameters();
	static const FNiagaraVariable MeshModeVar(FNiagaraTypeDefinition(StaticEnum<ECEClonerMeshRenderMode>()), TEXT("MeshRenderMode"));
	ExposedParameters.SetParameterValue<int32>(static_cast<int32>(MeshRenderMode), MeshModeVar);
}

int32 UCEClonerMeshRendererExtension::GetClonerMeshesMaterialCount() const
{
	int32 MaterialCount = 0;

	UCEClonerLayoutBase* LayoutSystem = GetClonerLayout();

	if (!IsValid(LayoutSystem))
	{
		return MaterialCount;
	}

	UNiagaraMeshRendererProperties* MeshRenderer = LayoutSystem->GetMeshRenderer();

	if (!IsValid(MeshRenderer))
	{
		return MaterialCount;
	}

	for (const FNiagaraMeshRendererMeshProperties& MeshProperties : MeshRenderer->Meshes)
	{
		if (!MeshProperties.Mesh)
		{
			continue;
		}

		MaterialCount += MeshProperties.Mesh->GetNumSections(/** LOD */0);
	}

	return MaterialCount;
}

TArray<FNiagaraMeshMaterialOverride> UCEClonerMeshRendererExtension::GetOverrideMeshesMaterials() const
{
	TArray<FNiagaraMeshMaterialOverride> MaterialOverrides;

	const bool bOverrideMaterial = bUseOverrideMaterial || bVisualizeEffectors;

	if (bOverrideMaterial)
	{
		// Set same material for all available slots
		const int32 MaterialCount = GetClonerMeshesMaterialCount();
		MaterialOverrides.Reserve(MaterialCount);

		UMaterialInterface* OverrideMeshesMaterial = bVisualizeEffectors
			? LoadObject<UMaterialInterface>(nullptr, UCEClonerEffectorSettings::DefaultMaterialPath)
			: OverrideMaterial.Get();

		for (int32 Index = 0; Index < MaterialCount; Index++)
		{
			FNiagaraMeshMaterialOverride MaterialOverride;
			MaterialOverride.ExplicitMat = OverrideMeshesMaterial;
			MaterialOverrides.Add(MaterialOverride);
		}
	}

	return MaterialOverrides;
}

void UCEClonerMeshRendererExtension::OnOverrideMaterialOptionsChanged()
{
	if (IsValid(OverrideMaterial) && !UCEClonerComponent::IsMaterialUsageFlagSet(OverrideMaterial))
	{
		UE_LOG(LogCEClonerMeshRenderer, Warning, TEXT("%s : The override material (%s) you wish to use does not have the required usage flag (bUsedWithNiagaraMeshParticles) to work with the cloner, enable the flag on the material and save the asset"), *GetClonerComponent()->GetOwner()->GetActorNameOrLabel(), *OverrideMaterial->GetMaterial()->GetPathName());

#if WITH_EDITOR
		UCEClonerComponent::ShowMaterialWarning(1);
#endif

		OverrideMaterial = nullptr;
	}

	if (UCEClonerComponent* ClonerComponent = GetClonerComponent())
	{
		ClonerComponent->RefreshClonerMeshes();
	}
}

void UCEClonerMeshRendererExtension::OnClonerMeshesUpdated()
{
	Super::OnClonerMeshesUpdated();

	UCEClonerComponent* ClonerComponent = GetClonerComponent();

	if (!IsValid(ClonerComponent))
	{
		return;
	}

	const UCEClonerLayoutBase* Layout = GetClonerLayout();

	if (!IsValid(Layout))
	{
		return;
	}

	UNiagaraMeshRendererProperties* MeshRenderer = Layout->GetMeshRenderer();

	if (!IsValid(MeshRenderer))
	{
		return;
	}

	MeshRenderer->FacingMode = MeshFacingMode;
	MeshRenderer->bCastShadows = bMeshCastShadows;
	MeshRenderer->SortMode = bSortTranslucentParticles ? ENiagaraSortMode::ViewDepth : ENiagaraSortMode::None;

	// Use default meshes if nothing is attached
	if (ClonerComponent->GetAttachmentCount() == 0)
	{
		const TArray<TObjectPtr<UStaticMesh>>& NewDefaultMeshes = GetDefaultMeshes();
		MeshRenderer->Meshes.SetNum(NewDefaultMeshes.Num());

		for (int32 MeshIndex = 0; MeshIndex < NewDefaultMeshes.Num(); MeshIndex++)
		{
			UStaticMesh* DefaultMesh = NewDefaultMeshes[MeshIndex].Get();
			FNiagaraMeshRendererMeshProperties& MeshProperties = MeshRenderer->Meshes[MeshIndex];
			MeshProperties.Mesh = DefaultMesh && DefaultMesh->GetNumTriangles(0) > 0 ? DefaultMesh : nullptr;
		}
	}

	// Set material override
	MeshRenderer->bOverrideMaterials = bUseOverrideMaterial || bVisualizeEffectors;
	MeshRenderer->OverrideMaterials = GetOverrideMeshesMaterials();
}

#if WITH_EDITOR
const TCEPropertyChangeDispatcher<UCEClonerMeshRendererExtension> UCEClonerMeshRendererExtension::PropertyChangeDispatcher =
{
	/** Renderer */
	{ GET_MEMBER_NAME_CHECKED(UCEClonerMeshRendererExtension, MeshRenderMode), &UCEClonerMeshRendererExtension::OnExtensionPropertyChanged },
	{ GET_MEMBER_NAME_CHECKED(UCEClonerMeshRendererExtension, MeshFacingMode), &UCEClonerMeshRendererExtension::OnOverrideMaterialOptionsChanged },
	{ GET_MEMBER_NAME_CHECKED(UCEClonerMeshRendererExtension, bMeshCastShadows), &UCEClonerMeshRendererExtension::OnOverrideMaterialOptionsChanged },
	{ GET_MEMBER_NAME_CHECKED(UCEClonerMeshRendererExtension, DefaultMeshes), &UCEClonerMeshRendererExtension::OnOverrideMaterialOptionsChanged },
	{ GET_MEMBER_NAME_CHECKED(UCEClonerMeshRendererExtension, bUseOverrideMaterial), &UCEClonerMeshRendererExtension::OnOverrideMaterialOptionsChanged },
	{ GET_MEMBER_NAME_CHECKED(UCEClonerMeshRendererExtension, OverrideMaterial), &UCEClonerMeshRendererExtension::OnOverrideMaterialOptionsChanged },
	{ GET_MEMBER_NAME_CHECKED(UCEClonerMeshRendererExtension, bVisualizeEffectors), &UCEClonerMeshRendererExtension::OnOverrideMaterialOptionsChanged },
	{ GET_MEMBER_NAME_CHECKED(UCEClonerMeshRendererExtension, bSortTranslucentParticles), &UCEClonerMeshRendererExtension::OnOverrideMaterialOptionsChanged },
};

void UCEClonerMeshRendererExtension::PostEditChangeProperty(FPropertyChangedEvent& InPropertyChangedEvent)
{
	Super::PostEditChangeProperty(InPropertyChangedEvent);

	PropertyChangeDispatcher.OnPropertyChanged(this, InPropertyChangedEvent);
}
#endif
