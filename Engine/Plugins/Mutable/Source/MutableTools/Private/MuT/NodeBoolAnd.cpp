// Copyright Epic Games, Inc. All Rights Reserved.


#include "Misc/AssertionMacros.h"
#include "MuT/Node.h"
#include "MuT/NodeBool.h"
#include "MuT/NodeBoolPrivate.h"
#include "MuT/NodePrivate.h"


namespace mu
{

	//---------------------------------------------------------------------------------------------
	// Static initialisation
	//---------------------------------------------------------------------------------------------
	FNodeType NodeBoolAnd::Private::s_type = FNodeType(Node::EType::BoolParameter, NodeBool::GetStaticType() );


	//---------------------------------------------------------------------------------------------
	//!
	//---------------------------------------------------------------------------------------------

	MUTABLE_IMPLEMENT_NODE( NodeBoolAnd );


	//---------------------------------------------------------------------------------------------
	// Own Interface
	//---------------------------------------------------------------------------------------------
	Ptr<NodeBool> NodeBoolAnd::GetA() const
	{
		return m_pD->m_pA;
	}


	//---------------------------------------------------------------------------------------------
	void NodeBoolAnd::SetA(Ptr<NodeBool> p )
	{
		m_pD->m_pA = p;
	}


	//---------------------------------------------------------------------------------------------
	Ptr<NodeBool> NodeBoolAnd::GetB() const
	{
		return m_pD->m_pB;
	}


	//---------------------------------------------------------------------------------------------
	void NodeBoolAnd::SetB(Ptr<NodeBool> p )
	{
		m_pD->m_pB = p;
	}

}


