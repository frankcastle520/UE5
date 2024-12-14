// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Map.h"
#include "HAL/PlatformMath.h"
#include "Misc/AssertionMacros.h"
#include "MuR/Image.h"
#include "MuR/Mesh.h"
#include "MuR/MutableMath.h"
#include "MuR/MutableMemory.h"
#include "MuR/Operations.h"
#include "MuR/Parameters.h"
#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuR/System.h"
#include "MuT/AST.h"
#include "MuT/ASTOpSwitch.h"
#include "MuT/CodeGenerator_FirstPass.h"
#include "MuT/Compiler.h"
#include "MuT/ErrorLog.h"
#include "MuT/Node.h"
#include "MuT/NodeBool.h"
#include "MuT/NodeColourSampleImage.h"
#include "MuT/NodeComponentEdit.h"
#include "MuT/NodeComponentNew.h"
#include "MuT/NodeComponentSwitch.h"
#include "MuT/NodeExtensionData.h"
#include "MuT/NodeImageProject.h"
#include "MuT/NodeLOD.h"
#include "MuT/NodeLayout.h"
#include "MuT/NodeMeshGeometryOperation.h"
#include "MuT/NodeMeshTable.h"
#include "MuT/NodeObjectGroup.h"
#include "MuT/NodeObjectNew.h"
#include "MuT/NodeProjector.h"
#include "MuT/NodeString.h"
#include "MuT/NodeSurfaceNew.h"
#include "MuT/NodeModifierSurfaceEdit.h"
#include "MuT/Table.h"
#include "MuT/TablePrivate.h"
#include "Templates/TypeHash.h"


namespace mu
{
	class ASTOpParameter;
	class Layout;
	class NodeColourArithmeticOperation;
	class NodeColourConstant;
	class NodeColourFromScalars;
	class NodeColourParameter;
	class NodeColourSwitch;
	class NodeColourTable;
	class NodeColourVariation;
	class NodeImageBinarise;
	class NodeImageColourMap;
	class NodeImageConditional;
	class NodeImageConstant;
	class NodeImageFormat;
	class NodeImageGradient;
	class NodeImageInterpolate;
	class NodeImageInvert;
	class NodeImageLayer;
	class NodeImageLayerColour;
	class NodeImageLuminance;
	class NodeImageMipmap;
	class NodeImageMultiLayer;
	class NodeImageNormalComposite;
	class NodeImageParameter;
	class NodeImagePlainColour;
	class NodeImageResize;
	class NodeImageSaturate;
	class NodeImageSwitch;
	class NodeImageSwizzle;
	class NodeImageTable;
	class NodeImageTransform;
	class NodeImageVariation;
	class NodeMeshApplyPose;
	class NodeMeshClipDeform;
	class NodeMeshClipMorphPlane;
	class NodeMeshClipWithMesh;
	class NodeMeshConstant;
	class NodeMeshFormat;
	class NodeMeshFragment;
	class NodeMeshInterpolate;
	class NodeMeshMakeMorph;
	class NodeMeshMorph;
	class NodeMeshReshape;
	class NodeMeshSwitch;
	class NodeMeshTransform;
	class NodeMeshVariation;
	class NodeRange;
	class NodeScalarArithmeticOperation;
	class NodeScalarConstant;
	class NodeScalarCurve;
	class NodeScalarEnumParameter;
	class NodeScalarParameter;
	class NodeScalarSwitch;
	class NodeScalarTable;
	class NodeScalarVariation;
	class NodeStringConstant;
	class NodeStringParameter;
	class NodeMatrix;
	class NodeMatrixConstant;
	class NodeMatrixParameter;
	struct FObjectState;
	struct FProgram;



    //---------------------------------------------------------------------------------------------
    //! Code generator
    //---------------------------------------------------------------------------------------------
    class CodeGenerator
    {
		
		friend class FirstPassGenerator;

    public:

        CodeGenerator( CompilerOptions::Private* options );

        //! Data will be stored in States
        void GenerateRoot( const Ptr<const Node> );

	public:

		// Generic top-level nodes
		struct FGenericGenerationOptions
		{
			friend FORCEINLINE uint32 GetTypeHash(const FGenericGenerationOptions& InKey)
			{
				uint32 KeyHash = 0;
				KeyHash = HashCombineFast(KeyHash, ::GetTypeHash(InKey.State));
				KeyHash = HashCombineFast(KeyHash, ::GetTypeHash(InKey.ActiveTags.Num()));
				return KeyHash;
			}

			bool operator==(const FGenericGenerationOptions& InKey) const = default;

			int32 State = -1;
			TArray<FString> ActiveTags;
		};

		struct FComponentGenerationOptions : public FGenericGenerationOptions
		{
			FComponentGenerationOptions(const FGenericGenerationOptions& BaseOptions, const Ptr<ASTOp>& InBaseInstance )
			{
				BaseInstance = InBaseInstance;
				State = BaseOptions.State;
				ActiveTags = BaseOptions.ActiveTags;
			}

			/** Instance to which the possibly generated components should be added. */
			Ptr<ASTOp> BaseInstance;
		};

		struct FLODGenerationOptions : public FGenericGenerationOptions
		{
			FLODGenerationOptions(const FGenericGenerationOptions& BaseOptions, int32 InLODIndex, const NodeComponentNew* InComponent)
			{
				Component = InComponent;
				LODIndex = InLODIndex;
				State = BaseOptions.State;
				ActiveTags = BaseOptions.ActiveTags;
			}

			const NodeComponentNew* Component = nullptr;
			int32 LODIndex;
		};

		struct FSurfaceGenerationOptions : public FGenericGenerationOptions
		{
			explicit FSurfaceGenerationOptions(const FGenericGenerationOptions& BaseOptions)
			{
				Component = nullptr;
				LODIndex = -1;
				State = BaseOptions.State;
				ActiveTags = BaseOptions.ActiveTags;
			}

			explicit FSurfaceGenerationOptions(const FLODGenerationOptions& BaseOptions)
			{
				Component = BaseOptions.Component;
				LODIndex = BaseOptions.LODIndex;
				State = BaseOptions.State;
				ActiveTags = BaseOptions.ActiveTags;
			}

			const NodeComponentNew* Component = nullptr;
			int32 LODIndex = -1;
		};

		struct FGenericGenerationResult
		{
			Ptr<ASTOp> op;
		};

		struct FGeneratedCacheKey
		{
			Ptr<const Node> Node;
			FGenericGenerationOptions Options;

			friend FORCEINLINE uint32 GetTypeHash(const FGeneratedCacheKey& InKey)
			{
				uint32 KeyHash = 0;
				KeyHash = HashCombineFast(KeyHash, ::GetTypeHash(InKey.Node.get()));
				KeyHash = HashCombineFast(KeyHash, GetTypeHash(InKey.Options));
				return KeyHash;
			}

			FORCEINLINE bool operator==(const FGeneratedCacheKey& Other) const = default;
		};

		typedef TMap<FGeneratedCacheKey, FGenericGenerationResult> FGeneratedGenericNodesMap;
		FGeneratedGenericNodesMap GeneratedGenericNodes;

		Ptr<ASTOp> Generate_Generic(const Ptr<const Node>, const FGenericGenerationOptions& );
		void Generate_LOD(const FLODGenerationOptions&, FGenericGenerationResult&, const NodeLOD*);
		void Generate_ObjectNew(const FGenericGenerationOptions&, FGenericGenerationResult&, const NodeObjectNew*);
		void Generate_ObjectGroup(const FGenericGenerationOptions&, FGenericGenerationResult&, const NodeObjectGroup*);

    public:

        //! Settings
        CompilerOptions::Private* CompilerOptions = nullptr;

		//!
		FirstPassGenerator FirstPass;

        //!
        ErrorLogPtr ErrorLog;

        //! After the entire code generation this contains the information about all the states
        typedef TArray< TPair<FObjectState, Ptr<ASTOp>> > StateList;
        StateList States;

    private:

        /** Container of meshes generated to be able to reuse them. 
		* They are sorted by a cheap hash to speed up searches.
		*/
		struct FGeneratedConstantMesh
		{
			Ptr<Mesh> Mesh;
			Ptr<ASTOp> LastMeshOp;
		};
		TMap<uint64,TArray<FGeneratedConstantMesh>> GeneratedConstantMeshes;

        /** List of image resources for every image formats that have been generated so far as palceholders for missing images. */
        Ptr<Image> MissingImage[size_t(EImageFormat::IF_COUNT)];


        //! List of already used vertex ID groups that must be unique.
        TSet<uint32> UniqueVertexIDGroups;

        struct FParentKey
        {
            const NodeObjectNew* ObjectNode = nullptr;
            int32 Lod = -1;
         };

		TArray< FParentKey > CurrentParents;

        /** List of additional components to add to an object that come from child objects.
        * The index is the object and lod that should receive the components.
		*/
        struct FAdditionalComponentKey
        {
			FAdditionalComponentKey()
            {
				ObjectNode = nullptr;
            }

            const NodeObjectNew* ObjectNode;

			FORCEINLINE bool operator==(const FAdditionalComponentKey& Other) const
			{
				return ObjectNode == Other.ObjectNode;
			}

			friend FORCEINLINE uint32 GetTypeHash(const FAdditionalComponentKey& InKey)
			{
				uint32 KeyHash = 0;
				KeyHash = HashCombineFast(KeyHash, ::GetTypeHash(InKey.ObjectNode));
				return KeyHash;
			}
		};

		struct FAdditionalComponentData
		{
			Ptr<ASTOp> ComponentOp;
			Ptr<ASTOp> PlaceholderOp;
		};

        TMap< FAdditionalComponentKey, TArray<FAdditionalComponentData> > AdditionalComponents;


        struct FObjectGenerationData
        {
            // Condition that enables a specific object
            Ptr<ASTOp> Condition;
        };
		TArray<FObjectGenerationData> CurrentObject;

		/** The key for generated tables is made of the source table and a parameter name. */
		struct FTableCacheKey
		{
			Ptr<const Table> Table;
			FString ParameterName;

			friend FORCEINLINE uint32 GetTypeHash(const FTableCacheKey& InKey)
			{
				uint32 KeyHash = ::GetTypeHash(InKey.Table.get());
				KeyHash = HashCombineFast(KeyHash, GetTypeHash(InKey.ParameterName));
				return KeyHash;
			}

			FORCEINLINE bool operator==(const FTableCacheKey& InKey) const
			{
				if (Table != InKey.Table) return false;
				if (ParameterName != InKey.ParameterName) return false;
				return true;
			}
		};
		TMap< FTableCacheKey, Ptr<ASTOp> > GeneratedTables;

		struct FConditionalExtensionDataOp
		{
			Ptr<ASTOp> Condition;
			Ptr<ASTOp> ExtensionDataOp;
			FString ExtensionDataName;
		};

		TArray<FConditionalExtensionDataOp> ConditionalExtensionDataOps;

		struct FGeneratedComponentCacheKey
		{
			Ptr<const Node> Node;
			FGenericGenerationOptions Options;

			friend FORCEINLINE uint32 GetTypeHash(const FGeneratedComponentCacheKey& InKey)
			{
				uint32 KeyHash = 0;
				KeyHash = HashCombineFast(KeyHash, ::GetTypeHash(InKey.Node.get()));
				KeyHash = HashCombineFast(KeyHash, GetTypeHash(InKey.Options));
				return KeyHash;
			}

			FORCEINLINE bool operator==(const FGeneratedComponentCacheKey& Other) const
			{
				return Node == Other.Node && Options == Other.Options;
			}
		};

		typedef TMap<FGeneratedComponentCacheKey, FGenericGenerationResult> GeneratedComponentMap;
		GeneratedComponentMap GeneratedComponents;

		void GenerateComponent(const FComponentGenerationOptions&, FGenericGenerationResult&, const NodeComponent*);
		void GenerateComponent_New(const FComponentGenerationOptions&, FGenericGenerationResult&, const NodeComponentNew*);
		void GenerateComponent_Switch(const FComponentGenerationOptions&, FGenericGenerationResult&, const NodeComponentSwitch*);
		void GenerateComponent_Variation(const FComponentGenerationOptions&, FGenericGenerationResult&, const NodeComponentVariation*);

        //-----------------------------------------------------------------------------------------
        //!
        Ptr<ASTOp> GenerateTableVariable(Ptr<const Node>, const FTableCacheKey&, bool bAddNoneOption, const FString& DefaultRowName);

        //!
        Ptr<ASTOp> GenerateMissingBoolCode(const TCHAR* strWhere, bool value, const void* errorContext );

        //!
		template<class NODE_TABLE, ETableColumnType TYPE, OP_TYPE OPTYPE, typename F>
		Ptr<ASTOp> GenerateTableSwitch( const NODE_TABLE& node, F&& GenerateOption );


		//-----------------------------------------------------------------------------------------
		// Images
		
		/** Options that affect the generation of images. It is like list of what required data we want while parsing down the image node graph. */
		struct FImageGenerationOptions : public FGenericGenerationOptions
		{
			/** */
			CompilerOptions::TextureLayoutStrategy ImageLayoutStrategy = CompilerOptions::TextureLayoutStrategy::None;

			/** If different than {0,0} this is the mandatory size of the image that needs to be generated. */
			UE::Math::TIntVector2<int32> RectSize = {0, 0};

			/** Layout block that we are trying to generate if any. */
			uint64 LayoutBlockId = FLayoutBlock::InvalidBlockId;
			Ptr<const Layout> LayoutToApply;

			friend FORCEINLINE uint32 GetTypeHash(const FImageGenerationOptions& InKey)
			{
				uint32 KeyHash = 0;
				KeyHash = HashCombineFast(KeyHash, ::GetTypeHash(InKey.ImageLayoutStrategy));
				KeyHash = HashCombineFast(KeyHash, GetTypeHash(InKey.RectSize));
				KeyHash = HashCombineFast(KeyHash, ::GetTypeHash(InKey.State));
				KeyHash = HashCombineFast(KeyHash, ::GetTypeHash(InKey.LayoutBlockId));
				KeyHash = HashCombineFast(KeyHash, ::GetTypeHash(InKey.LayoutToApply.get()));
				return KeyHash;
			}

			FORCEINLINE bool operator==(const FImageGenerationOptions& Other) const
			{
				return ImageLayoutStrategy == Other.ImageLayoutStrategy
					&&
					RectSize == Other.RectSize
					&&
					State == Other.State
					&&
					LayoutBlockId == Other.LayoutBlockId
					&&
					LayoutToApply == Other.LayoutToApply
					&&
					ActiveTags == Other.ActiveTags;
			}

		};

		/** */
		struct FImageGenerationResult
		{
			Ptr<ASTOp> op;
		};

		/** */
		struct FGeneratedImageCacheKey
		{
			NodePtrConst Node;
			FImageGenerationOptions Options;

			friend FORCEINLINE uint32 GetTypeHash(const FGeneratedImageCacheKey& InKey)
			{
				uint32 KeyHash = 0;
				KeyHash = HashCombineFast(KeyHash, ::GetTypeHash(InKey.Node.get()));
				KeyHash = HashCombineFast(KeyHash, GetTypeHash(InKey.Options));
				return KeyHash;
			}

			FORCEINLINE bool operator==(const FGeneratedImageCacheKey& Other) const
			{
				return Node == Other.Node && Options == Other.Options;
			}
		};

		typedef TMap<FGeneratedImageCacheKey, FImageGenerationResult> GeneratedImagesMap;
		GeneratedImagesMap GeneratedImages;

		void GenerateImage(const FImageGenerationOptions&, FImageGenerationResult& result, const NodeImagePtrConst& node);
		void GenerateImage_Constant(const FImageGenerationOptions&, FImageGenerationResult&, const NodeImageConstant*);
		void GenerateImage_Interpolate(const FImageGenerationOptions&, FImageGenerationResult&, const NodeImageInterpolate*);
		void GenerateImage_Saturate(const FImageGenerationOptions&, FImageGenerationResult&, const NodeImageSaturate*);
		void GenerateImage_Table(const FImageGenerationOptions&, FImageGenerationResult&, const NodeImageTable*);
		void GenerateImage_Swizzle(const FImageGenerationOptions&, FImageGenerationResult&, const NodeImageSwizzle*);
		void GenerateImage_ColourMap(const FImageGenerationOptions&, FImageGenerationResult&, const NodeImageColourMap*);
		void GenerateImage_Gradient(const FImageGenerationOptions&, FImageGenerationResult&, const NodeImageGradient*);
		void GenerateImage_Binarise(const FImageGenerationOptions&, FImageGenerationResult&, const NodeImageBinarise*);
		void GenerateImage_Luminance(const FImageGenerationOptions&, FImageGenerationResult&, const NodeImageLuminance*);
		void GenerateImage_Layer(const FImageGenerationOptions&, FImageGenerationResult&, const NodeImageLayer*);
		void GenerateImage_LayerColour(const FImageGenerationOptions&, FImageGenerationResult&, const NodeImageLayerColour*);
		void GenerateImage_Resize(const FImageGenerationOptions&, FImageGenerationResult&, const NodeImageResize*);
		void GenerateImage_PlainColour(const FImageGenerationOptions&, FImageGenerationResult&, const NodeImagePlainColour*);
		void GenerateImage_Project(const FImageGenerationOptions&, FImageGenerationResult&, const NodeImageProject*);
		void GenerateImage_Mipmap(const FImageGenerationOptions&, FImageGenerationResult&, const NodeImageMipmap*);
		void GenerateImage_Switch(const FImageGenerationOptions&, FImageGenerationResult&, const NodeImageSwitch*);
		void GenerateImage_Conditional(const FImageGenerationOptions&, FImageGenerationResult&, const NodeImageConditional*);
		void GenerateImage_Format(const FImageGenerationOptions&, FImageGenerationResult&, const NodeImageFormat*);
		void GenerateImage_Parameter(const FImageGenerationOptions&, FImageGenerationResult&, const NodeImageParameter*);
		void GenerateImage_MultiLayer(const FImageGenerationOptions&, FImageGenerationResult&, const NodeImageMultiLayer*);
		void GenerateImage_Invert(const FImageGenerationOptions&, FImageGenerationResult&, const NodeImageInvert*);
		void GenerateImage_Variation(const FImageGenerationOptions&, FImageGenerationResult&, const NodeImageVariation*);
		void GenerateImage_NormalComposite(const FImageGenerationOptions&, FImageGenerationResult&, const NodeImageNormalComposite*);
		void GenerateImage_Transform(const FImageGenerationOptions&, FImageGenerationResult&, const NodeImageTransform*);

		//!
		Ptr<Image> GenerateMissingImage(EImageFormat);

		//!
		Ptr<ASTOp> GenerateMissingImageCode(const TCHAR* strWhere, EImageFormat, const void* errorContext, const FImageGenerationOptions& Options);

		//!
		Ptr<ASTOp> GeneratePlainImageCode(const FVector4f& Color, const FImageGenerationOptions& Options);

		//!
		Ptr<ASTOp> GenerateImageFormat(Ptr<ASTOp>, EImageFormat);

		//!
		Ptr<ASTOp> GenerateImageUncompressed(Ptr<ASTOp>);

		//!
		Ptr<ASTOp> GenerateImageSize(Ptr<ASTOp>, UE::Math::TIntVector2<int32>);

		/** Evaluate if the image to generate is big enough to be split in separate operations and tiled afterwards. */
		Ptr<ASTOp> ApplyTiling(Ptr<ASTOp> Source, UE::Math::TIntVector2<int32> Size, EImageFormat Format);

		/** Generate a layout block-sized image with a mask including all pixels in the blocks defined in the patch node. */
		Ptr<Image> GenerateImageBlockPatchMask(const NodeModifierSurfaceEdit::FTexture&, FIntPoint GridSize, int32 BlockPixelsX, int32 BlockPixelsY, box<FIntVector2> RectInCells);

		/** Generate all the operations to apply the block patching on top of the BlockOp, and masking with PatchMask. */
		Ptr<ASTOp> GenerateImageBlockPatch(Ptr<ASTOp> BlockOp, const NodeModifierSurfaceEdit::FTexture&, Ptr<Image> PatchMask, Ptr<ASTOp> ConditionOp, const FImageGenerationOptions&);

        //-----------------------------------------------------------------------------------------
        // Meshes

		/** */
		struct FGeneratedLayout
		{
			Ptr<const Layout> Layout;
			Ptr<const NodeLayout> Source;

			FORCEINLINE bool operator==(const FGeneratedLayout& Other) const
			{
				return Layout == Other.Layout
					&& Source == Other.Source;
			}
		};

		/** Options that affect the generation of meshes. It is like list of what required data we want
		* while parsing down the mesh node graph.
		*/
		struct FMeshGenerationOptions : public FGenericGenerationOptions
		{
			/** The meshes at the leaves will need their own layout block data. */
			bool bLayouts = false;

			/** If true, Ensure UV Islands are not split between two or more blocks. UVs shared between multiple 
			* layout blocks will be clamped to fit the one with more vertices belonging to the UV island.
			* Mainly used to keep consistent layouts when reusing textures between LODs. */
			bool bClampUVIslands = false;

			/** If true, UVs will be normalized. Normalize UVs should be done in cases where we operate with Images and Layouts */
			bool bNormalizeUVs = false;

			/** If true, assign vertices without layout to the first block. */
			bool bEnsureAllVerticesHaveLayoutBlock = true;

			/** If this has something the layouts in constant meshes will be ignored, because
			* they are supposed to match some other set of layouts. If the array is empty, layouts
			* are generated normally.
			*/
			TArray<FGeneratedLayout> OverrideLayouts;

			/** Optional context to use instead of the node error context.
			 * Be careful since it is not used everywhere. Check usages before assigning a value to it. */
			TOptional<const void*> OverrideContext;
			
			friend FORCEINLINE uint32 GetTypeHash(const FMeshGenerationOptions& InKey)
			{
				uint32 KeyHash = 0;
				KeyHash = HashCombineFast(KeyHash, ::GetTypeHash(InKey.bLayouts));
				KeyHash = HashCombineFast(KeyHash, ::GetTypeHash(InKey.OverrideLayouts.Num()));
				KeyHash = HashCombineFast(KeyHash, ::GetTypeHash(InKey.State));
				KeyHash = HashCombineFast(KeyHash, ::GetTypeHash(InKey.ActiveTags.Num()));
				return KeyHash;
			}

			FORCEINLINE bool operator==(const FMeshGenerationOptions& Other) const
			{
				return State==Other.State 
					&& bLayouts==Other.bLayouts 
					&& bClampUVIslands == Other.bClampUVIslands && bNormalizeUVs == Other.bNormalizeUVs
					&& bEnsureAllVerticesHaveLayoutBlock == Other.bEnsureAllVerticesHaveLayoutBlock
					&& ActiveTags==Other.ActiveTags
					&& OverrideLayouts ==Other.OverrideLayouts;
			}
		};

		//! Store the results of the code generation of a mesh.
		struct FMeshGenerationResult
		{
			//! Mesh after all code tree is applied
			Ptr<ASTOp> MeshOp;

			//! Original base mesh before removes, morphs, etc.
			Ptr<ASTOp> BaseMeshOp;

			/** Generated node layouts with their own block ids. */
			TArray<FGeneratedLayout> GeneratedLayouts;

			TArray<Ptr<ASTOp>> LayoutOps;

			struct FExtraLayouts
			{
				/** Source node layouts to use with these extra mesh. They don't have block ids. */
				TArray<FGeneratedLayout> GeneratedLayouts;
				Ptr<ASTOp> Condition;
				Ptr<ASTOp> MeshFragment;
			};
			TArray< FExtraLayouts > ExtraMeshLayouts;
		};
		
		struct FGeneratedMeshCacheKey
		{
			NodePtrConst Node;
			FMeshGenerationOptions Options;

			friend FORCEINLINE uint32 GetTypeHash(const FGeneratedMeshCacheKey& InKey)
			{
				uint32 KeyHash = 0;
				KeyHash = HashCombineFast(KeyHash, ::GetTypeHash(InKey.Node.get()));
				KeyHash = HashCombineFast(KeyHash, GetTypeHash(InKey.Options));
				return KeyHash;
			}

			FORCEINLINE bool operator==(const FGeneratedMeshCacheKey& Other) const
			{
				return Node == Other.Node && Options == Other.Options;
			}
		};

        typedef TMap<FGeneratedMeshCacheKey,FMeshGenerationResult> GeneratedMeshMap;
        GeneratedMeshMap GeneratedMeshes;

		/** Store the mesh generation data for surfaces that we intend to share across LODs. 
		* The key is the SharedSurfaceId.
		*/
		TMap<int32, FMeshGenerationResult> SharedMeshOptionsMap;

		//! Map of layouts found in the code already generated. The map is from the source layout
		//! node to the generated layout.
		struct FGeneratedLayoutKey
		{
			Ptr<const NodeLayout> SourceLayout;
			uint32 MeshIdPrefix;

			friend FORCEINLINE uint32 GetTypeHash(const FGeneratedLayoutKey& InKey)
			{
				uint32 KeyHash = ::GetTypeHash(InKey.SourceLayout.get());
				KeyHash = HashCombineFast(KeyHash, GetTypeHash(InKey.MeshIdPrefix));
				return KeyHash;
			}

			FORCEINLINE bool operator==(const FGeneratedLayoutKey& Other) const = default;
		};
		TMap<FGeneratedLayoutKey, Ptr<const Layout>> GeneratedLayouts;

        void GenerateMesh(const FMeshGenerationOptions&, FMeshGenerationResult& result, const NodeMeshPtrConst&);
        void GenerateMesh_Constant(const FMeshGenerationOptions&, FMeshGenerationResult&, const NodeMeshConstant* );
        void GenerateMesh_Format(const FMeshGenerationOptions&, FMeshGenerationResult&, const NodeMeshFormat* );
        void GenerateMesh_Morph(const FMeshGenerationOptions&, FMeshGenerationResult&, const NodeMeshMorph* );
        void GenerateMesh_MakeMorph(const FMeshGenerationOptions&, FMeshGenerationResult&, const NodeMeshMakeMorph* );
        void GenerateMesh_Fragment(const FMeshGenerationOptions&, FMeshGenerationResult&, const NodeMeshFragment* );
        void GenerateMesh_Interpolate(const FMeshGenerationOptions&, FMeshGenerationResult&, const NodeMeshInterpolate* );
        void GenerateMesh_Switch(const FMeshGenerationOptions&, FMeshGenerationResult&, const NodeMeshSwitch* );
        void GenerateMesh_Transform(const FMeshGenerationOptions&, FMeshGenerationResult&, const NodeMeshTransform* );
        void GenerateMesh_ClipMorphPlane(const FMeshGenerationOptions&, FMeshGenerationResult&, const NodeMeshClipMorphPlane* );
        void GenerateMesh_ClipWithMesh(const FMeshGenerationOptions&, FMeshGenerationResult&, const NodeMeshClipWithMesh* );
        void GenerateMesh_ApplyPose(const FMeshGenerationOptions&, FMeshGenerationResult&, const NodeMeshApplyPose* );
        void GenerateMesh_Variation(const FMeshGenerationOptions&, FMeshGenerationResult&, const NodeMeshVariation* );
		void GenerateMesh_Table(const FMeshGenerationOptions&, FMeshGenerationResult&, const NodeMeshTable*);
		void GenerateMesh_GeometryOperation(const FMeshGenerationOptions&, FMeshGenerationResult&, const NodeMeshGeometryOperation*);
		void GenerateMesh_Reshape(const FMeshGenerationOptions&, FMeshGenerationResult&, const NodeMeshReshape*);
		void GenerateMesh_ClipDeform(const FMeshGenerationOptions&, FMeshGenerationResult&, const NodeMeshClipDeform*);

		//-----------------------------------------------------------------------------------------
		void PrepareMeshForLayout(const FGeneratedLayout&, Ptr<Mesh>,
			int32 currentLayoutChannel,
			const void* errorContext,
			const FMeshGenerationOptions&,
			bool bUseAbsoluteBlockIds);

		//!
		Ptr<const Layout> GenerateLayout(Ptr<const NodeLayout> SourceLayout, uint32 MeshIDPrefix);

		struct FExtensionDataGenerationResult
		{
			Ptr<ASTOp> Op;
		};

		typedef const NodeExtensionData* FGeneratedExtensionDataCacheKey;
		typedef TMap<FGeneratedExtensionDataCacheKey, FExtensionDataGenerationResult> FGeneratedExtensionDataMap;
		FGeneratedExtensionDataMap GeneratedExtensionData;

		void GenerateExtensionData(FExtensionDataGenerationResult& OutResult, const FGenericGenerationOptions&, const Ptr<const NodeExtensionData>&);
		void GenerateExtensionData_Constant(FExtensionDataGenerationResult& OutResult, const FGenericGenerationOptions&, const class NodeExtensionDataConstant*);
		void GenerateExtensionData_Switch(FExtensionDataGenerationResult& OutResult, const FGenericGenerationOptions&, const class NodeExtensionDataSwitch*);
		void GenerateExtensionData_Variation(FExtensionDataGenerationResult& OutResult, const FGenericGenerationOptions&, const class NodeExtensionDataVariation*);
		Ptr<ASTOp> GenerateMissingExtensionDataCode(const TCHAR* StrWhere, const void* ErrorContext);

        //-----------------------------------------------------------------------------------------
        // Projectors
        struct FProjectorGenerationResult
        {
            Ptr<ASTOp> op;
            PROJECTOR_TYPE type;
        };

        typedef TMap<FGeneratedCacheKey,FProjectorGenerationResult> FGeneratedProjectorsMap;
        FGeneratedProjectorsMap GeneratedProjectors;

        void GenerateProjector( FProjectorGenerationResult&, const FGenericGenerationOptions&, const Ptr<const NodeProjector>& );
        void GenerateProjector_Constant( FProjectorGenerationResult&, const FGenericGenerationOptions&, const Ptr<const NodeProjectorConstant>& );
        void GenerateProjector_Parameter( FProjectorGenerationResult&, const FGenericGenerationOptions&, const Ptr<const NodeProjectorParameter>& );
        void GenerateMissingProjectorCode( FProjectorGenerationResult&, const void* errorContext );

		//-----------------------------------------------------------------------------------------
		// Bools
		struct FBoolGenerationResult
		{
			Ptr<ASTOp> op;
		};

		typedef TMap<FGeneratedCacheKey, FBoolGenerationResult> FGeneratedBoolsMap;
		FGeneratedBoolsMap GeneratedBools;

		void GenerateBool(FBoolGenerationResult&, const FGenericGenerationOptions&, const Ptr<const NodeBool>&);
		void GenerateBool_Constant(FBoolGenerationResult&, const FGenericGenerationOptions&, const Ptr<const NodeBoolConstant>&);
		void GenerateBool_Parameter(FBoolGenerationResult&, const FGenericGenerationOptions&, const Ptr<const NodeBoolParameter>&);
		void GenerateBool_Not(FBoolGenerationResult&, const FGenericGenerationOptions&, const Ptr<const NodeBoolNot>&);
		void GenerateBool_And(FBoolGenerationResult&, const FGenericGenerationOptions&, const Ptr<const NodeBoolAnd>&);

		//-----------------------------------------------------------------------------------------
		// Scalars
		struct FScalarGenerationResult
		{
			Ptr<ASTOp> op;
		};

		typedef TMap<FGeneratedCacheKey, FScalarGenerationResult> FGeneratedScalarsMap;
		FGeneratedScalarsMap GeneratedScalars;

		void GenerateScalar(FScalarGenerationResult&, const FGenericGenerationOptions&, const Ptr<const NodeScalar>&);
		void GenerateScalar_Constant(FScalarGenerationResult&, const FGenericGenerationOptions&, const Ptr<const NodeScalarConstant>&);
		void GenerateScalar_Parameter(FScalarGenerationResult&, const FGenericGenerationOptions&, const Ptr<const NodeScalarParameter>&);
		void GenerateScalar_Switch(FScalarGenerationResult&, const FGenericGenerationOptions&, const Ptr<const NodeScalarSwitch>&);
		void GenerateScalar_EnumParameter(FScalarGenerationResult&, const FGenericGenerationOptions&, const Ptr<const NodeScalarEnumParameter>&);
		void GenerateScalar_Curve(FScalarGenerationResult&, const FGenericGenerationOptions&, const Ptr<const NodeScalarCurve>&);
		void GenerateScalar_Arithmetic(FScalarGenerationResult&, const FGenericGenerationOptions&, const Ptr<const NodeScalarArithmeticOperation>&);
		void GenerateScalar_Variation(FScalarGenerationResult&, const FGenericGenerationOptions&, const Ptr<const NodeScalarVariation>&);
		void GenerateScalar_Table(FScalarGenerationResult&, const FGenericGenerationOptions&, const Ptr<const NodeScalarTable>&);
		Ptr<ASTOp> GenerateMissingScalarCode(const TCHAR* strWhere, float value, const void* errorContext);

		//-----------------------------------------------------------------------------------------
		// Colors
		struct FColorGenerationResult
		{
			Ptr<ASTOp> op;
		};

		typedef TMap<FGeneratedCacheKey, FColorGenerationResult> FGeneratedColorsMap;
		FGeneratedColorsMap GeneratedColors;

		void GenerateColor(FColorGenerationResult&, const FGenericGenerationOptions&, const Ptr<const NodeColour>&);
		void GenerateColor_Constant(FColorGenerationResult&, const FGenericGenerationOptions&, const Ptr<const NodeColourConstant>&);
		void GenerateColor_Parameter(FColorGenerationResult&, const FGenericGenerationOptions&, const Ptr<const NodeColourParameter>&);
		void GenerateColor_Switch(FColorGenerationResult&, const FGenericGenerationOptions&, const Ptr<const NodeColourSwitch>&);
		void GenerateColor_SampleImage(FColorGenerationResult&, const FGenericGenerationOptions&, const Ptr<const NodeColourSampleImage>&);
		void GenerateColor_FromScalars(FColorGenerationResult&, const FGenericGenerationOptions&, const Ptr<const NodeColourFromScalars>&);
		void GenerateColor_Arithmetic(FColorGenerationResult&, const FGenericGenerationOptions&, const Ptr<const NodeColourArithmeticOperation>&);
		void GenerateColor_Variation(FColorGenerationResult&, const FGenericGenerationOptions&, const Ptr<const NodeColourVariation>&);
		void GenerateColor_Table(FColorGenerationResult&, const FGenericGenerationOptions&, const Ptr<const NodeColourTable>&);
		Ptr<ASTOp> GenerateMissingColourCode(const TCHAR* strWhere, const void* errorContext);

		//-----------------------------------------------------------------------------------------
		// Strings
		struct FStringGenerationResult
		{
			Ptr<ASTOp> op;
		};

		typedef TMap<FGeneratedCacheKey, FStringGenerationResult> FGeneratedStringsMap;
		FGeneratedStringsMap GeneratedStrings;

		void GenerateString(FStringGenerationResult&, const FGenericGenerationOptions&, const Ptr<const NodeString>&);
		void GenerateString_Constant(FStringGenerationResult&, const FGenericGenerationOptions& Options, const Ptr<const NodeStringConstant>&);
		void GenerateString_Parameter(FStringGenerationResult&, const FGenericGenerationOptions& Options, const Ptr<const NodeStringParameter>&);

    	//-----------------------------------------------------------------------------------------
    	// Transforms
    	struct FMatrixGenerationResult
    	{
    		Ptr<ASTOp> op;
    	};

    	typedef TMap<FGeneratedCacheKey, FMatrixGenerationResult> FGeneratedMatrixMap;
    	FGeneratedMatrixMap GeneratedMatrices;

    	void GenerateMatrix(FMatrixGenerationResult&, const FGenericGenerationOptions&, const Ptr<const NodeMatrix>&);
    	void GenerateMatrix_Constant(FMatrixGenerationResult&, const FGenericGenerationOptions&, const Ptr<const NodeMatrixConstant>&);
    	void GenerateMatrix_Parameter(FMatrixGenerationResult&, const FGenericGenerationOptions&, const Ptr<const NodeMatrixParameter>&);

        //-----------------------------------------------------------------------------------------
        // Ranges
        struct FRangeGenerationResult
        {
            //
            Ptr<ASTOp> sizeOp;

            //
            FString rangeName;

            //
			FString rangeUID;
        };

        typedef TMap<FGeneratedCacheKey,FRangeGenerationResult> FGeneratedRangeMap;
        FGeneratedRangeMap GeneratedRanges;

        void GenerateRange(FRangeGenerationResult&, const FGenericGenerationOptions&, Ptr<const NodeRange>);


        //-----------------------------------------------------------------------------------------
        struct FSurfaceGenerationResult
        {
            Ptr<ASTOp> surfaceOp;
        };

        void GenerateSurface( FSurfaceGenerationResult&, const FSurfaceGenerationOptions&, Ptr<const NodeSurfaceNew> );

		//-----------------------------------------------------------------------------------------
		// Default Table Parameters
		Ptr<ASTOp> GenerateDefaultTableValue(ETableColumnType NodeType);


		//-----------------------------------------------------------------------------------------

		struct FLayoutBlockDesc
		{
			EImageFormat FinalFormat = EImageFormat::IF_NONE;
			int32 BlockPixelsX = 0;
			int32 BlockPixelsY = 0;
			bool bBlocksHaveMips = false;
		};

		void UpdateLayoutBlockDesc(FLayoutBlockDesc& Out, FImageDesc BlockDesc, FIntVector2 LayoutCellSize);

		// Get the modifiers that have to be applied to elements with a specific tag.
		void GetModifiersFor(const TArray<FString>& SurfaceTags, bool bModifiersForBeforeOperations, TArray<FirstPassGenerator::FModifier>& OutModifiers);

		// Used to avoid recursion when generating modifiers.
		TArray<FirstPassGenerator::FModifier> ModifiersToIgnore;

		// Apply the required mesh modifiers to the given operation.
		Ptr<ASTOp> ApplyMeshModifiers(
			const TArray<FirstPassGenerator::FModifier>&,
			const FMeshGenerationOptions&, 
			FMeshGenerationResult& BaseResults, 
			const FMeshGenerationResult* SharedSurfaceResults, 
			const void* ErrorContext,
			const NodeMeshConstant* OriginalMeshNode);

		Ptr<ASTOp> ApplyImageBlockModifiers(
			const TArray<FirstPassGenerator::FModifier>&, 
			const FImageGenerationOptions&, 
			Ptr<ASTOp> BaseImageOp,
			const NodeSurfaceNew::FImageData& ImageData,
			FIntPoint GridSize,
			const FLayoutBlockDesc& LayoutBlockDesc,
			box< FIntVector2 > RectInCells,
			const void* ErrorContext);

		Ptr<ASTOp> ApplyImageExtendModifiers(
			const TArray<FirstPassGenerator::FModifier>&,
			const FGenericGenerationOptions& Options, 
			const FMeshGenerationResult& BaseMeshResults,
			Ptr<ASTOp> ImageAd, 
			CompilerOptions::TextureLayoutStrategy ImageLayoutStrategy, 
			int32 LayoutIndex, 
			const NodeSurfaceNew::FImageData& ImageData,
			FIntPoint GridSize,
			CodeGenerator::FLayoutBlockDesc& InOutLayoutBlockDesc,
			const void* ModifiedNodeErrorContext);

		void CheckModifiersForSurface(const NodeSurfaceNew&, const TArray<FirstPassGenerator::FModifier>&);

    };

	
    //---------------------------------------------------------------------------------------------
    template<class NODE_TABLE, ETableColumnType TYPE, OP_TYPE OPTYPE, typename F>
    Ptr<ASTOp> CodeGenerator::GenerateTableSwitch( const NODE_TABLE& node, F&& GenerateOption )
    {
        Ptr<const Table> NodeTable = node.Table;
        Ptr<ASTOp> Variable;

		FTableCacheKey CacheKey = FTableCacheKey{ node.Table, node.ParameterName };
        Ptr<ASTOp>* it = GeneratedTables.Find( CacheKey );
        if ( it )
        {
            Variable = *it;
        }

        if ( !Variable)
        {
            // Create the table variable expression
            Variable = GenerateTableVariable( &node, CacheKey, node.bNoneOption, node.DefaultRowName);

            GeneratedTables.Add(CacheKey, Variable );
        }

		int32 NumRows = NodeTable->GetPrivate()->Rows.Num();

        // Verify that the table column is the right type
        int32 ColIndex = NodeTable->FindColumn( node.ColumnName );

		if (NumRows == 0)
		{
			ErrorLog->GetPrivate()->Add("The table has no rows.", ELMT_ERROR, node.GetMessageContext());
			return nullptr;
		}
        else if (ColIndex < 0)
        {
            ErrorLog->GetPrivate()->Add("Table column not found.", ELMT_ERROR, node.GetMessageContext());
            return nullptr;
        }

        if (NodeTable->GetPrivate()->Columns[ ColIndex ].Type != TYPE )
        {
            ErrorLog->GetPrivate()->Add("Table column type is not the right type.", ELMT_ERROR, node.GetMessageContext());
            return nullptr;
        }

        // Create the switch to cover all the options
        Ptr<ASTOp> lastSwitch;
        Ptr<ASTOpSwitch> SwitchOp = new ASTOpSwitch();
		SwitchOp->type = OPTYPE;
		SwitchOp->variable = Variable;
		SwitchOp->def = GenerateDefaultTableValue(TYPE);

		for (int32 RowIndex = 0; RowIndex < NumRows; ++RowIndex)
        {
            check(RowIndex <= 0xFFFF);
			auto Condition = (uint16)RowIndex;

            Ptr<ASTOp> Branch = GenerateOption( node, ColIndex, (int)RowIndex, ErrorLog.get() );

			if (Branch || TYPE != ETableColumnType::Mesh)
			{
				SwitchOp->cases.Add(ASTOpSwitch::FCase(Condition, SwitchOp, Branch));
			}
        }

        return SwitchOp;
    }
}
