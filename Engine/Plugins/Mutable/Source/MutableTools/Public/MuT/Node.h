// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "MuR/RefCounted.h"

namespace mu
{
	// Forward declarations
	class Node;
	typedef Ptr<Node> NodePtr;
	typedef Ptr<const Node> NodePtrConst;

	class NodeMap;
	typedef Ptr<NodeMap> NodeMapPtr;
	typedef Ptr<const NodeMap> NodeMapPtrConst;



    //! %Base class for all graphs used in the source data to define models and transforms.
	class MUTABLETOOLS_API Node : public RefCounted
	{
	public:

		/** Non-stable enumeration of all node types. */
		enum class EType : uint8
		{
			None,

			Node,

			Mesh,
			MeshConstant,
			MeshInterpolate,
			MeshTable,
			MeshFormat,
			MeshTangents,
			MeshMorph,
			MeshMakeMorph,
			MeshSwitch,
			MeshFragment,
			MeshTransform,
			MeshClipMorphPlane,
			MeshClipWithMesh,
			MeshApplyPose,
			MeshVariation,
			MeshGeometryOperation,
			MeshReshape,
			MeshClipDeform,

			Image,
			ImageConstant,
			ImageInterpolate,
			ImageSaturate,
			ImageTable,
			ImageSwizzle,
			ImageColorMap,
			ImageGradient,
			ImageBinarise,
			ImageLuminance,
			ImageLayer,
			ImageLayerColour,
			ImageResize,
			ImagePlainColour,
			ImageProject,
			ImageMipmap,
			ImageSwitch,
			ImageConditional,
			ImageFormat,
			ImageParameter, 
			ImageMultiLayer,
			ImageInvert,
			ImageVariation,
			ImageNormalComposite,
			ImageTransform,

			Bool,
			BoolConstant,
			BoolParameter,
			BoolNot,
			BoolAnd,

			Color,
			ColorConstant,
			ColorParameter,
			ColorSampleImage,
			ColorTable,
			ColorImageSize,
			ColorFromScalars,
			ColorArithmeticOperation,
			ColorSwitch,
			ColorVariation,

			Scalar,
			ScalarConstant,
			ScalarParameter,
			ScalarEnumParameter,
			ScalarCurve,
			ScalarSwitch,
			ScalarArithmeticOperation,
			ScalarVariation,
			ScalarTable,

			String,
			StringConstant,
			StringParameter,

			Projector,
			ProjectorConstant,
			ProjectorParameter,

			Range,
			RangeFromScalar,

			Layout,

			PatchImage,
			PatchMesh,

			Surface,
			SurfaceNew,
			SurfaceSwitch,
			SurfaceVariation,

			LOD,

			Component,
			ComponentNew,
			ComponentEdit,
			ComponentSwitch,
			ComponentVariation,

			Object,
			ObjectNew,
			ObjectGroup,

			Modifier,
			ModifierMeshClipMorphPlane,
			ModifierMeshClipWithMesh,
			ModifierMeshClipDeform,
			ModifierMeshClipWithUVMask,
			ModifierSurfaceEdit,
			ModifierTransformInMesh,

			ExtensionData,
			ExtensionDataConstant,
			ExtensionDataSwitch,
			ExtensionDataVariation,

			Matrix,
			MatrixConstant,
			MatrixParameter,

			Count
		};


		/** Information about the type of a node, to provide some means to the tools to deal generically with nodes. */
		struct FNodeType
		{
			FNodeType();
			FNodeType(Node::EType, const FNodeType* Parent);

			Node::EType Type;
			const FNodeType* Parent;

			inline bool IsA(const FNodeType* CandidateType) const
			{
				if (CandidateType == this)
				{
					return true;
				}

				if (Parent)
				{
					return Parent->IsA(CandidateType);
				}

				return false;
			}
		};


		//-----------------------------------------------------------------------------------------
		// Own Interface
		//-----------------------------------------------------------------------------------------

		/** Node type hierarchy data. */
		virtual const FNodeType* GetType() const { return &StaticType; }
		static const FNodeType* GetStaticType() { return &StaticType; }

		/** Set the opaque context returned in messages in the compiler log. */
		void SetMessageContext(const void* context);
		const void* GetMessageContext() const;

		//-----------------------------------------------------------------------------------------
        // Interface pattern
		//-----------------------------------------------------------------------------------------
		class Private;

	protected:

		inline ~Node() {}

		/** This is an opaque context used to attach to reported error messages. */
		const void* MessageContext = nullptr;

	private:

		static FNodeType StaticType;

	};


	using FNodeType = Node::FNodeType;
}
