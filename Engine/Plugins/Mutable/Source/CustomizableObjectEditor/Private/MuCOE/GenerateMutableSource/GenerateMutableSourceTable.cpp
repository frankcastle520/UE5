// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/GenerateMutableSource/GenerateMutableSourceTable.h"

#include "Animation/AnimInstance.h"
#include "Animation/PoseAsset.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/CompositeDataTable.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2DArray.h"
#include "GameplayTagContainer.h"
#include "Kismet2/StructureEditorUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "MuCO/CustomizableObjectSystem.h"
#include "MuCO/CustomizableObjectUIData.h"
#include "MuCO/UnrealPortabilityHelpers.h"
#include "MuCOE/CustomizableObjectCompiler.h"
#include "MuCOE/GenerateMutableSource/GenerateMutableSourceMesh.h"
#include "MuCOE/Nodes/CustomizableObjectNodeTable.h"
#include "MuCOE/Nodes/CustomizableObjectNodeAnimationPose.h"
#include "MuCOE/GraphTraversal.h"
#include "MuR/Mesh.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"
#include "MuCOE/CustomizableObjectVersionBridge.h"
#include "ClothingAsset.h"

#define LOCTEXT_NAMESPACE "CustomizableObjectEditor"


bool FillTableColumn(const UCustomizableObjectNodeTable* TableNode, mu::TablePtr MutableTable, const FString& ColumnName, const FString& RowName, const uint32 RowId, uint8* CellData, const FProperty* ColumnProperty,
	const int LODIndexConnected, const int32 SectionIndexConnected, int32 LODIndex, int32 SectionIndex, uint32 SectionMetadataId, const bool bOnlyConnectedLOD, FMutableGraphGenerationContext& GenerationContext)
{
	int32 CurrentColumn;
	UDataTable* DataTablePtr = GetDataTable(TableNode, GenerationContext);

	// Getting property type
	if (const FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(ColumnProperty))
	{
		FSoftObjectPtr SoftObject = SoftObjectProperty->GetPropertyValue(CellData);
		
		if (SoftObjectProperty->PropertyClass->IsChildOf(USkeletalMesh::StaticClass()))
		{
			GenerationContext.AddParticipatingObject(SoftObject);

			UObject* Object = GenerationContext.LoadObject(SoftObject, true);

			USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Object);
			if(!SkeletalMesh)
			{
				// Generating an Empty cell
				FString MutableColumnName = TableNode->GenerateSkeletalMeshMutableColumName(ColumnName, LODIndex, SectionIndex);

				CurrentColumn = MutableTable.get()->FindColumn(MutableColumnName);

				if (CurrentColumn == -1)
				{
					CurrentColumn = MutableTable->AddColumn(MutableColumnName, mu::ETableColumnType::Mesh);
				}

				mu::MeshPtr EmptySkeletalMesh = nullptr;
				MutableTable->SetCell(CurrentColumn, RowId, EmptySkeletalMesh.get());

				return true;
			}

			// Getting Animation Blueprint and Animation Slot
			FString AnimBP, AnimSlot, GameplayTag, AnimBPAssetTag;
			TArray<FGameplayTag> GameplayTags;
			FGuid ColumnPropertyId = FStructureEditorUtils::GetGuidForProperty(ColumnProperty);

			TableNode->GetAnimationColumns(ColumnPropertyId, AnimBP, AnimSlot, GameplayTag);

			if (!AnimBP.IsEmpty())
			{
				if (!AnimSlot.IsEmpty())
				{
					if (DataTablePtr)
					{
						uint8* AnimRowData = DataTablePtr->FindRowUnchecked(FName(*RowName));

						if (AnimRowData)
						{
							FName SlotIndex;

							// Getting animation slot row value from data table
							if (FProperty* AnimSlotProperty = DataTablePtr->FindTableProperty(FName(*AnimSlot)))
							{
								uint8* AnimSlotData = AnimSlotProperty->ContainerPtrToValuePtr<uint8>(AnimRowData, 0);

								if (AnimSlotData)
								{
									if (const FIntProperty* IntProperty = CastField<FIntProperty>(AnimSlotProperty))
									{
										FString Message = FString::Printf(
											TEXT("The column with name [%s] for the Anim Slot property should be an FName instead of an Integer, it will be internally converted to FName but should probaly be converted in the table itself."), 
											*AnimBP);
										GenerationContext.Log(FText::FromString(Message), TableNode, EMessageSeverity::Info);

										SlotIndex = FName(FString::FromInt(IntProperty->GetPropertyValue(AnimSlotData)));
									}
									else if (const FNameProperty* NameProperty = CastField<FNameProperty>(AnimSlotProperty))
									{
										SlotIndex = NameProperty->GetPropertyValue(AnimSlotData);
									}
								}
							}

							if (SlotIndex.GetStringLength() != 0)
							{
								// Getting animation instance soft class from data table
								if (FProperty* AnimBPProperty = DataTablePtr->FindTableProperty(FName(*AnimBP)))
								{
									uint8* AnimBPData = AnimBPProperty->ContainerPtrToValuePtr<uint8>(AnimRowData, 0);

									if (AnimBPData)
									{
										if (const FSoftClassProperty* SoftClassProperty = CastField<FSoftClassProperty>(AnimBPProperty))
										{
											TSoftClassPtr<UAnimInstance> AnimInstance(SoftClassProperty->GetPropertyValue(AnimBPData).ToSoftObjectPath());

											if (!AnimInstance.IsNull())
											{
												GenerationContext.AddParticipatingObject(AnimInstance);

												const int32 AnimInstanceIndex = GenerationContext.AnimBPAssets.AddUnique(AnimInstance);

												AnimBPAssetTag = GenerateAnimationInstanceTag(AnimInstanceIndex, SlotIndex);
											}
										}
									}
								}
							}
							else
							{
								FString msg = FString::Printf(TEXT("Could not find the Slot column of the animation blueprint column [%s] for the mesh column [%s] row [%s]."), *AnimBP, *ColumnName, *RowName);
								LogRowGenerationMessage(TableNode, DataTablePtr, GenerationContext, msg, RowName);
							}
						}
					}
				}
				else
				{
					FString msg = FString::Printf(TEXT("Could not found the Slot column of the animation blueprint column [%s] for the mesh column [%s]."), *AnimBP, *ColumnName);
					GenerationContext.Log(FText::FromString(msg), TableNode);
				}
			}

			// Getting Gameplay tags
			if (!GameplayTag.IsEmpty())
			{
				if (DataTablePtr)
				{
					uint8* GameplayRowData = DataTablePtr->FindRowUnchecked(FName(*RowName));

					if (GameplayRowData)
					{
						// Getting animation tag row value from data table
						if (FProperty* GameplayTagProperty = DataTablePtr->FindTableProperty(FName(*GameplayTag)))
						{
							uint8* GameplayTagData = GameplayTagProperty->ContainerPtrToValuePtr<uint8>(GameplayRowData, 0);

							if (const FStructProperty* StructProperty = CastField<FStructProperty>(GameplayTagProperty))
							{
								if (StructProperty->Struct == TBaseStructure<FGameplayTagContainer>::Get())
								{
									if (GameplayTagData)
									{
										const FGameplayTagContainer* TagContainer = reinterpret_cast<FGameplayTagContainer*>(GameplayTagData);
										TagContainer->GetGameplayTagArray(GameplayTags);
									}
								}
							}
						}
					}
				}
			}

			// Getting reference Mesh
			USkeletalMesh* ReferenceSkeletalMesh = TableNode->GetColumnDefaultAssetByType<USkeletalMesh>(ColumnName);

			if (!ReferenceSkeletalMesh)
			{
				FString msg = FString::Printf(TEXT("Reference Skeletal Mesh not found for column [%s]."), *ColumnName);
				GenerationContext.Log(FText::FromString(msg), TableNode);

				return false;
			}

			GetLODAndSectionForAutomaticLODs(GenerationContext, *TableNode, *SkeletalMesh, LODIndexConnected, SectionIndexConnected, LODIndex, SectionIndex, bOnlyConnectedLOD);
			
			// Parameter used for LOD differences
	
			if (GenerationContext.CurrentAutoLODStrategy != ECustomizableObjectAutomaticLODStrategy::AutomaticFromMesh || 
				SectionIndex == SectionIndexConnected)
			{
				const int32 NumLODs = SkeletalMesh->GetImportedModel()->LODModels.Num();

				if (NumLODs <= LODIndex)
				{
					LODIndex = NumLODs - 1;

					FString msg = FString::Printf(TEXT("Mesh from column [%s] row [%s] needs LOD %d but has less LODs than the reference mesh. LOD %d will be used instead. This can cause some performance penalties."),
						*ColumnName, *RowName, LODIndex, LODIndex);

					LogRowGenerationMessage(TableNode, DataTablePtr, GenerationContext, msg, RowName);
				}
			}

			FSkeletalMeshModel* ImportedModel = SkeletalMesh->GetImportedModel();
			
			if (ImportedModel->LODModels.IsValidIndex(LODIndex)) // Ignore error since this Section is empty due to Automatic LODs From Mesh
			{
				if (!ImportedModel->LODModels[LODIndex].Sections.IsValidIndex(SectionIndex))
				{
					FString msg = FString::Printf(TEXT("Mesh from column [%s] row [%s] does not have section %d at LOD %d"), *ColumnName, *RowName, SectionIndexConnected, LODIndex);
					LogRowGenerationMessage(TableNode, DataTablePtr, GenerationContext, msg, RowName);
				}
			}

			FString MutableColumnName = TableNode->GenerateSkeletalMeshMutableColumName(ColumnName, LODIndex, SectionIndex);

			CurrentColumn = MutableTable.get()->FindColumn(MutableColumnName);

			if (CurrentColumn == -1)
			{
				CurrentColumn = MutableTable->AddColumn(MutableColumnName, mu::ETableColumnType::Mesh);
			}

			// First process the mesh tags that are going to make the mesh unique and affect whether it's repeated in 
			// the mesh cache or not
			FString MeshUniqueTags;

			if (!AnimBPAssetTag.IsEmpty())
			{
				MeshUniqueTags += AnimBPAssetTag;
			}

			TArray<FString> ArrayAnimBPTags;

			for (const FGameplayTag& Tag : GameplayTags)
			{
				MeshUniqueTags += GenerateGameplayTag(Tag.ToString());
			}
			
			TArray<FCustomizableObjectStreameableResourceId> StreamedResources;

			if (GenerationContext.Object->bEnableAssetUserDataMerge)
			{
				const TArray<UAssetUserData*>* AssetUserDataArray = SkeletalMesh->GetAssetUserDataArray();

				if (AssetUserDataArray)
				{
					for (UAssetUserData* AssetUserData : *AssetUserDataArray)
					{
						if (!AssetUserData)
						{
							continue;
						}

						const int32 ResourceIndex = GenerationContext.AddAssetUserDataToStreamedResources(AssetUserData);
						
						if (ResourceIndex >= 0)
						{
							FCustomizableObjectStreameableResourceId ResourceId;
							ResourceId.Id = GenerationContext.AddAssetUserDataToStreamedResources(AssetUserData);
							ResourceId.Type = (uint8)FCustomizableObjectStreameableResourceId::EType::AssetUserData;

							StreamedResources.Add(ResourceId);
						}

						MeshUniqueTags += AssetUserData->GetPathName();
					}
				}
			}

			//TODO: Add AnimBp physics to Tables.
			constexpr bool bIsReference = false;
			mu::Ptr<mu::Mesh> MutableMesh = GenerateMutableMesh(SkeletalMesh, TSoftClassPtr<UAnimInstance>(), LODIndexConnected, SectionIndexConnected, 
																LODIndex, SectionIndex, MeshUniqueTags, SectionMetadataId, GenerationContext, TableNode, ReferenceSkeletalMesh, bIsReference);

			if (MutableMesh)
			{
 				if (SkeletalMesh->GetPhysicsAsset() && MutableMesh->GetPhysicsBody() && MutableMesh->GetPhysicsBody()->GetBodyCount())
				{
 					TSoftObjectPtr<UPhysicsAsset> PhysicsAsset = SkeletalMesh->GetPhysicsAsset();

					GenerationContext.AddParticipatingObject(*PhysicsAsset); 						

					const int32 AssetIndex = GenerationContext.PhysicsAssets.AddUnique(PhysicsAsset);
					FString PhysicsAssetTag = FString("__PA:") + FString::FromInt(AssetIndex);

					AddTagToMutableMeshUnique(*MutableMesh, PhysicsAssetTag);
				}

				if (!AnimBPAssetTag.IsEmpty())
				{
					AddTagToMutableMeshUnique(*MutableMesh, AnimBPAssetTag);
				}

				for (const FGameplayTag& Tag : GameplayTags)
				{
					AddTagToMutableMeshUnique(*MutableMesh, GenerateGameplayTag(Tag.ToString()));
				}

				for (FCustomizableObjectStreameableResourceId ResourceId : StreamedResources)
				{
					MutableMesh->AddStreamedResource(BitCast<uint64>(ResourceId));
				}

				AddSocketTagsToMesh(SkeletalMesh, MutableMesh, GenerationContext);

				if (UCustomizableObjectSystem::GetInstance()->IsMutableAnimInfoDebuggingEnabled())
				{
					FString MeshPath;
					SkeletalMesh->GetOuter()->GetPathName(nullptr, MeshPath);
					FString MeshTag = FString("__MeshPath:") + MeshPath;
					AddTagToMutableMeshUnique(*MutableMesh, MeshTag);
				}

				MutableTable->SetCell(CurrentColumn, RowId, MutableMesh.get(), SkeletalMesh);
			}
			else
			{
				FString msg = FString::Printf(TEXT("Error converting skeletal mesh LOD %d, Section %d from column [%s] row [%s] to mutable."),
					LODIndex, SectionIndex, *ColumnName, *RowName);
				LogRowGenerationMessage(TableNode, DataTablePtr, GenerationContext, msg, RowName);
			}
		}

		else if (SoftObjectProperty->PropertyClass->IsChildOf(UStaticMesh::StaticClass()))
		{
			GenerationContext.AddParticipatingObject(SoftObject);
			
			UObject* Object = GenerationContext.LoadObject(SoftObject);

			UStaticMesh* StaticMesh = Cast<UStaticMesh>(Object);
			if (!StaticMesh)
			{
				return false;
			}

			// Getting reference Mesh
			UStaticMesh* ReferenceStaticMesh = TableNode->GetColumnDefaultAssetByType<UStaticMesh>(ColumnName);

			if (!ReferenceStaticMesh)
			{
				FString msg = FString::Printf(TEXT("Reference Static Mesh not found for column [%s]."), *ColumnName);
				GenerationContext.Log(FText::FromString(msg), TableNode);

				return false;
			}

			// Parameter used for LOD differences
			int32 CurrentLOD = LODIndex;

			int NumLODs = StaticMesh->GetRenderData()->LODResources.Num();

			if (NumLODs <= CurrentLOD)
			{
				CurrentLOD = NumLODs - 1;

				FString msg = FString::Printf(TEXT("Mesh from column [%s] row [%s] needs LOD %d but has less LODs than the reference mesh. LOD %d will be used instead. This can cause some performance penalties."),
					*ColumnName, *RowName, LODIndex, CurrentLOD);
				LogRowGenerationMessage(TableNode, DataTablePtr, GenerationContext, msg, RowName);
			}

			int32 NumMaterials = StaticMesh->GetRenderData()->LODResources[CurrentLOD].Sections.Num();
			int32 ReferenceNumMaterials = ReferenceStaticMesh->GetRenderData()->LODResources[CurrentLOD].Sections.Num();

			if (NumMaterials != ReferenceNumMaterials)
			{
				FString FirstTextOption = NumMaterials > ReferenceNumMaterials ? "more" : "less";
				FString SecondTextOption = NumMaterials > ReferenceNumMaterials ? "Some will be ignored" : "This can cause some compilation errors.";

				FString msg = FString::Printf(TEXT("Mesh from column [%s] row [%s] has %s Sections than the reference mesh. %s"), *ColumnName, *RowName, *FirstTextOption, *SecondTextOption);
				LogRowGenerationMessage(TableNode, DataTablePtr, GenerationContext, msg, RowName);
			}

			FString MutableColumnName = TableNode->GenerateStaticMeshMutableColumName(ColumnName, SectionIndex);

			CurrentColumn = MutableTable.get()->FindColumn(MutableColumnName);

			if (CurrentColumn == -1)
			{
				CurrentColumn = MutableTable->AddColumn(MutableColumnName, mu::ETableColumnType::Mesh);
			}

			constexpr bool bIsReference = false;
			mu::MeshPtr MutableMesh = GenerateMutableMesh(StaticMesh, TSoftClassPtr<UAnimInstance>(), CurrentLOD, SectionIndex, CurrentLOD, SectionIndex, 
														  FString(), 0, GenerationContext, TableNode, nullptr, bIsReference);

			if (MutableMesh)
			{
				MutableTable->SetCell(CurrentColumn, RowId, MutableMesh.get(), StaticMesh);
			}
			else
			{
				FString msg = FString::Printf(TEXT("Error converting skeletal mesh LOD %d, Section %d from column [%s] row [%s] to mutable."),
					LODIndex, SectionIndex, *ColumnName, *RowName);
				LogRowGenerationMessage(TableNode, DataTablePtr, GenerationContext, msg, RowName);
			}
		}

		else if (SoftObjectProperty->PropertyClass->IsChildOf(UTexture::StaticClass()))
		{
			GenerationContext.AddParticipatingObject(SoftObject);

			// Removing encoding part
			const FString PinName = ColumnName.Replace(TEXT("--PassThrough"), TEXT(""), ESearchCase::CaseSensitive);
			
			UObject* Object = GenerationContext.LoadObject(SoftObject);

			UTexture* Texture = Cast<UTexture>(Object);
			if (!Texture)
			{
				Texture = TableNode->GetColumnDefaultAssetByType<UTexture>(PinName);

				FString Message = Cast<UObject>(Object) ? "not a suported Texture" : "null";
				FString WarningMessage = FString::Printf(TEXT("Texture from column [%s] row [%s] is %s. The default texture will be used instead."), *PinName, *RowName, *Message);
				LogRowGenerationMessage(TableNode, DataTablePtr, GenerationContext, WarningMessage, RowName);
			}

			// There will be always one of the two options
			check(Texture);

			// Getting column index from column name
			CurrentColumn = MutableTable->FindColumn(ColumnName);

			if (CurrentColumn == INDEX_NONE)
			{
				CurrentColumn = MutableTable->AddColumn(ColumnName, mu::ETableColumnType::Image);
			}

			const bool bIsPassthroughTexture = TableNode->GetColumnImageMode(PinName) == ETableTextureType::PASSTHROUGH_TEXTURE;
			mu::Ptr<mu::ResourceProxyMemory<mu::Image>> Proxy = new mu::ResourceProxyMemory<mu::Image>(GenerateImageConstant(Texture, GenerationContext, bIsPassthroughTexture));
			MutableTable->SetCell(CurrentColumn, RowId, Proxy.get());
		}

		else if (SoftObjectProperty->PropertyClass->IsChildOf(UMaterialInterface::StaticClass()))
		{
			GenerationContext.AddParticipatingObject(SoftObject);
			
			UObject* Object = GenerationContext.LoadObject(SoftObject);

			// Get display name of the column of the data table (name showed in the table and struct editors)
			// Will be used in the warnings to help to identify a column with errors.
			FString MaterialColumnDisplayName = ColumnProperty->GetDisplayNameText().ToString();
			
			// Get the real name of the Property column
			FString MaterialColumnName = ColumnProperty->GetName();

			UMaterialInstance* MaterialInstance = Cast<UMaterialInstance>(Object);
			UMaterialInstance* ReferenceMaterial = TableNode->GetColumnDefaultAssetByType<UMaterialInstance>(MaterialColumnName);

			if (!ReferenceMaterial)
			{
				FString msg = FString::Printf(TEXT("Default Material Instance not found for column [%s]."), *MaterialColumnDisplayName);
				GenerationContext.Log(FText::FromString(msg), TableNode);

				return false;
			}

			GenerationContext.AddParticipatingObject(*ReferenceMaterial);

			const bool bTableMaterialCheckDisabled = GenerationContext.Object->bDisableTableMaterialsParentCheck;
			const bool bMaterialParentMismatch = !bTableMaterialCheckDisabled && MaterialInstance
												 && ReferenceMaterial->GetMaterial() != MaterialInstance->GetMaterial();

			if (!MaterialInstance || bMaterialParentMismatch)
			{
				FText Warning;

				if (!MaterialInstance)
				{
					if (UMaterial* Material = Cast<UMaterial>(Object))
					{
						Warning = FText::Format(LOCTEXT("IsAMaterial", "Asset from column [{0}] row [{1}] is a Material and not a MaterialInstance. The default Material Instance will be used instead."),
							FText::FromString(MaterialColumnDisplayName), FText::FromString(RowName));
					}
					else
					{
						Warning = FText::Format(LOCTEXT("NullMaterialInstance", "Material Instance from column [{0}] row [{1}] is null. The default Material Instance will be used instead."),
							FText::FromString(MaterialColumnDisplayName), FText::FromString(RowName));
					}
				}
				else
				{
					Warning = FText::Format(LOCTEXT("MatInstanceFromDifferentParent","Material Instance from column [{0}] row [{1}] has a different Material Parent than the Default Material Instance. The Default Material Instance will be used instead."),
						FText::FromString(MaterialColumnDisplayName), FText::FromString(RowName));
				}

				MaterialInstance = ReferenceMaterial;

				LogRowGenerationMessage(TableNode, DataTablePtr, GenerationContext, Warning.ToString(), RowName);
			}
			
			FString EncodedSwitchParameterName = "__MutableMaterialId";
			if (ColumnName.Contains(EncodedSwitchParameterName))
			{
				CurrentColumn = MutableTable.get()->FindColumn(ColumnName);

				if (CurrentColumn == -1)
				{
					CurrentColumn = MutableTable->AddColumn(ColumnName, mu::ETableColumnType::Scalar);
				}

				int32 ReferenceMaterialId = GenerationContext.ReferencedMaterials.AddUnique(MaterialInstance);
				MutableTable->SetCell(CurrentColumn, RowId, (float)ReferenceMaterialId);

				return true;
			}

			int32 ColumnIndex;

			// Getting parameter value
			TArray<FMaterialParameterInfo> ParameterInfos;
			TArray<FGuid> ParameterGuids;

			MaterialInstance->GetMaterial()->GetAllParameterInfoOfType(EMaterialParameterType::Texture, ParameterInfos, ParameterGuids);
			
			FGuid ParameterId(GenerationContext.CurrentMaterialTableParameterId);
			int32 ParameterIndex = ParameterGuids.Find(ParameterId);

			if (ParameterIndex != INDEX_NONE && ParameterInfos[ParameterIndex].Name == GenerationContext.CurrentMaterialTableParameter)
			{
				// Getting column index from parameter name
				ColumnIndex = MutableTable->FindColumn(ColumnName);

				if (ColumnIndex == INDEX_NONE)
				{
					// If there is no column with the parameters name, we generate a new one
					ColumnIndex = MutableTable->AddColumn(ColumnName, mu::ETableColumnType::Image);
				}

				UTexture* ParentTextureValue = nullptr;
				MaterialInstance->GetMaterial()->GetTextureParameterValue(ParameterInfos[ParameterIndex], ParentTextureValue);
				
				UTexture2D* ParentParameterTexture = Cast<UTexture2D>(ParentTextureValue);
				if (!ParentParameterTexture)
				{
					FString ParamName = ParameterInfos[ParameterIndex].Name.ToString();
					FString Message = Cast<UObject>(ParentParameterTexture) ? "not a Texture2D" : "null";
					
					FString msg = FString::Printf(TEXT("Parameter [%s] from Default Material Instance of column [%s] is %s. This parameter will be ignored."), *ParamName, *MaterialColumnDisplayName, *Message);
					LogRowGenerationMessage(TableNode, DataTablePtr, GenerationContext, msg, RowName);
					 
					 return false;
				}

				UTexture* TextureValue = nullptr;
				MaterialInstance->GetTextureParameterValue(ParameterInfos[ParameterIndex], TextureValue);

				UTexture2D* ParameterTexture = Cast<UTexture2D>(TextureValue);

				if (!ParameterTexture)
				{
					ParameterTexture = ParentParameterTexture;

					FString ParamName = GenerationContext.CurrentMaterialTableParameter;
					FString Message = Cast<UObject>(TextureValue) ? "not a Texture2D" : "null";

					FString msg = FString::Printf(TEXT("Parameter [%s] from material instance of column [%s] row [%s] is %s. The parameter texture of the default material will be used instead."), *ParamName, *MaterialColumnDisplayName, *RowName, *Message);
					LogRowGenerationMessage(TableNode, DataTablePtr, GenerationContext, msg, RowName);
				}

				bool bIsPassthroughTexture = false;
				mu::Ptr<mu::ResourceProxyMemory<mu::Image>> Proxy = new mu::ResourceProxyMemory<mu::Image>(GenerateImageConstant(ParameterTexture, GenerationContext, bIsPassthroughTexture));
				MutableTable->SetCell(ColumnIndex, RowId, Proxy.get());

				return true;
			}
		}

		else if (SoftObjectProperty->PropertyClass->IsChildOf(UPoseAsset::StaticClass()))
		{
			GenerationContext.AddParticipatingObject(SoftObject);
			
			UObject* Object = GenerationContext.LoadObject(SoftObject);
			
			if (UPoseAsset* PoseAsset = Cast<UPoseAsset>(Object))
			{
				CurrentColumn = MutableTable.get()->FindColumn(ColumnName);

				if (CurrentColumn == -1)
				{
					CurrentColumn = MutableTable->AddColumn(ColumnName, mu::ETableColumnType::Mesh);
				}

				TArray<FName> ArrayBoneName;
				TArray<FTransform> ArrayTransform;
				UCustomizableObjectNodeAnimationPose::StaticRetrievePoseInformation(PoseAsset, GenerationContext.GetCurrentComponentInfo()->RefSkeletalMesh, ArrayBoneName, ArrayTransform);

				mu::Ptr<mu::Mesh> MutableMesh = new mu::Mesh();
				mu::Ptr<mu::Skeleton> MutableSkeleton = new mu::Skeleton;

				MutableMesh->SetSkeleton(MutableSkeleton);
				MutableMesh->SetBonePoseCount(ArrayBoneName.Num());
				MutableSkeleton->SetBoneCount(ArrayBoneName.Num());

				for (int32 i = 0; i < ArrayBoneName.Num(); ++i)
				{
					const FName BoneName = ArrayBoneName[i];
					const mu::FBoneName& BoneId = GenerationContext.GetBoneUnique(BoneName);

					MutableSkeleton->SetDebugName(i, BoneName);
					MutableSkeleton->SetBoneName(i, { BoneId });
					MutableMesh->SetBonePose(i, BoneId, (FTransform3f)ArrayTransform[i], mu::EBoneUsageFlags::Skinning);
				}

				MutableTable->SetCell(CurrentColumn, RowId, MutableMesh.get());
			}
		}

		else
		{
			// Unsuported Variable Type
			FString msg = FString::Printf(TEXT("[%s] is not a supported class for mutable nodes."), *SoftObjectProperty->PropertyClass.GetName());
			GenerationContext.Log(FText::FromString(msg), TableNode);

			return false;
		}
	}

	else if (const FStructProperty* StructProperty = CastField<FStructProperty>(ColumnProperty))
	{
		if (StructProperty->Struct == TBaseStructure<FLinearColor>::Get())
		{
			CurrentColumn = MutableTable->FindColumn(ColumnName);

			if (CurrentColumn == INDEX_NONE)
			{
				CurrentColumn = MutableTable->AddColumn(ColumnName, mu::ETableColumnType::Color);
			}

			// Setting cell value
			FLinearColor Value = *(FLinearColor*)CellData;
			MutableTable->SetCell(CurrentColumn, RowId, Value);
		}
		
		else
		{
			// Unsuported Variable Type
			return false;
		}
	}

	else if (const FNumericProperty* FloatNumProperty = CastField<FFloatProperty>(ColumnProperty))
	{
		CurrentColumn = MutableTable->FindColumn(ColumnName);

		if (CurrentColumn == INDEX_NONE)
		{
			CurrentColumn = MutableTable->AddColumn(ColumnName, mu::ETableColumnType::Scalar);
		}

		// Setting cell value
		float Value = FloatNumProperty->GetFloatingPointPropertyValue(CellData);
		MutableTable->SetCell(CurrentColumn, RowId, Value);
	}

	else if (const FNumericProperty* DoubleNumProperty = CastField<FDoubleProperty>(ColumnProperty))
	{
		CurrentColumn = MutableTable->FindColumn(ColumnName);
	
		if (CurrentColumn == INDEX_NONE)
		{
			CurrentColumn = MutableTable->AddColumn(ColumnName, mu::ETableColumnType::Scalar);
		}
	
		// Setting cell value
		float Value = DoubleNumProperty->GetFloatingPointPropertyValue(CellData);
		MutableTable->SetCell(CurrentColumn, RowId, Value);
	}

	else
	{
		// Unsuported Variable Type		
		return false;
	}

	return true;
}


uint8* GetCellData(const FName& RowName, const UDataTable& DataTable, const FProperty& ColumnProperty)
{
	// Get Row Data
	uint8* RowData = DataTable.FindRowUnchecked(RowName);

	if (RowData)
	{
		return ColumnProperty.ContainerPtrToValuePtr<uint8>(RowData, 0);
	}

	return nullptr;
}


FName GetAnotherOption(FName SelectedOptionName, const TArray<FName>& RowNames)
{
	for (const FName& CandidateOption : RowNames)
	{
		if (CandidateOption != SelectedOptionName)
		{
			return CandidateOption;
		}
	}

	return FName("None");
}


TArray<FName> GetEnabledRows(const UDataTable& DataTable, const UCustomizableObjectNodeTable& TableNode)
{
	TArray<FName> RowNames;
	const UScriptStruct* TableStruct = DataTable.GetRowStruct();

	if (!TableStruct)
	{
		return RowNames;
	}

	TArray<FName> TableRowNames = DataTable.GetRowNames();
	FBoolProperty* BoolProperty = nullptr;

	// Sort them to avoid cooked data indeterminism problems. Rows may come from different tables and their loading order
	// is not defined.
	TableRowNames.Sort([](const FName& A, const FName& B) { return A.ToString() < B.ToString(); });

	for (TFieldIterator<FProperty> PropertyIt(TableStruct); PropertyIt && TableNode.bDisableCheckedRows; ++PropertyIt)
	{
		BoolProperty = CastField<FBoolProperty>(*PropertyIt);

		if (BoolProperty)
		{
			for (const FName& RowName : TableRowNames)
			{
				if (uint8* CellData = GetCellData(RowName, DataTable, *BoolProperty))
				{
					if (!BoolProperty->GetPropertyValue(CellData))
					{
						RowNames.Add(RowName);
					}
				}
			}

			// There should be only one Bool column
			break;
		}
	}

	// There is no Bool column or we don't want to disable rows
	if (!BoolProperty)
	{
		return TableRowNames;
	}

	return RowNames;
}


void RestrictRowNamesToSelectedOption(TArray<FName>& InOutRowNames, const UCustomizableObjectNodeTable& TableNode, FMutableGraphGenerationContext& GenerationContext)
{
	if (!GenerationContext.Options.ParamNamesToSelectedOptions.IsEmpty())
	{
		FMutableParamNameSet* ParamNameSet = GenerationContext.TableToParamNames.Find(TableNode.Table->GetPathName());

		if (ParamNameSet && !ParamNameSet->ParamNames.IsEmpty())
		{
			TSet<FName> SelectedOptionNames;

			for (const FString& ParamName : ParamNameSet->ParamNames)
			{
				// If the param is in the map restrict to only the selected option
				const FString* SelectedOptionString = GenerationContext.Options.ParamNamesToSelectedOptions.Find(ParamName);

				if (SelectedOptionString)
				{
					if (!(*SelectedOptionString == FString("None") && TableNode.bAddNoneOption))
					{
						SelectedOptionNames.Add(FName(*SelectedOptionString));
					}
				}
			}

			if (!SelectedOptionNames.IsEmpty())
			{
				bool bRowNamesContainsSelectedOptionName = false;

				for (const FName& OptionName : SelectedOptionNames)
				{
					if (InOutRowNames.Contains(OptionName))
					{
						bRowNamesContainsSelectedOptionName = true;
						break;
					}
				}

				if (bRowNamesContainsSelectedOptionName)
				{
					InOutRowNames.Empty(SelectedOptionNames.Num());

					for (const FName& OptionName : SelectedOptionNames)
					{
						InOutRowNames.Add(OptionName);
					}
				}
				else
				{
					InOutRowNames.Empty(0);
				}
			}
		}
	}
}


void RestrictRowContentByVersion( TArray<FName>& InOutRowNames, const UDataTable& DataTable, const UCustomizableObjectNodeTable& TableNode, FMutableGraphGenerationContext& GenerationContext)
{
	FProperty* ColumnProperty = DataTable.FindTableProperty(TableNode.VersionColumn);

	if (!ColumnProperty)
	{
		return;
	}

	ICustomizableObjectVersionBridgeInterface* CustomizableObjectVersionBridgeInterface = Cast<ICustomizableObjectVersionBridgeInterface>(GenerationContext.RootVersionBridge);
	if (!CustomizableObjectVersionBridgeInterface)
	{
		const FString Message = "Found a data table with at least a row with a Custom Version asset but the Root Object does not have a Version Bridge asset assigned.";
		GenerationContext.Log(FText::FromString(Message), &TableNode, EMessageSeverity::Error);
		return;
	}

	TArray<FName> OutRowNames;
	OutRowNames.Reserve(InOutRowNames.Num());

	for (int32 RowIndex = 0; RowIndex < InOutRowNames.Num(); ++RowIndex)
	{
		if (uint8* CellData = GetCellData(InOutRowNames[RowIndex], DataTable, *ColumnProperty))
		{
			if (!CustomizableObjectVersionBridgeInterface->IsVersionPropertyIncludedInCurrentRelease(*ColumnProperty, CellData))
			{
				continue;
			}

			OutRowNames.Add(InOutRowNames[RowIndex]);
		}
	}

	InOutRowNames = OutRowNames;
}


void GenerateUniqueRowIds(const TArray<FName>& RowNames, TArray<uint32>& OutRowIds)
{
	const int32 NumRows = RowNames.Num();

	OutRowIds.SetNum(NumRows);

	for (int32 RowIndex = 0; RowIndex < NumRows; ++RowIndex)
	{
		const FString& RowName = RowNames[RowIndex].ToString();

		uint32 RowId = CityHash32(reinterpret_cast<const char*>(*RowName), RowName.Len() * sizeof(FString::ElementType));

		// Ensure Row Id is unique 
		bool bIsUnique = false;
		while (!bIsUnique)
		{
			bIsUnique = true;
			for (int32 RowIdIndex = 0; RowIdIndex < RowIndex; ++RowIdIndex)
			{
				if (OutRowIds[RowIdIndex] == RowId)
				{
					bIsUnique = false;
					++RowId;
					break;
				}
			}
		}

		OutRowIds[RowIndex] = RowId;
	}
}


TArray<FName> GetRowsToCompile(const UDataTable& DataTable, const UCustomizableObjectNodeTable& TableNode, FMutableGraphGenerationContext& GenerationContext, TArray<uint32>& OutRowIds)
{
	if (FMutableGraphGenerationContext::FGeneratedDataTablesData* Result = GenerationContext.GeneratedTables.Find(DataTable.GetName()))
	{
		OutRowIds = Result->RowIds;
		return Result->RowNames;
	}
	else
	{
		TArray<FName> RowNames = GetEnabledRows(DataTable, TableNode);

		if (!RowNames.IsEmpty())
		{
			RestrictRowNamesToSelectedOption(RowNames, TableNode, GenerationContext);
			RestrictRowContentByVersion(RowNames, DataTable, TableNode, GenerationContext);
		}

		GenerateUniqueRowIds(RowNames, OutRowIds);

		return RowNames;
	}
}


bool GenerateTableColumn(const UCustomizableObjectNodeTable* TableNode, const UEdGraphPin* Pin, mu::TablePtr MutableTable, const FString& DataTableColumnName, const FProperty* ColumnProperty,
	const int32 LODIndexConnected, const int32 SectionIndexConnected, const int32 LODIndex, const int32 SectionIndex, uint32 SectionMetadataId, const bool bOnlyConnectedLOD, FMutableGraphGenerationContext& GenerationContext)
{
	MUTABLE_CPUPROFILER_SCOPE(GenerateTableColumn);

	SCOPED_PIN_DATA(GenerationContext, Pin)

	if (!TableNode)
	{
		return false;
	}

	UDataTable* DataTable = GetDataTable(TableNode, GenerationContext);

	if (!DataTable || !DataTable->GetRowStruct())
	{
		return false;
	}
	
	GenerationContext.AddParticipatingObject(*DataTable);

	// Getting names of the rows to access the information
	TArray<uint32> RowIds;
	TArray<FName> RowNames = GetRowsToCompile(*DataTable, *TableNode, GenerationContext, RowIds);

	// Pre-pass to request async loading of all data. This seems to be slightly faster because it avoids the sync after every separate load.
	if (!GenerationContext.bParticipatingObjectsPass)
	{
		TArray<int32> LoadRequests;
		LoadRequests.Reserve(RowNames.Num());

		MUTABLE_CPUPROFILER_SCOPE(Preload);
		for (int32 RowIndex = 0; RowIndex < RowNames.Num(); ++RowIndex)
		{
			if (uint8* CellData = GetCellData(RowNames[RowIndex], *DataTable, *ColumnProperty))
			{
				// Getting property type
				if (const FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(ColumnProperty))
				{
					const FSoftObjectPtr& Path = SoftObjectProperty->GetPropertyValue(CellData);
					const FString PackageName = Path.GetLongPackageName();
					if (!PackageName.IsEmpty())
					{
						LoadRequests.Add( LoadPackageAsync(PackageName) );
					}
				}
			}
		}

		{
			MUTABLE_CPUPROFILER_SCOPE(Flush);
			FlushAsyncLoading(LoadRequests);
		}
	}

	for (int32 RowIndex = 0; RowIndex < RowNames.Num(); ++RowIndex)
	{
		if (uint8* CellData = GetCellData(RowNames[RowIndex], *DataTable, *ColumnProperty))
		{
			bool bCellGenerated = FillTableColumn(TableNode, MutableTable, DataTableColumnName, RowNames[RowIndex].ToString(), RowIds[RowIndex], CellData, ColumnProperty,
				LODIndexConnected, SectionIndexConnected, LODIndex, SectionIndex, SectionMetadataId, bOnlyConnectedLOD, GenerationContext);

			if (!bCellGenerated)
			{
				return false;
			}
		}
	}

	return true;
}


void GenerateTableParameterUIData(const UDataTable* DataTable, const UCustomizableObjectNodeTable* TableNode, FMutableGraphGenerationContext& GenerationContext)
{
	TArray<uint32> RowIds;
	TArray<FName> RowNames = GetRowsToCompile(*DataTable, *TableNode, GenerationContext, RowIds);

	for (const FName& Name : RowNames)
	{
		TSet<TSoftObjectPtr<UDataTable>>& DataTables = GenerationContext.IntParameterOptionDataTable.FindOrAdd(MakeTuple(TableNode->ParameterName, Name.ToString()));
		DataTables.Add(TSoftObjectPtr<UDataTable>(const_cast<UDataTable*>(DataTable)));
	}
	
	// Generating Parameter UI MetaData if not exists
	if (!GenerationContext.ParameterUIDataMap.Contains(TableNode->ParameterName))
	{
		// Getting Table and row names to access the information

		FMutableParameterData ParameterUIData(TableNode->ParamUIMetadata, EMutableParameterType::Int);
		ParameterUIData.IntegerParameterGroupType = TableNode->bAddNoneOption ? ECustomizableObjectGroupType::COGT_ONE_OR_NONE : ECustomizableObjectGroupType::COGT_ONE;
		
		FMutableParameterData& ParameterUIDataRef = GenerationContext.ParameterUIDataMap.Add(TableNode->ParameterName, ParameterUIData);
		FProperty* MetadataColumnProperty = DataTable->FindTableProperty(TableNode->ParamUIMetadataColumn);
		bool bIsValidMetadataColumn = MetadataColumnProperty &&
			CastField<FStructProperty>(MetadataColumnProperty) &&
			CastField<FStructProperty>(MetadataColumnProperty)->Struct == FMutableParamUIMetadata::StaticStruct();

		// Trigger warning only if the name is different than "None"
		if (!TableNode->ParamUIMetadataColumn.IsNone() && !bIsValidMetadataColumn)
		{
			FText LogMessage = FText::Format(LOCTEXT("InvalidParamUIMetadataColumn_Warning",
				"UI Metadata Column [{0}] is not a valid type or does not exist in the Structure of the Node."), FText::FromName(TableNode->ParamUIMetadataColumn));
			GenerationContext.Log(LogMessage, TableNode);
		}

		FProperty* ThumbnailColumnProperty = DataTable->FindTableProperty(TableNode->ThumbnailColumn);
		bool bIsValidThumbnailColumn = ThumbnailColumnProperty && CastField<FSoftObjectProperty>(ThumbnailColumnProperty);

		// Trigger warning only if the name is different than "None"
		if (!TableNode->ThumbnailColumn.IsNone() && !bIsValidThumbnailColumn)
		{
			FText LogMessage = FText::Format(LOCTEXT("InvalidThumbnailColumn_Warning",
				"Thumbnail Column [{0}] is not an objet type or does not exist in the Structure of the Node."), FText::FromName(TableNode->ThumbnailColumn));
			GenerationContext.Log(LogMessage, TableNode);
		}

		if (!bIsValidMetadataColumn)
		{
			return;
		}

		for (int32 NameIndex = 0; NameIndex < RowNames.Num(); ++NameIndex)
		{
			FName RowName = RowNames[NameIndex];

			if (uint8* MetadataCellData = GetCellData(RowName, *DataTable, *MetadataColumnProperty))
			{
				FMutableParamUIMetadata MetadataValue = *reinterpret_cast<FMutableParamUIMetadata*>(MetadataCellData);

				FIntegerParameterUIData IntegerMetadata = FIntegerParameterUIData(MetadataValue);

				// Add thumbnail
				if (bIsValidThumbnailColumn && MetadataValue.EditorUIThumbnailObject.IsNull())
				{
					if (uint8* ThumbnailCellData = GetCellData(RowName, *DataTable, *ThumbnailColumnProperty))
					{
						FSoftObjectPtr* ObjectPtr = reinterpret_cast<FSoftObjectPtr*>(ThumbnailCellData);
						IntegerMetadata.ParamUIMetadata.EditorUIThumbnailObject = ObjectPtr->ToSoftObjectPath();
					}
				}

				// Add tags
				if (TableNode->bGatherTags)
				{
					if (const UScriptStruct* Struct = DataTable->GetRowStruct())
					{
						for (TFieldIterator<FProperty> It(Struct); It; ++It)
						{
							FProperty* ColumnProperty = *It;

							if (!ColumnProperty)
							{
								continue;
							}

							if (const FStructProperty* StructProperty = CastField<FStructProperty>(ColumnProperty))
							{
								if (StructProperty->Struct == TBaseStructure<FGameplayTagContainer>::Get())
								{
									FName ColumnName = FName(DataTableUtils::GetPropertyExportName(ColumnProperty));
								
									FProperty* TagColumnProperty = DataTable->FindTableProperty(ColumnName);
									if (uint8* TagCellData = GetCellData(RowName, *DataTable, *TagColumnProperty))
									{
										FGameplayTagContainer* TagContainer = reinterpret_cast<FGameplayTagContainer*>(TagCellData);
										IntegerMetadata.ParamUIMetadata.GameplayTags.AppendTags(*TagContainer);
									}
								}
							}
						}
					}
				}

				ParameterUIDataRef.ArrayIntegerParameterOption.Add(RowName.ToString(), IntegerMetadata);
			}
		}
	}
}


mu::TablePtr GenerateMutableSourceTable(const UDataTable* DataTable, const UCustomizableObjectNodeTable* TableNode, FMutableGraphGenerationContext& GenerationContext)
{
	check(DataTable && TableNode);

	if (GenerationContext.Options.ParamNamesToSelectedOptions.IsEmpty())
	{
		FMutableParamNameSet* ParamNameSet = GenerationContext.TableToParamNames.Find(DataTable->GetPathName());

		if (!ParamNameSet)
		{
			ParamNameSet = &GenerationContext.TableToParamNames.Add(DataTable->GetPathName());
		}

		ParamNameSet->ParamNames.Add(TableNode->ParameterName);
	}

	// Checking if the table is in the cache
	const FString TableName = DataTable->GetName();

	if (FMutableGraphGenerationContext::FGeneratedDataTablesData* CachedTable = GenerationContext.GeneratedTables.Find(TableName))
	{
		// Generating Parameter Metadata for parameters that reuse a Table
		GenerateTableParameterUIData(DataTable, TableNode, GenerationContext);

		if (!CachedTable->HasSameSettings(TableNode))
		{
			TArray<const UObject*> Nodes;
			Nodes.Add(TableNode);
			Nodes.Add(CachedTable->ReferenceNode);

			GenerationContext.Log(LOCTEXT("TableNodesCompilationRestrictionError",
				"Found one or more Table Nodes with the same data table but different Compilation Restrictions."), Nodes);
		}

		return CachedTable->GeneratedTable;
	}

	mu::TablePtr MutableTable = new mu::Table();

	if (const UScriptStruct* TableStruct = DataTable->GetRowStruct())
	{
		// Getting Table and row names to access the information
		TArray<uint32> RowIds;
		TArray<FName> RowNames = GetRowsToCompile(*DataTable, *TableNode, GenerationContext, RowIds);

		// Adding and filling Name Column
		MutableTable->AddColumn("Name", mu::ETableColumnType::String);

		for (int32 RowIndex = 0; RowIndex < RowNames.Num(); ++RowIndex)
		{
			MutableTable->AddRow(RowIds[RowIndex]);
			MutableTable->SetCell(0, RowIds[RowIndex], RowNames[RowIndex].ToString());
		}

		// Generating Parameter Metadata for new table parameters
		GenerateTableParameterUIData(DataTable, TableNode, GenerationContext);

		FMutableGraphGenerationContext::FGeneratedDataTablesData GeneratedTable;
		GeneratedTable.GeneratedTable = MutableTable;
		GeneratedTable.bDisableCheckedRows = TableNode->bDisableCheckedRows;
		GeneratedTable.VersionColumn = TableNode->VersionColumn;
		GeneratedTable.RowNames = RowNames;
		GeneratedTable.RowIds = RowIds;
		GeneratedTable.ReferenceNode = TableNode;

		// Add table to cache
		GenerationContext.GeneratedTables.Add(TableName, GeneratedTable);
	}
	else
	{
		FString msg = "Couldn't find the Data Table's Struct asset in the Node.";
		GenerationContext.Log(FText::FromString(msg), DataTable);
		
		return nullptr;
	}

	return MutableTable;
}


void AddCompositeTablesToParticipatingObjetcts(const UDataTable* Table, FMutableGraphGenerationContext& GenerationContext)
{
	if (const UCompositeDataTable* CompositeTable = Cast<UCompositeDataTable>(Table))
	{
		GenerationContext.AddParticipatingObject(*CompositeTable);

		/*for (const TArray<TObjectPtr<UDataTable>>& ParentTable : CompositeTable.ParentTables) // TODO 
		{
			AddCompositeTablesToParticipatingObjetcts(ParentTable, GenerationContext);
		}*/
	}
}


UDataTable* GetDataTable(const UCustomizableObjectNodeTable* TableNode, FMutableGraphGenerationContext& GenerationContext)
{
	UDataTable* OutDataTable = nullptr;

	if (TableNode->TableDataGatheringMode == ETableDataGatheringSource::ETDGM_AssetRegistry)
	{
		OutDataTable = GenerateDataTableFromStruct(TableNode, GenerationContext);
	}
	else
	{
		OutDataTable = TableNode->Table;
	}

	AddCompositeTablesToParticipatingObjetcts(OutDataTable, GenerationContext);

	return OutDataTable;
}


UDataTable* GenerateDataTableFromStruct(const UCustomizableObjectNodeTable* TableNode, FMutableGraphGenerationContext& GenerationContext)
{
	if (!TableNode->Structure)
	{
		GenerationContext.Log(LOCTEXT("EmptyStructureError", "Empty structure asset."), TableNode);
		return nullptr;
	}

	FMutableGraphGenerationContext::FGeneratedCompositeDataTablesData DataTableData;
	DataTableData.ParentStruct = TableNode->Structure;
	DataTableData.FilterPaths = TableNode->FilterPaths;
	
	//Checking cache of generated data tables
	int32 DataTableIndex = GenerationContext.GeneratedCompositeDataTables.Find(DataTableData);
	if (DataTableIndex != INDEX_NONE)
	{
		// DataTable already generated
		UCompositeDataTable* GeneratedDataTable = GenerationContext.GeneratedCompositeDataTables[DataTableIndex].GeneratedDataTable;
		return Cast<UDataTable>(GeneratedDataTable);
	}

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.GetRegistry();

	if (TableNode->FilterPaths.IsEmpty())
	{
		// Preventing load all data tables of the project
		GenerationContext.Log(LOCTEXT("NoFilePathsError", "There are no filter paths selected. This is an error to prevent loading all data table of the project."), TableNode);

		return nullptr;
	}

	TArray<FAssetData> DataTableAssets = TableNode->GetParentTables();

	UCompositeDataTable* CompositeDataTable = NewObject<UCompositeDataTable>();
	CompositeDataTable->RowStruct = TableNode->Structure;

	TArray<UDataTable*> ParentTables;

	for (const FAssetData& DataTableAsset : DataTableAssets)
	{
		if (DataTableAsset.IsValid())
		{
			if (UDataTable* DataTable = Cast<UDataTable>(DataTableAsset.GetAsset()))
			{
				ParentTables.Add(DataTable);
			}
		}
	}

	if (ParentTables.IsEmpty())
	{
		GenerationContext.Log(LOCTEXT("NoDataTablesFoundWarning", "Could not find a data table with the specified struct in the selected paths."), TableNode);

		return nullptr;
	}

	// Map to find the original data table of a row
	TMap<FName, TArray<UDataTable*>> OriginalTableRowsMap;

	// Set to iterate faster the repeated rows inside the map
	TSet<FName> RepeatedRowNamesArray;

	// Checking if a row name is repeated in several tables
	for (int32 ParentIndx = 0; ParentIndx < ParentTables.Num(); ++ParentIndx)
	{
		const TArray<FName>& RowNames = ParentTables[ParentIndx]->GetRowNames();

		for (const FName& RowName : RowNames)
		{
			TArray<UDataTable*>* DataTablesNames = OriginalTableRowsMap.Find(RowName);

			if (DataTablesNames == nullptr)
			{
				TArray<UDataTable*> ArrayTemp;
				ArrayTemp.Add(ParentTables[ParentIndx]);
				OriginalTableRowsMap.Add(RowName, ArrayTemp);
			}
			else
			{
				DataTablesNames->Add(ParentTables[ParentIndx]);
				RepeatedRowNamesArray.Add(RowName);
			}
		}
	}

	for (const FName& RowName : RepeatedRowNamesArray)
	{
		const TArray<UDataTable*>& DataTablesNames = OriginalTableRowsMap[RowName];

		FString TableNames;

		for (int32 NameIndx = 0; NameIndx < DataTablesNames.Num(); ++NameIndx)
		{
			TableNames += DataTablesNames[NameIndx]->GetName();

			if (NameIndx + 1 < DataTablesNames.Num())
			{
				TableNames += ", ";
			}
		}

		FString Message = FString::Printf(TEXT("Row with name [%s] repeated in the following Data Tables: [%s]. The last row processed will be used [%s]."),
			*RowName.ToString(), *TableNames, *DataTablesNames.Last()->GetName());
		GenerationContext.Log(FText::FromString(Message), TableNode);
	}

	CompositeDataTable->AppendParentTables(ParentTables);

	// Adding Generated Data Table to the cache
	DataTableData.GeneratedDataTable = CompositeDataTable;
	GenerationContext.GeneratedCompositeDataTables.Add(DataTableData);
	GenerationContext.CompositeDataTableRowToOriginalDataTableMap.Add(CompositeDataTable, OriginalTableRowsMap);
	
	return Cast<UDataTable>(CompositeDataTable);
}


void LogRowGenerationMessage(const UCustomizableObjectNodeTable* TableNode, const UDataTable* DataTable, FMutableGraphGenerationContext& GenerationContext, const FString& Message, const FString& RowName)
{
	FString FinalMessage = Message;

	if (TableNode->TableDataGatheringMode == ETableDataGatheringSource::ETDGM_AssetRegistry)
	{
		TMap<FName, TArray<UDataTable*>>* ParameterDataTableMap = GenerationContext.CompositeDataTableRowToOriginalDataTableMap.Find(DataTable);

		if (ParameterDataTableMap)
		{
			TArray<UDataTable*>* DataTables = ParameterDataTableMap->Find(FName(*RowName));

			if (DataTables)
			{
				FString TableNames;

				for (int32 NameIndx = 0; NameIndx < DataTables->Num(); ++NameIndx)
				{
					TableNames += (*DataTables)[NameIndx]->GetName();

					if (NameIndx + 1 < DataTables->Num())
					{
						TableNames += ", ";
					}
				}

				FinalMessage += " Row from Composite Data Table, original Data Table/s: " + TableNames;
			}
		}
	}

	GenerationContext.Log(FText::FromString(FinalMessage), TableNode);
}


#undef LOCTEXT_NAMESPACE
