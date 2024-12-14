// Copyright Epic Games, Inc. All Rights Reserved.

#include "LandscapeEditLayer.h"
#include "LandscapeEditTypes.h"
#include "Landscape.h"

#define LOCTEXT_NAMESPACE "LandscapeEditLayer"

void ULandscapeEditLayerBase::SetBackPointer(ALandscape* Landscape)
{
	OwningLandscape = Landscape;
}

void ULandscapeEditLayerBase::PostLoad()
{
	Super::PostLoad();

	// TODO[jonathan.bard] Remove
	// Needed because we might have saved some layers before we realized we were missing this flag
	SetFlags(RF_Transactional);
}

#if WITH_EDITOR

const FLandscapeLayer* ULandscapeEditLayerBase::GetOwningLayer() const
{
	if (OwningLandscape != nullptr)
	{
		TArrayView<const FLandscapeLayer> Layers = OwningLandscape->GetLayers();
		return Layers.FindByPredicate([this](const FLandscapeLayer& Struct) { return Struct.EditLayer == this; });
	}

	return nullptr;
}

#endif // WITH_EDITOR


// ----------------------------------------------------------------------------------

bool ULandscapeEditLayer::SupportsTargetType(ELandscapeToolTargetType InType) const
{
	return (InType == ELandscapeToolTargetType::Heightmap) || (InType == ELandscapeToolTargetType::Weightmap) || (InType == ELandscapeToolTargetType::Visibility);
}


// ----------------------------------------------------------------------------------

bool ULandscapeEditLayerSplines::SupportsTargetType(ELandscapeToolTargetType InType) const
{
	return (InType == ELandscapeToolTargetType::Heightmap) || (InType == ELandscapeToolTargetType::Weightmap) || (InType == ELandscapeToolTargetType::Visibility);
}

void ULandscapeEditLayerSplines::OnLayerCreated(FLandscapeLayer& Layer)
{
	// Splines edit layer is always using alpha blend mode
	Layer.BlendMode = LSBM_AlphaBlend;
}

TArray<ULandscapeEditLayerSplines::FEditLayerAction> ULandscapeEditLayerSplines::GetActions() const
{
	TArray<FEditLayerAction> Actions;

#if WITH_EDITOR
	// Register an "Update Splines" action :
	Actions.Add(FEditLayerAction(
		LOCTEXT("LandscapeEditLayerSplines_UpdateSplines", "Update Splines"),
		FEditLayerAction::FExecuteDelegate::CreateWeakLambda(this, [](const FEditLayerAction::FExecuteParams& InParams)
		{
			InParams.GetLandscape()->UpdateLandscapeSplines(FGuid(), /*bUpdateOnlySelection = */false, /*bForceUpdate =*/true);
			return FEditLayerAction::FExecuteResult(/*bInSuccess = */true);
		}),
		FEditLayerAction::FCanExecuteDelegate::CreateWeakLambda(this, [](const FEditLayerAction::FExecuteParams& InParams, FText& OutReason)
		{
			if (InParams.GetLayer()->bLocked)
			{
				OutReason = FText::Format(LOCTEXT("LandscapeEditLayerSplines_CannotUpdateSplinesOnLockedLayer", "Cannot update splines on layer '{0}' : the layer is currently locked"), FText::FromName(InParams.GetLayer()->Name));
				return false;
			}

			OutReason = LOCTEXT("LandscapeEditLayerSplines_UpdateSplines_Tooltip", "Update Landscape Splines");
			return true;
		})));
#endif // WITH_EDITOR

	return Actions;
}

#undef LOCTEXT_NAMESPACE
