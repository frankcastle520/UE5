// Copyright Epic Games, Inc. All Rights Reserved.

#include "uLang/Toolchain/ModularFeatureManager.h"
#include "uLang/Toolchain/ModularFeature.h" // for IModularFeature
#include "uLang/Common/Containers/Array.h"
#include "uLang/Common/Containers/SharedPointer.h"

namespace uLang
{
namespace Private
{

class CModularFeatureRegistry : public Private::IModularFeatureRegistry
{
public:
    void Add(const TSRef<Private::IModularFeature>& NewFeature, const RegistryId FeatureId);
    bool Remove(const TSRef<Private::IModularFeature>& ToRemove);
    void MergeIn(const CModularFeatureRegistry& OtherRegistry);

    void SortDatabase();

    CSymbolTable _Symbols;
    struct SRegisteredFeature
    {
        SRegisteredFeature(const TSRef<Private::IModularFeature>& InFeature, const RegistryId InRegId, const CSymbolTable& SymTable)
            : _RegistrySym(SymTable.Get(InRegId))
            , _FeatureInst(InFeature)
        {}
        CSymbol _RegistrySym;
        TSRef<Private::IModularFeature> _FeatureInst;
    };
    TArray<SRegisteredFeature> _Database;
};

void CModularFeatureRegistry::Add(const TSRef<Private::IModularFeature>& NewFeature, const RegistryId FeatureId)
{
    _Database.Emplace(NewFeature, FeatureId, _Symbols);
    SortDatabase();
}

bool CModularFeatureRegistry::Remove(const TSRef<Private::IModularFeature>& ToRemove)
{
    int32_t FoundIndex = _Database.IndexOfByPredicate([&ToRemove](const SRegisteredFeature& Entry)
    {
        return Entry._FeatureInst == ToRemove;
    });

    if (FoundIndex != uLang::IndexNone)
    {
        _Database.RemoveAt(FoundIndex);
    }

    return (FoundIndex != uLang::IndexNone);
}

void CModularFeatureRegistry::MergeIn(const CModularFeatureRegistry& OtherRegistry)
{
    _Database.Reserve(_Database.Num() + OtherRegistry._Database.Num());
    for (const SRegisteredFeature& OtherFeature : OtherRegistry._Database)
    {
        CSymbol FixupSymbol = OtherFeature._RegistrySym;
        _Symbols.ReAdd(FixupSymbol);

        _Database.Emplace(OtherFeature._FeatureInst, FixupSymbol.GetId(), _Symbols);
    }
    SortDatabase();
}

void CModularFeatureRegistry::SortDatabase()
{
    // Determines if Lhs should come before Rhs?
    auto FeatureSort = [](const SRegisteredFeature& Lhs, const SRegisteredFeature& Rhs)->bool
    {
        if (Lhs._RegistrySym == Rhs._RegistrySym)
        {
            return Lhs._FeatureInst->GetPriority() > Rhs._FeatureInst->GetPriority();
        }
        return Lhs._RegistrySym > Rhs._RegistrySym;
    };
    _Database.Sort(FeatureSort);
}

} // namespace Private

namespace Private_ModularFeatureManagerImpl
{

static TOptional<TSRef<Private::CModularFeatureRegistry>>& GetMaybeRegistry()
{
    static TOptional<TSRef<Private::CModularFeatureRegistry>> FeatureRegistry;
    return FeatureRegistry;
}

const TSRef<Private::CModularFeatureRegistry>& GetRegistryRef()
{
    TOptional<TSRef<Private::CModularFeatureRegistry>>& MaybeRegistry = GetMaybeRegistry();
    if (!MaybeRegistry.IsSet())
    {
        MaybeRegistry.Emplace(TSRef<Private::CModularFeatureRegistry>::New());
    }
    return MaybeRegistry.GetValue();
}

struct SFindFeatureFunctor
{
    SFindFeatureFunctor(Private::RegistryId InFeatureId) : _FeatureId(InFeatureId) {}

    ULANG_FORCEINLINE bool operator()(const Private::CModularFeatureRegistry::SRegisteredFeature& Entry) const
    {
        return Entry._RegistrySym.GetId() == _FeatureId;
    }

    Private::RegistryId _FeatureId = SymbolId_Null;
};

} // namespace Private_ModularFeatureManagerImpl

const TSRef<Private::IModularFeatureRegistry>& Private::CModularFeatureRegistrar::GetRegistry()
{
    return Private_ModularFeatureManagerImpl::GetRegistryRef().As<Private::IModularFeatureRegistry>();
}

void Private::CModularFeatureRegistrar::SetRegistry(const TSRef<IModularFeatureRegistry>& InRegistry)
{
    // Safe downcast, since we've limited the IModularFeatureRegistry ctor to CModularFeatureRegistry instances
    const TSRef<CModularFeatureRegistry>& NewRegistry = InRegistry.As<CModularFeatureRegistry>();

    using namespace Private_ModularFeatureManagerImpl;

    TOptional<TSRef<CModularFeatureRegistry>>& CurrentRegistry = GetMaybeRegistry();
    if (CurrentRegistry.IsSet())
    {
        NewRegistry->MergeIn(**CurrentRegistry);
    }
    CurrentRegistry = NewRegistry;
}

void Private::CModularFeatureRegistrar::Register(const TSRef<IModularFeature>& NewModularFeature, const RegistryId FeatureId)
{
    const TSRef<Private::CModularFeatureRegistry>& Registry = Private_ModularFeatureManagerImpl::GetRegistryRef();
    Registry->Add(NewModularFeature, FeatureId);
}

bool Private::CModularFeatureRegistrar::Unregister(const TSRef<IModularFeature>& ModularFeature)
{
    const TSRef<Private::CModularFeatureRegistry>& Registry = Private_ModularFeatureManagerImpl::GetRegistryRef();
    return Registry->Remove(ModularFeature);
}

Private::RegistryId Private::CModularFeatureRegistrar::GetRegistryId(const char* FeatureName)
{
    const TSRef<Private::CModularFeatureRegistry>& Registry = Private_ModularFeatureManagerImpl::GetRegistryRef();
    return Registry->_Symbols.AddChecked(FeatureName).GetId();
}

int32_t Private::GetModularFeatureCount(const RegistryId FeatureId)
{
    using namespace Private_ModularFeatureManagerImpl;
    const TSRef<Private::CModularFeatureRegistry>& Registry = GetRegistryRef();

    int32_t Count = 0;

    const int32_t FeatureOffset = Registry->_Database.IndexOfByPredicate(SFindFeatureFunctor(FeatureId));
    if (FeatureOffset >= 0)
    {
        for (Count = 1; Registry->_Database.IsValidIndex(FeatureOffset + Count) && Registry->_Database[FeatureOffset + Count]._RegistrySym.GetId() == FeatureId;)
        {
            ++Count;
        }
    }

    return Count;
}

TSPtr<Private::IModularFeature> Private::GetModularFeature(const RegistryId FeatureId, const int32_t Index)
{
    using namespace Private_ModularFeatureManagerImpl;
    const TSRef<Private::CModularFeatureRegistry>& Registry = GetRegistryRef();

    const int32_t FeatureOffset = Registry->_Database.IndexOfByPredicate(SFindFeatureFunctor(FeatureId));
    if (FeatureOffset >= 0)
    {
        if (Registry->_Database.IsValidIndex(FeatureOffset + Index))
        {
            const Private::CModularFeatureRegistry::SRegisteredFeature& RegisteredFeature = Registry->_Database[FeatureOffset + Index];
            if (RegisteredFeature._RegistrySym.GetId() == FeatureId)
            {
                return RegisteredFeature._FeatureInst;
            }
        }
    }
    return TSPtr<Private::IModularFeature>();
}

} // namespace uLang
