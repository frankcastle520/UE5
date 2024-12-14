// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/MutableMemory.h"
#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuT/AST.h"
#include "MuT/Compiler.h"
#include "MuT/Node.h"
#include "MuT/NodeComponentEdit.h"
#include "MuT/NodeComponentNew.h"
#include "MuT/NodeComponentSwitch.h"
#include "MuT/NodeComponentVariation.h"
#include "MuT/NodeLOD.h"
#include "MuT/NodeModifier.h"
#include "MuT/NodeModifierMeshClipDeform.h"
#include "MuT/NodeModifierMeshClipMorphPlane.h"
#include "MuT/NodeModifierMeshClipWithMesh.h"
#include "MuT/NodeModifierMeshClipWithUVMask.h"
#include "MuT/NodeModifierSurfaceEdit.h"
#include "MuT/NodeObjectGroup.h"
#include "MuT/NodeObjectNew.h"
#include "MuT/NodeSurfaceNew.h"
#include "MuT/NodeSurfaceVariation.h"
#include "MuT/NodeSurfaceSwitch.h"
#include "MuT/ASTOpParameter.h"
#include "MuT/ErrorLog.h"

namespace mu
{
	class Layout;
	struct FObjectState;

	/** First pass of the code generation process.
	 * It collects data about the object hierarchy, the conditions for each object and the global modifiers.
	 */
	class FirstPassGenerator
	{
	public:

		FirstPassGenerator();

        void Generate(Ptr<ErrorLog>, const Node* Root, bool bIgnoreStates, class CodeGenerator*);

	private:

		void Generate_Generic(const Node*);
		void Generate_Modifier(const NodeModifier*);
		void Generate_SurfaceNew(const NodeSurfaceNew*);
		void Generate_SurfaceSwitch(const NodeSurfaceSwitch*);
		void Generate_SurfaceVariation(const NodeSurfaceVariation*);
		void Generate_ComponentNew(const NodeComponentNew*);
		void Generate_ComponentEdit(const NodeComponentEdit*);
		void Generate_ComponentSwitch(const NodeComponentSwitch*);
		void Generate_ComponentVariation(const NodeComponentVariation*);
		void Generate_LOD(const NodeLOD*);
		void Generate_ObjectNew(const NodeObjectNew*);
		void Generate_ObjectGroup(const NodeObjectGroup*);

	public:

		// Results
		//-------------------------

		//! Store the conditions that will enable or disable every object
		struct FObject
		{
			const NodeObjectNew* Node = nullptr;
            Ptr<ASTOp> Condition;
		};
		TArray<FObject> Objects;

        //! Type used to represent the activation conditions regarding states
        //! This is the state mask for the states in which this surface must be added. If it
        //! is empty it means the surface is valid for all states. Otherwise it is only valid
        //! for the states whose index is true.
        using StateCondition = TArray<uint8>;

		/** Store information about every component found. */
		struct FComponent
		{
			/** Main component node. */
			const NodeComponentNew* Component = nullptr;

			// List of tags that are required for the presence of this component
			TArray<FString> PositiveTags;

			// List of tags that block the presence of this component
			TArray<FString> NegativeTags;

			// This conditions is the condition of the object defining this surface which may not
			// be the parent object where this surface will be added.
			Ptr<ASTOp> ObjectCondition;

			// Condition for this component to be added.
			// This is filled in CodeGenerator_SecondPass.
			Ptr<ASTOp> ComponentCondition;
		};
		TArray<FComponent> Components;

		/** Store information about every surface including
		* - the component it may be added to
		* - the conditions that will enable or disable it
		* - all edit operators
        * A surface may have different versions depending on the different parents and conditions it is reached with.
		*/
		struct FSurface
		{
            Ptr<const NodeSurfaceNew> Node;

			/** Parent Component where this surface will be added.It may be different from the
			* Component that defined it (if it was an edit component).
			*/
            const NodeComponentNew* Component = nullptr;

			int32 LOD = 0;

            // List of tags that are required for the presence of this surface
			TArray<FString> PositiveTags;

            // List of tags that block the presence of this surface
			TArray<FString> NegativeTags;

			// This conditions is the condition of the object defining this surface which may not
			// be the parent object where this surface will be added.
            Ptr<ASTOp> ObjectCondition;

            // This is filled in the first pass.
            StateCondition StateCondition;

            // Combined condition for the surface and the object conditions.
            // This is filled in CodeGenerator_SecondPass.
            Ptr<ASTOp> FinalCondition;

            // This is filled in the final code generation pass
            Ptr<ASTOp> ResultSurfaceOp;
            Ptr<ASTOp> ResultMeshOp;
        };
		TArray<FSurface> Surfaces;

		//! Store the conditions that enable every modifier.
		struct FModifier
		{
            const NodeModifier* Node = nullptr;

            // List of tags that are required to apply this modifier
			TArray<FString> PositiveTags;

            // List of tags that block the activation of this modifier
			TArray<FString> NegativeTags;

            // This conditions is the condition of the object defining this modifier which may not
            // be the parent object where this surface will be added.
            Ptr<ASTOp> ObjectCondition;

			// Combined condition for the this modifier and the object conditions.
			// This is filled in CodeGenerator_SecondPass.
			Ptr<ASTOp> FinalCondition;

            // This is filled in CodeGenerator_SecondPass.
            StateCondition StateCondition;
						
			bool operator==(const FModifier&) const = default;
        };
		TArray<FModifier> Modifiers;

		/** Info about all found tags. */
		struct FTag
		{
			FString Tag;

            /** Surfaces that activate the tag.These are indices to the FirstPassGenerator::Surfaces array.*/
			TArray<int32> Surfaces;

            /** Modifiers that activate the tag. The index refers to the FirstPassGenerator::Modifiers. */
			TArray<int32> Modifiers;

            /** This conditions is the condition for this tag to be enabled considering no other condition. 
			* This is filled in CodeGenerator_SecondPass. 
			*/
            Ptr<ASTOp> GenericCondition;
        };
        TArray<FTag> Tags;

        /** Accumulate the model states found while generating code, with their generated root nodes. */
        typedef TArray< TPair<FObjectState, const Node*> > StateList;
        StateList States;

		/** Parameters added for every node. */
		TMap< Ptr<const Node>, Ptr<ASTOpParameter> > ParameterNodes;

	private:

        struct FConditionContext
        {
            Ptr<ASTOp> ObjectCondition;
        };
		TArray< FConditionContext > CurrentCondition;

        //!
		TArray< StateCondition > CurrentStateCondition;

		/** When processing surfaces, this is the parent component the surfaces may be added to. */
        const NodeComponentNew* CurrentComponent = nullptr;

        //! Current relevant tags so far. Used during traversal.
		TArray<FString> CurrentPositiveTags;
		TArray<FString> CurrentNegativeTags;

		//** Index of the LOD we are processing. */
        int32 CurrentLOD = -1;

		/** Non-owned reference to main code generator. */
		CodeGenerator* Generator = nullptr;

        //!
        Ptr<ErrorLog> ErrorLog;
	};

}
