// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuR/ImageTypes.h"
#include "MuT/Table.h"
#include "MuT/Node.h"
#include "MuT/NodeImage.h"


namespace mu
{


	/** This node provides the meshes stored in the column of a table. */
	class MUTABLETOOLS_API NodeImageTable : public NodeImage
	{
	public:

		FString ParameterName;

		Ptr<Table> Table;

		FString ColumnName;

		uint16 MaxTextureSize = 0;

		FImageDesc ReferenceImageDesc;

		bool bNoneOption = false;

		FString DefaultRowName;

		/** */
		FSourceDataDescriptor SourceDataDescriptor;

	public:

		// Node interface
		virtual const FNodeType* GetType() const override { return GetStaticType(); }
		static const FNodeType* GetStaticType() { return &StaticType; }

	protected:

		//! Forbidden. Manage with the Ptr<> template.
		~NodeImageTable() {}

	private:

		static FNodeType StaticType;


	};


}
