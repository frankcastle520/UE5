// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "uLang/Common/Common.h"

#include <atomic>
#include <utility>

namespace uLang
{

/// Concurrent queue modes.
enum class EQueueMode : int8_t
{
    MultipleProducersSingleConsumer,
    SingleProducerSingleConsumer,
};

/// Simple templated queue following Unreal's `TQueue` implementation using a lock-free linked list.
/// We're doing this instead of using a simple array/etc. since it matches Unreal's implementation 1:1 apart from
/// usage of the standard library for atomics.
// TODO: (yiliang.siew) Allow for custom allocators as the rest of `uLang` does.
// TODO: (yiliang.siew) See if some of the memory ordering requirements can be relaxed. For now
// since it guarantees an `MFENCE` instruction on x86-64 arch. Unreal issues a `_mm_sfence` intrinsic,
// which I'm not sure if I can guarantee via `std::atomic`.
template <typename InElementType, EQueueMode Mode = EQueueMode::SingleProducerSingleConsumer>
class TQueueG
{
public:
    TQueueG()
    {
        Head = Tail = new TNode();
    }

    ~TQueueG()
    {
        while (Tail != nullptr)
        {
            TNode* Node = Tail;
            Tail = Tail->Next;
            delete Node;
        }
    }

    /**
     * Adds an item to the head of the queue.
     *
     * @param InElement 	The item to add.
     *
     * @return 			`true` if the item was added, `false` otherwise.
     */
    bool Enqueue(const InElementType& InElement)
    {
        TNode* NewNode = new TNode(InElement);
        if (NewNode == nullptr)
        {
            return false;
        }
        TNode* OldHead;
        if (Mode == EQueueMode::MultipleProducersSingleConsumer)
        {
            OldHead = std::atomic_exchange_explicit(&Head, NewNode, std::memory_order_seq_cst);
            std::atomic_exchange_explicit(&OldHead->Next, NewNode, std::memory_order_seq_cst);
        }
        else
        {
            OldHead = Head;
            Head = NewNode;
            std::atomic_thread_fence(std::memory_order_seq_cst);
            OldHead->Next = NewNode;
        }

        return true;
    }

    bool Enqueue(const InElementType&& InElement)
    {
        TNode* NewNode = new TNode(InElement);
        if (NewNode == nullptr)
        {
            return false;
        }
        TNode* OldHead;
        if (Mode == EQueueMode::MultipleProducersSingleConsumer)
        {
            OldHead = std::atomic_exchange_explicit(&Head, NewNode, std::memory_order_seq_cst);
            std::atomic_exchange_explicit(&OldHead->Next, NewNode, std::memory_order_seq_cst);
        }
        else
        {
            OldHead = Head;
            Head = NewNode;
            std::atomic_thread_fence(std::memory_order_seq_cst);
            OldHead->Next = NewNode;
        }

        return true;
    }

    /**
     * Removes and returns the item from the tail of the queue.
     *
     * @param OutElement 	The item from the tail of the queue.
     *
     * @return 			`true` if a value was returned, `false` if the queue was empty.
     */
    bool Dequeue(InElementType& OutElement)
    {
        TNode* Popped = Tail->Next;
        if (Popped == nullptr)
        {
            return false;
        }
        OutElement = std::move(Popped->Element);
        TNode* OldTail = Tail;
        Tail = Popped;
        Tail->Element = InElementType();
        delete OldTail;

        return true;
    }
    /**
     * Removes the item from the tail of the queue.
     *
     * @return true if a value was removed, false if the queue was empty.
     */
    bool Pop()
    {
        TNode* Popped = Tail->Next;
        if (Popped == nullptr)
        {
            return false;
        }
        TNode* OldTail = Tail;
        Tail = Popped;
        Tail->Element = InElementType();
        delete OldTail;

        return true;
    }

    /**
     * Empty the queue, discarding all items.
     */
    void Empty()
    {
        while (Pop())
        {
            ;
        }
    }

    /**
     * Checks whether the queue is empty.
     *
     * @return 	`true` if the queue is empty, `false` otherwise.
     */
    bool IsEmpty()
    {
        return (Tail->Next.load() == nullptr);
    }

private:
    /** Structure for the internal linked list. */
    struct TNode
    {
        TNode() : Next(nullptr)
        {}

        explicit TNode(const InElementType& InElement) : Next(nullptr), Element(InElement)
        {}

        /// @overload move constructor.
        explicit TNode(const InElementType&& InElement) : Next(nullptr), Element(std::move(InElement))
        {}

        std::atomic<TNode*> Next;
        InElementType Element;
    };

    /** Holds a pointer to the head of the list. */
    std::atomic<TNode*> Head;

    /** Holds a pointer to the tail of the list. */
    TNode* Tail;

protected:
    /** Hidden copy constructor. */
    TQueueG(const TQueueG&) = delete;

    /** Hidden assignment operator. */
    TQueueG& operator=(const TQueueG&) = delete;
};

/// Queue that allocates elements on the heap.
template <typename InElementType>
using TQueue = TQueueG<InElementType>;

/// Queue that supports multiple producers adding elements that are allocated on the heap.
template <typename InElementType>
using TMQueue = TQueueG<InElementType, EQueueMode::MultipleProducersSingleConsumer>;

}    // namespace uLang
