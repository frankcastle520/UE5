// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Containers/Map.h"
#include "MetasoundEnvironment.h"
#include "Misc/Guid.h"
#include "Misc/ScopeLock.h"
#include "Templates/SharedPointer.h"

#ifndef UE_METASOUNDRENDERCOST_TRACK_NODE_HIERARCHY 
#define UE_METASOUNDRENDERCOST_TRACK_NODE_HIERARCHY (!UE_BUILD_SHIPPING)
#endif

namespace Metasound
{
	class FNodeRenderCost;

	/** FGraphRenderCost represents the accumulated render cost of a graph. Individual
	 * nodes in a graph can report their render cost through an FNodeRenderCost.
	 *
	 * The render cost of each node is added together to determine the graph's render
	 * cost. 
	 */
	class METASOUNDGRAPHCORE_API FGraphRenderCost : public TSharedFromThis<FGraphRenderCost>
	{
		// Private token to enforce creation of shared reference. 
		enum EPrivateToken { PrivateToken };
		friend class FNodeRenderCost;

	public:

		FGraphRenderCost(EPrivateToken InToken);

		static TSharedRef<FGraphRenderCost> MakeGraphRenderCost();

		/* Add a node to the graph's render cost.
		 *
		 * @param InNodeInstanceID - ID of the node to add.
		 * @param InEnv - The environment used to create the node. 
		 *
		 * @return FNodeRenderCost object for reporting the render cost of individual nodes. */
		FNodeRenderCost AddNode(const FGuid& InNodeInstanceID, const FMetasoundEnvironment& InEnv);

		/** Reset the individual node render costs to zero. */
		void ResetNodeRenderCosts();

		/** Adds all the individual node render costs and returns the result. */
		float ComputeGraphRenderCost() const;
		
	private:
		void SetNodeRenderCost(int32 InNodeIndex, float InRenderCost);

		TArray<float> NodeCosts;
		void AddNodeHierarchy(const FGuid& InNodeInstanceID, const FMetasoundEnvironment& InEnv);
#if UE_METASOUNDRENDERCOST_TRACK_NODE_HIERARCHY
		TArray<TArray<FGuid>> NodeHierarchies;
#endif
	};

	/** FNodeRenderCost allows individual nodes to report their render cost. 
	 * 
	 * FNodeRenderCost should be created with FGraphRenderCost::AddNode(...)
	 */
	class METASOUNDGRAPHCORE_API FNodeRenderCost
	{
		friend class FGraphRenderCost;
		FNodeRenderCost(int32 InNodeIndex, TSharedRef<FGraphRenderCost> InGraphRenderCost);
	public:
		/** The default constructor is provided for convenience, but creating a
		 * FNodeRenderCost with the default constructor will allow any method for
		 * reading or accumulating a nodes render cost. Instead, FNodeRenderCost
		 * should be created with FGraphRenderCost::AddNode(...) so that it is 
		 * associated and aggregated within a graph.
		 */
		FNodeRenderCost() = default;

		/** Set the render cost of this node. */
		void SetRenderCost(float InCost);

	private:
		int32 NodeIndex = INDEX_NONE;
		TSharedPtr<FGraphRenderCost> GraphRenderCost;
	};
}

