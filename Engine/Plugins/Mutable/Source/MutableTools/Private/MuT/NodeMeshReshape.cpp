// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuT/NodeMeshReshape.h"

#include "Containers/Array.h"
#include "HAL/PlatformCrt.h"
#include "Misc/AssertionMacros.h"
#include "MuT/NodeLayout.h"
#include "MuT/NodeMeshPrivate.h"
#include "MuT/NodeMeshReshapePrivate.h"
#include "MuT/NodePrivate.h"


namespace mu
{

	//---------------------------------------------------------------------------------------------
	// Static initialisation
	//---------------------------------------------------------------------------------------------
	FNodeType NodeMeshReshape::Private::s_type = FNodeType(Node::EType::MeshReshape, NodeMesh::GetStaticType());


	//---------------------------------------------------------------------------------------------
	//!
	//---------------------------------------------------------------------------------------------

	MUTABLE_IMPLEMENT_NODE(NodeMeshReshape )


	//---------------------------------------------------------------------------------------------
	// Own Interface
	//---------------------------------------------------------------------------------------------
	const Ptr<NodeMesh>& NodeMeshReshape::GetBaseMesh() const
	{
		return m_pD->BaseMesh;
	}


	//---------------------------------------------------------------------------------------------
	void NodeMeshReshape::SetBaseMesh(const Ptr<NodeMesh>& pNode)
	{
		m_pD->BaseMesh = pNode;
	}


	//---------------------------------------------------------------------------------------------
	const Ptr<NodeMesh>& NodeMeshReshape::GetBaseShape() const
	{
		return m_pD->BaseShape;
	}


	//---------------------------------------------------------------------------------------------
	void NodeMeshReshape::SetBaseShape(const Ptr<NodeMesh>& pNode)
	{
		m_pD->BaseShape = pNode;
	}


	//---------------------------------------------------------------------------------------------
	const Ptr<NodeMesh>& NodeMeshReshape::GetTargetShape() const
	{
		return m_pD->TargetShape;
	}


	//---------------------------------------------------------------------------------------------
	void NodeMeshReshape::SetTargetShape(const Ptr<NodeMesh>& pNode)
	{
		m_pD->TargetShape = pNode;
	}

	//---------------------------------------------------------------------------------------------
	void NodeMeshReshape::SetReshapeVertices(bool bEnable)
	{
		m_pD->bReshapeVertices = bEnable;
	}

	//---------------------------------------------------------------------------------------------
	void NodeMeshReshape::SetRecomputeNormals(bool bEnable)
	{
		m_pD->bRecomputeNormals = bEnable;
	}


	//---------------------------------------------------------------------------------------------
	void NodeMeshReshape::SetApplyLaplacian(bool bEnable)
	{
		m_pD->bApplyLaplacian = bEnable;
	}


	//---------------------------------------------------------------------------------------------
	void NodeMeshReshape::SetReshapeSkeleton(bool bEnable)
	{
		m_pD->bReshapeSkeleton = bEnable;
	}
	

	//---------------------------------------------------------------------------------------------
	void NodeMeshReshape::SetColorUsages(EVertexColorUsage R, EVertexColorUsage G, EVertexColorUsage B, EVertexColorUsage A)
	{	
		m_pD->ColorRChannelUsage = R;
		m_pD->ColorGChannelUsage = G;
		m_pD->ColorBChannelUsage = B;
		m_pD->ColorAChannelUsage = A;
	}	

	//---------------------------------------------------------------------------------------------
	void NodeMeshReshape::AddBoneToDeform(const FBoneName& BoneId)
	{
		m_pD->BonesToDeform.Emplace(BoneId);
	}

	//---------------------------------------------------------------------------------------------
	void NodeMeshReshape::AddPhysicsBodyToDeform(const FBoneName& BoneId)
	{
		m_pD->PhysicsToDeform.Emplace(BoneId);
	}

	void NodeMeshReshape::SetReshapePhysicsVolumes(bool bEnable)
	{
		m_pD->bReshapePhysicsVolumes = bEnable;
	}

}


