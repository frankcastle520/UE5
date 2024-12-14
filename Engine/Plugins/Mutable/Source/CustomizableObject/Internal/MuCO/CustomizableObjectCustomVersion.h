// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"

struct FGuid;

// Custom serialization version for assets/classes in the CustomizableObject and CustomizableObjectEditor modules
struct CUSTOMIZABLEOBJECT_API FCustomizableObjectCustomVersion
{
	enum Type
	{
		// Before any version changes were made in the plugin
		BeforeCustomVersionWasAdded = 0,

		FixBlueprintPinsUseRealNumbers,

		NodeExposePinOnNameChangedDelegateAdded,

		GroupProjectorPinTypeAdded,

		AutomaticNodeMaterial,
		
		AutomaticNodeMaterialPerformance,

		LayoutClassAdded,

		AutomaticNodeMaterialPerformanceBug,

		PinsNamesImageToTexture,

		BugPinsSwitch,

		PostLoadToCustomVersion, // Wrapped old backwards compatible code that was located at PostLoads inside this custom version.

		AutomaticNodeMaterialUXImprovements,

		ExtendMaterialOnlyMutableModeParameters,

		ExtendMaterialOnlyMutableModeParametersBug,
		
		ExtendMaterialRemoveImages,
		
		EditMaterialOnlyMutableModeParameters, // Force refresh to avoid having Images which are not in Mutable mode.

		GroupProjectorIntToScalarIndex,

		FixBlueprintPinsUseRealNumbersAgain,

		NodeMaterialAddTablePin,

		MorphMaterialAddFactorPin,

		NodeSkeletalMeshCorruptedPinRef,

		CustomizableObjectInstanceDescriptor,
		
		DescriptorBuildParameterDecorations,

		DescriptorMultilayerProjectors,

		DeformSkeletonOptionsAdded,

		GroupProjectorImagePin,

		GroupProjectorImagePinRemoved,

		StateTextureCompressionStrategyEnum,

		AddedTableNodesTextureMode,
		
		ConvertAnimationSlotToFName,

		RemovedParameterDecorations,

		AutomaticNodeSkeletalMesh,

		AutomaticNodeSkeletalMeshPinDataOuter,

		AutomaticNodeSkeletalMeshPinDataUProperty,

		EditMaterialMaskPinDesync, // MTBL-979
		
		IgnoreDisabledSections,

		SkeletalMeshNodeDefaultPinWithoutPinData,
		
		AddedTableMaterialSwitch,

		FixPinsNamesImageToTexture2,

		MeshReshapeVertexColorUsageSelection,

		DeduplicateNodeVariant,

		TextureCompressionEnum,

		NodeTextureParameterDefaultToReferenceValue,

		NodeVariationSerializationIssue, // The parent class and child classes had a member with the same name. Unreal was serialized the member from the child class but deserialized it to the parent class.

		RegenerateNodeObjectsIds,

		NodeMaterialPinDataImageDetails,

		CustomizableObjectStateHasSeparateNeverStreamFlag,

		CustomizableObjectNodeHasSeparateNeverStreamFlag,

		AddedColumnIdDataToTableNodePins,

		AddedAnyTextureTypeToPassThroughTextures,

		ProjectorNodesDefaultValueFix,

		StateUIMetadata,

		AddedRenameOptionToParameterNodes,

		CompilationOptions,

		NodeTablePinViewer,

		NewComponentOptions,

		NodeMaterialTypedImagePins,

		FixedMultilayerMaterialIds,

		UseUVRects, // Instead of pointing at layout blocks in the edited parent, define custom absolute UV layout rects.

		AddModifierPin, // Move the modifiers output pin from Material to Modifier

		CorrectlySerializeTableToParamNames,

		MovedCompatibilityFromPostBackwardsCompatibleFixup,

		FixModifierPin, 

		ConvertEditAndExtendToModifiers,

		NodeComponentMesh, // Moved LOD pins from NodeObject to NodeComponentMesh.

		MergeNodeComponents,
		
		UnifyRequiredTags,

		MaterialPinsRename, // Moved from "Material" to "Mesh Section"

		MoveLayoutToNodeSkeletalMesh, // Move the layouts from the NodeLayout to the NodeSkeletalMesh

		RemoveNodeLayout,

		ComponentsArray,

		FixMaterialPinsRename,

		ModifierClipWithMeshCleanup,

		RealTimeMorphTargetOverrideDataStructureRework,
		
		FixAutomaticBlocksStrategyLegacyNodes,

		SnapToBoneComponentIndexToName,

		// -----<new versions can be added above this line>-------------------------------------------------
		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};

	// The GUID for this custom version number
	const static FGuid GUID;

	private:
		FCustomizableObjectCustomVersion() {}
};
