// Copyright Epic Games, Inc. All Rights Reserved.


#include "MuT/NodeComponentEdit.h"

#include "Misc/AssertionMacros.h"
#include "MuT/NodeComponentNew.h"
#include "MuT/NodePrivate.h"
#include "MuT/NodeSurface.h"


namespace mu
{

	FNodeType NodeComponentEdit::StaticType = FNodeType(Node::EType::ComponentEdit, NodeComponent::GetStaticType() );


	//---------------------------------------------------------------------------------------------
	// Own Interface
	//---------------------------------------------------------------------------------------------
    void NodeComponentEdit::SetParent( NodeComponent* p )
	{
		m_pParent = p;
	}


	//---------------------------------------------------------------------------------------------
    NodeComponent* NodeComponentEdit::GetParent() const
	{
        return m_pParent.get();
	}


	//---------------------------------------------------------------------------------------------
	const NodeComponentNew* NodeComponentEdit::GetParentComponentNew() const
	{
		const NodeComponentNew* Parent = nullptr;
		if (m_pParent)
		{
			Parent = m_pParent->GetParentComponentNew();
		}

		return Parent;
	}

}


