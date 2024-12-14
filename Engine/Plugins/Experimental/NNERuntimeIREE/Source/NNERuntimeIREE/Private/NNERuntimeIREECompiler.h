// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NNERuntimeIREEMetaData.h"
#include "Serialization/JsonSerializerMacros.h"

#include "NNERuntimeIREECompiler.generated.h"

USTRUCT()
struct FNNERuntimeIREEArchitectureInfoCPU
{
	GENERATED_BODY()

	UPROPERTY()
	FString Architecture;

	UPROPERTY()
	FString RelativeDirPath;
	
	UPROPERTY()
	FString SharedLibraryFileName;

	UPROPERTY()
	FString VmfbFileName;

	UPROPERTY()
	FString SharedLibraryEntryPointName;
};

USTRUCT()
struct FNNERuntimeIREECompilerResultCPU
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FNNERuntimeIREEArchitectureInfoCPU> ArchitectureInfos;
};

#ifdef WITH_NNE_RUNTIME_IREE
#if WITH_EDITOR

namespace UE::NNERuntimeIREE::CPU
{
	struct FBuildTarget : public FJsonSerializable
	{
		FString Architecture;
		FString CompilerArguments;
		FString LinkerArguments;

		BEGIN_JSON_SERIALIZER
			JSON_SERIALIZE("Architecture", Architecture);
			JSON_SERIALIZE("CompilerArguments", CompilerArguments);
			JSON_SERIALIZE("LinkerArguments", LinkerArguments);
		END_JSON_SERIALIZER
	};

	struct FBuildConfig : public FJsonSerializable
	{
		TArray<FString> CompilerCommand;
		TArray<FString> LinkerCommand;
		FString SharedLibExt;
		TArray<FBuildTarget> BuildTargets;

		BEGIN_JSON_SERIALIZER
			JSON_SERIALIZE_ARRAY("CompilerCommand", CompilerCommand);
			JSON_SERIALIZE_ARRAY("LinkerCommand", LinkerCommand);
			JSON_SERIALIZE("SharedLibExt", SharedLibExt);
			JSON_SERIALIZE_ARRAY_SERIALIZABLE("Targets", BuildTargets, FBuildTarget);
		END_JSON_SERIALIZER
	};

	class FCompiler
	{
	private:
		FCompiler(const FString& InCompilerCommand, const FString& InLinkerCommand, const FString& InSharedLibExt, TConstArrayView<FBuildTarget> InBuildTargets);

	public:
		~FCompiler() {};
		static TUniquePtr<FCompiler> Make(const FString& InTargetPlatformName);

	public:
		bool CompileMlir(TConstArrayView<uint8> InFileData, const FString& InModelName, const FString& InOutputDir, FNNERuntimeIREECompilerResultCPU& OutCompilerResult, UNNERuntimeIREEModuleMetaData& ModuleMetaData);

	private:
		FString CompilerCommand;
		FString LinkerCommand;
		FString SharedLibExt;
		TArray<FBuildTarget> BuildTargets;
	};
} // UE::NNERuntimeIREE::CPU

#endif // WITH_EDITOR
#endif // WITH_NNE_RUNTIME_IREE