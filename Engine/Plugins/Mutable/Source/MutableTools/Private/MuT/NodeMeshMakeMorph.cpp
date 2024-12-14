// Copyright Epic Games, Inc. All Rights Reserved.


#include "MuT/NodeMeshMakeMorph.h"

#include "Misc/AssertionMacros.h"
#include "MuT/NodeLayout.h"
#include "MuT/NodeMeshMakeMorphPrivate.h"
#include "MuT/NodeMeshPrivate.h"
#include "MuT/NodePrivate.h"


namespace mu
{

	//---------------------------------------------------------------------------------------------
	// Static initialisation
	//---------------------------------------------------------------------------------------------
    FNodeType NodeMeshMakeMorph::Private::s_type = FNodeType(Node::EType::MeshMakeMorph, NodeMesh::GetStaticType() );


	//---------------------------------------------------------------------------------------------
	//!
	//---------------------------------------------------------------------------------------------

    MUTABLE_IMPLEMENT_NODE( NodeMeshMakeMorph )


	//---------------------------------------------------------------------------------------------
	// Own Interface
	//---------------------------------------------------------------------------------------------
    NodeMeshPtr NodeMeshMakeMorph::GetBase() const
	{
		return m_pD->m_pBase;
	}


	//---------------------------------------------------------------------------------------------
    void NodeMeshMakeMorph::SetBase( NodeMesh* p )
	{
		m_pD->m_pBase = p;
	}


	//---------------------------------------------------------------------------------------------
    NodeMeshPtr NodeMeshMakeMorph::GetTarget() const
	{
		return m_pD->m_pTarget;
	}


	//---------------------------------------------------------------------------------------------
    void NodeMeshMakeMorph::SetTarget( NodeMesh* p )
	{
		m_pD->m_pTarget = p;
	}

	//---------------------------------------------------------------------------------------------
	void NodeMeshMakeMorph::SetOnlyPositionAndNormal(bool bInOnlyPositionAndNormals)
	{
		m_pD->bOnlyPositionAndNormal = bInOnlyPositionAndNormals;
	}


	//---------------------------------------------------------------------------------------------
	bool NodeMeshMakeMorph::GetOnlyPositionAndNormal() const
	{
		return m_pD->bOnlyPositionAndNormal;
	}

}
