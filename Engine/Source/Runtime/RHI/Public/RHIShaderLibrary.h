// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RHIResources.h"
#include "Misc/CoreDelegates.h"

//
// Shader Library
//

class FRHIShaderLibrary : public FRHIResource
{
public:
	FRHIShaderLibrary(EShaderPlatform InPlatform, FString const& InName) : FRHIResource(RRT_ShaderLibrary), Platform(InPlatform), LibraryName(InName), LibraryId(GetTypeHash(InName)) {}
	virtual ~FRHIShaderLibrary() {}

	FORCEINLINE EShaderPlatform GetPlatform(void) const { return Platform; }
	FORCEINLINE const FString& GetName(void) const { return LibraryName; }
	FORCEINLINE uint32 GetId(void) const { return LibraryId; }

	virtual bool IsNativeLibrary() const = 0;
	virtual int32 GetNumShaderMaps() const = 0;
	virtual int32 GetNumShaders() const = 0;
	virtual int32 GetNumShadersForShaderMap(int32 ShaderMapIndex) const = 0;
	virtual int32 GetShaderIndex(int32 ShaderMapIndex, int32 i) const = 0;
	virtual void GetAllShaderIndices(int32 ShaderMapIndex, TArray<int32>& ShaderIndices) {}
	virtual uint32 GetSizeBytes() const = 0;
	virtual FSHAHash GetShaderHash(int32 ShaderMapIndex, int32 ShaderIndex) = 0;
	virtual int32 FindShaderMapIndex(const FSHAHash& Hash) = 0;
	virtual int32 FindShaderIndex(const FSHAHash& Hash) = 0;
	virtual bool IsPreloading(int32 ShaderIndex, FGraphEventArray& OutCompletionEvents) { return false; }
	virtual bool PreloadShader(int32 ShaderIndex, FGraphEventArray& OutCompletionEvents) { return false; }
	virtual bool PreloadShaderMap(int32 ShaderMapIndex, FGraphEventArray& OutCompletionEvents) { return false; }
	virtual bool PreloadShaderMap(int32 ShaderMapIndex, FCoreDelegates::FAttachShaderReadRequestFunc AttachShaderReadRequestFunc) { return false; }
	virtual void ReleasePreloadedShader(int32 ShaderIndex) {}

	virtual void AddRefPreloadedShaderGroup(int32 ShaderGroupIndex) {}
	virtual void ReleasePreloadedShaderGroup(int32 ShaderGroupIndex) {}
	virtual int32 GetGroupIndexForShader(int32 ShaderIndex) const { return INDEX_NONE; }
	virtual int32 GetLibraryId() { return LibraryId; }
	/*CreateShader can return a null shader when bRequired == false. Usefull to debug dynamic shader preloading or when shaders haven't finished loading.*/
	virtual TRefCountPtr<FRHIShader> CreateShader(int32 ShaderIndex, bool bRequired = true) { return nullptr; }
	virtual void Teardown() {};

protected:
	EShaderPlatform Platform;
	FString LibraryName;
	uint32 LibraryId;
};

class FRHIPipelineBinaryLibrary : public FRHIResource
{
public:
	FRHIPipelineBinaryLibrary(EShaderPlatform InPlatform, FString const& FilePath) : FRHIResource(RRT_PipelineBinaryLibrary), Platform(InPlatform) {}
	virtual ~FRHIPipelineBinaryLibrary() {}

	FORCEINLINE EShaderPlatform GetPlatform(void) const { return Platform; }

protected:
	EShaderPlatform Platform;
};
