// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangeMeshUtilities.h"

#include "AssetCompilingManager.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Async/Future.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAssetAsyncCompileUtils.h"
#include "Framework/Notifications/NotificationManager.h"
#include "InterchangeAssetImportData.h"
#include "InterchangeFactoryBase.h"
#include "InterchangeFilePickerBase.h"
#include "InterchangeEngineLogPrivate.h"
#include "InterchangeManager.h"
#include "InterchangeProjectSettings.h"
#include "InterchangePythonPipelineBase.h"
#include "InterchangeSourceData.h"
#include "Logging/LogMacros.h"
#include "MeshDescription.h"
#include "Misc/ScopedSlowTask.h"
#include "Rendering/SkeletalMeshLODImporterData.h"
#include "SkeletalMeshAttributes.h"
#include "StaticMeshAttributes.h"
#include "UObject/Class.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "Widgets/Notifications/SNotificationList.h"

#if WITH_EDITOR
#include "LODUtilities.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(InterchangeMeshUtilities)

namespace UE::Interchange::Private
{
	void DeletePathAssets(const FString& ImportAssetPath)
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

		TArray<FAssetData> AssetsToDelete;
		AssetRegistryModule.Get().GetAssetsByPath(FName(*ImportAssetPath), AssetsToDelete, true);
		for (FAssetData AssetData : AssetsToDelete)
		{
			UObject* ObjToDelete = AssetData.GetAsset();
			if (ObjToDelete)
			{
				//Avoid temporary package to be saved
				UPackage* Package = ObjToDelete->GetOutermost();
				Package->SetDirtyFlag(false);
				//Avoid gc, remove keep flags
				ObjToDelete->ClearFlags(RF_Standalone);
				ObjToDelete->ClearInternalFlags(EInternalObjectFlags::Async);
				//Make the object transient to prevent saving
				ObjToDelete->SetFlags(RF_Transient);
			}
		}
	}
}

FInterchangeSkeletalMeshAlternateSkinWeightPostImportTask::FInterchangeSkeletalMeshAlternateSkinWeightPostImportTask(USkeletalMesh* InSkeletalMesh)
	:SkeletalMesh(InSkeletalMesh)
{

}

void FInterchangeSkeletalMeshAlternateSkinWeightPostImportTask::Execute()
{
#if WITH_EDITOR
	//This code works only on the game thread and is not asynchronous
	if (!ensure(IsInGameThread()))
	{
		return;
	}

	//The delegate must be bound if we want to reimport the alternate skinning.
	if (!SkeletalMesh
		|| !ReimportAlternateSkinWeightDelegate.IsBound()
		|| ReImportAlternateSkinWeightsLods.IsEmpty())
	{
		return;
	}

	//User say yes so re-import the alternate skinning
	const int32 LodCount = SkeletalMesh->GetLODNum();
	float ProgressCount = LodCount + 0.1f;

	FScopedSlowTask Progress(ProgressCount, NSLOCTEXT("UInterchangeSkeletalMeshPostImportTask", "SkeletalMeshPostImportTaskGameThread", "Executing Skeletal Mesh Post Import Tasks..."));
	Progress.MakeDialog();
	{
		//Make sure we rebuild the skeletal mesh after re-importing all skin weight
		FScopedSkeletalMeshPostEditChange ScopePostEditChange(SkeletalMesh);

		//Wait until the asset is finish building then lock the skeletal mesh properties to prevent the UI to update during the alternate skinning reimport
		FEvent* LockEvent = SkeletalMesh->LockPropertiesUntil();
		FSkinnedAssetAsyncBuildScope AsyncBuildScope(SkeletalMesh);

		//We have a 0.1 progress for the lock
		Progress.EnterProgressFrame(0.1f);

		//Reimport all the alternate skinning
		for (int32 LodIndex = 0; LodIndex < LodCount; ++LodIndex)
		{
			if (ReImportAlternateSkinWeightsLods.Contains(LodIndex))
			{
				// This delegate should execute the following editor function
				// FSkinWeightsUtilities::ReimportAlternateSkinWeight(SkeletalMesh, LodIndex);
				ReimportAlternateSkinWeightDelegate.Execute(SkeletalMesh, LodIndex);
			}
			Progress.EnterProgressFrame(1.0f);
		}

		//Release the skeletal mesh async properties
		LockEvent->Trigger();

		//Skeletal mesh will rebuild when going out of scope
	}
#endif //WITH_EDITOR
}

//DECLARE_DELEGATE_RetVal_TwoParams(bool, FInterchangeReimportAlternateSkinWeight, USkeletalMesh*, int32 LodIndex);

bool FInterchangeSkeletalMeshAlternateSkinWeightPostImportTask::AddLodToReimportAlternate(int32 LodToAdd)
{
	if (!SkeletalMesh || !SkeletalMesh->IsValidLODIndex(LodToAdd))
	{
		return false;
	}
	ReImportAlternateSkinWeightsLods.AddUnique(LodToAdd);
	return true;
}

bool UInterchangeMeshUtilities::ShowMeshFilePicker(FString& OutFilename, const FText& Title)
{
	//Pop a file picker that join both interchange and other format
	//Ask the user for a file path
	const UInterchangeProjectSettings* InterchangeProjectSettings = GetDefault<UInterchangeProjectSettings>();
	UInterchangeFilePickerBase* FilePicker = nullptr;

	//In runtime we do not have any pipeline configurator
#if WITH_EDITORONLY_DATA
	TSoftClassPtr <UInterchangeFilePickerBase> FilePickerClass = InterchangeProjectSettings->FilePickerClass;
	if (FilePickerClass.IsValid())
	{
		UClass* FilePickerClassLoaded = FilePickerClass.LoadSynchronous();
		if (FilePickerClassLoaded)
		{
			FilePicker = NewObject<UInterchangeFilePickerBase>(GetTransientPackage(), FilePickerClassLoaded, NAME_None, RF_NoFlags);
		}
	}
#endif
	if (FilePicker)
	{
		FInterchangeFilePickerParameters Parameters;
		Parameters.bAllowMultipleFiles = false;
		Parameters.Title = Title;
		Parameters.bShowAllFactoriesExtension = false;
		TArray<FString> Filenames;
		bool bFilePickerResult = FilePicker->ScriptedFilePickerForTranslatorAssetType(EInterchangeTranslatorAssetType::Meshes, Parameters, Filenames);
		if (bFilePickerResult && Filenames.Num() > 0)
		{
			OutFilename = Filenames[0];
			return FPaths::FileExists(OutFilename);
		}
	}
	return false;
}

TFuture<bool> UInterchangeMeshUtilities::ImportCustomLod(UObject* MeshObject, const int32 LodIndex, const UInterchangeSourceData* SourceData, bool bAsync)
{
	TSharedPtr<TPromise<bool>> Promise = MakeShared<TPromise<bool>>();
	
	return InternalImportCustomLod(Promise, MeshObject, LodIndex, SourceData, bAsync);
}

TFuture<bool> UInterchangeMeshUtilities::InternalImportCustomLod(TSharedPtr<TPromise<bool>> Promise, UObject* MeshObject, const int32 LodIndex, const UInterchangeSourceData* SourceData, bool bAsync)
{
#if WITH_EDITOR
	UInterchangeManager& InterchangeManager = UInterchangeManager::GetInterchangeManager();

	UInterchangeAssetImportData* InterchangeAssetImportData = nullptr;
	USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(MeshObject);
	UStaticMesh* StaticMesh = Cast<UStaticMesh>(MeshObject);
	EInterchangePipelineContext ImportType = EInterchangePipelineContext::AssetCustomLODImport;
	bool bInvalidLodIndex = false;
	UObject* SourceImportData = nullptr;
	UClass* ObjectType = nullptr;
	if (SkeletalMesh)
	{
		SourceImportData = SkeletalMesh->GetAssetImportData();
		InterchangeAssetImportData = Cast<UInterchangeAssetImportData>(SourceImportData);
		if (SkeletalMesh->GetLODNum() > LodIndex)
		{
			ImportType = EInterchangePipelineContext::AssetCustomLODReimport;
		}
		if (LodIndex > SkeletalMesh->GetLODNum())
		{
			bInvalidLodIndex = true;
		}
		ObjectType = USkeletalMesh::StaticClass();
	}
	else if (StaticMesh)
	{
		SourceImportData = StaticMesh->GetAssetImportData();
		InterchangeAssetImportData = Cast<UInterchangeAssetImportData>(SourceImportData);
		if (StaticMesh->GetNumSourceModels() > LodIndex)
		{
			ImportType = EInterchangePipelineContext::AssetCustomLODReimport;
		}
		if (LodIndex > StaticMesh->GetNumSourceModels())
		{
			bInvalidLodIndex = true;
		}
		ObjectType = UStaticMesh::StaticClass();
	}
	else
	{
		//We support Import custom LOD only for skeletalmesh and staticmesh
		Promise->SetValue(false);
		return Promise->GetFuture();
	}

	if (bInvalidLodIndex)
	{
		UE_LOG(LogInterchangeEngine, Warning, TEXT("FInterchangeMeshUtilities::InternalImportCustomLod: Invalid mesh LOD index %d, no prior LOD index exists."), LodIndex);
		Promise->SetValue(false);
		return Promise->GetFuture();
	}

	const bool bInterchangeCanImportSourceData = InterchangeManager.CanTranslateSourceData(SourceData);

	if (!bInterchangeCanImportSourceData)
	{
		UE_LOG(LogInterchangeEngine, Warning, TEXT("FInterchangeMeshUtilities::InternalImportCustomLod: Cannot import mesh LOD index %d, no interchange translator support this source file. [%s]"), LodIndex, *(SourceData->GetFilename()));
		Promise->SetValue(false);
		return Promise->GetFuture();
	}

	//Convert the asset import data if needed
	if (!InterchangeAssetImportData)
	{
		//Try to convert the asset import data
		InterchangeManager.ConvertImportData(SourceImportData, UInterchangeAssetImportData::StaticClass(), reinterpret_cast<UObject**>(&InterchangeAssetImportData));
	}

	FImportAssetParameters ImportAssetParameters;
	ImportAssetParameters.bIsAutomated = true;
	FInterchangePipelineContextParams ContextParams;
	ContextParams.ContextType = ImportType;
	ContextParams.ImportObjectType = ObjectType;
	if (InterchangeAssetImportData)
	{
		TArray<UObject*> Pipelines = InterchangeAssetImportData->GetPipelines();
		for (UObject* SelectedPipeline : Pipelines)
		{
			UInterchangePipelineBase* GeneratedPipeline = nullptr;
			if (UInterchangePythonPipelineAsset* PythonPipelineAsset = Cast<UInterchangePythonPipelineAsset>(SelectedPipeline))
			{
				GeneratedPipeline = Cast<UInterchangePipelineBase>(StaticDuplicateObject(PythonPipelineAsset->GeneratedPipeline, GetTransientPackage()));
			}
			else
			{
				GeneratedPipeline = Cast<UInterchangePipelineBase>(StaticDuplicateObject(SelectedPipeline, GetTransientPackage()));
			}
			if (ensure(GeneratedPipeline))
			{
				GeneratedPipeline->AdjustSettingsForContext(ContextParams);
				ImportAssetParameters.OverridePipelines.Add(GeneratedPipeline);
			}
		}
	}
	else
	{
		//Create import data
		InterchangeAssetImportData = NewObject<UInterchangeAssetImportData>();
		const UInterchangeProjectSettings* InterchangeProjectSettings = GetDefault<UInterchangeProjectSettings>();

		if (const UClass* GenericPipelineClass = InterchangeProjectSettings->GenericPipelineClass.LoadSynchronous())
		{
			if (UInterchangePipelineBase* GenericPipeline = NewObject<UInterchangePipelineBase>(GetTransientPackage(), GenericPipelineClass))
			{
				GenericPipeline->ClearFlags(EObjectFlags::RF_Standalone | EObjectFlags::RF_Public);
				GenericPipeline->AdjustSettingsForContext(ContextParams);
				ImportAssetParameters.OverridePipelines.Add(GenericPipeline);
			}
		}
	}

 	FString ImportAssetPath = TEXT("/Engine/TempEditor/Interchange/") + FGuid::NewGuid().ToString(EGuidFormats::Base36Encoded);

	UE::Interchange::FAssetImportResultRef AssetImportResult = bAsync 
		? InterchangeManager.ImportAssetAsync(ImportAssetPath, SourceData, ImportAssetParameters)
		: InterchangeManager.ImportAssetWithResult(ImportAssetPath, SourceData, ImportAssetParameters);

	FString SourceDataFilename = SourceData->GetFilename();
	if (SkeletalMesh)
	{
		AssetImportResult->OnDone([Promise, SkeletalMesh, LodIndex, SourceDataFilename, ImportAssetPath](UE::Interchange::FImportResult& ImportResult)
			{
				USkeletalMesh* SourceSkeletalMesh = Cast< USkeletalMesh >(ImportResult.GetFirstAssetOfClass(USkeletalMesh::StaticClass()));

				if(SourceSkeletalMesh)
				{
					//Make sure we can modify the skeletalmesh properties
					FSkinnedAssetAsyncBuildScope AsyncBuildScope(SkeletalMesh);
					Promise->SetValue(FLODUtilities::SetCustomLOD(SkeletalMesh, SourceSkeletalMesh, LodIndex, SourceDataFilename));
				}
				else
				{
					Promise->SetValue(false);
				}
				UE::Interchange::Private::DeletePathAssets(ImportAssetPath);
			});
	}
	else if (StaticMesh)
	{
		AssetImportResult->OnDone([Promise, StaticMesh, LodIndex, SourceDataFilename, ImportAssetPath](UE::Interchange::FImportResult& ImportResult)
			{
				UStaticMesh* SourceStaticMesh = Cast< UStaticMesh >(ImportResult.GetFirstAssetOfClass(UStaticMesh::StaticClass()));
				if(SourceStaticMesh)
				{
					Promise->SetValue(StaticMesh->SetCustomLOD(SourceStaticMesh, LodIndex, SourceDataFilename));
				}
				else
				{
					Promise->SetValue(false);
				}
				UE::Interchange::Private::DeletePathAssets(ImportAssetPath);
			});
	}

	return Promise->GetFuture();
#else
	Promise->SetValue(false);
	return Promise->GetFuture();
#endif
}

TFuture<bool> UInterchangeMeshUtilities::ImportMorphTarget(USkeletalMesh* SkeletalMesh, const int32 LodIndex, const UInterchangeSourceData* SourceData, bool bAsync, const FString& MorphTargetName)
{
	TSharedPtr<TPromise<bool>> Promise = MakeShared<TPromise<bool>>();

#if WITH_EDITOR
	auto ExitNoImport = [&Promise]()
		{
			Promise->SetValue(false);
			FNotificationInfo NotificationInfo(FText::GetEmpty());
			NotificationInfo.ExpireDuration = 5.0f;
			NotificationInfo.bUseSuccessFailIcons = true;
			NotificationInfo.Text = NSLOCTEXT("InterchangeMeshUtilities", "ImportMorphTargetFail", "Fail importing morph target!");
			if (TSharedPtr<SNotificationItem> NotificationPtr = FSlateNotificationManager::Get().AddNotification(NotificationInfo))
			{
				NotificationPtr->SetCompletionState(SNotificationItem::CS_Fail);
			}
			return Promise->GetFuture();
		};

	if (!SkeletalMesh)
	{
		UE_LOG(LogInterchangeEngine, Warning, TEXT("FInterchangeMeshUtilities::ImportMorphTarget: Cannot import morph targets, invalid skeletal mesh."));
		return ExitNoImport();
	}

	UInterchangeManager& InterchangeManager = UInterchangeManager::GetInterchangeManager();

	UInterchangeAssetImportData* InterchangeAssetImportData = nullptr;
	EInterchangePipelineContext ImportType = MorphTargetName.IsEmpty() ? EInterchangePipelineContext::AssetCustomMorphTargetImport : EInterchangePipelineContext::AssetCustomMorphTargetReImport;
	UObject* SourceImportData = nullptr;
	SourceImportData = SkeletalMesh->GetAssetImportData();
	InterchangeAssetImportData = Cast<UInterchangeAssetImportData>(SourceImportData);
	if (LodIndex > SkeletalMesh->GetLODNum())
	{
		UE_LOG(LogInterchangeEngine, Warning, TEXT("FInterchangeMeshUtilities::ImportMorphTarget: Cannot import morph targets, invalid skeletal mesh LOD index %d."), LodIndex);
		return ExitNoImport();
	}

	const bool bInterchangeCanImportSourceData = InterchangeManager.CanTranslateSourceData(SourceData);

	if (!bInterchangeCanImportSourceData)
	{
		UE_LOG(LogInterchangeEngine, Warning, TEXT("FInterchangeMeshUtilities::ImportMorphTarget: Cannot import skeletal mesh morph targets, no interchange translator support this source file. [%s]"), *(SourceData->GetFilename()));
		return ExitNoImport();
	}

	//Convert the asset import data if needed
	if (!InterchangeAssetImportData)
	{
		//Try to convert the asset import data
		InterchangeManager.ConvertImportData(SourceImportData, UInterchangeAssetImportData::StaticClass(), reinterpret_cast<UObject**>(&InterchangeAssetImportData));
	}

	FImportAssetParameters ImportAssetParameters;
	ImportAssetParameters.bIsAutomated = true;
	FInterchangePipelineContextParams ContextParams;
	ContextParams.ContextType = ImportType;
	ContextParams.ImportObjectType = USkeletalMesh::StaticClass();
	if (InterchangeAssetImportData)
	{
		TArray<UObject*> Pipelines = InterchangeAssetImportData->GetPipelines();
		for (UObject* SelectedPipeline : Pipelines)
		{
			UInterchangePipelineBase* GeneratedPipeline = nullptr;
			if (UInterchangePythonPipelineAsset* PythonPipelineAsset = Cast<UInterchangePythonPipelineAsset>(SelectedPipeline))
			{
				GeneratedPipeline = Cast<UInterchangePipelineBase>(StaticDuplicateObject(PythonPipelineAsset->GeneratedPipeline, GetTransientPackage()));
			}
			else
			{
				GeneratedPipeline = Cast<UInterchangePipelineBase>(StaticDuplicateObject(SelectedPipeline, GetTransientPackage()));
			}
			if (ensure(GeneratedPipeline))
			{
				GeneratedPipeline->AdjustSettingsForContext(ContextParams);
				ImportAssetParameters.OverridePipelines.Add(GeneratedPipeline);
			}
		}
	}
	else
	{
		//Create import data
		InterchangeAssetImportData = NewObject<UInterchangeAssetImportData>();
		const UInterchangeProjectSettings* InterchangeProjectSettings = GetDefault<UInterchangeProjectSettings>();

		if (const UClass* GenericPipelineClass = InterchangeProjectSettings->GenericPipelineClass.LoadSynchronous())
		{
			if (UInterchangePipelineBase* GenericPipeline = NewObject<UInterchangePipelineBase>(GetTransientPackage(), GenericPipelineClass))
			{
				GenericPipeline->ClearFlags(EObjectFlags::RF_Standalone | EObjectFlags::RF_Public);
				GenericPipeline->AdjustSettingsForContext(ContextParams);
				ImportAssetParameters.OverridePipelines.Add(GenericPipeline);
			}
		}
	}

	FString ImportAssetPath = TEXT("/Engine/TempEditor/Interchange/") + FGuid::NewGuid().ToString(EGuidFormats::Base36Encoded);

	UE::Interchange::FAssetImportResultRef AssetImportResult = bAsync
		? InterchangeManager.ImportAssetAsync(ImportAssetPath, SourceData, ImportAssetParameters)
		: InterchangeManager.ImportAssetWithResult(ImportAssetPath, SourceData, ImportAssetParameters);

	FString SourceDataFilename = SourceData->GetFilename();
	AssetImportResult->OnDone([Promise, SkeletalMesh, LodIndex, SourceDataFilename, ImportAssetPath, bAsync, MorphTargetName](UE::Interchange::FImportResult& ImportResult)
		{
			UStaticMesh* SourceStaticMesh = Cast< UStaticMesh >(ImportResult.GetFirstAssetOfClass(UStaticMesh::StaticClass()));

			auto LambdaCleanup = [&Promise, &ImportAssetPath](bool bResult)
				{
					UE::Interchange::Private::DeletePathAssets(ImportAssetPath);
					Promise->SetValue(bResult);
					FNotificationInfo NotificationInfo(FText::GetEmpty());
					NotificationInfo.ExpireDuration = 5.0f;
					NotificationInfo.bUseSuccessFailIcons = true;
					NotificationInfo.Text = bResult
						? NSLOCTEXT("InterchangeMeshUtilities", "ImportMorphTargetSuccessful", "Morph target imported successfully!")
						: NSLOCTEXT("InterchangeMeshUtilities", "ImportMorphTargetFail", "Fail importing morph target!");
					TSharedPtr<SNotificationItem> NotificationPtr = FSlateNotificationManager::Get().AddNotification(NotificationInfo);
					if (NotificationPtr)
					{
						NotificationPtr->SetCompletionState(bResult ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
					}
				};

			if (SourceStaticMesh)
			{
				const int32 SourceLodCount = SourceStaticMesh->GetNumSourceModels();
				if (SourceLodCount == 0)
				{
					UE_LOG(LogInterchangeEngine, Warning, TEXT("FInterchangeMeshUtilities::ImportMorphTarget: Cannot import skeletal mesh morph targets, There was no geometry in the provided source file. [%s]"), *(SourceDataFilename));
					LambdaCleanup(false);
					return;
				}
				const int32 SourceLodIndex = SourceStaticMesh->IsMeshDescriptionValid(LodIndex) ? LodIndex : 0;
				FMeshDescription* SourceMeshDescription = SourceStaticMesh->GetMeshDescription(SourceLodIndex);
				if (!SourceMeshDescription)
				{
					UE_LOG(LogInterchangeEngine, Warning, TEXT("FInterchangeMeshUtilities::ImportMorphTarget: Cannot import skeletal mesh morph targets, There was no geometry for LOD index %d in the provided source file. [%s]"), LodIndex, *(SourceDataFilename));
					LambdaCleanup(false);
					return;
				}
				FMeshDescription* TargetMeshDescription = SkeletalMesh->GetMeshDescription(LodIndex);
				if (!TargetMeshDescription)
				{
					UE_LOG(LogInterchangeEngine, Warning, TEXT("FInterchangeMeshUtilities::ImportMorphTarget: Cannot import morph targets, missing skeletal mesh geometry for LOD index %d."), LodIndex);
					LambdaCleanup(false);
					return;
				}
				if (SourceMeshDescription->Vertices().Num() != TargetMeshDescription->Vertices().Num())
				{
					UE_LOG(LogInterchangeEngine, Warning, TEXT("FInterchangeMeshUtilities::ImportMorphTarget: Cannot import morph targets, the imported morph target geometry don't match target skeletal mesh geometry topology. (LOD index %d)"), LodIndex);
					LambdaCleanup(false);
					return;
				}

#if WITH_EDITORONLY_DATA
				FSkeletalMeshLODInfo* LodInfo = SkeletalMesh->GetLODInfo(LodIndex);
				if (!ensure(LodInfo))
				{
					UE_LOG(LogInterchangeEngine, Warning, TEXT("FInterchangeMeshUtilities::ImportMorphTarget: Cannot import morph targets, the skeletal mesh LOD info do not exist. (LOD index %d)"), LodIndex);
					LambdaCleanup(false);
					return;
				}
#endif //WITH_EDITORONLY_DATA

				//Use the provided MorphTargetName if not empty. Existing name will force a re-import
				const FString ImportedMorphTargetString = MorphTargetName.IsEmpty() ? SourceStaticMesh->GetName() : MorphTargetName;
				const FName ImportedMorphTargetName = FName(*ImportedMorphTargetString);

				FSkeletalMeshAttributes TargetAttributes(*TargetMeshDescription);
				if (!TargetAttributes.GetMorphTargetNames().Contains(ImportedMorphTargetName))
				{
					TargetAttributes.RegisterMorphTargetAttribute(ImportedMorphTargetName, false);
				}
				TVertexAttributesRef<FVector3f> TargetMorphTargetPosDeltaAttribute = TargetAttributes.GetVertexMorphPositionDelta(ImportedMorphTargetName);
				TVertexAttributesConstRef<FVector3f> TargetVertexPositions = TargetAttributes.GetVertexPositions();

				FStaticMeshConstAttributes SourceAttributes(*SourceMeshDescription);
				TVertexAttributesConstRef<FVector3f> SourceVertexPositions = SourceAttributes.GetVertexPositions();

				//Populate the deltas in the target mesh description
				for (FVertexID VertexID : SourceMeshDescription->Vertices().GetElementIDs())
				{
					TargetMorphTargetPosDeltaAttribute[VertexID] = SourceVertexPositions[VertexID] - TargetVertexPositions[VertexID];
				}

				//Commit the modified LodImportData
				{
					FScopedSkeletalMeshPostEditChange ScopePostEditChange(SkeletalMesh);
					SkeletalMesh->PreEditChange(nullptr);

#if WITH_EDITORONLY_DATA
					//Add the mapping to the lod info so we can re-import the morph target
					LodInfo->ImportedMorphTargetSourceFilename.FindOrAdd(ImportedMorphTargetString).SetSourceFilename(SourceDataFilename);
#endif //WITH_EDITORONLY_DATA

					SkeletalMesh->CommitMeshDescription(LodIndex);
				}
				
				//Wait until the skeletal mesh compilation is done
				if (!bAsync)
				{
					FAssetCompilingManager::Get().FinishCompilationForObjects({ SkeletalMesh });
				}

				//Set the promise to success
				LambdaCleanup(true);
			}
			else
			{

				LambdaCleanup(false);
			}
		});

	return Promise->GetFuture();
#else
	Promise->SetValue(false);
	return Promise->GetFuture();
#endif
}