// Copyright Epic Games, Inc. All Rights Reserved.


#include "MuR/Instance.h"

#include "HAL/LowLevelMemTracker.h"
#include "Misc/AssertionMacros.h"
#include "MuR/InstancePrivate.h"
#include "MuR/MutableMath.h"

namespace mu
{

	//---------------------------------------------------------------------------------------------
	//---------------------------------------------------------------------------------------------
	//---------------------------------------------------------------------------------------------
	Instance::Instance()
	{
		LLM_SCOPE_BYNAME(TEXT("MutableRuntime"));
		m_pD = new Private();
	}


	//---------------------------------------------------------------------------------------------
	Instance::~Instance()
	{
		LLM_SCOPE_BYNAME(TEXT("MutableRuntime"));
        check( m_pD );
		delete m_pD;
		m_pD = 0;
	}


	//---------------------------------------------------------------------------------------------
	Instance::Private* Instance::GetPrivate() const
	{
		return m_pD;
	}


    //---------------------------------------------------------------------------------------------
    InstancePtr Instance::Clone() const
    {
		LLM_SCOPE_BYNAME(TEXT("MutableRuntime"));
        InstancePtr pResult = new Instance();

        *pResult->GetPrivate() = *m_pD;

        return pResult;
    }


	//---------------------------------------------------------------------------------------------
	int32 Instance::GetDataSize() const
	{
		return 16 + sizeof(Private) + m_pD->Components.GetAllocatedSize() + m_pD->ExtensionData.GetAllocatedSize();
	}


    //---------------------------------------------------------------------------------------------
    Instance::ID Instance::GetId() const
    {
        return m_pD->Id;
    }


    //---------------------------------------------------------------------------------------------
    int32 Instance::GetComponentCount() const
    {
		return m_pD->Components.Num();
    }


	//---------------------------------------------------------------------------------------------
	int32 Instance::GetLODCount( int32 ComponentIndex ) const
	{
		if (m_pD->Components.IsValidIndex(ComponentIndex))
		{
			return m_pD->Components[ComponentIndex].LODs.Num();
		}
		check(false);
		return 0;
	}

	
	//---------------------------------------------------------------------------------------------
	uint16 Instance::GetComponentId( int32 ComponentIndex ) const
	{
		if (m_pD->Components.IsValidIndex(ComponentIndex))
		{
			return m_pD->Components[ComponentIndex].Id;
		}
		else
		{
			check(false);
		}

		return 0;
	}


    //---------------------------------------------------------------------------------------------
    int32 Instance::GetSurfaceCount( int32 ComponentIndex, int32 LODIndex ) const
    {
		if (m_pD->Components.IsValidIndex(ComponentIndex) &&
			m_pD->Components[ComponentIndex].LODs.IsValidIndex(LODIndex))
		{
			return m_pD->Components[ComponentIndex].LODs[LODIndex].Surfaces.Num();
		}
		else
		{
			check(false);
		}

		return 0;
	}


    //---------------------------------------------------------------------------------------------
    uint32 Instance::GetSurfaceId(int32 ComponentIndex, int32 LODIndex, int32 SurfaceIndex ) const
    {
        if (m_pD->Components.IsValidIndex(ComponentIndex) &&
			m_pD->Components[ComponentIndex].LODs.IsValidIndex(LODIndex) &&
			m_pD->Components[ComponentIndex].LODs[LODIndex].Surfaces.IsValidIndex(SurfaceIndex) )
        {
            return m_pD->Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex].InternalId;
        }
		else
		{
			check(false);
		}

        return 0;
    }


    //---------------------------------------------------------------------------------------------
    int32 Instance::FindSurfaceById(int32 ComponentIndex, int32 LODIndex, uint32 id ) const
    {
		if (m_pD->Components.IsValidIndex(ComponentIndex) &&
			m_pD->Components[ComponentIndex].LODs.IsValidIndex(LODIndex))
		{
			for (int32 i = 0; i < m_pD->Components[ComponentIndex].LODs[LODIndex].Surfaces.Num(); ++i)
			{
				if (m_pD->Components[ComponentIndex].LODs[LODIndex].Surfaces[i].InternalId == id)
				{
					return i;
				}
			}
		}
		else
		{
			check(false);
		}

        return -1;
    }

	
	//---------------------------------------------------------------------------------------------
	void Instance::FindBaseSurfaceBySharedId(int32 CompIndex, int32 SharedId, int32& OutSurfaceIndex, int32& OutLODIndex) const
	{
		if (m_pD->Components.IsValidIndex(CompIndex))
		{
			for (int32 LodIndex = 0; LodIndex < m_pD->Components[CompIndex].LODs.Num(); LodIndex++)
			{
				FInstanceLOD& LOD = m_pD->Components[CompIndex].LODs[LodIndex];
				for (int32 SurfaceIndex = 0; SurfaceIndex < LOD.Surfaces.Num(); ++SurfaceIndex)
				{
					if (LOD.Surfaces[SurfaceIndex].SharedId == SharedId)
					{
						OutSurfaceIndex = SurfaceIndex;
						OutLODIndex = LodIndex;
						return;
					}
				}
			}

		}

		OutSurfaceIndex = INDEX_NONE;
		OutLODIndex = INDEX_NONE;
	}


	//---------------------------------------------------------------------------------------------
	int32 Instance::GetSharedSurfaceId(int32 ComponentIndex, int32 LODIndex, int32 SurfaceIndex) const
	{
		if (m_pD->Components.IsValidIndex(ComponentIndex) &&
			m_pD->Components[ComponentIndex].LODs.IsValidIndex(LODIndex) &&
			m_pD->Components[ComponentIndex].LODs[LODIndex].Surfaces.IsValidIndex(SurfaceIndex))
		{
			return m_pD->Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex].SharedId;
		}
		else
		{
			check(false);
		}

		return 0;
	}
	

    //---------------------------------------------------------------------------------------------
    uint32 Instance::GetSurfaceCustomId(int32 ComponentIndex, int32 LODIndex, int32 SurfaceIndex ) const
    {
        if (m_pD->Components.IsValidIndex(ComponentIndex) &&
			m_pD->Components[ComponentIndex].LODs.IsValidIndex(LODIndex) &&
			m_pD->Components[ComponentIndex].LODs[LODIndex].Surfaces.IsValidIndex(SurfaceIndex))
        {
            return m_pD->Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex].ExternalId;
        }
		else
		{
			check(false);
		}

        return 0;
    }


	//---------------------------------------------------------------------------------------------
    int32 Instance::GetImageCount(int32 ComponentIndex, int32 LODIndex, int32 SurfaceIndex ) const
	{
		check(m_pD->Components.IsValidIndex(ComponentIndex));
		check(m_pD->Components[ComponentIndex].LODs.IsValidIndex(LODIndex));
		check(m_pD->Components[ComponentIndex].LODs[LODIndex].Surfaces.IsValidIndex(SurfaceIndex));

		return m_pD->Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex].Images.Num();
	}


	//---------------------------------------------------------------------------------------------
    int32 Instance::GetVectorCount(int32 ComponentIndex, int32 LODIndex, int32 SurfaceIndex ) const
	{
		check(m_pD->Components.IsValidIndex(ComponentIndex));
		check(m_pD->Components[ComponentIndex].LODs.IsValidIndex(LODIndex));
		check(m_pD->Components[ComponentIndex].LODs[LODIndex].Surfaces.IsValidIndex(SurfaceIndex));

		return m_pD->Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex].Vectors.Num();
	}


	//---------------------------------------------------------------------------------------------
    int32 Instance::GetScalarCount(int32 ComponentIndex, int32 LODIndex, int32 SurfaceIndex ) const
	{
		check(m_pD->Components.IsValidIndex(ComponentIndex));
		check(m_pD->Components[ComponentIndex].LODs.IsValidIndex(LODIndex));
		check(m_pD->Components[ComponentIndex].LODs[LODIndex].Surfaces.IsValidIndex(SurfaceIndex));

		return m_pD->Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex].Scalars.Num();
	}


    //---------------------------------------------------------------------------------------------
    int32 Instance::GetStringCount(int32 ComponentIndex, int32 LODIndex, int32 SurfaceIndex ) const
    {
		check(m_pD->Components.IsValidIndex(ComponentIndex));
		check(m_pD->Components[ComponentIndex].LODs.IsValidIndex(LODIndex));
		check(m_pD->Components[ComponentIndex].LODs[LODIndex].Surfaces.IsValidIndex(SurfaceIndex));

		return m_pD->Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex].Strings.Num();
	}


    //---------------------------------------------------------------------------------------------
	FResourceID Instance::GetMeshId(int32 ComponentIndex, int32 LODIndex ) const
    {
        check(m_pD->Components.IsValidIndex(ComponentIndex));
        check(m_pD->Components[ComponentIndex].LODs.IsValidIndex(LODIndex));

		return m_pD->Components[ComponentIndex].LODs[LODIndex].MeshId;
    }


	//---------------------------------------------------------------------------------------------
	FResourceID Instance::GetImageId(int32 ComponentIndex, int32 LODIndex, int32 SurfaceIndex, int32 ImageIndex) const
	{
		check(m_pD->Components.IsValidIndex(ComponentIndex));
		check(m_pD->Components[ComponentIndex].LODs.IsValidIndex(LODIndex));
        check(m_pD->Components[ComponentIndex].LODs[LODIndex].Surfaces.IsValidIndex(SurfaceIndex));
        check(m_pD->Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex].Images.IsValidIndex(ImageIndex));

		return m_pD->Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex].Images[ImageIndex].Id;
	}


	//---------------------------------------------------------------------------------------------
    FName Instance::GetImageName(int32 ComponentIndex, int32 LODIndex, int32 SurfaceIndex, int32 ImageIndex ) const
	{
		check(m_pD->Components.IsValidIndex(ComponentIndex));
		check(m_pD->Components[ComponentIndex].LODs.IsValidIndex(LODIndex));
        check(m_pD->Components[ComponentIndex].LODs[LODIndex].Surfaces.IsValidIndex(SurfaceIndex));
		check(m_pD->Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex].Images.IsValidIndex(ImageIndex));

        return m_pD->Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex].Images[ImageIndex].Name;
	}


	//---------------------------------------------------------------------------------------------
	FVector4f Instance::GetVector(int32 ComponentIndex, int32 LODIndex, int32 SurfaceIndex, int32 VectorIndex) const
	{
		check(m_pD->Components.IsValidIndex(ComponentIndex));
		check(m_pD->Components[ComponentIndex].LODs.IsValidIndex(LODIndex));
        check(m_pD->Components[ComponentIndex].LODs[LODIndex].Surfaces.IsValidIndex(SurfaceIndex));
		check(m_pD->Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex].Vectors.IsValidIndex(VectorIndex));

        return m_pD->Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex].Vectors[VectorIndex].Value;
	}


	//---------------------------------------------------------------------------------------------
	FName Instance::GetVectorName(int32 ComponentIndex, int32 LODIndex, int32 SurfaceIndex, int32 VectorIndex) const
	{
		check(m_pD->Components.IsValidIndex(ComponentIndex));
		check(m_pD->Components[ComponentIndex].LODs.IsValidIndex(LODIndex));
        check(m_pD->Components[ComponentIndex].LODs[LODIndex].Surfaces.IsValidIndex(SurfaceIndex));
		check(m_pD->Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex].Vectors.IsValidIndex(VectorIndex));

        return m_pD->Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex].Vectors[VectorIndex].Name;
	}


	//---------------------------------------------------------------------------------------------
    float Instance::GetScalar(int32 ComponentIndex, int32 LODIndex, int32 SurfaceIndex, int32 ScalarIndex ) const
	{
		check(m_pD->Components.IsValidIndex(ComponentIndex));
		check(m_pD->Components[ComponentIndex].LODs.IsValidIndex(LODIndex));
        check(m_pD->Components[ComponentIndex].LODs[LODIndex].Surfaces.IsValidIndex(SurfaceIndex));
		check(m_pD->Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex].Scalars.IsValidIndex(ScalarIndex));

        return m_pD->Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex].Scalars[ScalarIndex].Value;
	}


	//---------------------------------------------------------------------------------------------
	FName Instance::GetScalarName(int32 ComponentIndex, int32 LODIndex, int32 SurfaceIndex, int32 ScalarIndex) const
	{
		check(m_pD->Components.IsValidIndex(ComponentIndex));
		check(m_pD->Components[ComponentIndex].LODs.IsValidIndex(LODIndex));
        check(m_pD->Components[ComponentIndex].LODs[LODIndex].Surfaces.IsValidIndex(SurfaceIndex));
		check(m_pD->Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex].Scalars.IsValidIndex(ScalarIndex));

        return m_pD->Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex].Scalars[ScalarIndex].Name;
	}


    //---------------------------------------------------------------------------------------------
    FString Instance::GetString(int32 ComponentIndex, int32 LODIndex, int32 SurfaceIndex, int32 StringIndex ) const
    {
        check(m_pD->Components.IsValidIndex(ComponentIndex));
        check(m_pD->Components[ComponentIndex].LODs.IsValidIndex(LODIndex));
        check(m_pD->Components[ComponentIndex].LODs[LODIndex].Surfaces.IsValidIndex(SurfaceIndex));
		check(m_pD->Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex].Strings.IsValidIndex(StringIndex));

		bool bValid = m_pD->Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex].Strings.IsValidIndex(StringIndex);
		if (bValid)
        {
            return m_pD->Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex].Strings[StringIndex].Value;
        }

        return "";
    }


    //---------------------------------------------------------------------------------------------
	FName Instance::GetStringName(int32 ComponentIndex, int32 LODIndex, int32 SurfaceIndex, int32 StringIndex) const
    {
        check(m_pD->Components.IsValidIndex(ComponentIndex));
        check(m_pD->Components[ComponentIndex].LODs.IsValidIndex(LODIndex));
        check(m_pD->Components[ComponentIndex].LODs[LODIndex].Surfaces.IsValidIndex(SurfaceIndex));

		bool bValid = m_pD->Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex].Strings.IsValidIndex(StringIndex);
		if (bValid)
		{
            return m_pD->Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex].Strings[StringIndex].Name;
        }

        return NAME_None;
    }


    //---------------------------------------------------------------------------------------------
	int32 Instance::GetExtensionDataCount() const
	{
		return m_pD->ExtensionData.Num();
	}


    //---------------------------------------------------------------------------------------------
	void Instance::GetExtensionData(int32 Index, Ptr<const class ExtensionData>& OutExtensionData, FName& OutName) const
	{
		check(m_pD->ExtensionData.IsValidIndex(Index));

		OutExtensionData = m_pD->ExtensionData[Index].Data;
		OutName = m_pD->ExtensionData[Index].Name;
	}


    //---------------------------------------------------------------------------------------------
	int32 Instance::Private::AddComponent()
	{
		int32 result = Components.Emplace();
		return result;
	}


    //---------------------------------------------------------------------------------------------
    int32 Instance::Private::AddLOD( int32 ComponentIndex )
    {
        // Automatically create the necessary lods and components
        while (ComponentIndex >= Components.Num())
        {
            AddComponent();
        }

        return Components[ComponentIndex].LODs.Emplace();
    }


    //---------------------------------------------------------------------------------------------
    int32 Instance::Private::AddSurface( int32 ComponentIndex, int32 LODIndex )
    {
        // Automatically create the necessary lods and components
        while (ComponentIndex >= Components.Num())
        {
            AddComponent();
        }
        while (LODIndex >= Components[ComponentIndex].LODs.Num())
        {
            AddLOD(ComponentIndex);
        }

        return Components[ComponentIndex].LODs[LODIndex].Surfaces.Emplace();
    }


    //---------------------------------------------------------------------------------------------
    void Instance::Private::SetSurfaceName( int32 ComponentIndex, int32 LODIndex, int32 SurfaceIndex, FName Name)
    {
        // Automatically create the necessary lods and components
		while (ComponentIndex >= Components.Num())
		{
			AddComponent();
		}
		while (LODIndex >= Components[ComponentIndex].LODs.Num())
		{
			AddLOD(ComponentIndex);
		}
		while ( SurfaceIndex>=Components[ComponentIndex].LODs[LODIndex].Surfaces.Num() )
        {
            AddSurface( LODIndex, ComponentIndex );
        }

        FInstanceSurface& surface = Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex];
        surface.Name = Name;
    }


	//---------------------------------------------------------------------------------------------
	void Instance::Private::SetMesh(int32 ComponentIndex, int32 LODIndex, FResourceID meshId, FName Name)
	{
		// Automatically create the necessary lods and components
		while (ComponentIndex >= Components.Num())
		{
			AddComponent();
		}
		while (LODIndex >= Components[ComponentIndex].LODs.Num())
		{
			AddLOD(ComponentIndex);
		}

		FInstanceLOD& LOD = Components[ComponentIndex].LODs[LODIndex];
		LOD.MeshId = meshId;
		LOD.MeshName = Name;
	}


	//---------------------------------------------------------------------------------------------
    int32 Instance::Private::AddImage( int32 ComponentIndex, int32 LODIndex, int32 SurfaceIndex, FResourceID imageId, FName Name)
	{
		// Automatically create the necessary lods and components
		while (ComponentIndex >= Components.Num())
		{
			AddComponent();
		}
		while (LODIndex >= Components[ComponentIndex].LODs.Num())
		{
			AddLOD(ComponentIndex);
		}
		while (SurfaceIndex >= Components[ComponentIndex].LODs[LODIndex].Surfaces.Num())
		{
			AddSurface(LODIndex, ComponentIndex);
		}

		FInstanceSurface& Surface = Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex];
		return Surface.Images.Add({ imageId, Name });
	}


	//---------------------------------------------------------------------------------------------
    int32 Instance::Private::AddVector( int32 ComponentIndex, int32 LODIndex, int32 SurfaceIndex, const FVector4f& vec, FName Name)
	{
		// Automatically create the necessary lods and components
		while (ComponentIndex >= Components.Num())
		{
			AddComponent();
		}
		while (LODIndex >= Components[ComponentIndex].LODs.Num())
		{
			AddLOD(ComponentIndex);
		}
		while (SurfaceIndex >= Components[ComponentIndex].LODs[LODIndex].Surfaces.Num())
		{
			AddSurface(LODIndex, ComponentIndex);
		}

		FInstanceSurface& Surface = Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex];
		return Surface.Vectors.Add({ vec, Name } );
	}


    //---------------------------------------------------------------------------------------------
    int32 Instance::Private::AddScalar( int32 ComponentIndex, int32 LODIndex, int32 SurfaceIndex, float sca, FName Name)
    {
        // Automatically create the necessary lods and components
		while (ComponentIndex >= Components.Num())
		{
			AddComponent();
		}
		while (LODIndex >= Components[ComponentIndex].LODs.Num())
		{
			AddLOD(ComponentIndex);
		}
		while (SurfaceIndex >= Components[ComponentIndex].LODs[LODIndex].Surfaces.Num())
		{
			AddSurface(LODIndex, ComponentIndex);
		}

		FInstanceSurface& Surface = Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex];
		return Surface.Scalars.Add({ sca, Name });
    }


    //---------------------------------------------------------------------------------------------
    int32 Instance::Private::AddString( int32 ComponentIndex, int32 LODIndex, int32 SurfaceIndex, const FString& Value, FName Name)
    {
        // Automatically create the necessary lods and components
		while (ComponentIndex >= Components.Num())
		{
			AddComponent();
		}
		while (LODIndex >= Components[ComponentIndex].LODs.Num())
		{
			AddLOD(ComponentIndex);
		}
		while (SurfaceIndex >= Components[ComponentIndex].LODs[LODIndex].Surfaces.Num())
		{
			AddSurface(LODIndex, ComponentIndex);
		}

		FInstanceSurface& Surface = Components[ComponentIndex].LODs[LODIndex].Surfaces[SurfaceIndex];
		return Surface.Strings.Add({ Value, Name });
    }


    //---------------------------------------------------------------------------------------------
	void Instance::Private::AddExtensionData(const Ptr<const class ExtensionData>& Data, FName Name)
	{
		check(Data);

		NamedExtensionData& Entry = ExtensionData.AddDefaulted_GetRef();
		Entry.Data = Data;
		Entry.Name = Name;
	}
}

