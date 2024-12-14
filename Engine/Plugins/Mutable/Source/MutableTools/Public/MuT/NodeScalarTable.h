// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuT/Node.h"
#include "MuT/NodeScalar.h"
#include "MuT/Table.h"


namespace mu
{

	// Forward definitions
	class NodeScalarTable;
	typedef Ptr<NodeScalarTable> NodeScalarTablePtr;
	typedef Ptr<const NodeScalarTable> NodeScalarTablePtrConst;


	//! This node provides the meshes stored in the column of a table.
	class MUTABLETOOLS_API NodeScalarTable : public NodeScalar
	{
	public:

		FString ParameterName;
		Ptr<Table> Table;
		FString ColumnName;
		bool bNoneOption = false;
		FString DefaultRowName;

	public:

		// Node interface
		virtual const FNodeType* GetType() const override { return GetStaticType(); }
		static const FNodeType* GetStaticType() { return &StaticType; }

		//-----------------------------------------------------------------------------------------
		// Own Interface
		//-----------------------------------------------------------------------------------------

		//! Set the name of the implicit table parameter.
		void SetParameterName( const FString& strName );

		//!
		void SetColumn( const FString& strName );

		//! Adds the "None" option to the parameter that represents this table column
		void SetNoneOption(bool bAddOption);

		//! Set the row name to be used as default value
		void SetDefaultRowName(const FString& RowName);


	protected:

		//! Forbidden. Manage with the Ptr<> template.
		~NodeScalarTable() {}

	private:

		static FNodeType StaticType;

	};


}
