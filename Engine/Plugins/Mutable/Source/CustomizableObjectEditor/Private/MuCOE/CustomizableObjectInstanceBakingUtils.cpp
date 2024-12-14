// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/CustomizableObjectInstanceBakingUtils.h"

#include "MuCOE/CustomizableObjectEditor.h"
#include "MuCOE/CustomizableObjectEditorLogger.h"

#include "Misc/MessageDialog.h"
#include "FileHelpers.h"
#include "ObjectTools.h"
#include "UnrealBakeHelpers.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "PhysicsEngine/PhysicsAsset.h"

#include "MuCO/CustomizableObject.h"
#include "MuCO/CustomizableObjectInstanceAssetUserData.h"
#include "MuCO/CustomizableObjectInstancePrivate.h"
#include "MuCO/CustomizableObjectMipDataProvider.h"
#include "MuT/UnrealPixelFormatOverride.h"
#include "Rendering/SkeletalMeshModel.h"

#define LOCTEXT_NAMESPACE "CustomizableObjectEditor" 


/**
 * Simple wrapper to be able to invoke the generation of a popup or log message depending on the execution context in which this code is being ran
 * @param InMessage The message to display
 * @param InTitle The title to be used for the popup or the log generated
 */
void ShowErrorNotification(const FText& InMessage, const FText& InTitle = LOCTEXT("CustomizableObjecInstanceBakingUtils_GenericBakingError","Baking Error") )
{
	if (!FApp::IsUnattended())
	{
		FMessageDialog::Open(EAppMsgType::Ok, InMessage, InTitle);
	}
	else
	{
		UE_LOG(LogMutable, Error, TEXT("%s - %s"), *InTitle.ToString(), *InMessage.ToString());
	}
}

/**
 * Utility functions for the baking operation.
 */

/**
 * Validates the filename chosen for the baking data
 * @param FileName The filename chosen by the user
 * @return True if validation was successful, false otherwise
 */
bool ValidateProvidedFileName(const FString& FileName)
{
	if (FileName.IsEmpty())
	{
		UE_LOG(LogMutable, Error, TEXT("Invalid baking configuration : FileName string is empty.."));
		return false;
	}

	// Check for invalid characters in the name of the object to be serialized
	{
		TCHAR InvalidCharacter = '0';
		{
			FString InvalidCharacters = FPaths::GetInvalidFileSystemChars();
			for (int32 i = 0; i < InvalidCharacters.Len(); ++i)
			{
				TCHAR Char = InvalidCharacters[i];
				FString SearchedChar = FString::Chr(Char);
				if (FileName.Contains(SearchedChar))
				{
					InvalidCharacter = InvalidCharacters[i];
					break;
				}
			}
		}

		if (InvalidCharacter != '0')
		{
			const FText InvalidCharacterText = FText::FromString(FString::Chr(InvalidCharacter));
			const FText ErrorText = FText::Format(LOCTEXT("CustomizableObjecInstanceBakingUtils_InvalidCharacter", "The selected contains an invalid character ({0})."), InvalidCharacterText);

			ShowErrorNotification(ErrorText);
		
			return false;
		}
	}

	return true;
}


/**
 * Validates the AssetPath chosen for the baking data
 * @param FileName The filename chosen by the user
 * @param AssetPath The AssetPath chosen by the user
 * @param InstanceCO The CustomizableObject from the provided COI
 * @return True if validation was successful, false otherwise
 */
bool ValidateProvidedAssetPath(const FString& FileName, const FString& AssetPath, const UCustomizableObject* InstanceCO)
{
	if (AssetPath.IsEmpty())
	{
		UE_LOG(LogMutable, Error, TEXT("The AssetPath can not be empty!"));
		return false;
	}

	// Ensure we are not overriding the parent CO
	const FString FullAssetPath = AssetPath + FString("/") + FileName + FString(".") + FileName;		// Full asset path to the new asset we want to create
	const bool bWouldOverrideParentCO = InstanceCO->GetPathName() == FullAssetPath;
	if (bWouldOverrideParentCO)
	{
		const FText ErrorText = LOCTEXT("CustomizableObjecInstanceBakingUtils_OverwriteCO", "The selected path would overwrite the instance's parent Customizable Object.");

		ShowErrorNotification(ErrorText);
		
		return false;
	}

	return true;
}


/**
 * Outputs a string that we know it is unique.
 * @param InResource The resource we are working with
 * @param ResourceName The name of the resource we have provided. This should have the name of the current resource and will have the unique name for the resource once the method exits
 * @param InCachedResources Collection with all the already processed resources.
 * @param InCachedResourceNames Collection with all the already processed resources name's
 * @return True if the generation of the unique resource name was successful, false otherwise.
 */
bool GetUniqueResourceName(const UObject* InResource, FString& ResourceName, TArray<UObject*>& InCachedResources, const TArray<FString>& InCachedResourceNames)
{
	check(InResource);

	int32 FindResult = InCachedResourceNames.Find(ResourceName);
	if (FindResult != INDEX_NONE)
	{
		if (InResource == InCachedResources[FindResult])
		{
			return false;
		}

		uint32 Count = 0;
		while (FindResult != INDEX_NONE)
		{
			FindResult = InCachedResourceNames.Find(ResourceName + "_" + FString::FromInt(Count));
			Count++;
		}

		ResourceName += "_" + FString::FromInt(--Count);
	}

	return true;
}

/**
 * Ensures the resource we want to save is ready to be saved. It handles closing it's editor and warning the user about possible overriding of resources.
 * @param InAssetSavePath The directory path where to save the baked object
 * @param InObjName The name of the object to be baked
 * @param bOverridePermissionGranted Control flag that determines if the user has given or not permission to override resources already in disk
 * @param bIsUnattended
 * @param OutSaveResolution
 * @return True if the operation was successful, false otherwise
 */
bool ManageBakingAction(const FString& InAssetSavePath, const FString& InObjName, bool& bOverridePermissionGranted, const bool& bIsUnattended, EPackageSaveResolutionType& OutSaveResolution)
{
	// Before the value provided by "bOverridePermissionGranted" was being updated due to user request but not it is not. It will stay as is if unatended an
	// will get updated if this gets to be an attended execution.
	
	const FString PackagePath = InAssetSavePath + "/" + InObjName;
	UPackage* ExistingPackage = FindPackage(nullptr, *PackagePath);

	if (!ExistingPackage)
	{
		const FString PackageFilePath = PackagePath + "." + InObjName;

		FString PackageFileName;
		if (FPackageName::DoesPackageExist(PackageFilePath, &PackageFileName))
		{
			ExistingPackage = LoadPackage(nullptr, *PackageFileName, LOAD_EditorOnly);
		}
		else
		{
			// if package does not exist
			
			if (!bIsUnattended)
			{
				// If the run is attended (the user is participating in it) then we will take care in consideration his decision what he wants.
				bOverridePermissionGranted = false;
			}

			OutSaveResolution = EPackageSaveResolutionType::NewFile;
			return true;
		}
	}

	if (ExistingPackage)
	{
		// Checking if the asset is open in an editor
		TArray<IAssetEditorInstance*> ObjectEditors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorsForAssetAndSubObjects(ExistingPackage);
		if (ObjectEditors.Num())
		{
			for (IAssetEditorInstance* ObjectEditorInstance : ObjectEditors)
			{
				// Close the editors that contains this asset
				if (!ObjectEditorInstance->CloseWindow(EAssetEditorCloseReason::AssetEditorHostClosed))
				{
					const FText Caption = LOCTEXT("CustomizableObjecInstanceBakingUtils_OpenExisitngFile", "Open File");
					const FText Message = FText::Format(LOCTEXT("CustomizableObjecInstanceBakingUtils_CantCloseAsset", "This Obejct \"{0}\" is open in an editor and can't be closed automatically. Please close the editor and try to bake it again"), FText::FromString(InObjName));

					ShowErrorNotification(Message, Caption);
					
					return false;
				}
			}
		}

		// If the execution requires user interaction and we have no permission to override the existing file ask him if he wants or not to override data
		if (!bIsUnattended && !bOverridePermissionGranted)
		{
			check (!FApp::IsUnattended())
			const FText Caption = LOCTEXT("CustomizableObjecInstanceBakingUtils_AlreadyExistingBakedFiles", "Already existing baked files");
			const FText Message = FText::Format(LOCTEXT("CustomizableObjecInstanceBakingUtils_OverwriteBakedInstance", "Instance baked files already exist in selected destination \"{0}\", this action will overwrite them."), FText::AsCultureInvariant(InAssetSavePath));

			if (FMessageDialog::Open(EAppMsgType::OkCancel, Message, Caption) == EAppReturnType::Cancel)
			{
				return false;		// if the user cancels then we will still have no rights for overriding data
			}

			UE_LOG(LogMutable, Error, TEXT("%s - %s"), *Caption.ToString(), *Message.ToString());
			
			// If the user accepts the prompt then we will consider we have a green light to override the asset
			bOverridePermissionGranted = true;
		}
		
		// At this point we may or may not have permission to delete the existing asset
		
		// Delete the old asset if we have permission to do so
		UObject* ExistingObject = StaticFindObject(UObject::StaticClass(), ExistingPackage, *InObjName);
		if (ExistingObject)
		{
			// Based on if we have or not permission to override the file do or do not so
			if (bOverridePermissionGranted)
			{
				ExistingPackage->FullyLoad();

				TArray<UObject*> ObjectsToDelete;
				ObjectsToDelete.Add(ExistingObject);
				
				const FText Message = FText::Format(LOCTEXT("CustomizableObjecInstanceBakingUtils_AssetOverriden", "The COI asset \"{0}\" already exists and will be overriden due to user demand."), FText::FromString(ExistingPackage->GetName()));
				UE_LOG(LogMutable,Warning,TEXT("%s"), *Message.ToString());

				// Notify the caller we did proceed with the override (performed later)
				OutSaveResolution = EPackageSaveResolutionType::Overriden;
				
				// Delete objects in the package with the same name as the one we want to create
				const uint32 NumObjectsDeleted = ObjectTools::ForceDeleteObjects(ObjectsToDelete, false);
				return NumObjectsDeleted == ObjectsToDelete.Num();
			}
			else
			{
				// Notify the caller that the override will not be performed
				OutSaveResolution = EPackageSaveResolutionType::UnableToOverride;
				
				// Report that the file will not get overriden since we have no permission to do so
				const FText UnableToOverrideMessage = FText::Format(LOCTEXT("CustomizableObjecInstanceBakingUtils_AssetCanNotBeOverriden", "Could not replace the COI asset \"{0}\" as it already exists."), FText::FromString(ExistingPackage->GetName()));
				UE_LOG(LogMutable,Error,TEXT("%s"), *UnableToOverrideMessage.ToString());

				return false;
			}
		}
		else
		{
			// Notify the caller that no override was required
			OutSaveResolution = EPackageSaveResolutionType::NewFile;
		}
	}

	return true;
}


namespace PreBakeSystemSettings
{
	bool bIsProgressiveMipStreamingEnabled = false;
	bool bIsOnlyGenerateRequestedLODsEnabled = false;
}


// Prevents the execution of the baking in parallel for the baking operation. It will not prevent other updates from running (not baking updates)
// So you are encouraged to halt all other updates while you are baking instances
static bool bIsUpdateForBakingRunning = false;


void PrepareForBaking()
{
	// Implementation of the bake operation
	UCustomizableObjectSystem* System = UCustomizableObjectSystem::GetInstanceChecked();
	
	// The instance in the editor viewport does not have high quality mips in the platform data because streaming is enabled.
	// Disable streaming and retry with a newly generated temp instance.
	PreBakeSystemSettings::bIsProgressiveMipStreamingEnabled = System->IsProgressiveMipStreamingEnabled();
	System->SetProgressiveMipStreamingEnabled(false);
	// Disable requested LOD generation as it will prevent the new instance from having all the LODs
	PreBakeSystemSettings::bIsOnlyGenerateRequestedLODsEnabled = System->IsOnlyGenerateRequestedLODsEnabled();
	System->SetOnlyGenerateRequestedLODsEnabled(false);
	// Force high quality texture compression for this instance
	PrepareUnrealCompression();
	System->SetImagePixelFormatOverride(UnrealPixelFormatFunc);
}


void RestoreCustomizableObjectSettings(const FUpdateContext& Result)
{
	// Reenable Mutable texture streaming and requested LOD generation as they had been disabled to bake the textures
	UCustomizableObjectSystem* System =  UCustomizableObjectSystem::GetInstanceChecked();
	System->SetProgressiveMipStreamingEnabled(PreBakeSystemSettings::bIsProgressiveMipStreamingEnabled);
	System->SetOnlyGenerateRequestedLODsEnabled(PreBakeSystemSettings::bIsOnlyGenerateRequestedLODsEnabled);
	System->SetImagePixelFormatOverride(nullptr);

	// Tell the system we have finished the update and that we can schedule another "for baking" update
	bIsUpdateForBakingRunning = false;
}


void UpdateInstanceForBaking(UCustomizableObjectInstance& InInstance, FInstanceUpdateNativeDelegate& InInstanceUpdateDelegate)
{
	if (bIsUpdateForBakingRunning)
	{
		UE_LOG(LogMutable, Error, TEXT("The COInstance update for baking could not be scheduled. Another instance is being updated for baking."));
		InInstanceUpdateDelegate.Broadcast({EUpdateResult::Error});
		return;
	}

	// Set the update for the baking of the instance as running so we prevent other baking updates while we do run our own
	bIsUpdateForBakingRunning = true;
	
	// Prepare the customizable object system for baking
	PrepareForBaking();
	
	// Ensure we clear the changes in the COSystem after performing the update so later updates do not get affected
	InInstanceUpdateDelegate.AddStatic(&RestoreCustomizableObjectSettings);
	
	// Schedule the update
	InInstance.UpdateSkeletalMeshAsyncResult(InInstanceUpdateDelegate,true,true);

	UE_LOG(LogMutable, Display, TEXT("The COInstance Update operation for baking was succesfuly scheduled."));
}


bool BakeCustomizableObjectInstance(
	UCustomizableObjectInstance& InInstance,
	const FString& FileName,
	const FString& AssetPath,
	const bool bExportAllResources,
	const bool bGenerateConstantMaterialInstances,
	bool bHasPermissionToOverride,
	bool bIsUnattendedExecution,
	TArray<TPair<EPackageSaveResolutionType,UPackage*>>& OutSavedPackages)
{
	OutSavedPackages.Reset();
	
	// Ensure that the state of the COI provided is valid --------------------------------------------------------------------------------------------
	UCustomizableObject* InstanceCO = InInstance.GetCustomizableObject();

	// Ensure the CO of the COI is accessible 
	if (!InstanceCO || InstanceCO->GetPrivate()->IsLocked())
	{
		FCustomizableObjectEditorLogger::CreateLog(
		LOCTEXT("CustomizableObjecInstanceBakingUtils_LockedObject", "Please wait until the Customizable Object is compiled"))
		.Category(ELoggerCategory::COInstanceBaking)
		.CustomNotification()
		.Notification(true)
		.Log();

		return false;
	}
	
	if (InstanceCO->GetPrivate()->Status.Get() == FCustomizableObjectStatus::EState::Loading)
	{
		FCustomizableObjectEditorLogger::CreateLog(
			LOCTEXT("CustomizableObjecInstanceBakingUtils_LoadingObject","Please wait unitl Customizable Object is loaded"))
		.Category(ELoggerCategory::COInstanceBaking)
		.CustomNotification()
		.Notification(true)
		.Log();

		return false;
	}
	
	if (!ValidateProvidedFileName(FileName))
	{
		UE_LOG(LogMutable, Error, TEXT("The FileName for the instance baking is not valid."));
		return false;
	}

	if (!ValidateProvidedAssetPath(FileName,AssetPath,InstanceCO))
	{
		UE_LOG(LogMutable, Error, TEXT("The AssetPath for the instance baking is not valid."));
		return false;
	}
	
	// Exit early if the provided instance does not have a skeletal mesh
	if (!InInstance.HasAnySkeletalMesh())
	{
		UE_LOG(LogMutable, Error, TEXT("The provided instance does not have an skeletal mesh."));
		return false;
	}

	// COI Validation completed : Proceed with the baking operation ----------------------------------------------------------------------------------
	
	// Notify of better configuration -> Continue operation normally
	if (InstanceCO->GetPrivate()->GetCompileOptions().TextureCompression != ECustomizableObjectTextureCompression::HighQuality)
	{
		FCustomizableObjectEditorLogger::CreateLog(
		LOCTEXT("CustomizableObjecInstanceBakingUtils_LowQualityTextures", "The Customizable Object wasn't compiled with high quality textures. For the best baking results, change the Texture Compression setting and recompile it."))
		.Category(ELoggerCategory::COInstanceBaking)
		.CustomNotification()
		.Notification(true)
		.Log();
	}
	
	// Set the overriding flag to true or false:
	//	- We ask the user at least once about if he is willing to override old baked data (attended operation) and this makes the flag change 
	//	- We never ask the user (and therefore the value in bUsedGrantedOverridingRights never changes) when we work in Unattended mode.
	bool bUsedGrantedOverridingRights = bHasPermissionToOverride;
	if (FApp::IsUnattended() || GIsRunningUnattendedScript)
	{
		bIsUnattendedExecution = true;
	}
	
	
	const int32 NumComponents = InstanceCO->GetComponentCount();
	for (int32 ComponentIndex = 0; ComponentIndex < NumComponents; ++ComponentIndex)
	{
		const FName ComponentName = InstanceCO->GetComponentName(ComponentIndex);
		USkeletalMesh* Mesh = InInstance.GetComponentMeshSkeletalMesh(ComponentName);

		if (!Mesh)
		{
			continue;
		}

		FString ObjectName = FileName;
		if (NumComponents > 1)
		{
			ObjectName = FileName + "_Component_" + FString::FromInt(ComponentIndex);
		}

		TMap<UObject*, UObject*> ReplacementMap;
		TArray<FString> ArrayCachedElement;
		TArray<UObject*> ArrayCachedObject;

		if (bExportAllResources)
		{
			UMaterialInstance* Inst;
			UMaterial* Material;
			UTexture* Texture;
			FString MaterialName;
			FString ResourceName;
			FString PackageName;
			UObject* DuplicatedObject;
			TArray<TMap<int, UTexture*>> TextureReplacementMaps;

			// Duplicate Textures found in the Material Instances of the SkeletalMesh so we can later assign them to the
			// duplicates of those material instances. At the end of the baking we will have a series of materials with the 
			// parameters set as the material instances they are based of.
			for (int32 m = 0; m < Mesh->GetMaterials().Num(); ++m)
			{
				UMaterialInterface* Interface = Mesh->GetMaterials()[m].MaterialInterface;
				Material = Interface->GetMaterial();
				MaterialName = Material ? Material->GetName() : "Material";
				Inst = Cast<UMaterialInstance>(Interface);

				TextureReplacementMaps.AddDefaulted();
				
				if (Material != nullptr && Inst != nullptr)
				{
					TArray<FName> ParameterNames = FUnrealBakeHelpers::GetTextureParameterNames(Material);
					for (int32 i = 0; i < ParameterNames.Num(); i++)
					{
						if (Inst->GetTextureParameterValue(ParameterNames[i], Texture))
						{
							UTexture2D* SrcTex = Cast<UTexture2D>(Texture);
							if (!SrcTex)
							{
								continue;
							}

							FString ParameterSanitized = ParameterNames[i].GetPlainNameString();
							RemoveRestrictedChars(ParameterSanitized);
							ResourceName = ObjectName + "_" + MaterialName + "_" + ParameterSanitized;
							if (!GetUniqueResourceName(SrcTex, ResourceName, ArrayCachedObject, ArrayCachedElement))
							{
								continue;
							}
							
							EPackageSaveResolutionType SaveType = EPackageSaveResolutionType::None;
							if (!ManageBakingAction(AssetPath, ResourceName, bUsedGrantedOverridingRights, bIsUnattendedExecution, SaveType))
							{
								return false;
							}

							// Skip already processed resource
							if (ArrayCachedElement.Find(ResourceName) != INDEX_NONE)
							{
								continue;
							}

							bool bIsMutableTexture = false;
							for (UAssetUserData* UserData : *SrcTex->GetAssetUserDataArray())
							{
								if (Cast<UMutableTextureMipDataProviderFactory>(UserData))
								{
									bIsMutableTexture = true;
								}
							}
							
							// Duplicating mutable generated textures
							if (bIsMutableTexture)
							{
								if (SrcTex->GetPlatformData() && SrcTex->GetPlatformData()->Mips.Num() > 0)
								{
									// Recover original name of the texture parameter value, now substituted by the generated Mutable texture
									UTexture* OriginalTexture = nullptr;
									Inst->Parent->GetTextureParameterValue(FName(*ParameterNames[i].GetPlainNameString()), OriginalTexture);

									PackageName = AssetPath + FString("/") + ResourceName;
									TMap<UObject*, UObject*> FakeReplacementMap;
									UTexture2D* DupTex = FUnrealBakeHelpers::BakeHelper_CreateAssetTexture(SrcTex, ResourceName, PackageName, OriginalTexture, true, FakeReplacementMap, bUsedGrantedOverridingRights);
									ArrayCachedElement.Add(ResourceName);
									ArrayCachedObject.Add(DupTex);
								
									TPair<EPackageSaveResolutionType, UPackage*> PackageToSave {SaveType, DupTex->GetPackage()};
									OutSavedPackages.Add(PackageToSave);

									if (OriginalTexture != nullptr)
									{
										TextureReplacementMaps[m].Add(i, DupTex);
									}
								}
							}
							else
							{
								// Duplicate the non-mutable textures of the Material instance (pass-through textures)
								
								PackageName = AssetPath + FString("/") + ResourceName;
								TMap<UObject*, UObject*> FakeReplacementMap;
								DuplicatedObject = FUnrealBakeHelpers::BakeHelper_DuplicateAsset(Texture, ResourceName, PackageName, true, FakeReplacementMap, bUsedGrantedOverridingRights, false);
								ArrayCachedElement.Add(ResourceName);
								ArrayCachedObject.Add(DuplicatedObject);

								TPair<EPackageSaveResolutionType, UPackage*> PackageToSave {SaveType, DuplicatedObject->GetPackage()};
								OutSavedPackages.Add(PackageToSave);

								UTexture* DupTexture = Cast<UTexture>(DuplicatedObject);
								TextureReplacementMaps[m].Add(i, DupTexture);
							}
						}
					}
				}
			}


			// Duplicate the materials used by each material instance so that the replacement map has proper information 
			// when duplicating the material instances
			for (int32 m = 0; m < Mesh->GetMaterials().Num(); ++m)
			{
				UMaterialInterface* Interface = Mesh->GetMaterials()[m].MaterialInterface;
				Material = Interface ? Interface->GetMaterial() : nullptr;

				if (Material)
				{
					ResourceName = ObjectName + "_Material_" + Material->GetName();

					if (!GetUniqueResourceName(Material, ResourceName, ArrayCachedObject, ArrayCachedElement))
					{
						continue;
					}

					EPackageSaveResolutionType SaveType = EPackageSaveResolutionType::None;
					if (!ManageBakingAction(AssetPath, ResourceName, bUsedGrantedOverridingRights, bIsUnattendedExecution, SaveType))
					{
						return false;
					}

					PackageName = AssetPath + FString("/") + ResourceName;
					TMap<UObject*, UObject*> FakeReplacementMap;
					DuplicatedObject = FUnrealBakeHelpers::BakeHelper_DuplicateAsset(Material, ResourceName, PackageName, 
						false, FakeReplacementMap, bUsedGrantedOverridingRights, bGenerateConstantMaterialInstances);
					ArrayCachedElement.Add(ResourceName);
					ArrayCachedObject.Add(DuplicatedObject);
					ReplacementMap.Add(Interface, DuplicatedObject);

					TPair<EPackageSaveResolutionType, UPackage*> PackageToSave {SaveType, DuplicatedObject->GetPackage()};
					OutSavedPackages.Add(PackageToSave);

					FUnrealBakeHelpers::CopyAllMaterialParameters(DuplicatedObject, Interface, TextureReplacementMaps[m]);
				}
			}
		}
		else
		{
			// Duplicate the material instances
			for (int32 MaterialIndex = 0; MaterialIndex < Mesh->GetMaterials().Num(); ++MaterialIndex)
			{
				UMaterialInterface* Interface = Mesh->GetMaterials()[MaterialIndex].MaterialInterface;
				UMaterial* ParentMaterial = Interface->GetMaterial();
				FString MaterialName = ParentMaterial ? ParentMaterial->GetName() : "Material";

				// Material
				FString MatObjName = ObjectName + "_" + MaterialName;

				if (!GetUniqueResourceName(Interface, MatObjName, ArrayCachedObject, ArrayCachedElement))
				{
					continue;
				}

				EPackageSaveResolutionType SaveType = EPackageSaveResolutionType::None;
				if (!ManageBakingAction(AssetPath, MatObjName, bUsedGrantedOverridingRights, bIsUnattendedExecution, SaveType))
				{
					return false;
				}

				FString MatPkgName = AssetPath + FString("/") + MatObjName;
				UObject* DupMat = FUnrealBakeHelpers::BakeHelper_DuplicateAsset(Interface, MatObjName, 
					MatPkgName, false, ReplacementMap, bUsedGrantedOverridingRights, bGenerateConstantMaterialInstances);
				ArrayCachedObject.Add(DupMat);
				ArrayCachedElement.Add(MatObjName);

				TPair<EPackageSaveResolutionType, UPackage*> PackageToSave {SaveType, DupMat->GetPackage()};
				OutSavedPackages.Add(PackageToSave);
		

				UMaterialInstance* Inst = Cast<UMaterialInstance>(Interface);

				// Only need to duplicate the generate textures if the original material is a dynamic instance
				// If the material has Mutable textures, then it will be a dynamic material instance for sure
				if (Inst)
				{
					// Duplicate generated textures
					UMaterialInstanceDynamic* InstDynamic = Cast<UMaterialInstanceDynamic>(DupMat);
					UMaterialInstanceConstant* InstConstant = Cast<UMaterialInstanceConstant>(DupMat);

					if (InstDynamic || InstConstant)
					{
						for (int32 TextureIndex = 0; TextureIndex < Inst->TextureParameterValues.Num(); ++TextureIndex)
						{
							if (Inst->TextureParameterValues[TextureIndex].ParameterValue)
							{
								if (Inst->TextureParameterValues[TextureIndex].ParameterValue->HasAnyFlags(RF_Transient))
								{
									UTexture2D* SrcTex = Cast<UTexture2D>(Inst->TextureParameterValues[TextureIndex].ParameterValue);

									if (SrcTex)
									{
										FString ParameterSanitized = Inst->TextureParameterValues[TextureIndex].ParameterInfo.Name.ToString();
										RemoveRestrictedChars(ParameterSanitized);

										FString TexObjName = ObjectName + "_" + MaterialName + "_" + ParameterSanitized;

										if (!GetUniqueResourceName(SrcTex, TexObjName, ArrayCachedObject, ArrayCachedElement))
										{
											UTexture* PrevTexture = Cast<UTexture>(ArrayCachedObject[ArrayCachedElement.Find(TexObjName)]);

											if (InstDynamic)
											{
												InstDynamic->SetTextureParameterValue(Inst->TextureParameterValues[TextureIndex].ParameterInfo.Name, PrevTexture);
											}
											else if (InstConstant)
											{
												InstConstant->SetTextureParameterValueEditorOnly(Inst->TextureParameterValues[TextureIndex].ParameterInfo.Name, PrevTexture);
											}
											
											continue;
										}

										EPackageSaveResolutionType TextureSaveType = EPackageSaveResolutionType::None;
										if (!ManageBakingAction(AssetPath, TexObjName, bUsedGrantedOverridingRights, bIsUnattendedExecution, TextureSaveType))
										{
											return false;
										}

										FString TexPkgName = AssetPath + FString("/") + TexObjName;
										TMap<UObject*, UObject*> FakeReplacementMap;
										UTexture2D* DupTex = FUnrealBakeHelpers::BakeHelper_CreateAssetTexture(SrcTex, TexObjName, TexPkgName, nullptr, false, FakeReplacementMap, bUsedGrantedOverridingRights);
										ArrayCachedObject.Add(DupTex);
										ArrayCachedElement.Add(TexObjName);

										TPair<EPackageSaveResolutionType, UPackage*> TexturePackageToSave {TextureSaveType, DupTex->GetPackage()};
										OutSavedPackages.Add(TexturePackageToSave);
										
										if (InstDynamic)
										{
											InstDynamic->SetTextureParameterValue(Inst->TextureParameterValues[TextureIndex].ParameterInfo.Name, DupTex);
										}
										else if(InstConstant)
										{
											InstConstant->SetTextureParameterValueEditorOnly(Inst->TextureParameterValues[TextureIndex].ParameterInfo.Name, DupTex);
										}
									}
									else
									{
										UE_LOG(LogMutable, Error, TEXT("A Mutable texture that is not a Texture2D has been found while baking a CustomizableObjectInstance."));
									}
								}
								else
								{
									// If it's not transient it's not a mutable texture, it's a pass-through texture
									// Just set the original texture
									if (InstDynamic)
									{
										InstDynamic->SetTextureParameterValue(Inst->TextureParameterValues[TextureIndex].ParameterInfo.Name, Inst->TextureParameterValues[TextureIndex].ParameterValue);
									}
									else if (InstConstant)
									{
										InstConstant->SetTextureParameterValueEditorOnly(Inst->TextureParameterValues[TextureIndex].ParameterInfo.Name, Inst->TextureParameterValues[TextureIndex].ParameterValue);
									}
								}
							}
						}
					}
				}
			}
		}
		
		// Skeletal Mesh's Skeleton
		if (Mesh->GetSkeleton())
		{
			const bool bTransient = Mesh->GetSkeleton()->GetPackage() == GetTransientPackage();

			// Don't duplicate if not transient or export all assets.
			if (bTransient || bExportAllResources)
			{
				FString SkeletonName = ObjectName + "_Skeleton";
				EPackageSaveResolutionType SaveType = EPackageSaveResolutionType::None;
				if (!ManageBakingAction(AssetPath, SkeletonName, bUsedGrantedOverridingRights, bIsUnattendedExecution, SaveType))
				{
					return false;
				}

				FString SkeletonPkgName = AssetPath + FString("/") + SkeletonName;
				UObject* DuplicatedSkeleton = FUnrealBakeHelpers::BakeHelper_DuplicateAsset(Mesh->GetSkeleton(), SkeletonName, 
					SkeletonPkgName, false, ReplacementMap, bUsedGrantedOverridingRights, false);

				ArrayCachedObject.Add(DuplicatedSkeleton);
				TPair<EPackageSaveResolutionType, UPackage*> PackageToSave {SaveType, DuplicatedSkeleton->GetPackage()};
				OutSavedPackages.Add(PackageToSave);
				ReplacementMap.Add(Mesh->GetSkeleton(), DuplicatedSkeleton);
			}
		}

		// Skeletal Mesh
		EPackageSaveResolutionType SaveType = EPackageSaveResolutionType::None;
		if (!ManageBakingAction(AssetPath, ObjectName, bUsedGrantedOverridingRights, bIsUnattendedExecution, SaveType))
		{
			return false;
		}

		FString PkgName = AssetPath + FString("/") + ObjectName;
		UObject* DupObject = FUnrealBakeHelpers::BakeHelper_DuplicateAsset(Mesh, ObjectName, PkgName, 
			false, ReplacementMap, bUsedGrantedOverridingRights, false);
		ArrayCachedObject.Add(DupObject);

		TPair<EPackageSaveResolutionType, UPackage*> PackageToSave {SaveType, DupObject->GetPackage()};
		OutSavedPackages.Add(PackageToSave);

		Mesh->Build();

		USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(DupObject);
		if (SkeletalMesh)
		{
			SkeletalMesh->ResetLODInfo();
			for (int32 LODIndex = 0; LODIndex < Mesh->GetLODNum(); ++LODIndex)
        	{
        		SkeletalMesh->AddLODInfo(*Mesh->GetLODInfo(LODIndex));
        	}
		
			SkeletalMesh->GetImportedModel()->SkeletalMeshModelGUID = FGuid::NewGuid();

			// Duplicate AssetUserData
			{
				const TArray<UAssetUserData*>* AssetUserDataArray = Mesh->GetAssetUserDataArray();
				for (const UAssetUserData* AssetUserData : *AssetUserDataArray)
				{
					if (AssetUserData)
					{
						// Duplicate to change ownership
						UAssetUserData* NewAssetUserData = Cast<UAssetUserData>(StaticDuplicateObject(AssetUserData, SkeletalMesh));
						SkeletalMesh->AddAssetUserData(NewAssetUserData);
					}
				}
			}

			// Add Instance Info in a custom AssetUserData
			{
				const FCustomizableInstanceComponentData* ComponentData = InInstance.GetPrivate()->GetComponentData(ComponentName);
				check(ComponentData);
				
				if (InInstance.GetAnimationGameplayTags().Num() ||
					ComponentData->AnimSlotToBP.Num())
				{
					UCustomizableObjectInstanceUserData* InstanceData = NewObject<UCustomizableObjectInstanceUserData>(SkeletalMesh, NAME_None, RF_Public | RF_Transactional);
					InstanceData->AnimationGameplayTag = InInstance.GetAnimationGameplayTags();

					for (const TTuple<FName, TSoftClassPtr<UAnimInstance>>& AnimSlot : ComponentData->AnimSlotToBP)
					{
						FCustomizableObjectAnimationSlot AnimationSlot;
						AnimationSlot.Name = AnimSlot.Key;
						AnimationSlot.AnimInstance = AnimSlot.Value;
				
						InstanceData->AnimationSlots.Add(AnimationSlot);
					}

					SkeletalMesh->AddAssetUserData(InstanceData);
				}
			}

			// Duplicate PhysicsAsset
			{
				const UPhysicsAsset* PhysicsAsset = Mesh->GetPhysicsAsset();

				if (PhysicsAsset)
				{
					// Duplicate to change from the Transient Package to the baked mesh one
					UPhysicsAsset* NewPhysicsAsset = Cast<UPhysicsAsset>(StaticDuplicateObject(PhysicsAsset, SkeletalMesh));
					SkeletalMesh->SetPhysicsAsset(NewPhysicsAsset);
				}
			}

			// Copy LODSettings from the Reference Skeletal Mesh
			{
				if (InstanceCO && InstanceCO->GetPrivate()->GetModelResources().ReferenceSkeletalMeshesData.IsValidIndex(ComponentIndex))
				{
					USkeletalMeshLODSettings* LODSettings = InstanceCO->GetPrivate()->GetModelResources().ReferenceSkeletalMeshesData[ComponentIndex].SkeletalMeshLODSettings;
					SkeletalMesh->SetLODSettings(LODSettings);
				}
			}

			// Generate render data
			SkeletalMesh->Build();
		}

		// Remove duplicated UObjects from Root (previously added to avoid objects from being GC in the middle of the bake process)
		for (UObject* Obj : ArrayCachedObject)
		{
			Obj->RemoveFromRoot();
		}
	}

	// Save the packages generated during the baking operation  --------------------------------------------------------------------------------------
	
	// Complete the baking by saving the packages we have cached during the baking operation
	if (OutSavedPackages.Num())
	{
		// Prepare the list of assets we want to provide to "PromptForCheckoutAndSave" for saving
		TArray<UPackage*> PackagesToSaveProxy;
		PackagesToSaveProxy.Reserve(OutSavedPackages.Num());
		for (TPair<EPackageSaveResolutionType, UPackage*> DataToSave : OutSavedPackages)
		{
			PackagesToSaveProxy.Push(DataToSave.Value);
		}

		// List of packages that could not be saved
		TArray<UPackage*> FailedToSavePackages;
		const bool bWasSavingSuccessful = FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSaveProxy, false, !bIsUnattendedExecution, &FailedToSavePackages, false, false) == FEditorFileUtils::EPromptReturnCode::PR_Success;

		// Remove all packages that were going to be saved but failed to do so
		const int32 RemovedPackagesCount = OutSavedPackages.RemoveAll([&](const TPair<EPackageSaveResolutionType, UPackage*> ToSavePackage)
		{
			return FailedToSavePackages.Contains(ToSavePackage.Value);
		});
		OutSavedPackages.Shrink();

		return RemovedPackagesCount > 0 ? false : bWasSavingSuccessful;
	}
	
	// The operation will fail if no packages are there to save
	return false;
}

#undef LOCTEXT_NAMESPACE 
