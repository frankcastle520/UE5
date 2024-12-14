// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MetalShaderLibrary.h: Metal RHI Shader Library Class.
=============================================================================*/

#pragma once

#include "MetalRHIPrivate.h"
#include "MetalShaderResources.h"
#include "RHIShaderLibrary.h"
#include "ShaderCodeArchive.h"
#include "Async/MappedFileHandle.h"

class FMetalDevice;

class FMetalShaderLibrary final : public FRHIShaderLibrary
{
public:

#if !USE_MMAPPED_SHADERARCHIVE
    using FShaderCodeArrayType = TArray<uint8>;
#else
    struct FShaderLibDataOwner
    {
        TArray<uint8> Mem;
        TUniquePtr<IMappedFileHandle> MappedCacheFile;
        TUniquePtr<IMappedFileRegion> MappedRegion;
    };
    
	using FShaderCodeArrayType = TArrayView<const uint8>;
private:
    TUniquePtr<FShaderLibDataOwner> MemOwner;
public:
    
    FMetalShaderLibrary(FMetalDevice& Device,
		EShaderPlatform Platform,
        FString const& Name,
        const FString& InShaderLibraryFilename,
        const FMetalShaderLibraryHeader& InHeader,
        FSerializedShaderArchive&& InSerializedShaders,
        FShaderCodeArrayType&& InShaderCode,
        const TArray<MTLLibraryPtr>& InLibrary,
        TUniquePtr<FShaderLibDataOwner>&& InMemOwner)
        :
        FMetalShaderLibrary(Device, Platform, Name, InShaderLibraryFilename, InHeader, MoveTemp(InSerializedShaders), MoveTemp(InShaderCode), InLibrary)
        {
            MemOwner = MoveTemp(InMemOwner);
        }
#endif
  
	static FCriticalSection LoadedShaderLibraryMutex;
	static TMap<FString, FRHIShaderLibrary*> LoadedShaderLibraryMap;

	FMetalShaderLibrary(FMetalDevice& Device,
						EShaderPlatform Platform,
						FString const& Name,
						const FString& InShaderLibraryFilename,
						const FMetalShaderLibraryHeader& InHeader,
						FSerializedShaderArchive&& InSerializedShaders,
						FShaderCodeArrayType&& InShaderCode,
						const TArray<MTLLibraryPtr>& InLibrary);

	virtual ~FMetalShaderLibrary();

	virtual bool IsNativeLibrary() const override final;

	virtual int32 GetNumShaders() const override;
	virtual int32 GetNumShaderMaps() const override;
	virtual uint32 GetSizeBytes() const override;
	virtual int32 GetNumShadersForShaderMap(int32 ShaderMapIndex) const override;
	virtual int32 GetShaderIndex(int32 ShaderMapIndex, int32 i) const override;

	virtual int32 FindShaderMapIndex(const FSHAHash& Hash) override;
	virtual int32 FindShaderIndex(const FSHAHash& Hash) override;
	virtual FSHAHash GetShaderHash(int32 ShaderMapIndex, int32 ShaderIndex) override
	{ 
		return SerializedShaders.GetShaderHashes()[GetShaderIndex(ShaderMapIndex, ShaderIndex)];
	};

	virtual bool PreloadShader(int32 ShaderIndex, FGraphEventArray& OutCompletionEvents) override { return false; }
	virtual bool PreloadShaderMap(int32 ShaderMapIndex, FGraphEventArray& OutCompletionEvents) override { return false; }

	virtual TRefCountPtr<FRHIShader> CreateShader(int32 Index, bool bRequired = true) override;

private:
	FMetalDevice& Device;
	FString ShaderLibraryFilename;
	TArray<MTLLibraryPtr> Library;
	FMetalShaderLibraryHeader Header;
	FSerializedShaderArchive SerializedShaders;
	FShaderCodeArrayType ShaderCode;
#if !UE_BUILD_SHIPPING
	class FMetalShaderDebugZipFile* DebugFile;
#endif
};
