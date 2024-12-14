// Copyright Epic Games, Inc. All Rights Reserved.


#include "MuT/NodeMeshFormat.h"

#include "Misc/AssertionMacros.h"
#include "MuT/NodeLayout.h"
#include "MuT/NodeMeshFormatPrivate.h"
#include "MuT/NodeMeshPrivate.h"
#include "MuT/NodePrivate.h"


namespace mu
{
class FMeshBufferSet;

	//---------------------------------------------------------------------------------------------
	// Static initialisation
	//---------------------------------------------------------------------------------------------
	FNodeType NodeMeshFormat::Private::s_type = FNodeType(Node::EType::MeshFormat, NodeMesh::GetStaticType() );


	//---------------------------------------------------------------------------------------------
	//!
	//---------------------------------------------------------------------------------------------

	MUTABLE_IMPLEMENT_NODE( NodeMeshFormat )


	//---------------------------------------------------------------------------------------------
	// Own Interface
	//---------------------------------------------------------------------------------------------
	NodeMeshPtr NodeMeshFormat::GetSource() const
	{
		return m_pD->Source;
	}


	void NodeMeshFormat::SetSource( NodeMesh* pValue )
	{
		m_pD->Source = pValue;
	}


	FMeshBufferSet& NodeMeshFormat::GetVertexBuffers()
	{
		return m_pD->VertexBuffers;
	}


	FMeshBufferSet& NodeMeshFormat::GetIndexBuffers()
	{
		return m_pD->IndexBuffers;
	}


	void NodeMeshFormat::SetOptimizeBuffers(bool bEnable)
	{
		m_pD->bOptimizeBuffers = bEnable;
	}

}


