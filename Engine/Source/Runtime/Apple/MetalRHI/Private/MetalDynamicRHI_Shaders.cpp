// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	MetalDynamicRHI_Shaders.cpp: Metal Dynamic RHI Class Shader Methods.
=============================================================================*/

#include "MetalDynamicRHI.h"
#include "MetalRHIPrivate.h"
#include "MetalShaderTypes.h"
#include "Shaders/MetalShaderLibrary.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "Serialization/StaticMemoryReader.h"
#include "Interfaces/IPluginManager.h"

//------------------------------------------------------------------------------

#pragma mark - Metal Dynamic RHI Shader Methods


FVertexShaderRHIRef FMetalDynamicRHI::RHICreateVertexShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{
    MTL_SCOPED_AUTORELEASE_POOL;
    
	FMetalVertexShader* Shader = new FMetalVertexShader(*Device, Code);
	return Shader;
}

FPixelShaderRHIRef FMetalDynamicRHI::RHICreatePixelShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{
    MTL_SCOPED_AUTORELEASE_POOL;
    
	FMetalPixelShader* Shader = new FMetalPixelShader(*Device, Code);
	return Shader;
}

FGeometryShaderRHIRef FMetalDynamicRHI::RHICreateGeometryShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{
    MTL_SCOPED_AUTORELEASE_POOL;
#if PLATFORM_SUPPORTS_GEOMETRY_SHADERS
    FMetalGeometryShader* Shader = new FMetalGeometryShader(*Device, Code);
    return Shader;
#else
	return nullptr;
#endif
}

FComputeShaderRHIRef FMetalDynamicRHI::RHICreateComputeShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{
    MTL_SCOPED_AUTORELEASE_POOL;
    return new FMetalComputeShader(*Device, Code, MTLLibraryPtr());
}

#if PLATFORM_SUPPORTS_MESH_SHADERS
FMeshShaderRHIRef FMetalDynamicRHI::RHICreateMeshShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{
    MTL_SCOPED_AUTORELEASE_POOL;

    return new FMetalMeshShader(*Device, Code);
}

FAmplificationShaderRHIRef FMetalDynamicRHI::RHICreateAmplificationShader(TArrayView<const uint8> Code, const FSHAHash& Hash)
{
    MTL_SCOPED_AUTORELEASE_POOL;

    return new FMetalAmplificationShader(*Device, Code);
}
#endif

#if METAL_RHI_RAYTRACING
FRayTracingShaderRHIRef FMetalDynamicRHI::RHICreateRayTracingShader(TArrayView<const uint8> Code, const FSHAHash& Hash, EShaderFrequency ShaderFrequency)
{
	switch (ShaderFrequency)
    {
		default: checkNoEntry(); return nullptr;
	}
}
#endif // METAL_RHI_RAYTRACING

FRHIShaderLibraryRef FMetalDynamicRHI::RHICreateShaderLibrary(EShaderPlatform Platform, FString const& FilePath, FString const& Name)
{
	FString METAL_MAP_EXTENSION(TEXT(".metalmap"));

    MTL_SCOPED_AUTORELEASE_POOL;
    
    FRHIShaderLibraryRef Result = nullptr;

    const FName PlatformName = FDataDrivenShaderPlatformInfo::GetName(Platform);
    const FName ShaderFormatName = LegacyShaderPlatformToShaderFormat(Platform);
    FString ShaderFormatAndPlatform = ShaderFormatName.ToString() + TEXT("-") + PlatformName.ToString();

    FString LibName = FString::Printf(TEXT("%s_%s"), *Name, *ShaderFormatAndPlatform);
    LibName.ToLowerInline();

    FString BinaryShaderFile = FilePath / LibName + METAL_MAP_EXTENSION;

    if (IFileManager::Get().FileExists(*BinaryShaderFile) == false)
    {
        // the metal map files are stored in UFS file system
        // for pak files this means they might be stored in a different location as the pak files will mount them to the project content directory
        // the metal libraries are stores non UFS and could be anywhere on the file system.
        // if we don't find the metalmap file straight away try the pak file path
        BinaryShaderFile = FPaths::ProjectContentDir() / LibName + METAL_MAP_EXTENSION;

		if (IFileManager::Get().FileExists(*BinaryShaderFile) == false)
		{
			// See if its in a Plugin
			const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(*Name);
			if(Plugin !=  nullptr)
			{
				BinaryShaderFile = FPaths::Combine(Plugin->GetContentDir(), LibName + METAL_MAP_EXTENSION);
			}
			else
			{
				// GFP might not be loaded yet
				BinaryShaderFile = FPaths::ProjectPluginsDir() / TEXT("GameFeatures") / Name / TEXT("Content") / LibName + METAL_MAP_EXTENSION;
			}
		}
    }

    FScopeLock Lock(&FMetalShaderLibrary::LoadedShaderLibraryMutex);

    FRHIShaderLibrary** FoundShaderLibrary = FMetalShaderLibrary::LoadedShaderLibraryMap.Find(BinaryShaderFile);
    if (FoundShaderLibrary)
    {
        return *FoundShaderLibrary;
    }

    auto SerializeShaderCode = [](FMetalShaderLibrary::FShaderCodeArrayType& Array, FArchive& Ar)
    {
#if !USE_MMAPPED_SHADERARCHIVE
		Ar << Array;
#else
       if (Ar.GetArchiveName() == TEXT("FStaticMemoryReader"))
       {
			using ArrayType = std::remove_cvref_t <decltype(Array)>;
			FStaticMemoryReader* MemReader = static_cast<FStaticMemoryReader*>(&Ar);

			typename ArrayType::SizeType SerializeNum;
			Ar << SerializeNum;

			uint64 ArrayBytes = SerializeNum * sizeof(typename ArrayType::ElementType);
			uint64 Offset = Ar.Tell();

			Array = FMetalShaderLibrary::FShaderCodeArrayType(MemReader->GetData() + Offset, SerializeNum);
			Ar.Seek(Offset + ArrayBytes);
       }
	   else
	   {
		   UE_LOG(LogMetal, Fatal, TEXT("mmapped array must be serialized via FStaticMemoryReader"));
	   }
#endif
	};

 #if !USE_MMAPPED_SHADERARCHIVE
    FArchive* BinaryShaderAr = IFileManager::Get().CreateFileReader(*BinaryShaderFile);
#else
	FStaticMemoryReader* BinaryShaderAr = nullptr;
    TUniquePtr<FMetalShaderLibrary::FShaderLibDataOwner> MemOwner = MakeUnique<FMetalShaderLibrary::FShaderLibDataOwner>();
	if (FPlatformProperties::SupportsMemoryMappedFiles())
	{
		MemOwner->MappedCacheFile = TUniquePtr<IMappedFileHandle>(FPlatformFileManager::Get().GetPlatformFile().OpenMapped(*BinaryShaderFile));
		if (MemOwner->MappedCacheFile.IsValid())
		{
			MemOwner->MappedRegion = TUniquePtr<IMappedFileRegion>(MemOwner->MappedCacheFile->MapRegion(0, MemOwner->MappedCacheFile->GetFileSize()));
			if(MemOwner->MappedRegion.IsValid())
			{
				UE_LOG(LogMetal, Display, TEXT("mmapping %s, %d bytes"), *BinaryShaderFile, MemOwner->MappedCacheFile->GetFileSize());
				BinaryShaderAr = new FStaticMemoryReader(MemOwner->MappedRegion->GetMappedPtr(), MemOwner->MappedCacheFile->GetFileSize());
			}
		}
	}

	if(BinaryShaderAr == nullptr)
	{
		TArray<uint8>& FileData = MemOwner->Mem;
		if (FFileHelper::LoadFileToArray(FileData, *BinaryShaderFile))
		{
			UE_LOG(LogMetal, Display, TEXT("emulating mmapping %s, %d bytes!"), *BinaryShaderFile, FileData.Num());
			BinaryShaderAr = new FStaticMemoryReader(FileData.GetData(), FileData.Num());
		}
	}
#endif
    if( BinaryShaderAr != NULL )
    {
        FMetalShaderLibraryHeader Header;
        FSerializedShaderArchive SerializedShaders;
        FMetalShaderLibrary::FShaderCodeArrayType ShaderCode;

        *BinaryShaderAr << Header;
        *BinaryShaderAr << SerializedShaders;
		SerializeShaderCode(ShaderCode,*BinaryShaderAr);
        BinaryShaderAr->Flush();
        delete BinaryShaderAr;

        // Would be good to check the language version of the library with the archive format here.
        if (Header.Format == ShaderFormatName.GetPlainNameString())
        {
            check(((SerializedShaders.GetNumShaders() + Header.NumShadersPerLibrary - 1) / Header.NumShadersPerLibrary) == Header.NumLibraries);

            TArray<MTLLibraryPtr> Libraries;
            Libraries.Empty(Header.NumLibraries);

            for (uint32 i = 0; i < Header.NumLibraries; i++)
            {
                FString MetalLibraryFilePath = (FilePath / LibName) + FString::Printf(TEXT(".%d.metallib"), i);
                FString MetalLibraryAbsoluteFilePath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*MetalLibraryFilePath);

                METAL_GPUPROFILE(FScopedMetalCPUStats CPUStat(FString::Printf(TEXT("NewLibraryFile: %s"), *MetalLibraryAbsoluteFilePath)));
                NS::Error* Error;
                NS::String* MetalLibraryFilePathNSString = FStringToNSString(MetalLibraryAbsoluteFilePath);
                NS::URL *metalLibraryURL = NS::URL::fileURLWithPath(MetalLibraryFilePathNSString);
                MTLLibraryPtr Library = NS::TransferPtr(Device->GetDevice()->newLibrary(metalLibraryURL, &Error));

                if (Library.get() == nullptr)
                {
                    // Metallib not found. Is Zen server being used?
                    static const bool bRunningWithZenStore = FPlatformFileManager::Get().FindPlatformFile(TEXT("StorageServer")) != nullptr;
                    if( bRunningWithZenStore )
                    {
                        TArray<uint8> LibraryData;
                        if (FFileHelper::LoadFileToArray(LibraryData, *MetalLibraryFilePath))
                        {
                            dispatch_data_t data = dispatch_data_create(LibraryData.GetData(), LibraryData.Num(), nil, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
                            Library = NS::TransferPtr(Device->GetDevice()->newLibrary(data, &Error));
                        }
                        else
                        {
                            UE_LOG(LogMetal, Warning, TEXT("Metallib '%s' unable to be read from ZenStore."), *MetalLibraryFilePath);
                            return nullptr;
                        }
                    }
                    else
                    {
                        UE_LOG(LogMetal, Warning, TEXT("Metallib '%s' not found and ZenStore is not being used."), *MetalLibraryFilePath);
                        return nullptr;
                    }
                }

                if (Library.get() != nullptr)
                {
                    Libraries.Add(Library);
                }
                else
                {
                    UE_LOG(LogMetal, Display, TEXT("Failed to create library: %s"), *NSStringToFString(Error->description()));
                    return nullptr;
                }
            }

            FMetalShaderLibrary* MtlLib = new FMetalShaderLibrary(*Device, Platform, Name, BinaryShaderFile, Header, MoveTemp(SerializedShaders), MoveTemp(ShaderCode), Libraries
#if USE_MMAPPED_SHADERARCHIVE
				,MoveTemp(MemOwner)
#endif
			);
            Result = MtlLib;
            FMetalShaderLibrary::LoadedShaderLibraryMap.Add(BinaryShaderFile, Result.GetReference());
        }
		else
		{
		    UE_LOG(LogMetal, Display, TEXT("Unknown shader format for %s!"), *LibName);
		}
    }
    else
    {
        UE_LOG(LogMetal, Display, TEXT("No .metalmap file found for %s!"), *LibName);
    }

    return Result;
}

FBoundShaderStateRHIRef FMetalDynamicRHI::RHICreateBoundShaderState(
	FRHIVertexDeclaration* VertexDeclarationRHI,
	FRHIVertexShader* VertexShaderRHI,
	FRHIPixelShader* PixelShaderRHI,
	FRHIGeometryShader* GeometryShaderRHI)
{
	NOT_SUPPORTED("RHICreateBoundShaderState");
	return nullptr;
}

FRHIShaderLibraryRef FMetalDynamicRHI::RHICreateShaderLibrary_RenderThread(class FRHICommandListImmediate& RHICmdList, EShaderPlatform Platform, FString FilePath, FString Name)
{
	return RHICreateShaderLibrary(Platform, FilePath, Name);
}
