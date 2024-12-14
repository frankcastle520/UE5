// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/CustomizableObjectInstanceBaker.h"

#include "MuCO/CustomizableObject.h"
#include "MuCO/CustomizableObjectInstance.h"
#include "MuCO/CustomizableObjectSystem.h"
#include "MuCOE/CustomizableObjectInstanceBakingUtils.h"


void UCustomizableObjectInstanceBaker::BakeInstance(UCustomizableObjectInstance* InTargetInstance, const FBakingConfiguration& InBakingConfig, const TSharedPtr<const FOnBakerFinishedWork> InOnBakerFinishedWork)
{
	// Cache the configuration and the callback used to report the baking results
	Configuration = InBakingConfig;

	// Cache a delegate that we will later invoke once the baking operation has finished all the work (so we can delete this object safelly)
	if (InOnBakerFinishedWork)
	{
		OnWorkFinished = InOnBakerFinishedWork;
	}
	
	if (!InTargetInstance)
	{
		UE_LOG(LogMutable,Error,TEXT("No instance has been provided."))
		FinishBakingOperation(false, nullptr);
		return;
	}
	InstanceToBake = InTargetInstance;
	
	if (Configuration.OutputPath.IsEmpty())
	{
		UE_LOG(LogMutable,Error,TEXT("No target save path has been provided."))
		FinishBakingOperation(false, nullptr);
		return;
	}

	// If no name is provided then compose one
	Configuration.OutputFilesBaseName = !Configuration.OutputFilesBaseName.IsEmpty() ? Configuration.OutputFilesBaseName : InstanceToBake.GetName();	

	// Schedule update of the instance for baking
	UpdateInstance();
}


void UCustomizableObjectInstanceBaker::UpdateInstance()
{
	// Call the instance update async method
	FInstanceUpdateNativeDelegate UpdateDelegate;
	UpdateDelegate.AddUObject(this, &UCustomizableObjectInstanceBaker::OnInstanceUpdate);
	
	// Once the update finishes successfully the baking operation will engage
	UpdateInstanceForBaking(*InstanceToBake, UpdateDelegate);
}


void UCustomizableObjectInstanceBaker::OnInstanceUpdate(const FUpdateContext& Result) const
{
	const EUpdateResult InstanceUpdateResult = Result.UpdateResult;
	UE_LOG(LogMutable, Display, TEXT("Instance finished update with state : %s."), *UEnum::GetValueAsString(InstanceUpdateResult));
	
	if (UCustomizableObjectSystem::IsUpdateResultValid(InstanceUpdateResult))
	{
		Bake();
	}
	else
	{
		// Report a failed baking operation.
		UE_LOG(LogMutable, Display, TEXT("Instance updating for baking failed. Aborting instance baking operation."));
		FinishBakingOperation(false, nullptr);
	}
}


void UCustomizableObjectInstanceBaker::Bake() const
{
	// Set this flag so we ensure that the systems that make use of it know that should not display interaction messages to the user
	const bool GIsRunningUnattendedScript_Cached = GIsRunningUnattendedScript;
	GIsRunningUnattendedScript = true;
	
	// Array with all the packages that did get saved. Packages that failed the saving procedure will not appear in this array.
	TArray<TPair<EPackageSaveResolutionType,UPackage*>> SavedPackages;

	// Ensures no interaction from the user will be required (alongside GIsRunningUnattendedScript)
	constexpr bool bIsUnhandledExecution = true;
	
	const bool bBakeWasSuccessful = BakeCustomizableObjectInstance
	(*InstanceToBake,
		Configuration.OutputFilesBaseName,
		Configuration.OutputPath,
		Configuration.bExportAllResourcesOnBake,
		Configuration.bGenerateConstantMaterialInstancesOnBake,
		Configuration.bAllowOverridingOfFiles,
		bIsUnhandledExecution,					
		SavedPackages
	);
	
	// Generate the array of structs to output based on the packages reported as saved
	TArray<FBakedResourceData> SavedPackagesData;
	{
		// Display a list with the paths of the assets saved
		UE_LOG(LogMutable, Log, TEXT("Saved asets (%d) :"), SavedPackages.Num());
		SavedPackagesData.Reserve(SavedPackages.Num());
		for (const TPair<EPackageSaveResolutionType, UPackage*>& SavedPackagePair : SavedPackages)
		{
			FBakedResourceData BakeData;
			BakeData.SaveType = SavedPackagePair.Key;
			BakeData.AssetPath = SavedPackagePair.Value->GetPathName();

			// Sanity checks
			check(BakeData.SaveType != EPackageSaveResolutionType::None);
			check(!BakeData.AssetPath.IsEmpty());
			
			// Log the path for the caller to know
			UE_LOG(LogMutable, Log, TEXT("\t%s --- %s"),  *UEnum::GetValueAsString(BakeData.SaveType), *BakeData.AssetPath);
			
			SavedPackagesData.Push(BakeData);
		}
	}

	// Report that the baking operation has been completed
	UE_LOG(LogMutable, Display, TEXT("Finsishing COI baking procedure."));
	FinishBakingOperation(bBakeWasSuccessful, &SavedPackagesData);
	
	// Reset the GIsRunningUnattendedScript flag 
	GIsRunningUnattendedScript = GIsRunningUnattendedScript_Cached;
}


void UCustomizableObjectInstanceBaker::FinishBakingOperation(const bool bBakeWasSuccessful, TArray<FBakedResourceData>* InSavedPackagesData) const
{
	// Report that the baking operation has been completed
	if (Configuration.OnBakeOperationCompletedCallback.IsBound())
	{
		FCustomizableObjectInstanceBakeOutput Output;
		Output.bWasBakeSuccessful = bBakeWasSuccessful;
		if (InSavedPackagesData && !InSavedPackagesData->IsEmpty())
		{
			Output.SavedPackages = MoveTemp(*InSavedPackagesData);
		}
		Configuration.OnBakeOperationCompletedCallback.Execute(Output);
	}
	
	// Notify whatever caller that the baker completed it's work and therefore is safe to destroy/discard
	if (OnWorkFinished && OnWorkFinished->IsBound())
	{
		OnWorkFinished->Execute();
	}
}

