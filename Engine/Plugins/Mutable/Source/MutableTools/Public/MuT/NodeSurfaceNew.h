// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuT/Node.h"
#include "MuT/NodeSurface.h"
#include "MuT/NodeMesh.h"
#include "MuT/NodeImage.h"
#include "MuT/NodeScalar.h"
#include "MuT/NodeString.h"
#include "MuT/NodeColour.h"

namespace mu
{

	/** This node makes a new Surface (Mesh Material Section) from a mesh and several material parameters like images, vectors and scalars. */
	class MUTABLETOOLS_API NodeSurfaceNew : public NodeSurface
	{
	public:

		FString Name;

		/** An optional, opaque id that will be returned in the surfaces of the created
		* instances. Can be useful to identify surfaces on the application side.
		*/
		uint32 ExternalId = 0;

		/** Add an id that will be used to identify the same surface in other LODs. */
		int32 SharedSurfaceId = INDEX_NONE;

		Ptr<NodeMesh> Mesh;

		struct FImageData
		{
			FString Name;
			FString MaterialName;
			FString MaterialParameterName;
			Ptr<NodeImage> Image;

			/** Index of the layout transform to apply to this image. It could be negative, to indicate no layout transform. */
			int8 LayoutIndex = 0;
		};

		TArray<FImageData> Images;

		struct FVectorData
		{
			FString Name;
			Ptr<NodeColour> Vector;
		};

		TArray<FVectorData> Vectors;

		struct FScalar
		{
			FString Name;
			Ptr<NodeScalar> Scalar;
		};

		TArray<FScalar> Scalars;

		struct FStringData
		{
			FString Name;
			Ptr<NodeString> String;
		};

		TArray<FStringData> Strings;

		/** Tags added to the surface:
		* - the surface will be affected by modifier nodes with the same tag
		* - the tag will be enabled when the surface is added to an object, and it can activate
		* variations for any surface.
		*/
		TArray<FString> Tags;

	public:

		// Node interface
		virtual const FNodeType* GetType() const override { return &StaticType; }
		static const FNodeType* GetStaticType() { return &StaticType; }

	protected:

		//! Forbidden. Manage with the Ptr<> template.
		~NodeSurfaceNew() {}

	private:

		static FNodeType StaticType;

	};


}
