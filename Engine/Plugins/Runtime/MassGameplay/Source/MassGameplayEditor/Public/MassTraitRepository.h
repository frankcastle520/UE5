// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassEntityTemplateRegistry.h"
#include "EditorSubsystem.h"
#include "MassEntitySubsystem.h"
#include "StructUtils/StructView.h"
#include "MassDebugger.h"
#include "MassTraitRepository.generated.h"

class UMassEntityTraitBase;
class UWorld;

namespace UE::Mass::Editor
{
	extern MASSGAMEPLAYEDITOR_API TConstArrayView<FName> GetTraitsNameAddingElements(const FName ElementName);
}

struct FMassTraitInspectionContext
{
	struct FInvestigationContext : public FMassEntityTemplateBuildContext
	{
		using Super = FMassEntityTemplateBuildContext;
		explicit FInvestigationContext(FMassEntityTemplateData& InTemplate);
		void SetTrait(const UMassEntityTraitBase& Trait);
	};

	FMassTraitInspectionContext();

	FMassEntityTemplateData EntityTemplate;
	FInvestigationContext BuildContext;
};

/** 
 * Subsystem to store information about Mass traits so that we can make helpful suggestions to users like which
 * traits supply a fragment they need.
 * It also serves to post trait validation information and fix options to the MessageLog.
 */
UCLASS()
class MASSGAMEPLAYEDITOR_API UMassTraitRepository : public UEditorSubsystem
{
	GENERATED_BODY()
	
public:
	static UWorld* GetInvestigationWorld();

	TConstArrayView<FName> GetTraitsNameAddingElements(const FName ElementName);
	TWeakObjectPtr<UClass> GetTraitClass(const FName TraitClassName) const;

protected:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

#if WITH_MASSENTITY_DEBUG
	/** 
	 * Note that the function does nothing until InitRepository is called. This is done to avoid collecting trait
	 * data until it's actually necessary.
	 */
	void OnNewTraitType(UMassEntityTraitBase& Trait);

	void OnDebugEvent(const FName EventName, FConstStructView Payload, EMassDebugMessageSeverity SeverityOverride);
#endif // WITH_MASSENTITY_DEBUG
	/** 
	 * The method gathers all existing trait classes and processed them. We don't do that on subsystem's init since
	 * very often the data won't be needed during the given editor run. We call the function lazily the first time 
	 * the data is needed.
	 */
	void InitRepository();

	FDelegateHandle OnNewTraitTypeHandle;
	struct FTraitAndElements
	{
		TWeakObjectPtr<UClass> TraitClass;
		TArray<FName> ElementNames;
	};
	TMap<FName, FTraitAndElements> TraitClassNameToDataMap;
	TMap<FName, TArray<FName>> ElementTypeToTraitMap;


	/** the World we use to host all the subsystems required to process traits */
	UPROPERTY()
	TObjectPtr<UWorld> InvestigationWorld;

	/** We initialize the repository's data lazily and this property indicates whether it has been already done. */
	bool bIsRepositoryInitialized;

	static TWeakObjectPtr<UWorld> GlobalInvestigationWorld;
};


UCLASS()
class UMassDebugEntitySubsystem : public UMassEntitySubsystem
{
	GENERATED_BODY()
	
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
};
