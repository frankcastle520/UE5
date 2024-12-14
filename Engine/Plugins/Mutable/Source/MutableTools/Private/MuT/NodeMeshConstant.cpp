// Copyright Epic Games, Inc. All Rights Reserved.


#include "MuT/NodeMeshConstant.h"

#include "Misc/AssertionMacros.h"
#include "MuT/NodeLayout.h"
#include "MuT/NodeMeshConstantPrivate.h"
#include "MuT/NodePrivate.h"
#include "MuR/Mesh.h"


namespace mu
{
	FNodeType NodeMeshConstant::Private::s_type = FNodeType(Node::EType::MeshConstant, NodeMesh::GetStaticType() );


	MUTABLE_IMPLEMENT_NODE( NodeMeshConstant );


	Ptr<Mesh> NodeMeshConstant::GetValue() const
	{
		return m_pD->Value;
	}


	void NodeMeshConstant::SetValue( Ptr<Mesh> pValue )
	{
		m_pD->Value = pValue;

        if (m_pD->Value)
        {
            // Ensure mesh is well formed
            m_pD->Value->EnsureSurfaceData();
        }
    }


	void NodeMeshConstant::AddMorph(const FString& Name, Ptr<Mesh> Morphed)
	{
		m_pD->Morphs.Add({Name,Morphed});
	}


	Ptr<Mesh> NodeMeshConstant::FindMorph(const FString& Name) const
	{
		for (Private::FMorph& Morph : m_pD->Morphs )
		{
			if (Morph.Name == Name)
			{
				return Morph.MorphedMesh;
			}
		}
		return nullptr;
	}


	int32 NodeMeshConstant::GetLayoutCount() const
	{
		return m_pD->Layouts.Num();
	}


	void NodeMeshConstant::SetLayoutCount( int32 num )
	{
		check( num >=0 );
		m_pD->Layouts.SetNum( num );
	}


	Ptr<NodeLayout> NodeMeshConstant::GetLayout( int32 index ) const
	{
		check( index >=0 && index < m_pD->Layouts.Num() );

		Ptr<NodeLayout> pResult;

		if (index >= 0 && index < m_pD->Layouts.Num())
		{
			pResult = m_pD->Layouts[index];
		}
		return pResult;
	}


	void NodeMeshConstant::SetLayout( int32 index, Ptr<NodeLayout> pLayout )
	{
		check( index >=0 && index < m_pD->Layouts.Num() );

		m_pD->Layouts[ index ] = pLayout;
	}


}


