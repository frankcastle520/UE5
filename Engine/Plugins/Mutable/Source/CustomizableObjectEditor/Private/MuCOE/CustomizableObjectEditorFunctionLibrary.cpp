// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/CustomizableObjectEditorFunctionLibrary.h"

#include "AssetToolsModule.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "MuCOE/CustomizableObjectCompiler.h"
#include "MuCO/CustomizableObject.h"
#include "MuCO/ICustomizableObjectEditorModule.h"
#include "MuCOE/CustomizableObjectFactory.h"
#include "UObject/SavePackage.h"


ECustomizableObjectCompilationState UCustomizableObjectEditorFunctionLibrary::CompileCustomizableObjectSynchronously(
	UCustomizableObject* CustomizableObject,
	ECustomizableObjectOptimizationLevel InOptimizationLevel,
	ECustomizableObjectTextureCompression InTextureCompression,
	bool bGatherReferences)
{
	// store package dirty state so that we can restore it - compile is not an edit:
	const bool bPackageWasDirty = CustomizableObject->GetOutermost()->IsDirty();

	const double StartTime = FPlatformTime::Seconds();

	TSharedRef<FCompilationRequest> CompileRequest = MakeShared<FCompilationRequest>(*CustomizableObject, false);
	FCompilationOptions& Options = CompileRequest->GetCompileOptions();
	Options.OptimizationLevel = static_cast<int32>(InOptimizationLevel);
	Options.TextureCompression = InTextureCompression;
	Options.bSilentCompilation = false;
	Options.bGatherReferences = bGatherReferences;
	ICustomizableObjectEditorModule::GetChecked().CompileCustomizableObject(CompileRequest);

	check(CompileRequest->GetCompilationState() == ECompilationStatePrivate::Completed);

	CustomizableObject->GetOutermost()->SetDirtyFlag(bPackageWasDirty);

	const bool bCompilationSuccess = CompileRequest->GetCompilationResult() == ECompilationResultPrivate::Success ||
		CompileRequest->GetCompilationResult() == ECompilationResultPrivate::Warnings;
	
	const double CurrentTime = FPlatformTime::Seconds();
	UE_LOG( LogMutable, Display,
		TEXT("Synchronously Compiled %s %s in %f seconds"),
		*GetPathNameSafe(CustomizableObject), 
		bCompilationSuccess ? TEXT("successfully") : TEXT("unsuccessfully"),
		CurrentTime - StartTime
	);

	if (!CustomizableObject->IsCompiled())
	{
		UE_LOG(LogMutable, Warning, TEXT("CO not marked as compiled"));
	}

	return bCompilationSuccess ? ECustomizableObjectCompilationState::Completed : ECustomizableObjectCompilationState::Failed;
}


UCustomizableObject* UCustomizableObjectEditorFunctionLibrary::NewCustomizableObject(const FNewCustomizableObjectParameters& Parameters)
{
	const FString PackageName = Parameters.PackagePath + "/" + Parameters.AssetName;
	if (FindPackage(nullptr, *PackageName))
	{
		UE_LOG(LogMutable, Error, TEXT("Package [%s] already exists."), *PackageName);
		return nullptr;
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools");
	
	FString OutAssetName;
	FString OutPackageName;
	AssetToolsModule.Get().CreateUniqueAssetName(PackageName, TEXT(""), OutPackageName, OutAssetName);

	if (OutPackageName != PackageName)
	{
		UE_LOG(LogMutable, Error, TEXT("Invalid package name [%s]. Possible valid package name: [%s]"), *PackageName, *OutPackageName);
		return nullptr;
	}
	
	if (OutAssetName != Parameters.AssetName)
	{
		UE_LOG(LogMutable, Error, TEXT("Invalid asset name [%s]. Possible valid asset name: [%s]"), *Parameters.AssetName, *OutAssetName);
		return nullptr;
	}
		
	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		UE_LOG(LogMutable, Error, TEXT("Could not create package [%s]."), *PackageName);
		return nullptr;
	}
	
	UCustomizableObjectFactory* Factory = NewObject<UCustomizableObjectFactory>();

	const FString PackagePath = FPackageName::GetLongPackagePath(PackageName);

	UObject* Object = AssetToolsModule.Get().CreateAsset(Parameters.AssetName, PackagePath, UCustomizableObject::StaticClass(), Factory);
	if (!Object)
	{
		UE_LOG(LogMutable, Error, TEXT("Could not create Asset [%s]."), *Parameters.AssetName);
		return nullptr;
	}

	FSavePackageArgs SavePackageArgs;
	SavePackageArgs.TopLevelFlags = RF_Public | RF_Standalone;
	UPackage::Save(Package, Object, *FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension()), SavePackageArgs);

	FAssetRegistryModule::AssetCreated(Object);

	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");

	TArray<UObject*> Objects;
	Objects.Add(Object);
	ContentBrowserModule.Get().SyncBrowserToAssets(Objects);
	
	return CastChecked<UCustomizableObject>(Object);
}
