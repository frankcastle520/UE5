// Copyright Epic Games, Inc. All Rights Reserved.


#include "MuT/NodeScalarTable.h"

#include "Misc/AssertionMacros.h"
#include "MuT/NodePrivate.h"
#include "MuT/Table.h"


namespace mu
{


	//---------------------------------------------------------------------------------------------
	// Static initialisation
	//---------------------------------------------------------------------------------------------
	FNodeType NodeScalarTable::StaticType = FNodeType(Node::EType::ScalarTable, NodeScalar::GetStaticType() );



	//---------------------------------------------------------------------------------------------
	// Own Interface
	//---------------------------------------------------------------------------------------------
	void NodeScalarTable::SetColumn( const FString& strName )
	{
		ColumnName = strName;
	}


	//---------------------------------------------------------------------------------------------
	void NodeScalarTable::SetParameterName( const FString& strName )
	{
		ParameterName = strName;
	}


	//---------------------------------------------------------------------------------------------
	void NodeScalarTable::SetNoneOption(bool bAddNoneOption)
	{
		bNoneOption = bAddNoneOption;
	}

	//---------------------------------------------------------------------------------------------
	void NodeScalarTable::SetDefaultRowName(const FString& RowName)
	{
		DefaultRowName = RowName;
	}
}


