// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ChaosVDGeometryDataComponent.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "Templates/SharedPointer.h"
#include "IDetailCustomization.h"
#include "PropertyHandle.h"
#include "DataWrappers/ChaosVDParticleDataWrapper.h"

class SChaosVDMainTab;
struct FChaosVDParticleDataWrapper;
class AChaosVDParticleActor;

/** Custom details panel for the ChaosVD Particle Actor */
class FChaosVDParticleActorCustomization : public IDetailCustomization
{
public:
	FChaosVDParticleActorCustomization(const TWeakPtr<SChaosVDMainTab>& InMainTab);
	virtual ~FChaosVDParticleActorCustomization() override;

	inline static FName ParticleDataCategoryName = FName("Particle Data");
	inline static FName GeometryCategoryName = FName("Geometry Shape Data");

	static TSharedRef<IDetailCustomization> MakeInstance(TWeakPtr<SChaosVDMainTab> InMainTab);

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:

	template<typename TStruct>
	TSharedPtr<IPropertyHandle> AddExternalStructure(TStruct& CachedStruct, IDetailLayoutBuilder& DetailBuilder, FName CategoryName, const FText& InPropertyName);

	TSet<FName> AllowedCategories;
	TWeakObjectPtr<AChaosVDParticleActor> CurrentObservedActor;

	void ResetCachedView();

	void RegisterCVDScene(const TSharedPtr<FChaosVDScene>& InScene);

	void HandleSceneUpdated();

	/* Copy of the last known geometry shape data structure of a selected particle and mesh instance -  Used to avoid rebuild the layout every time we change frame in CVD */
	FChaosVDParticleDataWrapper CachedParticleData;
	/* Copy of the last known particle data structure of a selected particle -  Used to avoid rebuild the layout every time we change frame in CVD */
	FChaosVDMeshDataInstanceState CachedGeometryDataInstanceCopy;
	
	TWeakPtr<FChaosVDScene> SceneWeakPtr = nullptr;

	TWeakPtr<SChaosVDMainTab> MainTabWeakPtr = nullptr;
};

template <typename TStruct>
TSharedPtr<IPropertyHandle> FChaosVDParticleActorCustomization::AddExternalStructure(TStruct& CachedStruct, IDetailLayoutBuilder& DetailBuilder, FName CategoryName, const FText& InPropertyName)
{
	DetailBuilder.EditCategory(CategoryName, FText::GetEmpty(), ECategoryPriority::Important);
	IDetailCategoryBuilder& CVDMainCategoryBuilder = DetailBuilder.EditCategory(CategoryName).InitiallyCollapsed(false);

	const TSharedPtr<FStructOnScope> DataView = MakeShared<FStructOnScope>(TStruct::StaticStruct(), reinterpret_cast<uint8*>(&CachedStruct));

	FAddPropertyParams AddParams;
	AddParams.CreateCategoryNodes(true);

	if (IDetailPropertyRow* PropertyRow = CVDMainCategoryBuilder.AddExternalStructureProperty(DataView, NAME_None, EPropertyLocation::Default, AddParams))
	{
		PropertyRow->ShouldAutoExpand(true);
		PropertyRow->DisplayName(InPropertyName);
		return PropertyRow->GetPropertyHandle();
	}

	return nullptr;
}
