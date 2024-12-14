// Copyright Epic Games, Inc. All Rights Reserved.

#include "JsonObjectGraphFunctionLibrary.h"

#include "HAL/FileManager.h"
#include "Engine/Blueprint.h"
#include "UnrealEngine.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"

namespace UE::Private 
{

static const TCHAR* SnapshotBlueprintsHelp = TEXT("Usage: snapshotblueprints label - label is a name for the folder where snapshots are saved");

static void SnapshotBlueprints(const TArray<FString>& Args)
{
	if (Args.Num() == 0)
	{
		UE_LOG(LogEngine, Display, TEXT("%s"), SnapshotBlueprintsHelp);
		return;
	}

	const FString& Label = Args[0];
	FString ScratchFilenameWritten;
	for (TObjectIterator<UBlueprint> BPIt; BPIt; ++BPIt)
	{
		const UBlueprint* BP = *BPIt;
		UJsonObjectGraphFunctionLibrary::WritePackageToTempFile(BP, Label, FJsonStringifyOptions(), ScratchFilenameWritten);
	}
}

FAutoConsoleCommand SnapshotBlueprintsCommand(
	TEXT("snapshotblueprints"),
	*FString::Format(TEXT("Write out a snapshot to the Saved directory of currently loaded blueprints.\n{0}"), { SnapshotBlueprintsHelp }),
	FConsoleCommandWithArgsDelegate::CreateStatic(&SnapshotBlueprints),
	ECVF_Default
);

static const TCHAR* SnapshotBlueprintClassesHelp = TEXT("Usage: snapshotblueprintclasses label - label is a name for the folder where snapshots are saved");

static void SnapshotBlueprintClasses(const TArray<FString>& Args)
{
	if (Args.Num() == 0)
	{
		UE_LOG(LogEngine, Display, TEXT("%s"), SnapshotBlueprintClassesHelp);
		return;
	}

	const FString& Label = Args[0];
	FString ScratchFilenameWritten;
	for (TObjectIterator<UBlueprint> BPIt; BPIt; ++BPIt)
	{
		const UBlueprint* BP = *BPIt;
		// skip editor only blueprint classes, they may have unstable
		// data because they are never cooked:
		if (!BP->GeneratedClass || IsEditorOnlyObject(BP->GeneratedClass))
		{
			continue;
		}

		UJsonObjectGraphFunctionLibrary::WriteBlueprintClassToTempFile(BP, Label, FJsonStringifyOptions(), ScratchFilenameWritten);
	}
}

FAutoConsoleCommand SnapshotBlueprintClassesCommand(
	TEXT("snapshotblueprintclasses"),
	*FString::Format(TEXT("Write out a snapshot to the Saved directory of currently loaded blueprint classes - the principle outputs of blueprint compilation.\n{0}"), { SnapshotBlueprintClassesHelp }),
	FConsoleCommandWithArgsDelegate::CreateStatic(&SnapshotBlueprintClasses),
	ECVF_Default
);

FString GetIntermediateAssetName(const UPackage* Package, const TCHAR* Prefix)
{
	return
		FPaths::ProjectSavedDir() +
		FString(TEXT("Temp/")) + Prefix +
		Package->GetPathName() +
		FString(TEXT("_snap.json"));
}
}

void UJsonObjectGraphFunctionLibrary::Stringify(const TArray<UObject*>& Objects, FJsonStringifyOptions Options, FString& ResultString)
{
	FUtf8String Result = UE::JsonObjectGraph::Stringify(Objects, Options);
	ResultString = FString(Result);
}

void UJsonObjectGraphFunctionLibrary::WritePackageToTempFile(const UObject* Object, const FString& Label, FJsonStringifyOptions Options, FString& OutFilename)
{
	OutFilename = FString();
	if (Object == nullptr)
	{
		return;
	}

	const UPackage* Package = Object->GetPackage();
	if (Package == GetTransientPackage())
	{
		UE_LOG(LogBlueprint, Warning, TEXT("Attempted to snapshot the transient package"));
		return;
	}

	FUtf8String Result = UE::JsonObjectGraph::Stringify({Package}, Options);
	FString Filename = UE::Private::GetIntermediateAssetName(Package, *Label);
	if (ensure(!Result.IsEmpty()))
	{
		TUniquePtr<FArchive> FileArchive(IFileManager::Get().CreateDebugFileWriter(*Filename));
		FileArchive->Serialize((void*)Result.GetCharArray().GetData(), Result.GetCharArray().Num() - 1);
		OutFilename = MoveTemp(Filename);
	}
}

void UJsonObjectGraphFunctionLibrary::WriteBlueprintClassToTempFile(const UBlueprint* BP, const FString& Label, FJsonStringifyOptions Options, FString& OutFilename)
{
	OutFilename = FString();
	if (BP == nullptr)
	{
		return;
	}

	UClass* BPGC = BP->GeneratedClass;
	if (BP->BlueprintType == BPTYPE_MacroLibrary ||
		!BPGC)
	{
		return;
	}

	if (!BPGC->ClassDefaultObject)
	{
		UE_LOG(LogBlueprint, Warning, TEXT("Attempted to serialize class with no CDO: %s"), *BPGC->GetPathName());
		return;
	}

	// When writing a class we should always exclude editor only data:
	Options.Flags |= EJsonStringifyFlags::FilterEditorOnlyData;
	FUtf8String Result = UE::JsonObjectGraph::Stringify({ BPGC, BPGC->ClassDefaultObject }, Options);
	FString Filename = UE::Private::GetIntermediateAssetName(BP->GetPackage(), *Label);
	TUniquePtr<FArchive> FileArchive(IFileManager::Get().CreateDebugFileWriter(*Filename));
	FileArchive->Serialize((void*)Result.GetCharArray().GetData(), Result.GetCharArray().Num() - 1);
	OutFilename = MoveTemp(Filename);
}
