// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Containers/Array.h"

namespace uLang
{
template<typename InElementType>
struct TDiGraphTopologicalIterator_Base;

template<typename InElementType>
class TDirectedGraph
{
public:
    using NodeId = int32_t;
    constexpr static NodeId InvalidNodeId = uLang::IndexNone;

    NodeId AddNode(const InElementType& Item)
    {
        return _Nodes.Emplace(Item);
    }

    NodeId AddNode(InElementType&& Item)
    {
        return _Nodes.Emplace(uLang::MoveIfPossible(Item));
    }

    bool AddDirectedEdge(const NodeId From, const NodeId To)
    {
        if (ULANG_ENSUREF(_Nodes.IsValidIndex(From) && _Nodes.IsValidIndex(To), "Invalid edge indices."))
        {
            _Nodes[From]._Successors.Add(To);
            ++_Nodes[To]._InDegree;
            return true;
        }
        return false;
    }

    bool AddDirectedEdgeUnique(const NodeId From, const NodeId To)
    {
        if (ULANG_ENSUREF(_Nodes.IsValidIndex(From) && _Nodes.IsValidIndex(To), "Invalid edge indices."))
        {
            TArrayG<int32_t, TInlineElementAllocator<4>>& FromSuccessors = _Nodes[From]._Successors;
            int32_t PrevNumSuccessors = FromSuccessors.Num();
            FromSuccessors.AddUnique(To);
            if (FromSuccessors.Num() > PrevNumSuccessors)
            {
                ++_Nodes[To]._InDegree;
                return true;
            }
        }
        return false;
    }

    void Reserve(int32_t NodesSlack, int32_t EdgesSlack = 0)
    {
        _Nodes.Reserve(NodesSlack);
    }

    void Empty(int32_t NodesSlack = 0, int32_t EdgesSlack = 0)
    {
        _Nodes.Empty(NodesSlack);
    }

    int32_t NumNodes() const
    {
        return _Nodes.Num();
    }

    const InElementType& operator[](NodeId Index) const
    {
        return _Nodes[Index]._Item;
    }

    InElementType& operator[](NodeId Index)
    {
        return _Nodes[Index]._Item;
    }

    bool TopologicalSort(TArray<InElementType>& OutItems) const;
    bool TopologicalSort_Pointers(TArray<const InElementType*>& OutItems) const;
    bool TopologicalSort_Pointers(TArray<InElementType*>& OutItems);

    TArray<TArray<NodeId>> FindCycles() const;

private:
    friend struct TDiGraphTopologicalIterator_Base<InElementType>;

    struct SNode
    {
        SNode(const InElementType& Item)  : _Item(Item) {}
        SNode(InElementType&& Item) : _Item(Move(Item)) {}

        InElementType _Item;

        // Tracks how many incoming edges this node has
        int32_t _InDegree = 0;
        // Node indices for outgoing edges
        TArrayG<int32_t, TInlineElementAllocator<4>> _Successors;
    };
    TArray<SNode> _Nodes;
};

/** Base functionality for both const and non-const directed-graph iterators. */
template<typename InElementType>
struct TDiGraphTopologicalIterator_Base
{
    using DiGraphType = TDirectedGraph<InElementType>;

    TDiGraphTopologicalIterator_Base(const DiGraphType& InContainer)
    {
        Reset(InContainer);
    }

    explicit operator bool() const
    {
        return !_NodesToVisit.IsEmpty();
    }

    /** This automatically increments the iterator to the next element. You don't need to increment the iterator after calling this. */
    typename DiGraphType::NodeId SkipCurrent()
    {
        return _NodesToVisit.Pop(/*bAllowShrinking =*/false);
    }

    void Enqueue(TArray<typename DiGraphType::NodeId>&& NodesToVisit)
    {
        _NodesToVisit.Append(NodesToVisit);
    }

protected:
    void Increment(const DiGraphType& Container)
    {
        if (_NodesToVisit.Num() > 0)
        {
            const int32_t NodeIndex = _NodesToVisit.Pop(/*bAllowShrinking =*/false);

            const typename DiGraphType::SNode& Node = Container._Nodes[NodeIndex];

            for (int32_t SuccessorIndex : Node._Successors)
            {
                // If we've visited this Successor node for each one of it's incoming edges
                if (++_VisitCounters[SuccessorIndex] == Container._Nodes[SuccessorIndex]._InDegree)
                {
                    // We're ready to process it in order
                    _NodesToVisit.Push(SuccessorIndex);
                }
            }
        }
    }

    typename DiGraphType::NodeId CurrentNodeIndex() const
    {
        return _NodesToVisit.Top();
    }

    void Reset(const DiGraphType& Container)
    {
        _NodesToVisit.Empty(Container._Nodes.Num());
        // Find all the head nodes (ones without incoming edges)
        for (int32_t NodeIndex = 0; NodeIndex < Container._Nodes.Num(); ++NodeIndex)
        {
            if (Container._Nodes[NodeIndex]._InDegree == 0)
            {
                _NodesToVisit.Add(NodeIndex);
            }
        }

        _VisitCounters.Reset();
        _VisitCounters.AddDefaulted(Container._Nodes.Num());
    }

private:
    TArray<typename DiGraphType::NodeId> _NodesToVisit;
    TArray<int32_t> _VisitCounters;
};

/** Iterator for directed-graph elements. */
template<typename InElementType>
struct TDiGraphTopologicalIterator : public TDiGraphTopologicalIterator_Base<InElementType>
{
    using Super = TDiGraphTopologicalIterator_Base<InElementType>;
    using DiGraphType = typename Super::DiGraphType;

    TDiGraphTopologicalIterator(DiGraphType& InContainer)
        : TDiGraphTopologicalIterator_Base<InElementType>(InContainer)
        , _Container(InContainer)
    {}

    TDiGraphTopologicalIterator& operator++()
    {
        Super::Increment(_Container);
        return *this;
    }

    InElementType& operator*()
    {
        return _Container[Super::CurrentNodeIndex()];
    }

    InElementType* operator->() const
    {
        return &_Container[Super::CurrentNodeIndex()];
    }

    void Reset()
    {
        Super::Reset(_Container);
    }

private:
    DiGraphType& _Container;
};

/** Const iterator for directed-graph elements. */
template<typename InElementType>
struct TDiGraphConstTopologicalIterator : public TDiGraphTopologicalIterator_Base<InElementType>
{
    using Super = TDiGraphTopologicalIterator_Base<InElementType>;
    using DiGraphType = typename Super::DiGraphType;

    TDiGraphConstTopologicalIterator(const DiGraphType& InContainer)
        : TDiGraphTopologicalIterator_Base<InElementType>(InContainer)
        , _Container(InContainer)
    {}

    TDiGraphConstTopologicalIterator& operator++()
    {
        Super::Increment(_Container);
        return *this;
    }

    const InElementType& operator*()
    {
        return _Container[Super::CurrentNodeIndex()];
    }

    const InElementType* operator->() const
    {
        return &_Container[Super::CurrentNodeIndex()];
    }

    void Reset()
    {
        Super::Reset(_Container);
    }

private:
    const DiGraphType& _Container;
};

template<typename InElementType>
bool TDirectedGraph<InElementType>::TopologicalSort(TArray<InElementType>& OutItems) const
{
    const int32_t ExpectedSize = OutItems.Num() + _Nodes.Num();
    OutItems.Reserve(ExpectedSize);

    for (TDiGraphConstTopologicalIterator<InElementType> It(*this); It; ++It)
    {
        OutItems.Add(*It);
    }
    return OutItems.Num() == ExpectedSize;
}

template<typename InElementType>
bool TDirectedGraph<InElementType>::TopologicalSort_Pointers(TArray<const InElementType*>& OutItems) const
{
    const int32_t ExpectedSize = OutItems.Num() + _Nodes.Num();
    OutItems.Reserve(ExpectedSize);

    for (TDiGraphConstTopologicalIterator<InElementType> It(*this); It; ++It)
    {
        OutItems.Add(&(*It));
    }
    return OutItems.Num() == ExpectedSize;
}

template<typename InElementType>
bool TDirectedGraph<InElementType>::TopologicalSort_Pointers(TArray<InElementType*>& OutItems)
{
    const int32_t ExpectedSize = OutItems.Num() + _Nodes.Num();
    OutItems.Reserve(ExpectedSize);

    for (TDiGraphTopologicalIterator<InElementType> It(*this); It; ++It)
    {
        OutItems.Add(&(*It));
    }
    return OutItems.Num() == ExpectedSize;
}

template<typename InElementType>
TArray<TArray<typename TDirectedGraph<InElementType>::NodeId>> TDirectedGraph<InElementType>::FindCycles() const
{
    TArray<bool> NodeVisitedMap;
    NodeVisitedMap.Reset(_Nodes.Num());
    NodeVisitedMap.AddZeroed(_Nodes.Num());
    
    struct SStackEntry
    {
        int32_t _NodeIndex;
        int32_t _NextSuccessorIndex;
    };
    TArray<SStackEntry> Stack;

    TArray<TArray<NodeId>> Cycles;

    // Do a depth-first traversal from each node until all nodes have been visited, keeping track of
    // the path taken and checking for cycles in it.
    for (int32_t RootNodeIndex = 0; RootNodeIndex < _Nodes.Num(); ++RootNodeIndex)
    {
        if (!NodeVisitedMap[RootNodeIndex])
        {
            NodeVisitedMap[RootNodeIndex] = true;
            Stack.Add(SStackEntry{RootNodeIndex,0});
            while (Stack.Num())
            {
                // Pop the node from the top of the stack and check whether it has any successors
                // left to check.
                SStackEntry& StackTop = Stack.Top();
                const SNode& Node = _Nodes[StackTop._NodeIndex];
                if (StackTop._NextSuccessorIndex < Node._Successors.Num())
                {
                    // Check the node's next successor.
                    const int32_t SuccessorNodeIndex = Node._Successors[StackTop._NextSuccessorIndex];
                    ++StackTop._NextSuccessorIndex;

                    // If the successor node hasn't been visited yet, push it on the stack to visit.
                    if (!NodeVisitedMap[SuccessorNodeIndex])
                    {
                        NodeVisitedMap[SuccessorNodeIndex] = true;
                        Stack.Add(SStackEntry{SuccessorNodeIndex, 0});
                    }
                    else
                    {
                        // If the successor node has already been visited, check if it's on the
                        // stack as part of the current path.
                        for (int32_t StackIndex = 0; StackIndex < Stack.Num(); ++StackIndex)
                        {
                            if (Stack[StackIndex]._NodeIndex == SuccessorNodeIndex)
                            {
                                // If the successor node was already on the stack, the path on the
                                // stack is cyclic. Extract it to the cycle list to return.
                                TArray<NodeId> CycleNodeIds;
                                for (int32_t CycleStackIndex = StackIndex; CycleStackIndex < Stack.Num(); ++CycleStackIndex)
                                {
                                    CycleNodeIds.Add(Stack[CycleStackIndex]._NodeIndex);
                                }
                                Cycles.Add(CycleNodeIds);

                                break;
                            }
                        }
                    }
                }
                else
                {
                    // If all the node's successors have been visited, pop the node from the stack
                    // and continue with the new top of the stack.
                    Stack.Pop();
                }
            };
        }
    }

    return Cycles;
}

/** 
 * Stateful helper for visiting, skipping, and revisiting directed graph nodes.
 * Useful for performing pass, fail, retry operations on a dependency graph.
 */
template<typename InElementType>
struct TDiGraphVisitor
{
    using DiGraphType  = TDirectedGraph<InElementType>;
    using IteratorType = TDiGraphTopologicalIterator<InElementType>;

    TDiGraphVisitor(DiGraphType& DiGraph)
        : _GraphIterator(DiGraph)
    {
    }

    /** 
     * Iterates the associated directed-graph using a specified Visitor.
     *
     * @param  Visitor  A functor of the form: bool(InElementType&). NOTE: Returning false
     *                  from the specified Visitor will skip that node along with any of
     *                  its children, enquing them for subsequent iterations.
     *
     * @return `true` if the full graph was processed, otherwise `false` if it
     *         subsequent `Iterate()` calls are required.
     */
    template<typename VisitorType>
    bool Iterate(VisitorType Visitor)
    {
        TArray<typename DiGraphType::NodeId> SkippedNodes;

        while (_GraphIterator)
        {
            if (Visitor(*_GraphIterator))
            {
                ++_GraphIterator;
            }
            else
            {
                // NOTE: This reverses the order that they were processed in before, 
                //       but that's fine because they were all ready to be processed regardless
                SkippedNodes.Add(_GraphIterator.SkipCurrent());
            }
        }

        _GraphIterator.Enqueue(Move(SkippedNodes));
        return (IsComplete());
    }

    bool IsComplete() const
    {
        return !(bool)_GraphIterator;
    }

    void Reset()
    {
        _GraphIterator.Reset();
    }

    IteratorType _GraphIterator;
};

} // namespace uLang
