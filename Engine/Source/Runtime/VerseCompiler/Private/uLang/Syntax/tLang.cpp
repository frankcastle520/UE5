// Copyright Epic Games, Inc. All Rights Reserved.
#include "uLang/Common/Common.h"
#include "uLang/Common/Text/Unicode.h"
#include "uLang/Common/Text/StringUtils.h"
#include "uLang/Common/Misc/EnumUtils.h"
#include "uLang/Common/Text/FilePathUtils.h"
#include "uLang/Common/Text/UTF8StringBuilder.h"
#include "uLang/Common/Text/VerseStringEscaping.h"
#include "uLang/Syntax/VstNode.h"
#include "uLang/Syntax/vsyntax_types.h"  // Alter for new parser?
#include "uLang/Semantics/Expression.h"
#include "uLang/Diagnostics/Glitch.h"

namespace Verse
{
    struct PrettyPrintVisitor
    {
        PrettyPrintVisitor(uLang::CUTF8StringBuilder& out_string, const int32_t InitialIndent = 0)
            : os(out_string)
            , indent_amount(InitialIndent)
            , PrettyFlags(EPrettyPrintBehaviour::Default)
            , bNewlinePending(false)
            , bSpacingNewlinePending(false)
        {
            do_indent();
        }
        
        PrettyPrintVisitor(uLang::CUTF8StringBuilder& out_string, const EPrettyPrintBehaviour PrettyFlags, const int32_t InitialIndent = 0)
            : os(out_string)
            , indent_amount(InitialIndent)
            , PrettyFlags(PrettyFlags)
            , bNewlinePending(false)
            , bSpacingNewlinePending(false)
        {
            do_indent();
        }

        void PrintCommaSeparatedChildren(const Vst::Node& parent)
        {
            const int32_t NumChildren = parent.GetChildCount();
            for (int32_t ChildIndex = 0; ChildIndex < NumChildren; ++ChildIndex)
            {
                if (ChildIndex)
                {
                    os.Append(',');
                }
                PrintElement(parent.GetChildren()[ChildIndex]);
            }
        }

        void PrintAuxAfter(const TSPtr<Vst::Clause>& Aux)
        {
            if (!Aux)
            {
                return;
            }

            for (const TSRef<Vst::Node>& CurrentChild : Aux->AccessChildren())
            {                    
                // the actual attribute node is wrapped in a dummy Clause (used to preserve comments in the VST)
                ULANG_ASSERTF(CurrentChild->IsA<Vst::Clause>(), "attribute nodes are expected to be wrapped in a dummy Clause node with a single child");
                ULANG_ASSERTF(CurrentChild->GetChildCount() == 1, "attribute nodes are expected to be wrapped in a dummy Clause node with a single child");

                for (const TSRef<Vst::Node>& PreCommentNode : CurrentChild->GetPrefixComments())
                {
                    Vst::Node::VisitWith(PreCommentNode, *this);
                }
                // NOTE: (yiliang.siew) We force newlines to never occur for aux attributes that appear after since
                // there is no good way to break them up for the parser atm.
                if (this->bNewlinePending)
                {
                    this->bNewlinePending = false;
                }
                if (this->bSpacingNewlinePending)
                {
                    this->bSpacingNewlinePending = false;
                }
                // TODO: (yiliang.siew)
                //CurrentChild->GetChildren()[0]->SetNewLineAfter(false);
                os.Append('<');
                PrintElement(CurrentChild->GetChildren()[0]);
                os.Append('>');

                for (const TSRef<Vst::Node>& PostCommentNode : CurrentChild->GetPostfixComments())
                {
                    Vst::Node::VisitWith(PostCommentNode, *this);
                }
            }
        }

        void PrintNodeNewLinesBefore(const TSRef<Vst::Node>& InNode)
        {
            if (InNode->HasNewLinesBefore())
            {
                for (int32_t Index = 0; Index < InNode->NumNewLinesBefore(); ++Index)
                {
                    os.Append('\n');
                }
                do_indent();
                bNewlinePending = false;
            }
        }

        void PrintNodeNewLinesAfter(const TSRef<Vst::Node>& InNode)
        {
            if (bNewlinePending && InNode->HasNewLineAfter())
            {
                for (int32_t Index = 0; Index < InNode->NumNewLinesAfter(); ++Index)
                {
                    os.Append('\n');
                }
                bNewlinePending = false;
            }
        }

        void PrintElement(const TSRef<Vst::Node>& InNode)
        {
            PrintNodeNewLinesBefore(InNode);
            // NOTE: (yiliang.siew) We indent when needed, which means after there is a line break.
            // This simplifies trying to indent when printing newlines after the current node, because
            // there are multiple callsites that invoke `PrintElement` for lists of expressions, and
            // we don't want to generate needless indentation.
            if (os.LastByte() == '\n')
            {
                do_indent();
            }
            const TSPtr<Vst::Clause>& Aux = InNode->GetAux();
            bool bPrintAuxAfter = InNode->IsA<Vst::Identifier>() || InNode->IsA<Vst::PrePostCall>();
            bool bPrintAnyAux = Aux.IsValid() && !InNode->IsA<Vst::Mutation>();
            bool bCanHaveLineBreak = true;
            if (InNode->HasParent())
            {
                const Vst::Node* Parent = InNode->GetParent();
                // If the current node is the return type in a function, we cannot place it on a newline;
                // the parser doesn't support this syntax.
                if (Parent->IsA<Vst::TypeSpec>() && Parent->GetChildren().IndexOfByKey(InNode) == 1)
                {
                    bCanHaveLineBreak = false;
                }
            }
            if (bSpacingNewlinePending && bCanHaveLineBreak)
            {
                // TODO: (YiLiangSiew) All instances that assume adding a newline as just an LF character is wrong.
                // This _has_ to take existing line endings into account. And indentation as well.
                os.Append('\n');
                do_indent();
                bSpacingNewlinePending = false;
            }
            if (bPrintAnyAux && !bPrintAuxAfter)
            {
                for (const TSRef<Vst::Node>& CurrentChild : Aux->AccessChildren())
                {
                    if (bNewlinePending)
                    {
                        bNewlinePending = false;
                        os.Append('\n');
                        do_indent();
                    }
                    // Because each attribute will have a newline after itself, we take into account if a attribute
                    // was just printed with a newline, and add the necessary indentation.
                    else if (os.LastByte() == '\n')
                    {
                        do_indent();
                    }

                    // the actual attribute node is wrapped in a dummy Clause (used to preserve comments in the VST)
                    ULANG_ASSERTF(CurrentChild->IsA<Vst::Clause>(), "attribute nodes are expected to be wrapped in a dummy Clause node with a single child");
                    ULANG_ASSERTF(CurrentChild->GetChildCount() == 1, "attribute nodes are expected to be wrapped in a dummy Clause node with a single child");

                    for (const TSRef<Vst::Node>& PreCommentNode : CurrentChild->GetPrefixComments())
                    {
                        Vst::Node::VisitWith(PreCommentNode, *this);
                    }

                    os.Append('@');
                    const TSRef<Vst::Node> AttributeNode = CurrentChild->GetChildren()[0];
                    PrintElement(AttributeNode);
                    // NOTE: (yiliang.siew) Force spaces between attributes or newlines, depending on the context.
                    if (!AttributeNode->HasNewLineAfter()
                        // If there is already a newline, do not add a space. This newline could have been printed by a postfix comment.
                        && bCanHaveLineBreak && !HasTrailingNewLine(this->os))
                    {
                        os.Append(' ');
                    }

                    for (const TSRef<Vst::Node>& PostCommentNode : CurrentChild->GetPostfixComments())
                    {
                        Vst::Node::VisitWith(PostCommentNode, *this);
                    }
                }
            }
            // NOTE: (yiliang.siew) We print the prefix comments here because if a node has prepend attributes,
            // they need to be printed _before_ the prefix comments for the current node.
            // Except in the special case of qualified identifiers; we defer printing them because we want to print
            // the preceding expression, then the prefix comments of the identifier, and then the identifier itself.
            // The printing of such prefix comments is handled in the `visit` overload for identifiers.
            const bool bIsQualifiedIdentifier = InNode->IsA<Vst::Identifier>() && InNode->GetChildCount() != 0;
            if (!bIsQualifiedIdentifier)
            {
                for (const TSRef<Vst::Node>& PreCommentNode : InNode->GetPrefixComments())
                {
                    PrintElement(PreCommentNode);
                }
            }

            if (bNewlinePending)
            {
                os.Append('\n');
                do_indent();
                bNewlinePending = false;
            }
            // NOTE: (yiliang.siew) This is a really stupid HACK, but to account for cases where indentation _is_
            // needed, but no newline is actually requested, this check makes sure that the pretty-printer doesn't
            // forget to do the indentation when needed but also not add extraneous newlines in the process.
            else if (os.LastByte() == '\n' && indent_amount > 0)
            {
                do_indent();
            }
            Vst::Node::VisitWith(InNode, *this);
            if (bPrintAnyAux && bPrintAuxAfter)
            {
                PrintAuxAfter(Aux);
            }
            for (const TSRef<Vst::Node>& PostCommentNode : InNode->GetPostfixComments())
            {
                PrintElement(PostCommentNode);
            }

            bNewlinePending = bNewlinePending || InNode->HasNewLineAfter();
            PrintNodeNewLinesAfter(InNode);
        }

        void VisitExpressionList(const Vst::NodeArray& Expressions, const CUTF8StringView& Separator)
        {
            const int32_t NumExpressions = Expressions.Num();
            for (int32_t Idx = 0; Idx < NumExpressions; ++Idx)
            {
                const TSRef<Vst::Node>& Expression = Expressions[Idx];
                ULANG_ASSERT(Expression.IsValid());
                PrintElement(Expression);
                bool bCommentFollowsCurrentComment = false;
                if (Expression->IsA<Vst::Comment>() && Idx < NumExpressions - 1)
                {
                    const TSRef<Vst::Node>& NextExpression = Expressions[Idx + 1];
                    if (NextExpression.IsValid() && NextExpression->IsA<Vst::Comment>())
                    {
                        bCommentFollowsCurrentComment = true;
                    }
                }
                // If there are already trailing newlines between expressions, we do not need an additional separator.
                if (!bNewlinePending && Idx != Expressions.Num() - 1 && CountNumTrailingNewLines(os) == 0 &&
                    // Block comments do not need separators between them.
                    !bCommentFollowsCurrentComment)
                {
                    os.Append(Separator);
                }
            }
        }

        void VisitClause(const TSRef<Vst::Clause>& Node, const CUTF8StringView& Separator)
        {
            VisitExpressionList(Node->GetChildren(), Separator);
        }

        void VisitBinaryOp(const TSRef<Vst::Node>& Operand1, const char* OperandCstr, const TSRef<Vst::Node>& Operand2)
        {
            // NOTE: (yiliang.siew) Because for syntax such as:
            //
            // ```
            // f():void=
            //    return 5
            // ```
            //
            // We do not want the printing of the `f():void` typespec to print a newline before printing the `=` operand,
            // if we notice that the typespec expression has a newline after, we manually remove it and print that newline here
            // _after_ printing the operand. However, it is forced to be limited to 1 newline, as any other value would be invalid.
            if (Operand1->HasNewLineAfter())
            {
                ULANG_ENSUREF(Operand1->NumNewLinesAfter() == 1, "A typespec definition had more than 1 newline set after it, which would result in an invalid parse; this was forced to a single newline instead!");
                Operand1->SetNewLineAfter(false);    // So that we don't print a newline before the operand in this pretty-printing.
                PrintElement(Operand1);
                Operand1->SetNewLineAfter(true);
                bNewlinePending = true;
            }
            else
            {
                PrintElement(Operand1);
            }
            os.Append(OperandCstr);

            int32_t SavedIndentAmount = indent_amount;
            const bool bIsRhsIndentedBlock = bNewlinePending;
            if (bNewlinePending)
            {
                os.Append('\n');
                ++indent_amount;
                do_indent();
                bNewlinePending = false;
            }

            if (Operand2->GetElementType() == Vst::NodeType::Clause)
            {
                const TSRef<Vst::Clause>& RhsClause = Operand2.As<Vst::Clause>();
                if (bIsRhsIndentedBlock)
                {
                    for (const TSRef<Vst::Node>& CommentNode : RhsClause->GetPrefixComments())
                    {
                        Vst::Node::VisitWith(CommentNode, *this);
                    }
                    VisitClause(RhsClause, "");
                    for (const TSRef<Vst::Node>& CommentNode : RhsClause->GetPostfixComments())
                    {
                        Vst::Node::VisitWith(CommentNode, *this);
                    }
                }
                else if (RhsClause->GetForm() == Vst::Clause::EForm::NoSemicolonOrNewline)
                {
                    if (RhsClause->GetChildCount() == 0)
                    {
                        for (const TSRef<Vst::Node>& CommentNode : RhsClause->GetPrefixComments())
                        {
                            Vst::Node::VisitWith(CommentNode, *this);
                        }
                        os.Append('{');
                        for (const TSRef<Vst::Node>& CommentNode : RhsClause->GetPostfixComments())
                        {
                            Vst::Node::VisitWith(CommentNode, *this);
                        }
                        os.Append('}');
                    }
                    else if (RhsClause->GetChildCount() == 1)
                    {
                        for (const TSRef<Vst::Node>& CommentNode : RhsClause->GetPrefixComments())
                        {
                            Vst::Node::VisitWith(CommentNode, *this);
                        }
                        const Vst::Clause::EPunctuation RhsClausePunctuation = RhsClause->GetPunctuation();
                        if (RhsClausePunctuation == Vst::Clause::EPunctuation::Braces)
                        {
                            os.Append('{');
                            VisitClause(RhsClause, ", ");
                            os.Append('}');
                        }
                        else
                        {
                            VisitClause(RhsClause, ", ");
                        }
                        for (const TSRef<Vst::Node>& CommentNode : RhsClause->GetPostfixComments())
                        {
                            Vst::Node::VisitWith(CommentNode, *this);
                        }
                    }
                    else
                    {
                        for (const TSRef<Vst::Node>& CommentNode : RhsClause->GetPrefixComments())
                        {
                            Vst::Node::VisitWith(CommentNode, *this);
                        }
                        os.Append('{');
                        VisitClause(RhsClause, ", ");
                        os.Append('}');
                        for (const TSRef<Vst::Node>& CommentNode : RhsClause->GetPostfixComments())
                        {
                            Vst::Node::VisitWith(CommentNode, *this);
                        }
                    }
                }
                else
                {
                    for (const TSRef<Vst::Node>& CommentNode : RhsClause->GetPrefixComments())
                    {
                        Vst::Node::VisitWith(CommentNode, *this);
                    }
                    os.Append('{');
                    VisitClause(RhsClause, "; ");
                    os.Append(";}");
                    for (const TSRef<Vst::Node>& CommentNode : RhsClause->GetPostfixComments())
                    {
                        Vst::Node::VisitWith(CommentNode, *this);
                    }
                    // // NOTE: (yiliang.siew) Because we don't know what this clause will be in terms of its formatting
                    // // whether using semicolons only, or newlines only, or a mixture. Ideally we need to store trailing
                    // // semicolon information per-VST node as well and pretty-print that to roundtrip accurately.
                    // // This is a stopgap measure; if all the nodes in the clause have newlines, then we'll infer that curly
                    // // braces are not required.
                    // bool bUsingCurlyBraces = false;
                    // for (const TSRef<Vst::Node>& Node : RhsClause->GetChildren())
                    // {
                    //     if (!Node->HasNewLineAfter())
                    //     {
                    //         bUsingCurlyBraces = true;
                    //         break;
                    //     }
                    // }
                    // if (!bUsingCurlyBraces)
                    // {
                    //     // NOTE: (yiliang.siew) We need to manually add a newline after the `=` token.
                    //     os.Append('\n');
                    //     ++indent_amount;
                    //     do_indent();
                    //     VisitClause(RhsClause, "");
                    //     --indent_amount;
                    // }
                    // else
                    // {
                    //     os.Append('{');
                    //     VisitClause(RhsClause, "; ");
                    //     os.Append(';');
                    // }
                    // for (const TSRef<Vst::Node>& CommentNode : RhsClause->GetPostfixComments())
                    // {
                    //     Vst::Node::VisitWith(CommentNode, *this);
                    // }
                    // if (bUsingCurlyBraces)
                    // {
                    //     os.Append('}');
                    // }
                }
            }
            else
            {
                PrintElement(Operand2);
            }

            indent_amount = SavedIndentAmount;
        }

        void visit(const Vst::Comment& node)
        {
            if (this->bNewlinePending)
            {
                os.Append("\n");
                this->bNewlinePending = false;
            }
            if (node._Type == Vst::Comment::EType::block)
            {
                os.Append(node.GetSourceText());
            }
            else if(node._Type == Vst::Comment::EType::line)
            {
                os.Append(node.GetSourceText());
                bNewlinePending = true;
            }
            else if (node._Type == Vst::Comment::EType::ind)
            {
                os.Append(node.GetSourceText());
                bNewlinePending = true;
            }
            else if (node._Type == Vst::Comment::EType::frag)
            {
                os.Append(node.GetSourceText());
                bNewlinePending = true;
            }
        }

        void visit(const Vst::Project& node)
        {
            for (const TSRef<Vst::Node>& child : node.GetChildren())
            {
                PrintElement(child);
            }
        }

        void visit(const Vst::Package& node)
        {
            for (const TSRef<Vst::Node>& child : node.GetChildren())
            {
                PrintElement(child);
            }
        }

        void visit(const Vst::Module& node)
        {
            for (const TSRef<Vst::Node>& child : node.GetChildren())
            {
                PrintElement(child);
            }
        }

        void visit(const Vst::Snippet& node)
        {
            VisitExpressionList(node.GetChildren(), node.GetForm() == Vst::Clause::EForm::NoSemicolonOrNewline ? ", " : "");
        }

        void visit(const Vst::PrefixOpLogicalNot& node)
        {
            os.Append("not");

            const TSRef<Vst::Node>& Operand = node.GetInnerNode();

            if (node.Whence().EndRow() < Operand->Whence().BeginRow())
            {
                os.Append('\n');
                ++indent_amount;
                do_indent();
                Vst::Node::VisitWith(Operand, *this);
                --indent_amount;
            }
            else
            {
                os.Append(' ');
                Vst::Node::VisitWith(Operand, *this);
            }
        }

        void visit(const Vst::Definition& Node)
        {
            const TSRef<Vst::Node>& LeftOperand = Node.GetOperandLeft();

            const char* OpCStr;
            if (LeftOperand->IsA<Vst::TypeSpec>())
            {
                // `x:t := v` can be simplified to `x:t = v`
                // TODO: (yiliang.siew) This needs to not have the spaces baked in since the roundtripping tests
                // are also specialized to that as well.
                OpCStr = " = ";
            }
            else
            {
                OpCStr = " := ";
            }

            const TSRef<Vst::Node>& RightOperand = Node.GetOperandRight();

            VisitBinaryOp(LeftOperand, OpCStr, RightOperand);

            // Adding a newline between declarations
            // Search RHS child[0][0][0][0]... for a newline
            bool bEncounteredNewline = LeftOperand->HasNewLineAfter();
            auto CurrentNode = RightOperand;
            while (CurrentNode || !bEncounteredNewline)
            {
                if (CurrentNode->HasNewLineAfter())
                {
                    bEncounteredNewline = true;
                    break;
                }

                if (CurrentNode->AccessChildren().Num() > 0)
                {
                    CurrentNode = CurrentNode->AccessChildren()[0];
                }
                else
                {
                    CurrentNode.Reset();
                    break;
                }
            }
            if (bEncounteredNewline)
            {
                bSpacingNewlinePending = true;
            }
        }

        void visit(const Vst::Assignment& Node)
        {
            using EOp = Vst::Assignment::EOp;

            const char* OpCStr = " UnknownOp";

            switch (Node.GetOperandRight()->GetTag<EOp>())
            {
            case EOp::assign: OpCStr = " = "; break;
            case EOp::addAssign: OpCStr = " += "; break;
            case EOp::subAssign: OpCStr = " -= "; break;
            case EOp::mulAssign: OpCStr = " *= "; break;
            case EOp::divAssign: OpCStr = " /= "; break;
            default: ULANG_ENSUREF(false, "Unknown assignment operator!"); break;
            }

            VisitBinaryOp(Node.GetOperandLeft(), OpCStr, Node.GetOperandRight());
        }

        void visit(const Vst::BinaryOpCompare& node)
        {
            const char* OpCStr = " UnknownOp";

            switch (node.GetOp())
            {
            case Vst::BinaryOpCompare::op::lt:    OpCStr = " < ";  break;
            case Vst::BinaryOpCompare::op::lteq:  OpCStr = " <= "; break;
            case Vst::BinaryOpCompare::op::gt:    OpCStr = " > ";  break;
            case Vst::BinaryOpCompare::op::gteq:  OpCStr = " >= "; break;
            case Vst::BinaryOpCompare::op::eq:    OpCStr = " = ";  break;
            case Vst::BinaryOpCompare::op::noteq: OpCStr = " <> "; break;
            default: ULANG_ENSUREF(false, "Unknown compare operator!"); break;
            }

            VisitBinaryOp(node.GetOperandLeft(), OpCStr, node.GetOperandRight());
        }

        void visit(const Vst::BinaryOpLogicalOr& node)
        {
            const auto& children = node.GetChildren();
            const auto num_children = children.Num();

            if (num_children > 1)
            {
                // Note, we are forcing all uses of 'and' in the context of an 'or' to be parenthesized
                //TODO: (jcotton) parentheses being forced is causing if conditions to be borderline non-functional to edit in VV, disabled until a better solution is decided on for VV
                // e.g. (a and b) or c     # those parentheses are mandatory
                //const int32_t OpAnd_Precedence_NotATypo = GetOperatorPrecedence(Vst::NodeType::BinaryOpLogicalAnd);

                //const bool bFirstEltNeedsParen = children[0]->GetPrecedence() <= OpAnd_Precedence_NotATypo;
                //if (bFirstEltNeedsParen) os.Append('(');
                PrintElement(children[0]);
                //if (bFirstEltNeedsParen) os.Append(')');

                for (int32_t i = 1; i < num_children; ++i)
                {
                    os.Append(" or ");

                    //const bool bNeedParensDueToChildAfterOperator = children[i]->GetPrecedence() <= OpAnd_Precedence_NotATypo;

                    //if (bNeedParensDueToChildAfterOperator) os.Append('(');
                    PrintElement(children[i]);
                    //if (bNeedParensDueToChildAfterOperator) os.Append(')');
                }
            }
            else if (num_children == 1)
            {
                ULANG_ERRORF("LogicalOperatorOr has just one child; how did that happen?");
                PrintElement(children[0]);
            }
            else
            {
                ULANG_ERRORF("LogicalOperatorOr has no child nodes; why does it even exist?.");
            }
        }

        void visit(const Vst::BinaryOpLogicalAnd& node)
        {
            const auto& children = node.GetChildren();
            const auto num_children = children.Num();

            if (num_children > 1)
            {
                //const int32_t OpAnd_Precedence = GetOperatorPrecedence(Vst::NodeType::BinaryOpLogicalAnd);
                //const bool bFirstEltNeedsParen = children[0]->GetPrecedence() <= OpAnd_Precedence;
                //if (bFirstEltNeedsParen) os.Append('(');
                PrintElement(children[0]);
                //if (bFirstEltNeedsParen) os.Append(')');

                for (int32_t i = 1; i < num_children; ++i)
                {
                    os.Append(" and ");

                    //const bool bNeedParensDueToChildAfterOperator = children[i]->GetPrecedence() <= OpAnd_Precedence;

                    //if (bNeedParensDueToChildAfterOperator) os.Append('(');
                    PrintElement(children[i]);
                    //if (bNeedParensDueToChildAfterOperator) os.Append(')');
                }
            }
            else if (num_children == 1)
            {
                ULANG_ERRORF("LogicalOperatorAnd has just one child; how did that happen?");
                PrintElement(children[0]);
            }
            else
            {
                ULANG_ERRORF("LogicalOperatorAnd has no child nodes; why does it even exist?.");
            }
        }

        void visit(const Vst::BinaryOp& node)
        {
            const auto& children = node.GetChildren();
            const auto num_children = children.Num();

            if (num_children > 1)
            {
                Vst::NodeType NodeType = node.GetElementType();
                const int32_t Op_Precedence = GetOperatorPrecedence(NodeType);
                const bool bFirstEltNeedsParen = children[0]->GetPrecedence() <= Op_Precedence;
                
                const bool bHasPrefix = children[0]->GetElementType() == Vst::NodeType::Operator;
                if (bHasPrefix)
                {
                    os.Append(children[0]->As<Vst::Operator>().GetSourceText());
                }
                else
                {
                    if (bFirstEltNeedsParen) os.Append('(');
                    PrintElement(children[0]);
                    if (bFirstEltNeedsParen) os.Append(')');
                }

                for (int32_t i = 1; i < num_children; ++i)
                {
                    auto& Operator = children[i];
                    if (Operator->GetElementType() == Vst::NodeType::Operator)
                    {
                        os.Append(' ');
                        os.Append(Operator->As<Vst::Operator>().GetSourceText());
                        os.Append(' ');
                    }
                    else
                    {
                        PrintElement(Operator);
                    }

                    // print the operand
                    i += 1; // move to next child
                    
                    if (node.IsA<Vst::BinaryOpMulDivInfix>())
                    {
                        ULANG_ENSUREF(node.IsA<Vst::BinaryOpAddSub>() || i < num_children, "Malformed binary mul/div node -- missing trailing operand.");
                    }

                    if (i < num_children)
                    {
                        const bool bNeedParensDueToChildAfterOperator = children[i]->GetPrecedence() <= Op_Precedence;

                        if (bNeedParensDueToChildAfterOperator) os.Append('(');

                        auto& TrailingNode = children[i];
                        if (TrailingNode->GetElementType() == Vst::NodeType::Operator)
                        {
                            os.Append(' ');
                            os.Append(TrailingNode->As<Vst::Operator>().GetSourceText());
                            os.Append(' ');
                        }
                        else
                        {
                            PrintElement(TrailingNode);
                        }

                        if (bNeedParensDueToChildAfterOperator) os.Append(')');
                    }
                }
            }
            else if (num_children == 1)
            {
                ULANG_ERRORF("BinaryOp has just one child; how did that happen?");
                PrintElement(children[0]);
            }
            else
            {
                ULANG_ERRORF("BinaryOp has no child nodes; why does it even exist?.");
            }
        }

        void visit(const Vst::BinaryOpRange& node)
        {
            const auto& children = node.GetChildren();
            if (children.Num() == 2)
            {
                const int32_t Range_Precedence = GetOperatorPrecedence(Vst::NodeType::BinaryOpRange);
                const bool bFirstEltNeedsParen = children[0]->GetPrecedence() <= Range_Precedence;
                if (bFirstEltNeedsParen) os.Append('(');
                PrintElement(children[0]);
                if (bFirstEltNeedsParen) os.Append(')');

                os.Append("..");

                const bool bSecondEltNeedsParen = children[1]->GetPrecedence() <= Range_Precedence;
                if (bSecondEltNeedsParen) os.Append('(');
                PrintElement(children[1]);
                if (bSecondEltNeedsParen) os.Append(')');
            }
            else
            {
                ULANG_ERRORF("BinaryOpRange must have exactly two children.");
            }
        }

        void visit(const Vst::BinaryOpArrow& node)
        {
            const auto& children = node.GetChildren();
            if (children.Num() == 2)
            {
                const int32_t ArrowPrecedence = GetOperatorPrecedence(Vst::NodeType::BinaryOpArrow);
                const bool bFirstEltNeedsParen = children[0]->GetPrecedence() <= ArrowPrecedence;
                if (bFirstEltNeedsParen) os.Append('(');
                PrintElement(children[0]);
                if (bFirstEltNeedsParen) os.Append(')');

                os.Append("->");

                const bool bSecondEltNeedsParen = children[1]->GetPrecedence() <= ArrowPrecedence;
                if (bSecondEltNeedsParen) os.Append('(');
                PrintElement(children[1]);
                if (bSecondEltNeedsParen) os.Append(')');
            }
            else
            {
                ULANG_ERRORF("BinaryOpArrow must have exactly two children.");
            }
        }

        void visit(const Vst::Where& Node)
        {
            if (Node.GetChildCount() < 1)
            {
                ULANG_ERRORF("Where must have at least one child.");
            }
            PrintElement(Node.GetLhs());
            os.Append(" where");
            Vst::Where::RhsView Rhs = Node.GetRhs();
            if (Rhs.IsEmpty())
            {
                return;
            }
            os.Append(' ');
            auto I = Rhs.begin();
            PrintElement(*I);
            ++I;
            for (auto Last = Rhs.end(); I != Last; ++I)
            {
                os.Append(", ");
                PrintElement(*I);
            }
        }

        void visit(const Vst::Mutation& Node)
        {
            if (Node.GetChildCount() != 1)
            {
                ULANG_ERRORF("Var must have one child.");
            }
            switch (Node._Keyword)
            {
            case Vst::Mutation::EKeyword::Var:
                os.Append("var");
                PrintAuxAfter(Node.GetAux());
                os.Append(' ');
                break;
            case Vst::Mutation::EKeyword::Set:
                os.Append("set ");
                break;
            default:
                ULANG_UNREACHABLE();
            }
            PrintElement(Node.Child());
        }

        void visit(const Vst::TypeSpec& node)
        {
            if (node.GetChildCount() == 2)
            {
                const auto& Lhs = node.GetLhs();
                const auto& Rhs = node.GetRhs();
                const bool LhsNeedsParens = Lhs->GetPrecedence() <= GetOperatorPrecedence(Vst::NodeType::TypeSpec);
                const bool RhsNeedsParens = Rhs->GetPrecedence() <= GetOperatorPrecedence(Vst::NodeType::TypeSpec);

                if (LhsNeedsParens) os.Append('(');
                PrintElement(Lhs);
                if (LhsNeedsParens) os.Append(')');
                os.Append(':');

                for (const TSRef<Vst::Node>& CommentNode : node._TypeSpecComments)
                {
                    Vst::Node::VisitWith(CommentNode, *this);
                }

                if (RhsNeedsParens) os.Append('(');
                PrintElement(Rhs);
                if (RhsNeedsParens) os.Append(')');
            }
            else if (node.GetChildCount() == 1)
            {
                os.Append(':');

                for (const TSRef<Vst::Node>& CommentNode : node._TypeSpecComments)
                {
                    Vst::Node::VisitWith(CommentNode, *this);
                }

                const auto& Type = node.GetChildren()[0];
                PrintElement(Type);
            }
            else
            {
                ULANG_ERRORF("TypeSpec must have either one or two children.");
            }
        }

        void visit(const Vst::FlowIf& node)
        {
            // if any children have a trailing newline we are vertical form.                 
            struct CheckChildrenForNewLine
            {
                static bool Do(const Vst::NodeArray& NodeArray)
                {
                    for (auto& curNode : NodeArray)
                    {
                        if (curNode->HasNewLineAfter())
                        {
                            return true;
                        }

                        if (Do(curNode->AccessChildren()))
                        {
                            return true;
                        }
                    }
                    return false;
                }
            };

            using EOp = Vst::FlowIf::ClauseTag;

            const Vst::NodeArray& NodeChildren = node.GetChildren();
            const int32_t NumChildren = node.GetChildCount();

            bool bIsVerticalForm = CheckChildrenForNewLine::Do(NodeChildren);
            bool bCStyleIf = (NumChildren >= 1) && (NodeChildren[1]->As<Vst::Clause>().GetChildCount() == 1);

            for (int32_t idx = 0; idx < NumChildren; idx += 1)
            {
                const auto& IfClause = NodeChildren[idx]->As<Vst::Clause>();
                const bool bIsFirstEntry = (idx == 0);
                const bool bMoreThanOneChild = IfClause.GetChildCount() > 1; 
                const bool bNoChildren = IfClause.GetChildCount() == 0; 
                const bool bOneChild = IfClause.GetChildCount() == 1; 
                const bool bSingleChildIsComment = bOneChild && IfClause.GetChildren()[0]->IsA< Vst::Comment>();

                bool bUseBraces = false;
                bool bDoIndent = false; // indent after tag

                // TODO: (yiliang.siew) Maybe not needed?
                if (bIsVerticalForm && (idx != 0) && (IfClause.GetTag<EOp>() != EOp::condition) && (IfClause.GetTag<EOp>() != EOp::then_body || !bCStyleIf))
                {
                    if (os.LastByte() != '\n')
                    {
                        os.Append('\n');
                    }
                    // NOTE: (YiLiangSiew) If we already added a newline, doesn't matter which node prior has a pending newline anymore;
                    // it's no longer relevant. This way, we avoid "doubling up" on newline formattting.
                    this->bNewlinePending = false;
                    do_indent();
                }

                if (IfClause.GetTag<EOp>() == EOp::if_identifier)
                {
                    if (!bIsFirstEntry)
                    {
                        if (bIsVerticalForm && !bCStyleIf)
                        {
                            bNewlinePending = true;
                        }
                        else
                        {
                            bUseBraces = true;
                        }
                    }

                    for (const TSRef<Vst::Node>& CommentNode : IfClause.GetPrefixComments())
                    {
                        Vst::Node::VisitWith(CommentNode, *this);
                    }

                    if(!bIsFirstEntry)
                    {
                        os.Append("else ");
                    }

                    os.Append("if");

                    for (const TSRef<Vst::Node>& CommentNode : IfClause.GetPostfixComments())
                    {
                        Vst::Node::VisitWith(CommentNode, *this);
                    }
                }

                if (IfClause.GetTag<EOp>() == EOp::condition)
                {
                    // For printing an if with a conditional, but no body
                    if (NumChildren == 1 && bOneChild)
                    {
                        os.Append(":");
                        bIsVerticalForm = true;
                        bUseBraces = false; 
                        bDoIndent = true;
                    }
                    else if (bIsVerticalForm)
                    {
                        if (bCStyleIf)
                        {
                            os.Append(" ");
                            bDoIndent = false;
                            bUseBraces = true;
                        }
                        else
                        {
                            bNewlinePending = true;
                            bDoIndent = true;
                            os.Append(':');
                        }
                    }
                    else
                    {
                        bUseBraces = true;
                    }
                }

                if (IfClause.GetTag<EOp>() == EOp::then_body)
                {
                    if (bIsVerticalForm)
                    {
                        if (bCStyleIf)
                        {
                            bDoIndent = true;
                        }
                        else
                        {
                            os.Append("then:");

                            bNewlinePending = true;
                            bDoIndent = true;
                        }
                    }
                    else
                    {
                        os.Append("then ");
                    }
                }

                if (IfClause.GetTag<EOp>() == EOp::else_body)
                {
                    if (bIsVerticalForm)
                    {
                        bNewlinePending = true;
                    }
                    for (const TSRef<Vst::Node>& CurComment : IfClause.GetPrefixComments())
                    {
                        Vst::Node::VisitWith(CurComment, *this);
                    }
                    os.Append("else");

                    if (bIsVerticalForm)
                    {
                        os.Append(':');
                    }
                    else
                    {
                        os.Append(" ");
                    }

                    if (bCStyleIf)
                    {
                        if (bIsVerticalForm)
                        {
                            bDoIndent = true;
                        }
                    }
                    else
                    {
                        if (bIsVerticalForm)
                            bDoIndent = true;
                    }
                }

                const int32_t ConditionalChildCount = IfClause.GetChildCount();
                bool bWasIndented = false;
                if (bIsVerticalForm && bDoIndent)
                {
                    // TODO: (yiliang.siew) Maybe not required?
                    ++indent_amount;
                    bWasIndented = true;
                    if (bCStyleIf)
                    {
                        bNewlinePending = true;
                    }
                }

                if (IfClause.GetTag<EOp>() != EOp::if_identifier)
                {
                    // NOTE: (yiliang.siew) We can skip this for `else` clauses because we already printed it above.
                    if (IfClause.GetTag<EOp>() != EOp::else_body)
                    {
                        for (const TSRef<Vst::Node>& CommentNode : IfClause.GetPrefixComments())
                        {
                            Vst::Node::VisitWith(CommentNode, *this);
                        }
                    }

                    if (bUseBraces)
                        os.Append("(");
                    else if ((bMoreThanOneChild || bNoChildren || bSingleChildIsComment) && !bIsVerticalForm)
                        os.Append("{");
                }

                for (int32_t ConditionalIdx = 0; ConditionalIdx < ConditionalChildCount; ConditionalIdx += 1)
                {
                    if (!bIsVerticalForm)
                    {
                        bNewlinePending = false;
                    }

                    PrintElement(IfClause.GetChildren()[ConditionalIdx]);

                    if (bIsVerticalForm)
                    {
                        if (!IfClause.GetChildren()[ConditionalIdx]->HasNewLineAfter())
                        {
                            if (ConditionalIdx != ConditionalChildCount - 1)
                            {
                                os.Append(", ");
                            }
                        }
                    }
                    else
                    {
                        if (ConditionalIdx != ConditionalChildCount - 1)
                        {
                            os.Append("; ");
                        }
                    }
                }

                if (IfClause.GetTag<EOp>() != EOp::if_identifier)
                {
                    if (bUseBraces)
                    {
                        os.Append(")");
                    }
                    else if ((bMoreThanOneChild || bNoChildren || bSingleChildIsComment) && !bIsVerticalForm)
                    {
                        os.Append("}");
                    }

                    for (const TSRef<Vst::Node>& CommentNode : IfClause.GetPostfixComments())
                    {
                        Vst::Node::VisitWith(CommentNode, *this);
                    }

                    if (bCStyleIf && IfClause.GetTag<EOp>() == EOp::condition && bIsVerticalForm && NumChildren != 1)
                    {
                        os.Append(':');
                    }

                    // TODO: (yiliang.siew) Maybe not required anymore?
                    if (bWasIndented){ --indent_amount; }

                    if (!bIsVerticalForm && idx != NumChildren-1) { os.Append(' '); }
                }
            }
        }

        void VisitPrePostCall(const Vst::PrePostCall& node, int32_t First, int32_t Last)
        {
            for (int i = First; i <= Last; i += 1)
            {
                const auto& child = node.GetChildren()[i];
                using Op = Verse::Vst::PrePostCall::Op;
                auto thisOp = child->GetTag<Op>();
                bool bPrintPostComments = true;
                if (thisOp == Op::Expression)
                {
                    PrintElement(child);
                    bPrintPostComments = false;
                }
                else if (thisOp == Op::DotIdentifier)
                {
                    if (i > First)
                    {
                        os.Append('.');
                    }
                    PrintElement(child);
                    bPrintPostComments = false;
                }
                else if (thisOp == Op::FailCall || thisOp == Op::SureCall)
                {
                    os.Append(thisOp == Op::SureCall ? "(" : "[");
                    for (const TSRef<Vst::Node>& CommentNode : child->GetPrefixComments())
                    {
                        Vst::Node::VisitWith(CommentNode, *this);
                    }
                    VisitClause(child.As<Vst::Clause>(), ", ");
                    for (const TSRef<Vst::Node>& CommentNode : child->GetPostfixComments())
                    {
                        Vst::Node::VisitWith(CommentNode, *this);
                    }
                    os.Append(thisOp == Op::SureCall ? ")" : "]");
                    bPrintPostComments = false;
                }
                else if (thisOp == Op::Pointer)
                {
                    os.Append('^');
                }
                else if (thisOp == Op::Option)
                {
                    os.Append('?');
                }

                if (bPrintPostComments)
                {
                    for (const TSRef<Vst::Node>& PostCommentNode : child->GetPostfixComments())
                    {
                        Vst::Node::VisitWith(PostCommentNode, *this);
                    }
                }
            }
        }

        void visit(const Vst::PrePostCall& node)
        {
            const int32_t NumChildren = node.GetChildCount();
            VisitPrePostCall(node, 0, NumChildren - 1);
        }

        void visit(const Vst::Identifier& node)
        {
            if (node.GetChildCount())
            {
                for (const TSRef<Vst::Node>& CurComment : node._QualifierPreComments)
                {
                    Vst::Node::VisitWith(CurComment, *this);
                }
                os.Append('(');
                PrintCommaSeparatedChildren(node);
                os.Append(":)");

                for (const TSRef<Vst::Node>& CurComment : node.GetPrefixComments())
                {
                    Vst::Node::VisitWith(CurComment, *this);
                }
                for (const TSRef<Vst::Node>& PostCommentNode : node._QualifierPostComments)
                {
                    Vst::Node::VisitWith(PostCommentNode, *this);
                }
            }
            os.Append(node.GetStringValue());
        }
        
        void visit(const Vst::Operator& node)
        {
            os.Append(node.GetStringValue());
        }

        void visit(const Vst::IntLiteral& node)
        {
            os.Append(node.GetStringValue());
        }

        void visit(const Vst::FloatLiteral& node)
        {
            os.Append(node.GetStringValue());
        }

        void visit(const Vst::CharLiteral& node)
        {
            os.Append("\'");
            os.Append(node.GetStringValue());
            os.Append("\'");
        }

        void visit(const Vst::StringLiteral& node)
        {
            os.Append("\"");
            os.Append(uLang::VerseStringEscaping::EscapeString(node.GetStringValue()));
            os.Append("\"");
        }

        void visit(const Vst::PathLiteral& node)
        {
            os.Append(node.GetStringValue());
        }

        void visit(const Vst::InterpolatedString& node)
        {
            os.Append("\"");
            os.Append(node.GetStringValue());
            os.Append("\"");
        }

        void PrintClause(const Vst::Clause& Clause)
        {
            if (Clause.GetChildCount() == 0
                || Clause.GetForm() == Vst::Clause::EForm::NoSemicolonOrNewline)
            {
                const bool bNeedsBraces = Clause.GetChildCount() != 1;
                if (bNeedsBraces)
                {
                    os.Append('{');
                }
                PrintCommaSeparatedChildren(Clause);
                if (bNeedsBraces)
                {
                    os.Append('}');
                }
            }
            else
            {
                const bool bVerticalForm = bNewlinePending || Clause.GetPunctuation() == Vst::Clause::EPunctuation::Indentation;
                if (!bVerticalForm)
                {
                    os.Append('{');
                }
                ++indent_amount;

                const int32_t NumChildren = Clause.GetChildCount(); 
                for (int32_t ChildIndex = 0; ChildIndex < NumChildren; ChildIndex += 1)
                {
                    const TSRef<Vst::Node>& Child = Clause.GetChildren()[ChildIndex];
                    PrintElement(Child);
                    // NOTE: (yiliang.siew) Do not add a semicolon to the start of an expression which already has a newline in front of it.
                    if (!bNewlinePending && ChildIndex + 1 < NumChildren && !HasTrailingNewLine(os))
                    {
                        os.Append(';');
                    }
                }
                
                --indent_amount;
                if (!bVerticalForm)
                {
                    os.Append('}');
                }
            }
        }

        void visit(const Vst::Lambda& node)
        {
            const int32_t NumChildren = node.GetChildCount();
            if (ULANG_ENSUREF(NumChildren >= 2, "Lambda must have at least 2 children"))
            {
                PrintElement(node.GetDomain());
                // NOTE: (yiliang.siew) We take into account if the lambda clause has a leading newline
                // for its first member, and manipulate the pretty-printer into printing it here properly.
                if (node.GetChildCount() > 1 && node.GetChildren()[1]->IsA<Vst::Clause>())
                {
                    const Vst::Clause& TheClause = node.GetChildren()[1]->As<Vst::Clause>();
                    if (TheClause.GetChildCount() > 0 && TheClause.GetChildren()[0]->HasNewLinesBefore())
                    {
                        bNewlinePending = true;
                    }
                }
                os.Append(bNewlinePending ? " =>" : " => ");
                PrintClause(*node.GetRange());
            }
        }

        void visit(const Vst::Control& node)
        {
            bool bPrintReturnExpression = false;

            switch (node._Keyword)
            {
                case Vst::Control::EKeyword::Return:
                    os.Append("return");
                    bPrintReturnExpression = true;
                    break;
                case Vst::Control::EKeyword::Break:
                    os.Append("break");
                    break;
                case Vst::Control::EKeyword::Yield:
                    os.Append("yield");
                    break;
                case Vst::Control::EKeyword::Continue:
                    os.Append("continue");
                    break;
                default:
                    ULANG_UNREACHABLE();
            }

            if (node.GetChildCount() == 0)
            {
                bNewlinePending = true;
                return;
            }

            if (bPrintReturnExpression)
            {
                const TSRef<Vst::Node>& ReturnExpr = node.GetReturnExpression();
                if (ReturnExpr.IsValid())
                {
                    // Append a space after the `return` token.
                    os.Append(' ');
                }
                PrintElement(ReturnExpr);
            }
        }

        void visit(const Vst::Macro& node)
        {
            PrintElement(node.GetName());

            ULANG_ENSUREF(node.GetChildCount() > 1, "Malformed macro");

            const TSRef<Vst::Identifier>& LeftChild = node.GetChildren()[0].As<Vst::Identifier>();
            TSPtr<Vst::Clause> SecondChild;
            if (node.GetChildCount() > 1)
            {
                SecondChild = node.GetChildren()[1].As<Vst::Clause>();
            }
            bool bIsVerticalForm = false;
            for (int ChildIndex = 1; ChildIndex < node.GetChildCount(); ++ChildIndex)
            {
                const TSRef<Vst::Clause>& Child = node.GetChildren()[ChildIndex].As<Vst::Clause>();
                if ((SecondChild && SecondChild->HasNewLineAfter() && SecondChild->GetChildCount() > 0 &&    // If the clause has no children, there's no point giving it a vertical form.
                        ChildIndex == node.GetChildCount() - 1) ||
                    LeftChild->HasNewLineAfter() ||    // If there is a newline after, it has to be a vertical form.
                    Child->HasNewLineAfter())
                {
                    bIsVerticalForm = true;
                }
                else
                {
                    bIsVerticalForm = false;
                }
                const auto Keyword = Child->GetTag<vsyntax::res_t>();

                const bool bUseRoundBrackets = Keyword == vsyntax::res_of && ChildIndex == 1;
                if (bUseRoundBrackets)
                {
                    os.Append("(");
                }
                else
                {
                    if (bIsVerticalForm)
                    {
                        os.Append(':');
                        bNewlinePending = true; 
                    }
                    else
                    {
                        os.Append(" ");
                    }

                    os.Append(vsyntax::scan_reserved_t()[Keyword]);

                    if (!bIsVerticalForm && Keyword != '\0')
                        os.Append(" ");
                    
                    if (!bIsVerticalForm)
                        os.Append("{");
                }

                if (bIsVerticalForm )
                {
                    ++indent_amount;
                }

                const int32_t NumDescendants = Child->GetChildCount();
                for (int DescendantIndex = 0; DescendantIndex < NumDescendants; ++DescendantIndex)
                {
                    const uLang::TSRef<Vst::Node> CurChild = Child->GetChildren()[DescendantIndex];
                    PrintElement(CurChild);
                    // Always print the newlines regardless of whether it is the final element, since
                    // that best respects the attribute set on the VST node.
                    bool bIndentationMaybeNeeded = false;
                    if (bNewlinePending && CurChild->HasNewLineAfter())
                    {
                        for (int32_t Index = 0; Index < CurChild->NumNewLinesAfter(); ++Index)
                        {
                            os.Append('\n');
                        }
                        bIndentationMaybeNeeded = true;
                        bNewlinePending = false;
                    }
                    if (DescendantIndex + 1 < NumDescendants)
                    {
                        // If we are not the last node, we can indent for the next node to be printed. Otherwise
                        // this would just create unneeded indentation.
                        if (bIndentationMaybeNeeded)
                        {
                            do_indent();
                            bIndentationMaybeNeeded = false;
                        }
                        VERSE_SUPPRESS_UNUSED(PrettyFlags);

                        // We do not add commas after comments since it actually changes their text.
                        // We also do not add commas if there is already a trailing newline separator or a newline about to be printed.
                        if (!bNewlinePending && !CurChild->HasNewLineAfter() && !CurChild->IsA<Vst::Comment>() && !HasTrailingNewLine(this->os))
                        {
                            os.Append(", ");
                        }
                    }
                }
                if (bUseRoundBrackets)
                {
                    os.Append(')');
                    // This could occur in a macro's 2nd clause (i.e. `C := class(D):` where `(D)` is considered a vertical form clause that increases the indentation level.)
                    if (bIsVerticalForm)
                    {
                        --indent_amount;
                    }
                }
                else
                { 
                    if (bIsVerticalForm)
                    {   
                        --indent_amount;
                    }
                    else
                    {
                        os.Append('}');
                    }
                }

                for (const TSRef<Vst::Node>& PostCommentNode : Child->GetPostfixComments())
                {
                    Vst::Node::VisitWith(PostCommentNode, *this);
                }

                // Account for clauses that have newlines after themselves set, even if their children do not.
                // NOTE: (yiliang.siew) We print the comments first before setting this attribute so that trailing
                // block comments do not have newlines inserted _before_ they get printed.
                if (Child->HasNewLineAfter())
                {
                    PrintNodeNewLinesAfter(Child);
                }
            }
        }

        /// This is only necessary since we declare the `VISIT_VSTNODE` macro for all VST node types.
        void visit(const Vst::Clause& node)
        {
           ULANG_ENSUREF(false, "A clause means nothing without the context of its parent, the parent is responsible for serializing it");
        }

        void visit(const Vst::Parens& node)
        {
            os.Append('(');
            int32_t ChildCount = node.GetChildCount();

            if (ChildCount)
            {
                PrintElement(node.GetChildren()[0]);

                for (int32_t i = 1; i < ChildCount; ++i)
                {
                    os.Append(", ");
                    PrintElement(node.GetChildren()[i]);
                }
            }
            os.Append(')');
        }

        void visit(const Vst::Commas& node)
        {
            int32_t ChildCount = node.GetChildCount();
            if (ChildCount)
            {
                PrintElement(node.GetChildren()[0]);

                for (int32_t i = 1; i < ChildCount; ++i)
                {
                    os.Append(", ");
                    PrintElement(node.GetChildren()[i]);
                }
            }
        }

        void visit(const Vst::Placeholder& Placeholder)
        {
            // TODO: (YiLiangSiew) This has to take line endings into account.
            if (this->bNewlinePending)
            {
                os.Append("\n");
                this->bNewlinePending = false;
            }
            os.Append("stub{");
            os.Append(Placeholder.GetSourceText());
            os.Append("}");
        }

        void visit(const Vst::ParseError& Error)
        {
            os.AppendFormat("Error (%d:%d) : %s", Error.Whence().BeginRow(), Error.Whence().BeginColumn(), Error.GetError());
        }

        void visit(const Vst::Escape& Escape)
        {
            os.Append('&');
            if (Escape.GetChildCount() == 1)
            {
                PrintElement(Escape.GetChildren()[0]);
            }
        }

    private:
        static constexpr char IndentationString[] = "    ";

        void do_indent()
        {
            for (int i = 0; i < indent_amount; ++i)
            {
                os.Append(IndentationString);
            }
        }

        uLang::CUTF8StringBuilder& os;
        int32_t indent_amount;
        EPrettyPrintBehaviour PrettyFlags;
        bool bNewlinePending;
        bool bSpacingNewlinePending;

    };    // PrettyPrintVisitor

    void VstAsCodeSourceAppend(const TSRef<Vst::Node>& VstNode, uLang::CUTF8StringBuilder& Source)
    {
        PrettyPrintVisitor prettyPrinter(Source);
        Vst::Node::VisitWith(VstNode, prettyPrinter);
    }

    void VstAsCodeSourceAppend(const TSRef<Vst::Node>& VstNode, const EPrettyPrintBehaviour Flags, uLang::CUTF8StringBuilder& Source)
    {
        PrettyPrintVisitor prettyPrinter(Source, Flags);
        Vst::Node::VisitWith(VstNode, prettyPrinter);
    }

    void VstAsCodeSourceAppend(const TSRef<Vst::PrePostCall>& VstNode, uLang::CUTF8StringBuilder& Source, int32_t First, int32_t Last)
    {
        PrettyPrintVisitor prettyPrinter(Source);
        prettyPrinter.VisitPrePostCall(*VstNode.Get(), First, Last);
    }

    void VstAsCodeSourceAppend(const TSRef<Vst::Clause>& VstClause, CUTF8StringBuilder& Source, int32_t InitialIndent, CUTF8String const& Separator)
    {
        PrettyPrintVisitor prettyPrinter(Source, InitialIndent);
        for (const TSRef<Vst::Node>& CommentNode : VstClause->GetPrefixComments())
        {
            Vst::Node::VisitWith(CommentNode, prettyPrinter);
        }
        prettyPrinter.VisitClause(VstClause, Separator);
        for (const TSRef<Vst::Node>& CommentNode : VstClause->GetPostfixComments())
        {
            Vst::Node::VisitWith(CommentNode, prettyPrinter);
        }
    }

    bool GeneratePathToPostfixComment(const TSRef<Vst::Node>& Target, const TSRef<Vst::Node>& Node, int32_t& CommentIndex)
    {
        for (int idx = 0; idx < Node->GetPostfixComments().Num(); idx++)
        {
            if (Node->GetPostfixComments()[idx] == Target)
            {
                CommentIndex = idx;
                return true;
            }
        }

        return false;
    }

    bool GeneratePathToPrefixComment(const TSRef<Vst::Node>& Target, const TSRef<Vst::Node>& Node, int32_t& CommentIndex)
    {
        for (int idx = 0; idx < Node->GetPrefixComments().Num(); idx++)
        {
            if (Node->GetPrefixComments()[idx] == Target)
            {
                CommentIndex = idx;
                return true;
            }
        }

        return false;
    }

    bool GeneratePathToAuxNode(const TSRef<Vst::Node>& Target, const TSRef<Vst::Node>& Node, LArray<int32_t>& AuxPath)
    {
        //special case where the Aux node is what were looking for
        if (Target == Node)
        {
            AuxPath.Add(-1);
            return true;
        }

        for (int idx = 0; idx < Node->GetChildCount(); idx++)
        {
            TSRef<Vst::Node> Child = Node->GetChildren()[idx];
            if (Child == Target || GeneratePathToAuxNode(Target, Child, AuxPath))
            {
                AuxPath.Add(idx);
                return true;
            }
        }

        return false;
    }

    bool GeneratePathToNode_Internal(const TSRef<Vst::Node>& Node, const Vst::NodeArray& Snippet, SPathToNode& PathToNode)
    {
        for (int idx = 0; idx < Snippet.Num(); idx++)
        {
            TSRef<Vst::Node> Child = Snippet[idx];
            if (Child == Node
                || GeneratePathToNode_Internal(Node, Child->GetChildren(), PathToNode)
                || (Child->GetAux() && GeneratePathToAuxNode(Node, Child->GetAux().AsRef(), PathToNode.AuxPath))
                || GeneratePathToPrefixComment(Node, Child, PathToNode.PreCommentIndex)
                || GeneratePathToPostfixComment(Node, Child, PathToNode.PostCommentIndex))
            {
                PathToNode.Path.Add(idx);
                return true;
            }
        }

        return false;
    }

    bool GeneratePathToNode(const TSRef<Vst::Node>& Node, const TSRef<Vst::Snippet>& VstSnippet, SPathToNode& PathToNode)
    {
        PathToNode.Path.Empty(); 
        PathToNode.AuxPath.Empty();
        PathToNode.PostCommentIndex = -1;
        PathToNode.PreCommentIndex = -1;
        return GeneratePathToNode_Internal(Node, VstSnippet->GetChildren(), PathToNode);
    }

    // Returns null if path does not return a node
    TSPtr<Vst::Node> GetNodeFromPath(const TSRef<Vst::Snippet>& VstSnippet, const SPathToNode& PathData, bool bReturnParent)
    {
        if (PathData.Path.IsEmpty())
        {
            return nullptr;
        }

        TSPtr<Vst::Node> CurrNode = VstSnippet;
        for (int idx = PathData.Path.Num() - 1; idx >= 0; idx--)
        {
            if (!(bReturnParent && idx == 0 && PathData.AuxPath.IsEmpty()) && CurrNode->GetChildren().IsValidIndex(PathData.Path[idx]))
            {
                CurrNode = CurrNode->GetChildren()[PathData.Path[idx]];
            }
        }

        if (!PathData.AuxPath.IsEmpty() && CurrNode && CurrNode->GetAux())
        {
            CurrNode = CurrNode->GetAux();
            if (PathData.AuxPath[0] == -1)
            {
                return CurrNode;
            }

            for (int idx = PathData.AuxPath.Num() - 1; idx >= 0; idx--)
            {
                if (!(bReturnParent && idx == 0) && CurrNode->GetChildren().IsValidIndex(PathData.AuxPath[idx]))
                {
                    CurrNode = CurrNode->GetChildren()[PathData.AuxPath[idx]];
                }
            }
        }

        if (CurrNode->GetPostfixComments().IsValidIndex(PathData.PostCommentIndex) && !bReturnParent)
        {
            CurrNode = CurrNode->GetPostfixComments()[PathData.PostCommentIndex];
        }

        if (CurrNode->GetPrefixComments().IsValidIndex(PathData.PreCommentIndex) && !bReturnParent)
        {
            CurrNode = CurrNode->GetPrefixComments()[PathData.PreCommentIndex];
        }


        return CurrNode;
    }

    namespace Vst
    {
        Node::~Node()
        {
            Empty();

            if (_MappedAstNode)
            {
                if (ULANG_ENSUREF(_MappedAstNode->_MappedVstNode == this, "Syntax<>Semantic mappings must be reciprocal."))
                {
                    _MappedAstNode->_MappedVstNode = nullptr;
                }
            }
        }

        void Node::ReplaceSelfWith(const TSRef<Node>& replacement)
        {
            ULANG_ASSERTF(_Parent, "Must have parent to be removed from.");
            int32_t idx = _Parent->AccessChildren().Find(SharedThis(this));
            // @nicka, @sree : seems like we could use an "IsOperator()" functionality here? or "CaresAboutTag()?"
            if (idx >= 1 && (
                _Parent->GetElementType() == NodeType::BinaryOpCompare 
                || _Parent->GetElementType() == NodeType::BinaryOpAddSub
                || _Parent->GetElementType() == NodeType::BinaryOpMulDivInfix))
            {
                replacement->SetTag(GetTag<uint8_t>());
            }
            _Parent->AccessChildren().RemoveAt(idx);
            _Parent->AppendChildAt(replacement, idx);
            replacement->_Parent = _Parent;
            _Parent = nullptr;
            DebugOrphanCheck();
        }

        bool Node::RemoveFromParent(int32_t idx)
        {
            if (!ULANG_ENSUREF(_Parent, "Must have parent to be removed from."))
            {
                return false;
            }

            if (idx == uLang::IndexNone)
            {
                idx = _Parent->GetChildren().IndexOfByKey(this);
            }

            if (_Type == NodeType::Comment)
            {
                if (idx != uLang::IndexNone)
                {
                    _Parent->AccessChildren().RemoveAt(idx);
                    _Parent = nullptr;
                    return true;
                }
                idx = _Parent->GetPostfixComments().IndexOfByKey(this);
                if (idx != uLang::IndexNone)
                {
                    _Parent->AccessPostfixComments().RemoveAt(idx);
                    _Parent = nullptr;
                    return true;
                }

                idx = _Parent->GetPrefixComments().IndexOfByKey(this);
                if (idx != uLang::IndexNone)
                {
                    _Parent->AccessPrefixComments().RemoveAt(idx);
                    _Parent = nullptr;
                    return true;
                }

                return false;
            }
            else if (_Parent->GetAux() == this)
            {
                _Parent->_Aux.Reset();
            }
            else
            {
                _Parent->AccessChildren().RemoveAt(idx);
                _Parent = nullptr;
            }

            return true;
        }

        void Node::AddMapping(uLang::CAstNode* AstNode) const
        {
            // Make previous mapping non reciprocal, if there was one.
            if (_MappedAstNode)
            {
                if (ULANG_ENSUREF(_MappedAstNode->_MappedVstNode == this, "Syntax<>Semantic mappings must be reciprocal."))
                {
                    _MappedAstNode->_VstMappingType = uLang::EVstMappingType::AstNonReciprocal;
                }
            }

            // If there's already a non-reciprocal mapping from the AST node to this VST node, promote it to be reciprocal.
            if (AstNode->_VstMappingType == uLang::EVstMappingType::AstNonReciprocal && AstNode->_MappedVstNode == this)
            {
                _MappedAstNode = AstNode;
                AstNode->_VstMappingType = uLang::EVstMappingType::Ast;
            }
            else if (ULANG_ENSUREF(AstNode->_MappedVstNode == nullptr, "Expression already mapped to an Vst node."))
            {
                _MappedAstNode = AstNode;
                AstNode->_MappedVstNode = this;
            }
        }

        void Node::RemoveMapping(uLang::CAstNode* AstNode)
        {
            if(AstNode->_MappedVstNode && ULANG_ENSUREF(AstNode->_MappedVstNode->_MappedAstNode == AstNode, "Syntax<>Semantic mappings must be reciprocal."))
            {
                AstNode->_MappedVstNode->_MappedAstNode = nullptr;
                AstNode->_MappedVstNode = nullptr;
            }
        }

        const TSRef<Node> MakeStub(const SLocus& Whence)
        {
            return TSRef<Placeholder>::New(Whence);
        }

        bool Node::HasAttributes() const
        {
            return _Aux && (_Aux->GetChildCount() > 0);
        }

        const Identifier* Node::GetAttributeIdentifier(const CUTF8StringView& AttributeName) const
        {
            if (!_Aux)
            {
                return nullptr;
            }

            for (const auto& Child : _Aux->GetChildren())
            {
                // the actual attribute node is wrapped in a dummy Clause (used to preserve comments in the VST)
                ULANG_ASSERTF(Child->IsA<Vst::Clause>(), "attribute nodes are expected to be wrapped in a dummy Clause node with a single child");
                ULANG_ASSERTF(Child->GetChildCount() == 1, "attribute nodes are expected to be wrapped in a dummy Clause node with a single child");

                const Node& Attr = *Child->GetChildren()[0];
                if (const Identifier* AttrIdentifier = Attr.AsNullable<Identifier>())
                {
                    if(AttrIdentifier->GetSourceText() == AttributeName)
                    {
                        return AttrIdentifier;
                    }
                }
            }

            return nullptr;
        }

        bool Node::IsAttributePresent(const CUTF8StringView& AttributeName) const
        {
            return GetAttributeIdentifier(AttributeName) != nullptr;
        }

        const Node* Node::TryGetFirstAttributeOfType(NodeType Type) const
        {
            if (!_Aux)
            {
                return nullptr;
            }

            for (const auto& Child : _Aux->GetChildren())
            {
                // the actual attribute node is wrapped in a dummy Clause (used to preserve comments in the VST)
                ULANG_ASSERTF(Child->IsA<Vst::Clause>(), "attribute nodes are expected to be wrapped in a dummy Clause node with a single child");
                ULANG_ASSERTF(Child->GetChildCount() == 1, "attribute nodes are expected to be wrapped in a dummy Clause node with a single child");

                const Node& Attr = *Child->GetChildren()[0];
                if (Attr.GetElementType() == Type)
                {
                    return &Attr;
                }
            }

            return nullptr;
        }

        void Node::EnsureAuxAllocated()
        {
            if (!_Aux.IsValid())
            {
                _Aux = TSPtr<Clause>::New(Whence(), Clause::EForm::Synthetic);
                _Aux->_Parent = this;
            }
        }

        void Node::PrependAux(const TSRef<Node>& AuxChild)
        {
            EnsureAuxAllocated();
            _Aux->AppendChildAt(AuxChild, 0);
        }

        void Node::PrependAux(const NodeArray& AuxChildren)
        {
            EnsureAuxAllocated();
            _Aux->PrependChildren(AuxChildren);
        }

        void Node::AppendAux(const TSRef<Node>& AuxChild)
        {
            EnsureAuxAllocated();
            _Aux->AppendChild(AuxChild);
        }

        void Node::AppendAux(const NodeArray& AuxChildren)
        {
            EnsureAuxAllocated();
            _Aux->AppendChildren(AuxChildren);
        }

        void Node::AppendAuxAt(const TSRef<Node>& AuxChild, int32_t Idx)
        {
            EnsureAuxAllocated();
            _Aux->AppendChildAt(AuxChild, Idx);
        }

        void Node::SetAux(const TSRef<Clause>& Aux) 
        { 
            if (ULANG_ENSUREF(!Aux->GetParent(), "Aux Node already has a parent!"))
            {
                _Aux = Aux;
                _Aux->_Parent = this;
            } 
        }

        void Node::AppendPrefixComment(const TSRef<Node>& CommentNode)
        {
            CommentNode->_Parent = this;
            _PreComments.Add(CommentNode);
        }

        void Node::AppendPrefixComments(const NodeArray& CommentNodes)
        {
            for(auto& CommentNode : CommentNodes)
            {
                CommentNode->_Parent = this;
            }

            _PreComments.Append(CommentNodes);
        }

        void Node::AppendPostfixComment(const TSRef<Node>& CommentNode)
        {
            CommentNode->_Parent = this;
            _PostComments.Add(CommentNode);
        }

        void Node::AppendPostfixComments(const NodeArray& CommentNodes)
        {
            for (auto& CommentNode : CommentNodes)
            {
                CommentNode->_Parent = this;
            }

            _PostComments.Append(CommentNodes);
        }

        const CUTF8String& Node::GetSnippetPath() const
        {
            if (_Type == NodeType::Package)
            {
                return As<Package>()._FilePath;
            }

            if (_Type == NodeType::Module && As<Module>()._FilePath.IsFilled())
            {
                return As<Module>()._FilePath;
            }

            const Snippet* MySnippet = GetParentOfType<Snippet>();
            return MySnippet ? MySnippet->_Path : CUTF8String::GetEmpty();
        }

        const Snippet* Node::FindSnippetByFilePath(const CUTF8StringView& FilePath) const
        {
            // Is it this?
            if (_Type == NodeType::Snippet)
            {
                const Snippet* FoundSnippet = &As<Snippet>();
                return uLang::FilePathUtils::NormalizePath(FoundSnippet->_Path) == FilePath ? FoundSnippet : nullptr;
            }

            // Check children only if project, package or module
            if (_Type != NodeType::Project && _Type != NodeType::Package && _Type != NodeType::Module)
            {
                return nullptr;
            }

            // Is it any of the children?
            for (const TSRef<Vst::Node>& child : _Children)
            {
                const Snippet* FoundSnippet = child->FindSnippetByFilePath(FilePath);
                if (FoundSnippet)
                {
                    return FoundSnippet;
                }
            }

            // Nothing found
            return nullptr;
        }

        const Node* Node::FindChildByPosition(const SPosition& TextPosition) const
        {
            // Is it any of the children?
            for (const TSRef<Vst::Node>& child : _Children)
            {
                const Node* FoundNode = child->FindChildByPosition(TextPosition);
                if (FoundNode)
                {
                    return FoundNode;
                }
            }

            // Is it this?
            if (_Whence.IsValid() && _Whence.IsInRange(TextPosition))
            {
                return this;
            }

            // Nothing found
            return nullptr;
        }

        const TSRef<Node> Node::FindChildClosestToPosition(const SPosition& TextPosition, const CUTF8StringView& SourceText) const
        {
            ULANG_ASSERTF(TextPosition.IsValid(), "An invalid text position was passed in as a parameter!");
            // NOTE: (yiliang.siew) We DFS the VST and at each traversal, we store the signed distance between the node and
            // the text position. At the end, we sort the signed distances in order to find the minimal absolute value.
            constexpr uint32_t DefaultArraySize = 64;
            LArray<uLang::LocusDistanceResult> AbsDistances;
            AbsDistances.Reserve(DefaultArraySize);
            LArray<TSRef<Node>> Stack;
            Stack.Reserve(DefaultArraySize);
            Stack.Add(this->GetSharedSelf());
            while (!Stack.IsEmpty())
            {
                // TODO: (yiliang.siew) Comments/Aux nodes are technically also nodes that could be desired to be found.
                // However, because `VerseAssist` doesn't yet handle those node types gracefully, we defer getting their distances here as well.
                const TSRef<Node> CurrentNode = Stack.Pop();
                // Since we're just finding the closest node regardless of prefix/suffix, we can rely on the absolute distance.
                const int32_t AbsDistance = abs(GetSignedDistanceBetweenPositionAndLocus(CurrentNode->Whence(), TextPosition, SourceText));
                AbsDistances.Add({CurrentNode, AbsDistance});
                Stack.Append(CurrentNode->GetChildren());
            }
            ULANG_ASSERTF(AbsDistances.Num() > 0, "Invalid traversal of VST encountered!");
            uLang::LocusDistanceResult MinDistance = AbsDistances[0];
            for (const uLang::LocusDistanceResult& CurDistance : AbsDistances)
            {
                if (CurDistance.Distance < MinDistance.Distance)
                {
                    MinDistance = CurDistance;
                }
            }
            return MinDistance.Node;
        }

        const char* CommentTypeToString(Comment::EType Type)
        {
            switch (Type)
            {
            case Comment::EType::block: return "block";
            case Comment::EType::line: return "line";
            case Comment::EType::ind: return "ind";
            case Comment::EType::frag: return "frag";
            default: break;
            }

            ULANG_UNREACHABLE();
            return nullptr;
        }

        TSRef<Module> Package::FindOrAddModule(const CUTF8StringView& ModuleName, const CUTF8StringView& ParentModuleName)
        {
            uLang::TOptional<TSRef<Module>> FoundModule = FindModule(*this, ModuleName);
            if (FoundModule)
            {
                return *FoundModule;
            }

            TSRef<Module> NewModule = TSRef<Module>::New(ModuleName);
            Node* ModuleContainer = this;
            if (!ParentModuleName.IsEmpty())
            {
                uLang::TOptional<TSRef<Module>> FoundParent = FindModule(*this, ParentModuleName);
                if (ULANG_ENSUREF(FoundParent, "Parent module does not exist!"))
                {
                    ModuleContainer = &**FoundParent;
                }
            }
            return ModuleContainer->AppendChild(NewModule).As<Module>();
        }

        uLang::TOptional<TSRef<Module>> Package::FindModule(const Node& ModuleContainer, const CUTF8StringView& ModuleName)
        {
            for (const TSRef<Verse::Vst::Node>& Child : ModuleContainer.GetChildren())
            {
                if (Child->GetElementType() == NodeType::Module)
                {
                    TSRef<Module> FoundModule = Child.As<Module>();
                    if (FoundModule->_Name == ModuleName)
                    {
                        return FoundModule;
                    }

                    uLang::TOptional<TSRef<Module>> FoundSubmodule = FindModule(*FoundModule, ModuleName);
                    if (FoundSubmodule)
                    {
                        return *FoundSubmodule;
                    }
                }
            }

            return EResult::Unspecified;
        }

        void BinaryOpAddSub::AppendAddOperation(const SLocus& AddWhence, const TSRef<Node>& RhsOperand)
        {
            AppendOperation_Internal(TSRef<Operator>::New("+", AddWhence), RhsOperand);
        }

        void BinaryOpAddSub::AppendSubOperation(const SLocus& SubWhence, const TSRef<Node>& RhsOperand)
        {
            AppendOperation_Internal(TSRef<Operator>::New("-", SubWhence), RhsOperand);
        }

        void BinaryOpMulDivInfix::AppendMulOperation(const SLocus& MulWhence, const TSRef<Node>& RhsOperand)
        {
            AppendOperation_Internal(TSRef<Operator>::New("*", MulWhence), RhsOperand);
        }

        void BinaryOpMulDivInfix::AppendDivOperation(const SLocus& DivWhence, const TSRef<Node>& RhsOperand)
        {
            AppendOperation_Internal(TSRef<Operator>::New("/", DivWhence), RhsOperand);
        }

        TSRef<Clause> PrePostCall::PrependQMark(const SLocus& Whence)
        {
            TSRef<Clause> QMarkClause(TSRef<Clause>::New(static_cast<uint8_t>(Op::Option), Whence, Clause::EForm::Synthetic));
            AppendChildAt(QMarkClause, 0);
            return QMarkClause;
        }

        TSRef<Clause> PrePostCall::PrependHat(const SLocus& Whence)
        {
            TSRef<Clause> HatClause(TSRef<Clause>::New(static_cast<uint8_t>(Op::Pointer), Whence, Clause::EForm::Synthetic));
            AppendChildAt(HatClause, 0);
            return HatClause;
        }

        void PrePostCall::PrependCallArgs(bool bCanFail, const TSRef<Clause>& Args)
        {
            Args->SetTag<Op>(bCanFail ? FailCall : SureCall);
            AppendChildAt(Args, 0);
        }

        void PrePostCall::AppendQMark(const SLocus& Whence)
        {
            AppendChild(TSRef<Clause>::New(static_cast<uint8_t>(Op::Option), Whence, Clause::EForm::Synthetic));
        }

        void PrePostCall::AppendHat(const SLocus& Whence)
        {
            AppendChild(TSRef<Clause>::New(static_cast<uint8_t>(Op::Pointer), Whence, Clause::EForm::Synthetic));
        }

        void PrePostCall::AppendCallArgs(bool bCanFail, const TSRef<Clause>& Args)
        {
            Args->SetTag<Op>(bCanFail ? FailCall : SureCall);
            AppendChild(Args);
        }

        void PrePostCall::AppendDotIdent(const SLocus& Whence, const TSRef<Identifier>& Ident)
        {
            Ident->SetTag<Op>(Op::DotIdentifier);
            AppendChild(Ident);
        }

        TSPtr<Clause> PrePostCall::TakeLastArgs()
        {
            if (GetChildCount() > 1)
            {
                const auto Op = GetChildren().Last()->GetTag<PrePostCall::Op>();
                if (Op == PrePostCall::SureCall || Op == PrePostCall::FailCall)
                {
                    const auto Args = TakeChildAt(GetChildCount() - 1);
                    return Args.As<Clause>();
                }
            }

            return TSPtr<Clause>();
        }

    } // namespace Vst

    int32_t GetSignedDistanceBetweenPositionAndLocus(const SLocus& A, const SPosition& B, const CUTF8StringView& SourceText)
    {
        ULANG_ASSERTF(A.IsValid() && B.IsValid(), "Invalid parameters passed into function!");
        ULANG_ASSERTF(!SourceText.IsEmpty(), "Invalid zero-length text was specified!");
        if (A.GetBegin() == B || A.GetEnd() == B)
        {
            return 0;
        }
        const CUTF8StringView SourceTextA = TextRangeToStringView(SourceText, A);
        // If A is not a valid locus for the document, we return a sentinel value by ensuring the distance is as far as possible.
        if (SourceTextA.IsEmpty())
        {
            return INT32_MAX;
        }
        const uLang::SIdxRange RangeA = SourceText.SubRange(SourceTextA);
        uLang::TOptional<int32_t> IndexPositionB = uLang::ScanToRowCol(SourceText, B);
        ULANG_ASSERTF(IndexPositionB.IsSet(), "The position provided was not valid for the source text!");
        if (A.IsInRange(B))
        {
            const int32_t DistanceFromStart = IndexPositionB.GetValue() - RangeA._Begin;
            const int32_t DistanceFromEnd = RangeA._End - IndexPositionB.GetValue();

            return DistanceFromStart < DistanceFromEnd ? DistanceFromStart : -DistanceFromEnd;
        }
        if (B > A.GetEnd())    // B comes after A
        {
            return IndexPositionB.GetValue() - RangeA._End;
        }
        // A comes after B
        return (RangeA._Begin - IndexPositionB.GetValue()) * -1;
    }

}  // namespace Verse
