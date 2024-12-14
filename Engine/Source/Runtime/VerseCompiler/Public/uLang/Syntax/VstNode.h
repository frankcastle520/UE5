// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "uLang/Common/Algo/Cases.h"
#include "uLang/Common/Containers/Array.h"
#include "uLang/Common/Containers/RangeView.h"
#include "uLang/Common/Containers/SharedPointer.h"
#include "uLang/Common/Containers/SharedPointerArray.h"
#include "uLang/Common/Misc/EnumUtils.h"
#include "uLang/Common/Misc/Optional.h"
#include "uLang/Common/Text/TextRange.h"
#include "uLang/Common/Text/UTF8String.h"
#include "uLang/Common/Text/UTF8StringBuilder.h"
#include "uLang/Common/Text/UTF8StringView.h"
#include "uLang/SourceProject/PackageRole.h"
#include "uLang/SourceProject/UploadedAtFNVersion.h"
#include "uLang/SourceProject/VerseScope.h"
#include "uLang/Syntax/NodeDecls.inl"

namespace Verse
{
namespace Vst
{
struct Node;
}
}    // namespace Verse

namespace uLang
{
class CAstNode;

/// This is used as a helper struct for storing the result of a signed distance check.
struct LocusDistanceResult
{
    TSRef<Verse::Vst::Node> Node;
    int32_t Distance;
};
}    // namespace uLang

namespace Verse
{
class FTile;

using string = uLang::CUTF8String;
using CUTF8String = uLang::CUTF8String;
using CUTF8StringView = uLang::CUTF8StringView;
using CUTF8StringBuilder = uLang::CUTF8StringBuilder;

using ChType = uLang::UTF8Char;

// uLang Types
template<typename T>
using TSPtr = uLang::TSPtr<T>;

template<typename T>
using TSRef = uLang::TSRef<T>;

template<typename T>
using LArray = uLang::TArray<T>;

using EResult = uLang::EResult;
using CSharedMix = uLang::CSharedMix;
using SLocus = uLang::STextRange;
using SPosition = uLang::STextPosition;
using EVerseScope = uLang::EVerseScope;

ULANG_FORCEINLINE SLocus NullWhence()
{
	return SLocus(0, 0, 0, 0);
}

namespace Vst
{
    struct Clause;
    struct Snippet;
    struct Identifier;

    enum class EChildDeletionBehavior : uint8_t
    {
        CreatePlaceholder,
        Delete,
        Default    
    };
    
    enum class ESupportsManyChildren : uint8_t
    {
        Anywhere,
        TrailingOnly,
        Nowhere
    };

#define VISIT_VSTNODE(NodeName, RequiredChildren, SupportsManyChildren, Precedence, ChildDeletionBehavior) NodeName,
    enum class NodeType : uint8_t
    {
        VERSE_ENUM_VSTNODES(VISIT_VSTNODE)
    };
#undef VISIT_VSTNODE

#define VISIT_VSTNODE(NodeName, RequiredChildren, Precedence, SupportsManyChildren, ChildDeletionBehavior) {#NodeName, RequiredChildren, Precedence, ESupportsManyChildren::SupportsManyChildren, EChildDeletionBehavior::ChildDeletionBehavior},
    constexpr struct NodeInfo
    {
        const char* FormalName;
        int32_t RequiredChildren;
        int32_t Precedence;
        ESupportsManyChildren SupportsManyChildren;
        EChildDeletionBehavior ChildDeletionBehavior;
    } NodeInfos[] = {
        VERSE_ENUM_VSTNODES(VISIT_VSTNODE)};
#undef VISIT_VSTNODE

    static constexpr uint8_t NumNodeTypes = static_cast<uint8_t>(ULANG_COUNTOF(NodeInfos));

    static const uint8_t TagNone = 255;

    ULANG_FORCEINLINE const char* GetNodeTypeName(const NodeType TypeOfNode)
    {
        return NodeInfos[static_cast<uint8_t>(TypeOfNode)].FormalName;
    }

    ULANG_FORCEINLINE int32_t GetNumRequiredChildren(const NodeType TypeOfNode)
    {
        return NodeInfos[static_cast<uint8_t>(TypeOfNode)].RequiredChildren;
    }

    ULANG_FORCEINLINE int32_t GetOperatorPrecedence(const NodeType TypeOfNode)
    {
        return NodeInfos[static_cast<uint8_t>(TypeOfNode)].Precedence;
    }

    ULANG_FORCEINLINE ESupportsManyChildren GetSupportsManyChildren(const NodeType TypeOfNode)
    {
        return NodeInfos[static_cast<uint8_t>(TypeOfNode)].SupportsManyChildren;
    }

    ULANG_FORCEINLINE EChildDeletionBehavior GetChildDeletionBehavior(const NodeType TypeOfNode)
    {
        return NodeInfos[static_cast<uint8_t>(TypeOfNode)].ChildDeletionBehavior;
    }

    struct VERSECOMPILER_API Node : public CSharedMix
    {
        using NodeArray = LArray<TSRef<Node>>;

        Node()
            : _Children()
            , _Parent(nullptr)
            , _Aux()
            , _PreComments()
            , _PostComments()
            , _NumNewLinesBefore(0)
            , _NumNewLinesAfter(0)
            , _Tag(0)
            , _Type(static_cast<NodeType>(0))
            , _MappedAstNode(nullptr)
            , _Tile(nullptr)
        {}

        Node(NodeType in_type) : Node()
        {
            _Type = in_type;
            ULANG_ASSERTF(
                   in_type == NodeType::Project
                || in_type == NodeType::Package
                || in_type == NodeType::Module
                || in_type == NodeType::Snippet,
                "Invalid use of locus-free Node constructor for node type that requires a locus");
        }

        Node(NodeType in_type, const SLocus& whence) : Node()
        {
            _Type = in_type;
            _Whence = whence;
            ULANG_ASSERTF(_Whence.IsValid(), "Node created with invalid locus");
        }

        virtual ~Node();

        static SLocus CombineLocii(const NodeArray& Nodes)
        {
            if (Nodes.IsFilled())
            {
                SLocus Whence = Nodes[0]->Whence();
                for (int32_t Index = 1; Index < Nodes.Num(); ++Index)
                {
                    Whence |= Nodes[Index]->Whence();
                }
                return Whence;
            }
            return SLocus();
        }

        TSRef<Node> AsShared() { return CSharedMix::SharedThis(this); }
        //TSRef<const Node> AsShared() const { return CSharedMix::SharedThis(this); }

        template<typename TNodeType>
        TNodeType& As() { ULANG_ASSERTF(IsA<TNodeType>(), "Vst Node is type `%s` not of expected type `%s` so cannot cast!", NodeInfos[GetElementTypeInt()].FormalName, NodeInfos[static_cast<int32_t>(TNodeType::StaticType)].FormalName); return *static_cast<TNodeType*>(this); }

        template<typename TNodeType>
        const TNodeType& As() const { ULANG_ASSERTF(IsA<TNodeType>(), "Vst Node is type `%s` not of expected type `%s` so cannot cast!", NodeInfos[GetElementTypeInt()].FormalName, NodeInfos[static_cast<int32_t>(TNodeType::StaticType)].FormalName); return *static_cast<const TNodeType*>(this); }

        template <typename TNodeType>
        TNodeType* AsNullable() { return IsA<TNodeType>()? static_cast<TNodeType*>(this) : nullptr; }

        template <typename TNodeType>
        const TNodeType* AsNullable() const { return IsA<TNodeType>() ? static_cast<const TNodeType*>(this) : nullptr; }

        template<typename TNodeType>
        bool IsA() const { return GetElementType() == TNodeType::StaticType; }

        int GetChildCount() const { return _Children.Num(); }
        NodeType GetElementType() const { return _Type; }
        int32_t GetElementTypeInt() const { return static_cast<int32_t>(GetElementType()); }
        template<typename OpType>
        OpType GetTag() const { return static_cast<OpType>(_Tag); }
        template<typename OpType=uint8_t>
        void SetTag(OpType in_op) { _Tag = static_cast<uint8_t>(in_op); }

        bool HasAttributes() const;
        const Identifier* GetAttributeIdentifier(const CUTF8StringView& AttributeName) const;
        bool IsAttributePresent(const CUTF8StringView& AttributeName) const;

        const Node* TryGetFirstAttributeOfType(NodeType Type) const;

        template<typename TNodeType>
        const TNodeType* TryGetFirstAttributeOfType() const
        {
            const Node* Result = TryGetFirstAttributeOfType(TNodeType::StaticType);

            if(Result != nullptr)
            {
                return &Result->As<TNodeType>();
            }

            return nullptr;
        }

        void PrependAux(const TSRef<Node>& AuxChild);
        void PrependAux(const NodeArray& AuxChildren);
        void AppendAux(const TSRef<Node>& AuxChild);
        void AppendAux(const NodeArray& AuxChildren);
        void AppendAuxAt(const TSRef<Node>& AuxChild, int32_t Idx);
        const TSPtr<Clause>& GetAux() const { return _Aux; }
        void SetAux(const TSRef<Clause>& Aux);
        void RemoveAux() { _Aux.Reset(); }

        void AppendPrefixComment(const TSRef<Node>& CommentNode);
        void AppendPrefixComments(const NodeArray& CommentNodes);
        void AppendPostfixComment(const TSRef<Node>& CommentNode);
        void AppendPostfixComments(const NodeArray& CommentNodes);
        const NodeArray& GetPrefixComments() const { return _PreComments; }
        const NodeArray& GetPostfixComments() const { return _PostComments; }
        NodeArray& AccessPrefixComments() { return _PreComments; }
        NodeArray& AccessPostfixComments() { return _PostComments; }

        void SetWhence(const SLocus& Whence) { _Whence = Whence; }
        void CombineWhenceWith(const SLocus& Whence) { _Whence |= Whence; }
        const SLocus& Whence() const         { return _Whence; }
        const CUTF8String& GetSnippetPath() const;
        const Snippet* FindSnippetByFilePath(const CUTF8StringView& FilePath) const;
        const Node* FindChildByPosition(const SPosition& TextPosition) const;
        const TSRef<Node> FindChildClosestToPosition(const SPosition& TextPosition, const CUTF8StringView& SourceText) const;

        template<class VisitPolicy, typename ReturnType = void >
        static void VisitWith(const TSRef<Vst::Node>& node, VisitPolicy& visit_policy);

        const NodeInfo& GetElementInfo() const { return NodeInfos[GetElementTypeInt()]; }
        const ChType* GetElementName() const { return (const ChType*)NodeInfos[GetElementTypeInt()].FormalName; }
        int32_t GetPrecedence() const { return GetOperatorPrecedence(GetElementType()); }
        int32_t NumRequiredChildren() const
        {
            return GetNumRequiredChildren(GetElementType());
        }
        ESupportsManyChildren IsManyChildrenSupported() const
        {
            return GetSupportsManyChildren(GetElementType());
        }

        int32_t NumNewLinesBefore() const
        {
            return _NumNewLinesBefore;
        }

        void SetNumNewLinesBefore(const int32_t Num)
        {
            _NumNewLinesBefore = Num;
        }

        int32_t NumNewLinesAfter() const
        {
            return _NumNewLinesAfter;
        }

        void SetNumNewLinesAfter(const int32_t Num)
        {
            _NumNewLinesAfter = Num;
        }

        void SetNewLineAfter(const bool bNewLineAfter)
        {
            if (bNewLineAfter)
            {
                // Don't touch the node if it already has new lines after it; we preserve the information.
                if (NumNewLinesAfter() > 0)
                {
                    return;
                }
                else
                {
                    SetNumNewLinesAfter(1);
                }
            }
            else
            {
                SetNumNewLinesAfter(0);
            }
        }

        bool HasNewLineAfter() const
        {
            return _NumNewLinesAfter > 0;
        }

        bool HasNewLinesBefore() const
        {
            return _NumNewLinesBefore > 0;
        }

        bool IsEmpty() const { return _Children.IsEmpty(); }
        const NodeArray& GetChildren() const { return _Children; }
        NodeArray& AccessChildren() { return _Children; }
        NodeArray TakeChildren()
        {
            for (const TSRef<Node>& Child : _Children)
            {
                Child->_Parent = nullptr;
            }
            return Move(_Children);
        }
        TSPtr<Node> GetRightmostChild() const
        {
            return GetChildCount() > 0
                ? GetChildren()[GetChildCount() - 1]
                : TSPtr<Node>(nullptr);
        }
        Node* AccessParent() { return _Parent; }
        TSRef<Node> GetSharedSelf() { return SharedThis(this); }
        TSRef<Node> GetSharedSelf() const { return SharedThis(const_cast<Node*>(this)); }
        const Node* GetParent() const { return _Parent; }
        bool HasParent() const
        {
            return this->GetParent() != nullptr;
        }
        template<class Type>
        const Type* GetParentOfType() const
        {
            for (const Node* CurNode = this; CurNode; CurNode = CurNode->_Parent)
            {
                if (CurNode->GetElementType() == Type::StaticType)
                {
                    return static_cast<const Type*>(CurNode);
                }
            }

            return nullptr;
        }

        bool IsElementType(NodeType InType) const { return InType == _Type; }
        bool IsChildElementType(int32_t idx, NodeType InType) const { return _Children.Num() > idx && _Children[idx]->IsElementType(InType); }

        bool IsError() const { return GetElementType() == NodeType::ParseError; }

        void DebugOrphanCheck()
        {
#ifdef _DEBUG
            const Node* OrphanedNode = FindOrphanedNode(*this);
            ULANG_ASSERTF(!OrphanedNode, "An orphaned node was encountered!");
#endif
        }

        bool Contains(const Node& Target, const bool bRecursive = true) const
        {
            struct FVstContains_Visitor
            {
                static bool Contains(const TSRef<Node>& Root, const Node& RecursiveTarget)
                {
                    bool bRecursiveContains = false;
                    for (const TSPtr<Node> Child : Root->AccessChildren())
                    {
                        if (Child.Get() == &RecursiveTarget)
                        {
                            bRecursiveContains = true;
                        }
                        else if (Child.IsValid())
                        {
                            bRecursiveContains = Contains(Child.AsRef(), RecursiveTarget);
                        }

                        if (bRecursiveContains)
                        {
                            break;
                        }
                    }

                    return bRecursiveContains;
                }
            };

            bool bContains = false;
            for (const TSRef<Node>& Child : _Children)
            {
                if (Child.Get() == &Target)
                {
                    bContains = true;
                }
                else if (bRecursive)
                {
                    bContains = FVstContains_Visitor::Contains(Child, Target);
                }

                if (bContains)
                {
                    break;
                }
            }

            return bContains;
        }

        int32_t FindPreviousSibling()
        {
            if (!this->HasParent())
            {
                return uLang::IndexNone;
            }
            const Node* Parent = this->GetParent();
            const int32_t NumChildren = Parent->GetChildCount();
            if (NumChildren == 1)
            {
                return uLang::IndexNone;
            }
            const NodeArray& Children = Parent->GetChildren();
            for (int32_t Index = 0; Index < NumChildren; ++Index)
            {
                const TSRef<Node>& CurNode = Children[Index];
                if (CurNode == this)
                {
                    return Index - 1;
                }
            }
            return uLang::IndexNone;
        }

        const TSRef<Node>& AppendChild(const TSRef<Node>& child)
        {
            DropParent(child);
            child->_Parent = this;
            _Children.Push(child);
            DebugOrphanCheck();
            return _Children.Last();
        }

        const TSRef<Node>& AppendChildAt(const TSRef<Node>& child, int32_t idx)
        {
            DropParent(child);
            child->_Parent = this;
            _Children.Insert(child, idx);
            DebugOrphanCheck();
            return _Children[idx];
        }

        TSRef<Node> TakeChildAt(int32_t idx, const TSPtr<Node> Replacement = TSPtr<Node>())
        {
            const auto ChildAtIdx = _Children[idx];
            if (Replacement.IsValid())
            {
                Replacement->_Parent = this;
                _Children[idx] = Replacement.AsRef();
            }
            else
            {
                _Children.RemoveAt(idx);
            }
            ChildAtIdx->_Parent = nullptr;
            return ChildAtIdx;
        }

        void AppendChildren(const uLang::TArray<TSRef<Node>>& Children, int32_t NumToAppend = -1)
        {
            NumToAppend = NumToAppend == -1 ? Children.Num() : NumToAppend;
            _Children.Reserve(_Children.Num() + NumToAppend);
            for(int32_t i=0; i<NumToAppend; i+=1)
            {
                const auto& Expr = Children[i];
                AppendChild(Expr);
            }
        }

        void AppendChildren(const uLang::TSRefArray<Node>& Children, int32_t NumToAppend = -1)
        {
            NumToAppend = NumToAppend == -1 ? Children.Num() : NumToAppend;
            _Children.Reserve(_Children.Num() + NumToAppend);
            for (int32_t i = 0; i < NumToAppend; i += 1)
            {
                const auto& Expr = Children[i];
                AppendChild(Expr);
            }
        }

        // Prepend the given nodes to this node's child list in reverse order
        // i.e. the last node in the given list will end up as the first child of this node.
        void PrependChildren(const uLang::TArray<TSRef<Node>>& Children, int32_t NumToAppend = -1)
        {
            NumToAppend = NumToAppend == -1 ? Children.Num() : NumToAppend;
            _Children.Reserve(_Children.Num() + NumToAppend);
            for (int32_t i = 0; i < NumToAppend; i += 1)
            {
                const auto& Expr = Children[i];
                AppendChildAt(Expr, 0);
            }
        }

        void ReplaceSelfWith(const TSRef<Node>& replacement);
        
        // Supply an index if you have one
        bool RemoveFromParent(int32_t idx = uLang::IndexNone);

        void Empty()
        {
            for (const auto& Child : _Children)
            {
                if (ULANG_ENSUREF(Child->_Parent == this, "Child does not belong to me!"))
                {
                    Child->_Parent = nullptr;
                }
            }

            _Children.Reset();
        }

        const Snippet* FindSnippet()
        {
            return GetParentOfType<Snippet>();
        }

        Node* FindRoot()
        {
            Node* root = this;
            while (root->_Parent != nullptr && root->_Parent->GetElementType() != NodeType::Snippet)
            {
                root = root->AccessParent();
            }

            return root;
        }

        static void TransferChildren(const TSRef<Node>& From, const TSRef<Node>& To, int32_t First, int32_t Last)
        {
            ULANG_ASSERTF(Last <= From->GetChildCount() - 1, "Not enough elements in source array");

            To->AccessChildren().Reserve(To->GetChildCount() + Last - First + 1);

            for (int32_t i = First; i <= Last; i += 1)
            {
                const auto & CurChild = From->GetChildren()[i]; //-V758
                CurChild->_Parent = nullptr;
                To->AppendChild(CurChild);
            }

            From->AccessChildren().RemoveAt(First, Last - First + 1);
        }

        static void TransferChildren(const TSRef<Node>& From, const TSRef<Node>& To)
        {
            TransferChildren(From, To, 0, From->GetChildCount() - 1);
        }

        static void TransferPrefixComments(const TSRef<Node>& From, const TSRef<Node>& To)
        {
            NodeArray& ToPrefixComments = To->AccessPrefixComments();
            NodeArray& FromPrefixComments = From->AccessPrefixComments();
            ToPrefixComments.Reserve(ToPrefixComments.Num() + FromPrefixComments.Num());
            for (TSRef<Node>& FromPrefixComment : FromPrefixComments)
            {
                FromPrefixComment->_Parent = nullptr;
                To->AppendPrefixComment(FromPrefixComment);
            }
            FromPrefixComments.Empty();
        }

        static void TransferPostfixComments(const TSRef<Node>& From, const TSRef<Node>& To)
        {
            NodeArray& ToPostfixComments = To->AccessPostfixComments();
            NodeArray& FromPostfixComments = From->AccessPostfixComments();
            ToPostfixComments.Reserve(ToPostfixComments.Num() + FromPostfixComments.Num());
            for (TSRef<Node>& FromPostfixComment : FromPostfixComments)
            {
                FromPostfixComment->_Parent = nullptr;
                To->AppendPostfixComment(FromPostfixComment);
            }
            FromPostfixComments.Empty();
        }

        const uLang::CAstNode* GetMappedAstNode() const { return _MappedAstNode; }
        void AddMapping(uLang::CAstNode* AstNode) const;
        static void RemoveMapping( uLang::CAstNode* AstNode );

        void EnsureAuxAllocated();

        void SetTile(FTile* Tile)
        {
            _Tile = Tile;
        }

        FTile* GetTile()
        {
            return _Tile;
        }

    protected:

        friend class uLang::CAstNode;

        /**
         * Checks for any nodes that have their parent set incorrectly within the hierarchy.
         *
         * @param Root 	A pointer to the root of the tree to inspect for any orphaned nodes.
         *
         * @return 		A  pointer to the first node that was found to have an incorrect parent set.
         *					If no such node was found, returns `nullptr`.
         */
        static const Node* FindOrphanedNode(const Node& InNode)
        {
            struct SChildParent
            {
                const Node* _Child;
                const Node* _Parent;
            };
            LArray<SChildParent> Stack;
            // NOTE: (YiLiangSiew) Arbitrary stack size chosen here to try avoiding re-allocation during traversal.
            Stack.Reserve(256);
            for (const TSRef<Node>& CurChild : InNode.GetChildren())
            {
                Stack.Add({CurChild.Get(), &InNode});
            }
            while (!Stack.IsEmpty())
            {
                const SChildParent& CurPair = Stack.Pop();
                if (CurPair._Child->GetParent() != CurPair._Parent)
                {
                    return CurPair._Child;
                }
                for (const TSRef<Node>& CurChild : CurPair._Child->GetChildren())
                {
                    Stack.Add({CurChild, CurPair._Child});
                }
            }
            return nullptr;
        };

        /**
         * Validates that the child being added to this node is currently orphaned.
         * If the child has a parent, then two nodes would own the same child, which is impossible.
         */
        void DropParent(const TSRef<Node>& Child)
        {
            if (ULANG_ENSUREF(Child->GetParent()==nullptr, "Child already has a parent!"))
            {
            }
            else
            {
                Child->AccessParent()->AccessChildren().Remove(Child);
                Child->_Parent = nullptr;
            }
        }

        void AppendChildInternal(const TSRef<Node>& child)
        {
            DropParent(child);

            child->_Parent = this;
            _Children.Push(child);

            ULANG_VERIFYF(FindOrphanedNode(*this) == nullptr, "Found orphaned child!");
        }

        NodeArray _Children;
        /** VstNodes point at their parent. We guarantee that a VstNode can only exist in one place in the Vst tree at a time, so the Parent<->Child relationship is unique. */
        Node* _Parent;
        /** Auxiliary data such as attributes associated with this VstNode. The transaction system addresses this via child index -1. */
        TSPtr<Clause> _Aux;

        // list of comment nodes that appear before or after this node
        NodeArray _PreComments;
        NodeArray _PostComments;

        /**
         * Text location from whence this node was parsed.
         * (1) must be contained in (= not be partially outside of) parent's _Whence;
         * (2) must not overlap any sibling's _Whence;
         * (3) snippets (text files), programs, and some modules will not have a valid locus
         */
        SLocus _Whence;

        /// The number of trailing newlines that should follow this node.
        int32_t _NumNewLinesBefore;
        int32_t _NumNewLinesAfter;

        /** Describes the role of this node in the context of its parent. e.g. Children of BinaryOpAddSub are tagged as `Operator` or `Operand` */
        uint8_t _Tag;
        /** Runtime type information about this node */
        NodeType _Type;
        mutable const uLang::CAstNode* _MappedAstNode;
        FTile* _Tile;
    };  // Node


    using NodeArray = Node::NodeArray;


    struct Clause : public Node
    {
        static const Vst::NodeType StaticType = NodeType::Clause;
        enum class EForm : int8_t
        {
            Synthetic,                // The clause doesn't occur in source, but is instead used as a way to group multiple subexpressions.
            NoSemicolonOrNewline,     // The clause does not contains a semicolon or a newline: x or {x} or {x,y} but not {x;}
            HasSemicolonOrNewline,    // The clause does contain a semicolon or a newline: {x;} or {x;y,z} or \n\tx

            /// Used for clauses that have a single attribute identifier VST node within it. This means the clause should use angle brackets (i.e. `<`/`>`) instead of curly braces.
            /// This also means that the clause is before the identifier (i.e. `<@custom_attribute>identifier`).
            IsPrependAttributeHolder,
            /// Used for clauses that have a single attribute identifier VST node within it. This means the clause should use angle brackets (i.e. `<`/`>`) instead of curly braces.
            /// This also means that the clause is after the identifier (i.e. `class<pure>`).
            IsAppendAttributeHolder
        };

        enum class EPunctuation : int8_t
        {
            Unknown,
            Braces,
            Colon,
            Indentation
        };

        Clause(const SLocus& Whence, const EForm Form, const EPunctuation Punctuation) : Node(StaticType, Whence), _Form(Form), _Punctuation(Punctuation) {}

        Clause(const SLocus& Whence, const EForm Form) : Node(StaticType, Whence), _Form(Form), _Punctuation(EPunctuation::Unknown) {}

        Clause(uint8_t ClauseType, const SLocus& Whence, EForm Form) : Node(StaticType, Whence), _Form(Form), _Punctuation(EPunctuation::Unknown)
        {
            _Tag = ClauseType;
        }

        Clause(const TSRef<Node>& Child, const SLocus& Whence, EForm Form) : Clause(0, Whence, Form)
        {
            AppendChild(Child);
        }

        Clause(const NodeArray& Children, const SLocus& Whence, EForm Form) : Clause(0, Whence, Form)
        {
            AppendChildren(Children);
        }

        Clause(const uLang::TSRefArray<Node>& Children, const SLocus& Whence, EForm Form) : Clause(0, Whence, Form)
        {
            AppendChildren(Children);
        }

        Clause(const NodeArray& Children, int32_t NumToAdd, const SLocus& Whence, EForm Form) : Clause(0, Whence, Form)
        {
            AppendChildren(Children, NumToAdd);
        }

        Clause(const uLang::TSRefArray<Node>& Children, int32_t NumToAdd, const SLocus& Whence, EForm Form) : Clause(0, Whence, Form)
        {
            AppendChildren(Children, NumToAdd);
        }

        EForm GetForm() const { return _Form; }
        void SetForm(const EForm InForm)
        {
            this->_Form = InForm;
        }

        EPunctuation GetPunctuation() const
        {
            return _Punctuation;
        }
        void SetPunctuation(const EPunctuation InPunctuation)
        {
            this->_Punctuation = InPunctuation;
        }

    private:
        EForm _Form;

        /// Tells us whether the clause should be using either colon and newlines, or curly braces and semicolons to separate expressions.
        EPunctuation _Punctuation;
    };

    VERSECOMPILER_API const TSRef<Node> MakeStub(const SLocus& Whence);

    /**
     * Syntax element that does not need children. These nodes do not have a structure (i.e. no children), but
     * rather are leaves in the hierarchy.
     **/
    class CAtom : public Node
    {
    public:

        // Public Data Members

        CUTF8String _OriginalCode;

        // Methods

        CAtom(const uLang::CUTF8StringView& CodeStr, NodeType InType, const SLocus& Whence) : Node(InType, Whence), _OriginalCode(CodeStr) {}

        const string&      GetStringValue() const  { return _OriginalCode; }
        const CUTF8String& GetSourceText() const   { return _OriginalCode; }
        const char*        GetSourceCStr() const   { return _OriginalCode.AsCString(); }
    };

    struct Comment : public CAtom
    {
        enum class EType : uint8_t { block, line, ind, frag };

        static const Vst::NodeType StaticType = NodeType::Comment;

        Comment(EType Type, const CUTF8StringView& InText, const SLocus& Whence) : CAtom(InText, StaticType, Whence), _Type(Type) {}

        EType _Type;
    };

    const char* CommentTypeToString(Comment::EType Type);

    // A collection of sub trees that are stored in the same source, e.g. a text file or UProperty
    struct Snippet : public Node
    {
        static const Vst::NodeType StaticType = NodeType::Snippet;
        Snippet() : Node(StaticType) {}
        Snippet(const CUTF8StringView& Path) : Node(StaticType), _Path(Path) {}
        Snippet(const CUTF8StringView& Path, const TSRef<Vst::Node>& FirstChild) : Node(StaticType, FirstChild->Whence()), _Path(Path) { AppendChild(FirstChild); }
        Snippet(const TSRef<Vst::Node>& FirstChild) : Node(StaticType, FirstChild->Whence()) { AppendChild(FirstChild); }

        bool HasErrors() const
        {
            bool bHasErrors = false;
            for (const TSRef<Node>& Node : _Children)
            {
                if (Node->IsError())
                {
                    bHasErrors = true;
                    break;
                }
            }
            return bHasErrors;
        }

        Clause::EForm GetForm() const { return _Form; }
        void SetForm(Clause::EForm Form) { _Form = Form; }

        // Where this snippet came from - usually this is the fully qualified path of the associated text file
        CUTF8String _Path;

    private:
        Clause::EForm _Form{Clause::EForm::Synthetic};
    };

    // A collection of snippets
    struct Module : public Node
    {
        static const Vst::NodeType StaticType = NodeType::Module;
        Module(const CUTF8StringView& Name) : Node(StaticType), _Name(Name) {}

        // The name of this module
        CUTF8String _Name;

        // File path of vmodule file if exists, or directory path with trailing slash
        CUTF8String _FilePath;
    };

    // A collection of Module nodes
    struct Package : public Node
    {
        static const Vst::NodeType StaticType = NodeType::Package;

        Package(const CUTF8StringView& Name) : Node(StaticType), _Name(Name) {} //-V730

        // The name of this package
        CUTF8String _Name;

        // Directory path where the source files are located
        CUTF8String _DirPath;

        // File path of vpackage file if exists, empty otherwise
        CUTF8String _FilePath;

        // Verse path of the root module of this package
        CUTF8String _VersePath;

        // Names of packages this package is dependent on
        LArray<CUTF8String> _DependencyPackages;

        // Destination directory for VNI generated C++ code (fully qualified)
        uLang::TOptional<CUTF8String> _VniDestDir;

        // The role this package plays in the project.
        uLang::EPackageRole _Role = uLang::EPackageRole::Source;

        // Origin/visibility of Verse code in this package
        EVerseScope _VerseScope = EVerseScope::PublicUser;

        // The language version targetted by the Verse code in this package.
        uLang::TOptional<uint32_t> _VerseVersion;

        /// This allows us to determine when a package was uploaded for a given Fortnite release version.
        /// It is a HACK that conditionally enables/disables behaviour in the compiler in order to
        /// support previous mistakes allowed to slip through in previous Verse langauge releases but
        /// now need to be supported for backwards compatability.
        /// When we can confirm that all Fortnite packages that are currently uploaded are beyond this
        /// version being used in all instances of the codebase, this can then be removed.
        uint32_t _UploadedAtFNVersion = VerseFN::UploadedAtFNVersion::Latest;

        // If true, module macros in this package's source and digest will be treated as implicit
        bool _bTreatModulesAsImplicit = false;

        // Whether to allow the use of experimental definitions in this package.
        bool _bAllowExperimental = false;

        VERSECOMPILER_API TSRef<Module> FindOrAddModule(const CUTF8StringView& ModuleName, const CUTF8StringView& ParentModuleName = CUTF8StringView());
        VERSECOMPILER_API static uLang::TOptional<TSRef<Module>> FindModule(const Node& ModuleContainer, const CUTF8StringView& ModuleName);
    };

    // A collection of Package nodes
    // Packages (children) are sorted in dependency order (i.e. dependents always follow their dependencies)
    struct Project : public Node
    {
        static const Vst::NodeType StaticType = NodeType::Project;
        Project(const CUTF8StringView& Name) : Node(StaticType), _Name(Name) {}

        // The name of this project
        CUTF8String _Name;

        // File path of vproject file if exists, empty otherwise
        CUTF8String _FilePath;

        VERSECOMPILER_API const TSRef<Module>& FindOrAddModule(const CUTF8StringView& ModuleName, const CUTF8StringView& FilePath, const CUTF8StringView& ParentModuleName = CUTF8StringView());
        VERSECOMPILER_API static uLang::TOptional<TSRef<Module>> FindModule(const Node& ModuleContainer, const CUTF8StringView& ModuleName);

        /**
         * Removes any packages from the project that have the given name.
         *
         * @param PackageName   Any package that has this name in the project will be removed.
         *
         * @return              `true` if any packages were removed, `false` if no packages with the name were found
         *                      or another error occurred.
         */
        VERSECOMPILER_API bool RemovePackagesWithName(const CUTF8StringView& PackageName)
        {
            // NOTE: (YiLiangSiew) This assumes that packages are not stored recursively in the project,
            // or that projects do not have other projects as their descendents.
            NodeArray& Children = this->AccessChildren();
            const int32_t NumChildren = Children.Num();
            if (NumChildren == 0)
            {
                return false;
            }
            int32_t NumPackagesRemoved = 0;
            for (int32_t Index = NumChildren - 1; Index >= 0; --Index)
            {
                TSRef<Node> CurChild = Children[Index];
                if (!CurChild->IsA<Package>())
                {
                    continue;
                }
                if (CurChild->As<Package>()._Name == PackageName)
                {
                    Children.RemoveAt(Index);
                    ++NumPackagesRemoved;
                }
            }
            return NumPackagesRemoved > 0;
        }

        VERSECOMPILER_API bool ReplaceSnippet(const CUTF8StringView& PathOfOldSnippetToReplace, TSRef<Snippet> NewSnippet, TSPtr<Snippet>* OutOldSnippet = nullptr)
        {
            for (const TSRef<Node>& CurChild : this->_Children)
            {
                if (CurChild->IsA<Package>())
                {
                    Package& CurPackage = CurChild->As<Package>();
                    for (const TSRef<Node>& CurPkgChild : CurPackage.GetChildren())
                    {
                        if (CurPkgChild->IsA<Module>())
                        {
                            Module& CurModule = CurPkgChild->As<Module>();
                            for (const TSRef<Node>& CurModuleChild : CurModule.GetChildren())
                            {
                                if (CurModuleChild->IsA<Snippet>())
                                {
                                    Snippet& CurSnippet = CurModuleChild->As<Snippet>();
                                    if (CurSnippet._Path == PathOfOldSnippetToReplace)
                                    {
                                        if (OutOldSnippet != nullptr)
                                        {
                                            *OutOldSnippet = CurModuleChild.As<Snippet>();
                                        }
                                        CurSnippet.ReplaceSelfWith(NewSnippet);
                                        return true;
                                    }
                                }
                            }
                        }
                        else if (CurPkgChild->IsA<Snippet>())
                        {
                            Snippet& CurSnippet = CurPkgChild->As<Snippet>();
                            if (CurSnippet._Path == PathOfOldSnippetToReplace)
                            {
                                if (OutOldSnippet != nullptr)
                                {
                                    *OutOldSnippet = CurPkgChild.As<Snippet>();
                                }
                                CurSnippet.ReplaceSelfWith(NewSnippet);
                                return true;
                            }
                        }
                    }
                }
            }
            return false;
        }
    };

    /**
     * Corresponds to `$1:$2 = $3` syntax, where the `$2` is optional.  If and
     * only if `$2` is present, the left-hand side is a `TypeSpec`.  For example,
     * @code
     * X:int = Y
     * @endcode
     * becomes
     * @code
     * Definition
     *   TypeSpec
     *     Identifier
     *       X
     *     Identifier
     *       int
     *   Identifier
     *     Y
     * @endcode
     * whereas
     * @code
     * X := Y
     * @endcode
     * becomes
     * @code
     * Definition
     *   Identifier
     *     X
     *   Identifier
     *     Y
     * @endcode
     */
    struct Definition : public Node
    {
        static const Vst::NodeType StaticType = NodeType::Definition;

        Definition(const SLocus& Whence, const TSRef<Vst::Node>& Lhs, const TSRef<Vst::Node>& Rhs) : Node(StaticType, Whence)
        {
            _Children.Reserve(2);
            AppendChildInternal(Lhs);
            AppendChildInternal(Rhs);
        }
        const TSRef<Vst::Node>& GetOperandLeft() const { return _Children[0]; }
        const TSRef<Vst::Node>& GetOperandRight() const { return _Children[1]; }
    };

    /**
     * Corresponds to `set $1 $op $2` syntax.  The left-hand side will always be
     * a `Mutation` node.  `$op` may be `=`, `+=`, `-=`, `*=`, or `/=` -
     * importantly, not `:=`.  For example,
     * @code
     * set X = Y
     * @endcode
     * becomes
     * @code
     * Assignment
     *   Mutation
     *     Set
     *     Identifier
     *       X
     *   Identifier
     *     Y
     * @endcode
     */
    struct Assignment : public Node
    {
        static const Vst::NodeType StaticType = NodeType::Assignment;
        enum class EOp : uint8_t { assign, addAssign, subAssign, mulAssign, divAssign };

        Assignment(const SLocus& Whence, const TSRef<Vst::Node>& lhs, EOp InOp, const TSRef<Vst::Node>& rhs) : Node(StaticType, Whence)
        {
            _Children.Reserve(2);
            AppendChildInternal(lhs);

            rhs->SetTag(static_cast<uint8_t>(InOp));
            AppendChildInternal(rhs);
        }
        const TSRef<Vst::Node>& GetOperandLeft() const { return _Children[0]; }
        const TSRef<Vst::Node>& GetOperandRight() const { return _Children[1]; }
    };

    inline const char* AssignmentOpAsCString(Assignment::EOp Op)
    {
        switch(Op)
        {
        case Assignment::EOp::assign: return "assign";
        case Assignment::EOp::addAssign: return "addAssign";
        case Assignment::EOp::subAssign: return "subAssign";
        case Assignment::EOp::mulAssign: return "mulAssign";
        case Assignment::EOp::divAssign: return "divAssign";
        default: ULANG_UNREACHABLE();
        };
    }

    // Conditional flow control with failure context in test condition.
    //
    // Children are Clause block nodes in the following order:
    //   - if_identifier ]
    //   - condition      |- Repeating
    //   - [then_body]   ]
    //   - [else_body]   -- Optional last node
    //
    // If Identifier blocks - must be present and is simply an empty Clause meant to
    //     hold information about comments surrounding the 'if' identifier in the
    //     original source code
    // Condition blocks - must be present (they cannot be omitted - there must be at
    //     least 1) and they must have 1 or more expressions where 1 or more of the
    //     expressions can fail. Any local variables within it's top scope are made
    //     available to any immediately following then block.
    // Then blocks - are optional (they can be omitted) and when present must follow a
    //     conditional block and may have zero (can be empty) or more expressions.
    // Else block - is optional (it can be omitted) and when present must follow a
    //     conditional or then block (it cannot be the only block), must be the last
    //     block and it may have zero (can be empty) or more expressions.
    //
    // Chained `else if` are automatically flattened into a single FlowIf node with
    // multiple condition/[then] block pairs followed by an optional else block.
    //
    // An `if` may be used as an expression with a result if all the control flow paths
    // have a common result - i.e. it will have a result if every condition is paired
    // with a then block and there is an ending else block and all the then blocks and
    // else block have a most common result type.
    struct FlowIf : public Node
    {
        static const Vst::NodeType StaticType = NodeType::FlowIf;

        // Tags for different kinds of clause block children.
        enum class ClauseTag : uint8_t { if_identifier, condition, then_body, else_body };

        FlowIf(const SLocus& Whence) : Node(StaticType, Whence)  {}

        void AddIfIdentifier(const TSRef<Node>& Child) { Child->SetTag(static_cast<uint8_t>(ClauseTag::if_identifier)); AppendChildInternal(Child); }
        void AddCondition(const TSRef<Node>& Child)    { Child->SetTag(static_cast<uint8_t>(ClauseTag::condition)); AppendChildInternal(Child); }
        void AddBody(const TSRef<Node>& Child)         { Child->SetTag(static_cast<uint8_t>(ClauseTag::then_body)); AppendChildInternal(Child); }
        void AddElseBody(const TSRef<Node>& Child)     { Child->SetTag(static_cast<uint8_t>(ClauseTag::else_body)); AppendChildInternal(Child); }
    };

    struct BinaryOpLogicalOr : public Node
    {
        static const Vst::NodeType StaticType = NodeType::BinaryOpLogicalOr;
        BinaryOpLogicalOr(const SLocus& Whence, const TSRef<Vst::Node>& Lhs, const TSRef<Vst::Node>& Rhs)
            : Node(StaticType, Whence)
        {
            _Children.Reserve(2);
            AppendChild(Lhs);
            AppendChild(Rhs);
        }

        void AppendChild(const TSRef<Vst::Node>& Rhs) { AppendChildInternal(Rhs); }
    };

    struct BinaryOpLogicalAnd : public Node
    {
        static const Vst::NodeType StaticType = NodeType::BinaryOpLogicalAnd;
        BinaryOpLogicalAnd(const SLocus& Whence, const TSRef<Node>& Lhs, const TSRef<Node>& Rhs)
            : Node(StaticType, Whence)
        {
            _Children.Reserve(2);
            AppendChild(Lhs);
            AppendChild(Rhs);
        }

        void AppendChild(const TSRef<Vst::Node>& Rhs) { AppendChildInternal(Rhs); }
    };

    struct PrefixOpLogicalNot : public Node
    {
        static const Vst::NodeType StaticType = NodeType::PrefixOpLogicalNot;
        PrefixOpLogicalNot( const SLocus& Whence, const TSRef<Node>& Rhs)
            : Node(StaticType, Whence)
        {
            AppendChildInternal(Rhs);
        }

        const TSRef<Vst::Node>& GetInnerNode() const { return _Children[0]; }
    };

    struct BinaryOpCompare : public Node
    {
        static const Vst::NodeType StaticType = NodeType::BinaryOpCompare;

        enum class op : uint8_t { lt, lteq, gt, gteq, eq, noteq };

        BinaryOpCompare(const SLocus& Whence, const TSRef<Vst::Node>& lhs, op in_op, const TSRef<Vst::Node>& rhs)
            : Node(StaticType, Whence)
        {
            _Children.Reserve(2);
            AppendChildInternal(lhs);
            AppendChildInternal(rhs);
            rhs->SetTag(static_cast<uint8_t>(in_op));
        }

        TSRef<Vst::Node> GetOperandLeft() const { return _Children[0]; }
        TSRef<Vst::Node> GetOperandRight() const { return _Children[1]; }
        op GetOp() const { return _Children[1]->GetTag<op>(); }
    };

    inline const char* BinaryCompareOpAsCString(BinaryOpCompare::op Op)
    {
        switch(Op)
        {
        case BinaryOpCompare::op::lt: return "lt";
        case BinaryOpCompare::op::lteq: return "lteq";
        case BinaryOpCompare::op::gt: return "gt";
        case BinaryOpCompare::op::gteq: return "gteq";
        case BinaryOpCompare::op::eq: return "eq";
        case BinaryOpCompare::op::noteq: return "noteq";
        default: ULANG_UNREACHABLE();
        };
    }

    inline BinaryOpCompare::op BinaryCompareOpFlip(BinaryOpCompare::op Op)
    {
        switch(Op)
        {
        case BinaryOpCompare::op::lt: return BinaryOpCompare::op::gt;
        case BinaryOpCompare::op::lteq: return BinaryOpCompare::op::gteq;
        case BinaryOpCompare::op::gt: return BinaryOpCompare::op::lt;
        case BinaryOpCompare::op::gteq: return BinaryOpCompare::op::lteq;
        case BinaryOpCompare::op::eq: return BinaryOpCompare::op::noteq;
        case BinaryOpCompare::op::noteq: return BinaryOpCompare::op::eq;
        default: ULANG_UNREACHABLE();
        }
    }

    struct Where : Node
    {
        static const NodeType StaticType = NodeType::Where;

        using RhsView = uLang::TRangeView<const TSRef<Node>*, const TSRef<Node>*>;

        Where(const SLocus& Whence, const TSRef<Node>& Lhs, const uLang::TArray<TSRef<Node>>& RhsArray)
            : Node(StaticType, Whence)
        {
            _Children.Reserve(1 + RhsArray.Num());
            AppendChildInternal(Lhs);
            for (const TSRef<Node>& Rhs : RhsArray)
            {
                AppendChildInternal(Rhs);
            }
        }

        Where(const SLocus& Whence, const TSRef<Node>& Lhs, const uLang::TSRefArray<Node>& RhsArray)
            : Node(StaticType, Whence)
        {
            _Children.Reserve(1 + RhsArray.Num());
            AppendChildInternal(Lhs);
            for (const TSRef<Node>& Rhs : RhsArray)
            {
                AppendChildInternal(Rhs);
            }
        }

        TSRef<Node> GetLhs() const
        {
            return _Children[0];
        }

        RhsView GetRhs() const
        {
            return {_Children.begin() + 1, _Children.end()};
        }
    };

    struct Mutation : Node
    {
        static const NodeType StaticType = NodeType::Mutation;

        enum class EKeyword : uint8_t
        {
            Var,
            Set
        };

        Mutation(const SLocus& Whence, const TSRef<Node>& Child, EKeyword Keyword)
            : Node(StaticType, Whence)
            , _Keyword(Keyword)
        {
            _Children.Reserve(1);
            AppendChildInternal(Child);
        }

        TSRef<Node> Child() const
        {
            return _Children[0];
        }

        EKeyword _Keyword;
    };

    struct TypeSpec : public Node
    {
        static const Vst::NodeType StaticType = NodeType::TypeSpec;

        // comments that go after the ':' of the typespec
        NodeArray _TypeSpecComments;

        TypeSpec(const SLocus& Whence, const TSRef<Node>& Lhs, const TSRef<Node>& Rhs)
            :Node(NodeType::TypeSpec, Whence)
        {
            _Children.Reserve(2);
            AppendChildInternal(Lhs);
            AppendChildInternal(Rhs);
        }

        TypeSpec(const SLocus& Whence, const TSRef<Node>& Rhs)
            :Node(NodeType::TypeSpec, Whence)
        {
            _Children.Reserve(1);
            AppendChildInternal(Rhs);
        }

        bool HasLhs() const { return _Children.Num() == 2; }
        TSRef<Vst::Node> GetLhs() const { ULANG_ASSERTF(HasLhs(), "Lhs assumes we have at least two children."); return _Children[0]; }
        TSRef<Vst::Node> TakeLhs() { ULANG_ASSERTF(HasLhs(), "Lhs assumes we have at least two children."); return TakeChildAt(0, MakeStub(Whence())); }
        TSRef<Vst::Node> GetRhs() const { return _Children[_Children.Num()-1]; }
        TSRef<Vst::Node> TakeRhs() { return TakeChildAt(_Children.Num()-1, MakeStub(Whence())); }
    };

    struct Identifier : public CAtom
    {
        static const Vst::NodeType StaticType = NodeType::Identifier;

        /// Comments that are to be suffixed to the qualifiers of this identifier.
        NodeArray _QualifierPostComments;

        /// Comments that are to be prefixed to the qualifiers of this identifier.
        NodeArray _QualifierPreComments;

        Identifier(const CUTF8StringView& InName, const SLocus& Whence) : CAtom(InName, StaticType, Whence) {}

        bool IsQualified() const { return _Children.Num() > 0; }
        const TSRef<Node>& GetQualification() const { return _Children[0]; }
    };
    
    struct Operator : public CAtom
    {
        static const Vst::NodeType StaticType = NodeType::Operator;

        Operator(const CUTF8StringView& InSourceText, const SLocus& Whence)
            : CAtom(InSourceText, StaticType, Whence)
        {}
    };

    struct BinaryOp : public Node
    {
        enum class op : uint8_t { Operator, Operand };

        BinaryOp(const SLocus& Whence, Vst::NodeType NodeType) : Node(NodeType, Whence) {}
        BinaryOp(const SLocus& Whence, const TSRef<Node>& LhsOperand, Vst::NodeType NodeType)
            : Node(NodeType, Whence)
        {
            LhsOperand->SetTag((uint8_t)op::Operand);
            AppendChildInternal(LhsOperand);
        }

        void AppendChild(op in_op, const TSRef<Node> in_child)
        {
            in_child->SetTag(static_cast<uint8_t>(in_op));
            AppendChildInternal(in_child);
        }

    protected:
        void AppendOperation_Internal(const TSRef<Node>& InOperator, const TSRef<Node>& Operand)
        {
            InOperator->SetTag((uint8_t)op::Operator);
            AppendChildInternal(InOperator);

            Operand->SetTag((uint8_t)op::Operand);
            AppendChildInternal(Operand);
        }
    };

    struct BinaryOpAddSub : public BinaryOp
    {
        static const Vst::NodeType StaticType = NodeType::BinaryOpAddSub;

        BinaryOpAddSub(const SLocus& Whence) : BinaryOp(Whence, StaticType) {}
        BinaryOpAddSub(const SLocus& Whence, const TSRef<Node>& LhsOperand)
            : BinaryOp(Whence, LhsOperand, StaticType)
        {
        }

        VERSECOMPILER_API void AppendAddOperation(const SLocus& AddWhence, const TSRef<Node>& RhsOperand);
        VERSECOMPILER_API void AppendSubOperation(const SLocus& SubWhence, const TSRef<Node>& RhsOperand);
    };

    struct BinaryOpMulDivInfix : public BinaryOp
    {
        static const Vst::NodeType StaticType = NodeType::BinaryOpMulDivInfix;

        BinaryOpMulDivInfix(const SLocus& Whence) : BinaryOp(Whence, StaticType) {}
        BinaryOpMulDivInfix(const SLocus& Whence, const TSRef<Node>& LhsOperand)
            : BinaryOp(Whence, LhsOperand, StaticType)
        {
        }

        void AppendInfixOperation(const TSRef<Identifier>& OpIdentifier, const TSRef<Node>& RhsOperand)
        {
            AppendOperation_Internal(OpIdentifier, RhsOperand);
        }

        VERSECOMPILER_API void AppendMulOperation(const SLocus& MulWhence, const TSRef<Node>& RhsOperand);
        VERSECOMPILER_API void AppendDivOperation(const SLocus& DivWhence, const TSRef<Node>& RhsOperand);
    };

    struct BinaryOpRange : public Node
    {
        static const Vst::NodeType StaticType = NodeType::BinaryOpRange;
        BinaryOpRange(const SLocus& Whence, const TSRef<Node>& Lhs, const TSRef<Node>& Rhs)
            : Node(StaticType, Whence)
        {
            _Children.Reserve(2);
            AppendChild(Lhs);
            AppendChild(Rhs);
        }

        void AppendChild(const TSRef<Vst::Node>& Rhs) { AppendChildInternal(Rhs); }
    };

    struct BinaryOpArrow : public Node
    {
        static const Vst::NodeType StaticType = NodeType::BinaryOpArrow;
        BinaryOpArrow(const SLocus& Whence, const TSRef<Node>& Lhs, const TSRef<Node>& Rhs)
            : Node(StaticType, Whence)
        {
            _Children.Reserve(2);
            AppendChild(Lhs);
            AppendChild(Rhs);
        }
    };

    struct PrePostCall : public Node
    {
        static const Vst::NodeType StaticType = NodeType::PrePostCall;

        enum Op { Expression, Option, Pointer, DotIdentifier, SureCall, FailCall };

        PrePostCall(const TSRef<Node>& FirstChild, const SLocus& Whence) : Node(StaticType, Whence)
        {
            AppendChild(FirstChild);
        }

        PrePostCall(const SLocus& Whence) : Node(StaticType, Whence)
        {
        }

        bool IsSimpleCall() const
        {
            return (GetChildCount() == 2)
                && (GetChildren()[0]->GetTag<Op>() == Expression)
                && ((GetChildren()[1]->GetTag<Op>() == SureCall) || (GetChildren()[1]->GetTag<Op>() == FailCall));
        }

        bool IsPostHat() const
        {
            return (GetChildCount() == 2)
                && (GetChildren()[0]->GetTag<Op>() == Expression)
                && (GetChildren()[1]->GetTag<Op>() == Pointer);
        }

        VERSECOMPILER_API
        TSRef<Clause> PrependQMark(const SLocus& Whence);

        VERSECOMPILER_API
        TSRef<Clause> PrependHat(const SLocus& Whence);

        VERSECOMPILER_API
        void PrependCallArgs(bool bCanFail, const TSRef<Clause>& Args);

        VERSECOMPILER_API
        void AppendQMark(const SLocus& Whence);

        VERSECOMPILER_API
        void AppendHat(const SLocus& Whence);

        VERSECOMPILER_API
        void AppendCallArgs(bool bCanFail, const TSRef<Clause>& Args);

        VERSECOMPILER_API
        void AppendDotIdent(const SLocus& Whence, const TSRef<Identifier>& Ident);

        VERSECOMPILER_API
        TSPtr<Clause> TakeLastArgs();

    };

    struct IntLiteral : public CAtom
    {
        static const Vst::NodeType StaticType = NodeType::IntLiteral;

        IntLiteral(const CUTF8StringView& InSourceText, const SLocus& Whence) : CAtom(InSourceText, StaticType, Whence) {}
    };

    struct FloatLiteral : public CAtom
    {
        static const Vst::NodeType StaticType = NodeType::FloatLiteral;

        enum class EFormat
        {
            Unspecified,
            F16,
            F32,
            F64
        };

        EFormat _Format;

        FloatLiteral(const CUTF8StringView& InSourceText, EFormat Format, const SLocus& Whence)
            : CAtom(InSourceText, StaticType, Whence)
            , _Format(Format)
        {}
    };

    struct CharLiteral : public CAtom
    {
        static const Vst::NodeType StaticType = NodeType::CharLiteral;

        enum class EFormat
        {
            UTF8CodeUnit,    // char8
            UnicodeCodePoint // char32
        };

        EFormat _Format;

        CharLiteral(const CUTF8StringView& InSourceText, EFormat Format, const SLocus& Whence)
            : CAtom(InSourceText, StaticType, Whence)
            , _Format(Format)
        {}
    };

    struct StringLiteral : public CAtom
    {
        static const Vst::NodeType StaticType = NodeType::StringLiteral;

        StringLiteral( const SLocus& Whence, const CUTF8StringView& SyntaxSource)
            : CAtom(SyntaxSource, StaticType, Whence)
        {}
    };

    struct PathLiteral : public CAtom
    {
        static const Vst::NodeType StaticType = NodeType::PathLiteral;

        PathLiteral(const CUTF8StringView& InPath, const SLocus& Whence) : CAtom(InPath, StaticType, Whence) {}
    };

    struct InterpolatedString : public CAtom
    {
        static const Vst::NodeType StaticType = NodeType::InterpolatedString;

        InterpolatedString(const SLocus& Whence, const CUTF8StringView& SyntaxSource)
            : CAtom(SyntaxSource, StaticType, Whence)
        {}
    };

    struct Lambda : public Node
    {
        static const Vst::NodeType StaticType = NodeType::Lambda;

        Lambda(const SLocus& Whence, const TSRef<Node>& Domain, const TSRef<Node>& Range)
            : Node(StaticType, Whence)
        {
            _Children.Reserve(2);
            AppendChild(Domain);
            AppendChild(Range);
        }

        const TSRef<Node>& GetDomain() const { return _Children[0]; }
        const TSRef<Clause>& GetRange() const { return _Children[1].As<Clause>(); }
    };

    struct Control : public Node
    {
        static const Vst::NodeType StaticType = NodeType::Control;

        enum class EKeyword : uint8_t
        {
            Return,
            Break,
            Yield,
            Continue
        };

        Control(const SLocus& Whence, Control::EKeyword Keyword) : Node(StaticType, Whence), _Keyword(Keyword)
        {
        }

        Control(const TSRef<Node>& Child, const SLocus& Whence, EKeyword Keyword)
            : Control(Whence, Keyword)
        {
            AppendChild(Child);
        }

        const TSRef<Vst::Node>& GetReturnExpression() const { return _Children[0]; }

        EKeyword _Keyword;
    };

    using ClauseArray = LArray<TSRef<Clause>>;

    struct Macro : public Node
    {
        static const Vst::NodeType StaticType = NodeType::Macro;

        Macro(SLocus Whence, const TSRef<Vst::Node>& MacroName, const uLang::TArray<TSRef<Vst::Clause>>& InChildren) : Node(StaticType, Whence)
        {
            _Children.Empty(InChildren.Num() + 1);
            AppendChildInternal(MacroName);
            for (const auto& Child : InChildren)
            {
                AppendChildInternal(Child);
            }
        }

        const TSRef<Vst::Node>& GetName() const { return GetChildren()[0]; }
        const TSRef<Vst::Clause>& GetClause(int32_t ClauseIndex) const { return GetChildren()[ClauseIndex + 1].As<Clause>(); }
        TSRef<Clause> TakeClause(int32_t ClauseIndex, const TSPtr<Node> Replacement = TSPtr<Clause>())
        {
            return TakeChildAt(ClauseIndex + 1, Replacement).As<Clause>();
        }
    };

    struct Parens : public Node
    {
        static const Vst::NodeType StaticType = NodeType::Parens;

        Parens(SLocus Whence, Clause::EForm Form, const NodeArray& InChildren = {})
            : Node(StaticType, Whence), _Form(Form)
        {
            _Children.Reserve(InChildren.Num());
            for (const auto& Child : InChildren)
            {
                AppendChild(Child);
            }
        }

        Parens(SLocus Whence, Clause::EForm Form, const uLang::TSRefArray<Node>& InChildren)
            : Node(StaticType, Whence), _Form(Form)
        {
            _Children.Reserve(InChildren.Num());
            for (const auto& Child : InChildren)
            {
                AppendChild(Child);
            }
        }

        Clause::EForm GetForm() const { return _Form; }

    private:
        Clause::EForm _Form;
    };

    struct Commas : Node
    {
        static const Vst::NodeType StaticType = NodeType::Commas;

        Commas(SLocus Whence, const uLang::TSRefArray<Node>& InChildren = {})
            : Node(StaticType, Whence)
        {
            AppendChildren(InChildren);
        }
    };

    struct Placeholder : public CAtom
    {
        static const Vst::NodeType StaticType = NodeType::Placeholder;

        Placeholder(const SLocus& Whence) : CAtom("", StaticType, Whence) {};
        Placeholder(const uLang::CUTF8StringView& CodeStr, const SLocus& Whence) : CAtom(CodeStr, StaticType, Whence) {};
    };

    struct ParseError : public Node
    {
    public:
        static const Vst::NodeType StaticType = NodeType::ParseError;

        ParseError(const char* Error, const SLocus& Whence)
            : Node(NodeType::ParseError, Whence)
            , _Error(Error)
        {
        }

        const char* GetError() const { return _Error; }

        void AddChild(const TSRef<Vst::Node>& InnerError) { AppendChildInternal(InnerError); }

    private:
        const char* _Error;

    };

    struct Escape : public Node
    {
        static const Vst::NodeType StaticType = NodeType::Escape;

        Escape(const SLocus& Whence, const TSRef<Node>& Child)
            : Node(StaticType, Whence)
        {
            AppendChild(Child);
        }
    };

    /**
     * WHY THIS DESIGN ?
     * -----------------
     *
     *
     * (1) Readability. All the pretty printing code is co-located. It is easier
     *      to work on and to understand.
     * (2) Design flexibility: Policy Pattern lets pipeline stages live outside
     *      of the node class.
     *
     *  e.g. The PrettyPrinter is easier to understand because you can read and
     *      step through the code without leaving a single file.
     *      The implementation can live in a Toolchain module.
     *      You can easily swap out to a different pretty printer implementation
     *      without touching the Vst nodes.
     *
     *  If changing this design (perhaps to improve performance), make sure
     *  to preserve both properties.
     *
     *
     *  NEEDS IMPROVEMENT
     * ------------------
     *
     *  When implementing a VisitPolicy, a programmer must implement every method for every type.
     *  This is not necessarily desirable. Especially true for certain semantic compiler passes.
     **/

    template <class VisitPolicy, typename ReturnType>
    void Node::VisitWith(const TSRef<Vst::Node>& node, VisitPolicy& visit_policy)
    {
        const Vst::NodeType node_type = node->GetElementType();
        switch (node_type)
        {
#define VISIT_VSTNODE(NodeName, RequiredChildren, SupportsManyChildren, Precedence, ChildDeletionBehavior) \
    case Vst::NodeType::NodeName:                                                                          \
        return visit_policy.visit(node->As<Vst::NodeName>());                                              \
        break;
            VERSE_ENUM_VSTNODES(VISIT_VSTNODE)
#undef VISIT_VSTNODE
            default:
                return ReturnType();
                break;
        }
    }

    // Verify all Vst::Node::StaticType are valid
#define VISIT_VSTNODE(NodeName, RequiredChildren, SupportsManyChildren, Precedence, ChildDeletionBehavior) static_assert(NodeName::StaticType == NodeType::NodeName, #NodeName "::StaticType must be Vst::NodeType::" #NodeName);
    VERSE_ENUM_VSTNODES(VISIT_VSTNODE)
#undef VISIT_VSTNODE

}  // namespace Vst

struct SPathToNode
{
    LArray<int32_t> Path;
    LArray<int32_t> AuxPath;

    int32_t PreCommentIndex;
    int32_t PostCommentIndex;
    // @sree @nicka This should be a UID into a Map of Snippets, or something. Anything but a ptr.
};

enum class EPrettyPrintBehaviour : uint32_t
{
    Default                          = 0,
    NewlinesAfterDefinitions         = 1 << 0,
    NewlinesAfterAttributes          = 1 << 1,
    NewlinesBetweenModuleMembers     = 1 << 2,
    UseVerticalFormForEnumerations   = 1 << 3
};
ULANG_ENUM_BIT_FLAGS(EPrettyPrintBehaviour, inline);

/**
 * Appends the text code version of this syntax snippet as closely as possible to how it
 * was originally authored and if it was not authored in text originally (such as added
 * via a VPL) then in as human readable canonical form as possible.
 *
 * @param Source string to append to
 * @see VstSnippetAsCodeSourceChain(), VstSnippetAsCodeSource(), VstAsCodeSourceAppend()
 **/
VERSECOMPILER_API void VstAsCodeSourceAppend(const TSRef<Vst::Node>& VstNode, CUTF8StringBuilder& Source);
VERSECOMPILER_API void VstAsCodeSourceAppend(const TSRef<Vst::Node>& VstNode, const EPrettyPrintBehaviour Flags, CUTF8StringBuilder& Source);
VERSECOMPILER_API void VstAsCodeSourceAppend(const TSRef<Vst::PrePostCall>& VstNode, CUTF8StringBuilder& OutSource, int32_t FirstChildIndex, int32_t LastChildIndex);
VERSECOMPILER_API void VstAsCodeSourceAppend(const TSRef<Vst::Clause>& VstClause, CUTF8StringBuilder& OutSource, int32_t InitialIndent, CUTF8String const& Separator);

ULANG_FORCEINLINE CUTF8String PrettyPrintVst(const TSRef<Vst::Node>& VstNode)
{
    CUTF8StringBuilder Source; VstAsCodeSourceAppend(VstNode, Source); return Source.MoveToString();
}

ULANG_FORCEINLINE CUTF8String PrettyPrintVst(const TSRef<Vst::Node>& VstNode, const EPrettyPrintBehaviour Flags)
{
    CUTF8StringBuilder Source; VstAsCodeSourceAppend(VstNode, Flags, Source); return Source.MoveToString();
}
	
ULANG_FORCEINLINE CUTF8String PrettyPrintVst(const TSRef<Vst::PrePostCall>& VstNode, int32_t FirstChildIndex, int32_t LastChildIndex)
{
    CUTF8StringBuilder Source; VstAsCodeSourceAppend(VstNode, Source, FirstChildIndex, LastChildIndex); return Source.MoveToString();
}

ULANG_FORCEINLINE CUTF8String PrettyPrintClause(const TSRef<Vst::Clause>& VstClause, int32_t InitialIndent, CUTF8String const& Separator)
{
    CUTF8StringBuilder OutSource;
    VstAsCodeSourceAppend(VstClause, OutSource, InitialIndent, Separator);
    return OutSource.MoveToString();
}

VERSECOMPILER_API bool GeneratePathToNode(const TSRef<Vst::Node>& Node, const TSRef<Vst::Snippet>& VstSnippet, SPathToNode& PathToNode);
VERSECOMPILER_API TSPtr<Vst::Node> GetNodeFromPath(const TSRef<Vst::Snippet>& VstSnippet, const SPathToNode& Path, bool bReturnParent=false);

/** 
 * Gets the signed distance between a locus and a row/column text position. The closest distance to the locus range is used.
 * 
 * @param A 			The locus range to compare.
 * @param B 			The text position to compare.
 * @param SourceText 	The original source text that both the locus and position refer to ranges of.
 * 
 * @return 			The signed distance between the two. If A comes before B, the result is positive and vice versa.
 */
VERSECOMPILER_API int32_t GetSignedDistanceBetweenPositionAndLocus(const SLocus& A, const SPosition& B, const CUTF8StringView& SourceText);

}  // namespace Verse
