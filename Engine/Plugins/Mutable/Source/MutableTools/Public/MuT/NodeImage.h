// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuT/Node.h"

namespace mu
{

	// Forward definitions
	class NodeImage;
	typedef Ptr<NodeImage> NodeImagePtr;
	typedef Ptr<const NodeImage> NodeImagePtrConst;

	/** Data related to the a source image that is necessary to classify the final image and mesh fragments
	 * that are derived from this source.
	 */
	struct FSourceDataDescriptor
	{
		enum ESpecialValues
		{
			EInvalid = -2,
			ENeutral = -1,
		};

		/** Number of mips in the source texture that are considered high resolution.
		* If -1 it means the entire descriptor is neutral.
		* If -2 it means the descriptor is invalid (results of an operation that shouldn't happen).
		*/
		int32 SourceHighResMips = ENeutral;

		/** Source tags that mark this data and prevent it from mixing with other data at compile time. */
		TArray<FString> Tags;

		/** Source Id */
		uint32 SourceId = MAX_uint32;

		inline bool operator==(const FSourceDataDescriptor& Other) const
		{
			return SourceHighResMips == Other.SourceHighResMips && Tags == Other.Tags;
		}

		inline bool IsInvalid() const
		{
			return SourceHighResMips == EInvalid;
		}

		inline bool IsNeutral() const
		{
			return SourceHighResMips == ENeutral;
		}

		void CombineWith(const FSourceDataDescriptor& Other)
		{
			if (IsInvalid() || Other.IsInvalid())
			{
				SourceHighResMips = EInvalid;
				SourceId = MAX_uint32;
				Tags.Empty();
				return;
			}

			if (Other.IsNeutral())
			{
				return;
			}

			if (IsNeutral())
			{
				SourceHighResMips = Other.SourceHighResMips;
				SourceId = Other.SourceId;
				Tags = Other.Tags;
				return;
			}

			if (!(*this == Other))
			{
				SourceHighResMips = EInvalid;
				SourceId = MAX_uint32;
				Tags.Empty();
				return;
			}

			return;
		}
	};


    /** Base class of any node that outputs an image. */
	class MUTABLETOOLS_API NodeImage : public Node
	{
	public:

		//-----------------------------------------------------------------------------------------
		// Node Interface
		//-----------------------------------------------------------------------------------------

		const FNodeType* GetType() const override;
		static const FNodeType* GetStaticType();

		//-----------------------------------------------------------------------------------------
		// Interface pattern
		//-----------------------------------------------------------------------------------------
		class Private;

	protected:

		//! Forbidden. Manage with the Ptr<> template.
		inline ~NodeImage() {}

	};



}
