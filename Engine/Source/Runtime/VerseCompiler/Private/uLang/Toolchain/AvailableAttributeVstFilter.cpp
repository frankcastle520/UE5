// Copyright Epic Games, Inc. All Rights Reserved.

#include "uLang/Toolchain/AvailableAttributeVstFilter.h"

#include "uLang/Common/Containers/SharedPointer.h"

namespace
{
    using namespace uLang;

    const CUTF8String* GetIdentifierString(const Verse::Vst::Node* Node)
    {
        if (Node != nullptr)
        {
            const Verse::Vst::Identifier* Ident = Node->AsNullable<Verse::Vst::Identifier>();
            if (Ident != nullptr)
            {
                return &Ident->GetStringValue();
            }
        }

        return nullptr;
    }

    TOptional<int64_t> FindIntegerInitValueByName(const char* TargetIdentifier, const Verse::Vst::Clause* ParentClause)
    {
        for (const Verse::Vst::Node* ChildNode : ParentClause->GetChildren())
        {
            if (const Verse::Vst::Definition* ChildDef = ChildNode->AsNullable<Verse::Vst::Definition>())
            {
                const CUTF8String* IdentStr = GetIdentifierString(ChildDef->GetOperandLeft());
                if (IdentStr && IdentStr->ToStringView() == TargetIdentifier)
                {
                    if (const Verse::Vst::Node* ValueNode = ChildDef->GetOperandRight())
                    {
                        const Verse::Vst::Clause* ValueClause = ValueNode->AsNullable<Verse::Vst::Clause>();
                        if (!ValueClause->IsEmpty())
                        {
                            if (const Verse::Vst::IntLiteral* IntValueLiteral = ValueClause->GetChildren()[0]->AsNullable<Verse::Vst::IntLiteral>())
                            {
                                const Verse::string& IntValue = IntValueLiteral->GetStringValue();
                                long long value = ::strtoll(IntValue.AsCString(), nullptr, 0);
                                return TOptional<int64_t>(value);
                            }
                        }
                    }
                }
            }
        }
        return TOptional<int64_t>();
    }

    bool ExtractAvailableAttribute(TSPtr<Verse::Vst::Node> DefinitionVstNode, const SBuildVersionInfo& BuildVersion)
    {
        // Prepended @attributes are in the Aux data
        if (TSPtr<Verse::Vst::Clause> Aux = DefinitionVstNode->GetAux())
        {
            // Each child can be an attribute
            for (const Verse::Vst::Node* Child : Aux->GetChildren())
            {
                if (const Verse::Vst::Clause* ChildClause = Child->AsNullable<Verse::Vst::Clause>())
                {
                    // Two child nodes, name and body
                    // Clause:                           @available { A := 0 } breaks down into
                    //     Macro:
                    //         [0]Identifier:             available
                    //         [1]Clause:
                    //             [0]Definition:         A := 0
                    //                 [0]Identifier:     A
                    //                 [1]Clause:
                    //                     [0]intLiteral: 0
                    if (!ChildClause->IsEmpty())
                    {
                        // Check that name == "available"
                        if (const Verse::Vst::Macro* VersionAttribMacro = ChildClause->GetChildren()[0]->AsNullable<Verse::Vst::Macro>())
                        {
                            const CUTF8String* VersionIdentStr = GetIdentifierString(VersionAttribMacro->GetName());
                            if (VersionIdentStr && VersionIdentStr->ToStringView() == "available")
                            {
                                if (Verse::Vst::Clause* VersionBodyClause = VersionAttribMacro->GetClause(0))
                                {
                                    // Each child is a Clause/Definition representing one initialized value
                                    if (TOptional<int64_t> MinUploadedAtFNVersion = FindIntegerInitValueByName("MinUploadedAtFNVersion", VersionBodyClause))
                                    {
                                        if (MinUploadedAtFNVersion.GetValue() > BuildVersion.UploadedAtFNVersion)
                                        {
                                            // filtered out
                                            return false;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // keep
        return true;
    }
}

namespace uLang
{
void CAvailableAttributeVstFilter::StaticFilter(const TSRef<Verse::Vst::Node>& VstNode, const SBuildContext& BuildParams)
{
    SBuildVersionInfo VersionInfo{ BuildParams._Params._UploadedAtFNVersion };

    // Go looking for package context in the parent-chain
    if (const Verse::Vst::Package* StartPackage = VstNode->GetParentOfType<Verse::Vst::Package>())
    {
        VersionInfo = { StartPackage->_UploadedAtFNVersion };
    }

    StaticFilterHelper(VstNode, BuildParams, VersionInfo);
}

void CAvailableAttributeVstFilter::StaticFilterHelper(
    const TSRef<Verse::Vst::Node>& VstNode,
    const SBuildContext& BuildParams,
    const SBuildVersionInfo& InBuildVersion)
{
    using namespace Verse::Vst;

    const SBuildVersionInfo* BuildVersion;

    // Package nodes might change the verse or uploaded-at versions
    SBuildVersionInfo LocalBuildVersion;
    if (const Package* VstPackage = VstNode->AsNullable<Package>())
    {
        // Don't process user-code packages
        if (VstPackage->_VerseScope == uLang::EVerseScope::PublicUser)
        {
            return;
        }

        LocalBuildVersion.UploadedAtFNVersion = VstPackage->_UploadedAtFNVersion;

        BuildVersion = &LocalBuildVersion;
    }
    else
    {
        // For non-packages, just pass the version info through
        BuildVersion = &InBuildVersion;
    }

    // Encountering an unprocessed vpackage macro means we can't continue without
    //  risking over-pruning the VST.
    if (const Macro* VstMacro = VstNode->AsNullable<Macro>())
    {
        if (const Identifier* MacroIdentifier = VstMacro->GetName()->AsNullable<Identifier>())
        {
            const CUTF8String& VstMacroName = MacroIdentifier->GetSourceText();
            if (VstMacroName == "vpackage")
            {
                return;
            }
        }
    }

    for (int32_t NodeChildIndex = 0; NodeChildIndex < VstNode->GetChildCount(); ++NodeChildIndex)
    {
        TSRef<Node> ChildNode = VstNode->GetChildren()[NodeChildIndex];

        if (ExtractAvailableAttribute(ChildNode, *BuildVersion))
        {
            StaticFilterHelper(ChildNode, BuildParams, *BuildVersion);
        }
        else
        {
            ChildNode->RemoveFromParent(NodeChildIndex--);
        }
    }
}

}