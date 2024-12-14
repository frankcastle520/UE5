// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuT/Node.h"
#include "MuT/NodeColour.h"
#include "MuT/Table.h"


namespace mu
{

	/** This node provides the meshes stored in the column of a table.
	*/
	class MUTABLETOOLS_API NodeColourTable : public NodeColour
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

	protected:

		//! Forbidden. Manage with the Ptr<> template.
		~NodeColourTable() {}

	private:

		static FNodeType StaticType;

	};


}
