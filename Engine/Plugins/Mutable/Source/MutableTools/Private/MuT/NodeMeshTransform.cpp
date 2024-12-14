// Copyright Epic Games, Inc. All Rights Reserved.


#include "MuT/NodeMeshTransform.h"

#include "HAL/PlatformString.h"
#include "Misc/AssertionMacros.h"
#include "MuR/MutableMath.h"
#include "MuT/NodeLayout.h"
#include "MuT/NodeMeshPrivate.h"
#include "MuT/NodeMeshTransformPrivate.h"
#include "MuT/NodePrivate.h"


namespace mu
{

	//---------------------------------------------------------------------------------------------
	// Static initialisation
	//---------------------------------------------------------------------------------------------
    FNodeType NodeMeshTransform::Private::s_type = FNodeType(Node::EType::MeshTransform, NodeMesh::GetStaticType() );


	//---------------------------------------------------------------------------------------------
	//!
	//---------------------------------------------------------------------------------------------

    MUTABLE_IMPLEMENT_NODE( NodeMeshTransform )


	//---------------------------------------------------------------------------------------------
	// Own Interface
	//---------------------------------------------------------------------------------------------
    NodeMeshPtr NodeMeshTransform::GetSource() const
	{
		return m_pD->Source;
	}


    //---------------------------------------------------------------------------------------------
    void NodeMeshTransform::SetSource( NodeMesh* p )
    {
        m_pD->Source = p;
    }


    //---------------------------------------------------------------------------------------------
    void NodeMeshTransform::SetTransform( const FMatrix44f& Value )
    {
        m_pD->Transform = Value;
    }


    //---------------------------------------------------------------------------------------------
	const FMatrix44f& NodeMeshTransform::GetTransform() const
    {
        return m_pD->Transform;
    }

}
