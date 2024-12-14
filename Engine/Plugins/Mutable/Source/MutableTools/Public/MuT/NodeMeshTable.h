// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuT/Node.h"
#include "MuT/NodeMesh.h"
#include "MuT/NodeLayout.h"
#include "MuT/NodeImage.h"
#include "MuT/Table.h"


namespace mu
{

	// Forward definitions
	class NodeMeshTable;
	typedef Ptr<NodeMeshTable> NodeMeshTablePtr;
	typedef Ptr<const NodeMeshTable> NodeMeshTablePtrConst;


	//! This node provides the meshes stored in the column of a table.
	class MUTABLETOOLS_API NodeMeshTable : public NodeMesh
	{
	public:

		FString ParameterName;
		Ptr<Table> Table;
		FString ColumnName;
		bool bNoneOption = false;
		FString DefaultRowName;

		TArray<Ptr<NodeLayout>> Layouts;

		/** */
		FSourceDataDescriptor SourceDataDescriptor;

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

		//! Get the number of layouts defined in the meshes on this column.
		int GetLayoutCount() const;
		void SetLayoutCount( int );

		//! Get the node defining a layout of the meshes on this column.
		Ptr<NodeLayout> GetLayout( int index ) const;
		void SetLayout( int index, Ptr<NodeLayout>);

		//! Adds the "None" option to the parameter that represents this table column
		void SetNoneOption(bool bAddOption);

		//! Set the row name to be used as default value
		void SetDefaultRowName(const FString& RowName);


	protected:

		//! Forbidden. Manage with the Ptr<> template.
		~NodeMeshTable() {}

	private:

		static FNodeType StaticType;

	};


}
