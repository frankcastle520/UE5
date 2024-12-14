// Copyright Epic Games, Inc. All Rights Reserved.


#include "MuT/NodeMeshGeometryOperation.h"

#include "Misc/AssertionMacros.h"
#include "MuT/NodeLayout.h"
#include "MuT/NodeMeshGeometryOperationPrivate.h"
#include "MuT/NodeMeshPrivate.h"
#include "MuT/NodePrivate.h"
#include "MuT/NodeScalar.h"



namespace mu
{

	//---------------------------------------------------------------------------------------------
	// Static initialisation
	//---------------------------------------------------------------------------------------------
	FNodeType NodeMeshGeometryOperation::Private::s_type = FNodeType(Node::EType::MeshGeometryOperation, NodeMesh::GetStaticType() );


	//---------------------------------------------------------------------------------------------
	//!
	//---------------------------------------------------------------------------------------------

	MUTABLE_IMPLEMENT_NODE( NodeMeshGeometryOperation )


	//---------------------------------------------------------------------------------------------
	// Own Interface
	//---------------------------------------------------------------------------------------------
	const NodeScalarPtr& NodeMeshGeometryOperation::GetScalarA() const
	{
		return m_pD->m_pScalarA;
	}


	//---------------------------------------------------------------------------------------------
	void NodeMeshGeometryOperation::SetScalarA(const NodeScalarPtr& pNode)
	{
		m_pD->m_pScalarA = pNode;
	}


	//---------------------------------------------------------------------------------------------
	const NodeScalarPtr& NodeMeshGeometryOperation::GetScalarB() const
	{
		return m_pD->m_pScalarB;
	}


	//---------------------------------------------------------------------------------------------
	void NodeMeshGeometryOperation::SetScalarB(const NodeScalarPtr& pNode)
	{
		m_pD->m_pScalarB = pNode;
	}


	//---------------------------------------------------------------------------------------------
	const NodeMeshPtr& NodeMeshGeometryOperation::GetMeshA() const
	{
		return m_pD->m_pMeshA;
	}


	//---------------------------------------------------------------------------------------------
	void NodeMeshGeometryOperation::SetMeshA(const NodeMeshPtr& pNode)
	{
		m_pD->m_pMeshA = pNode;
	}


	//---------------------------------------------------------------------------------------------
	const NodeMeshPtr& NodeMeshGeometryOperation::GetMeshB() const
	{
		return m_pD->m_pMeshB;
	}


	//---------------------------------------------------------------------------------------------
	void NodeMeshGeometryOperation::SetMeshB(const NodeMeshPtr& pNode)
	{
		m_pD->m_pMeshB = pNode;
	}

}


