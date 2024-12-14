// Copyright Epic Games, Inc. All Rights Reserved.


#include "MuT/NodeMeshTable.h"

#include "Misc/AssertionMacros.h"
#include "MuT/NodeLayout.h"
#include "MuT/NodePrivate.h"
#include "MuT/Table.h"


namespace mu
{

	//---------------------------------------------------------------------------------------------
	// Static initialisation
	//---------------------------------------------------------------------------------------------
	FNodeType NodeMeshTable::StaticType = FNodeType(Node::EType::MeshTable, NodeMesh::GetStaticType() );


	//---------------------------------------------------------------------------------------------
	// Own Interface
	//---------------------------------------------------------------------------------------------
	void NodeMeshTable::SetColumn( const FString& strName )
	{
		ColumnName = strName;
	}


	//---------------------------------------------------------------------------------------------
	void NodeMeshTable::SetParameterName( const FString& strName )
	{
		ParameterName = strName;
	}


	//---------------------------------------------------------------------------------------------
	int NodeMeshTable::GetLayoutCount() const
	{
		return Layouts.Num();
	}


	//---------------------------------------------------------------------------------------------
	void NodeMeshTable::SetLayoutCount( int i )
	{
		Layouts.SetNum( i );
	}


	//---------------------------------------------------------------------------------------------
	Ptr<NodeLayout> NodeMeshTable::GetLayout( int i ) const
	{
		Ptr<NodeLayout> pResult;

		if (i >= 0 && i < Layouts.Num())
		{
			pResult = Layouts[i];
		}

		return pResult;
	}


	//---------------------------------------------------------------------------------------------
	void NodeMeshTable::SetLayout( int i, Ptr<NodeLayout> pLayout )
	{
		check( i>=0 && i<GetLayoutCount() );
		Layouts[i] = pLayout;
	}


	//---------------------------------------------------------------------------------------------
	void NodeMeshTable::SetNoneOption(bool bAddNoneOption)
	{
		bNoneOption = bAddNoneOption;
	}


	//---------------------------------------------------------------------------------------------
	void NodeMeshTable::SetDefaultRowName(const FString& RowName)
	{
		DefaultRowName = RowName;
	}

}


