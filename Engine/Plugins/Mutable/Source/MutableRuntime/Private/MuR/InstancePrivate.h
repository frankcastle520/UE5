// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Instance.h"

#include "MuR/ImagePrivate.h"
#include "MuR/MeshPrivate.h"


namespace mu
{

	/** Helper functions to make and read FResourceIDs */
	inline FResourceID MakeResourceID(uint32 RootAddress, uint32 ParameterBlobIndex)
	{
		return (uint64(RootAddress) << 32) | uint64(ParameterBlobIndex);
	}

	/** */
    struct FInstanceSurface
	{
		FName Name;
        uint32 InternalId=0;
        uint32 ExternalId =0;
        uint32 SharedId =0;

		struct FInstanceImage
		{
			FResourceID Id;
			FName Name;
		};

		TArray<FInstanceImage, TInlineAllocator<4>> Images;

		struct FInstanceVector
		{
			FVector4f Value;
			FName Name;
		};

		TArray<FInstanceVector> Vectors;

        struct FInstanceScalar
        {
            float Value;
			FName Name;
		};

		TArray<FInstanceScalar> Scalars;

        struct FInstanceString
        {
			FString Value;
			FName Name;
		};

		TArray<FInstanceString> Strings;
    };


    struct FInstanceLOD
    {
		FResourceID MeshId;
		FName MeshName;

		// The order must match the meshes surfaces
		TArray<FInstanceSurface, TInlineAllocator<4>> Surfaces;
	};


    struct FInstanceComponent
    {
		uint16 Id;

		TArray<FInstanceLOD, TInlineAllocator<4>> LODs;
    };

	struct NamedExtensionData
	{
		Ptr<const ExtensionData> Data;
		FName Name;
	};

	class Instance::Private
	{
	public:

        //!
        Instance::ID Id = 0;

		//!
		TArray<FInstanceComponent,TInlineAllocator<4>> Components;

		// Every entry must have a valid ExtensionData and name
		TArray<NamedExtensionData> ExtensionData;

		int32 AddComponent();
		int32 AddLOD(int32 ComponentIndex);
		void SetMesh(int32 ComponentIndex, int32 LODIndex, FResourceID, FName Name);
		int32 AddSurface(int32 ComponentIndex, int32 LODIndex);
        void SetSurfaceName( int32 ComponentIndex, int32 LODIndex, int32 SurfaceIndex, FName Name);
		int32 AddImage( int32 ComponentIndex, int32 LODIndex, int32 SurfaceIndex, FResourceID, FName Name);
        int32 AddVector( int32 ComponentIndex, int32 LODIndex, int32 SurfaceIndex, const FVector4f&, FName Name);
        int32 AddScalar( int32 ComponentIndex, int32 LODIndex, int32 SurfaceIndex, float, FName Name);
        int32 AddString( int32 ComponentIndex, int32 LODIndex, int32 SurfaceIndex, const FString& Value, FName Name);
		
		// Data must be non-null
		void AddExtensionData(const Ptr<const class ExtensionData>& Data, FName Name);
    };
}
