// Copyright Epic Games, Inc. All Rights Reserved.
// uLang Parser Public API

#pragma once

#include "uLang/Common/Containers/SharedPointerArray.h"
#include "uLang/Common/Misc/Event.h"
#include "uLang/Diagnostics/Glitch.h"

namespace uLang
{
enum class EBuildEvent
{
    ExperimentalDefinition,
    PersistentWeakMapDefinition,
    FunctionDefinition,
    ClassDefinition,
    TopLevelDefinition
};

// In the future, for other types of events (e.g. memory calculation) this can be expanded to accomodate other types of statistics
// that aren't just a simple "number of events".
struct VERSECOMPILER_API SBuildEventInfo
{
    union
    {
        uint32_t Count;
    };
    EBuildEvent Type;
};

/// Various statistics for a given build that can be used in analytics.
struct VERSECOMPILER_API SBuildStatistics
{
    uint32_t NumExperimentalDefinitions = 0;
    uint32_t NumPersistentWeakMaps = 0;
    uint32_t NumFunctions = 0;
    uint32_t NumClasses = 0;
    uint32_t NumTopLevelDefinitions = 0;
};

// Accumulated issues for full set of compilation passes
class CDiagnostics : public CSharedMix
{
public:

    // Just warnings (no info or errors)
    ULANG_FORCEINLINE bool                            HasWarnings() const { return _Glitches.ContainsByPredicate([](const SGlitch* Glitch) -> bool { return Glitch->_Result.IsWarning(); }); }
    VERSECOMPILER_API int32_t            GetWarningNum() const;

    // Just errors (no info or warnings)
    ULANG_FORCEINLINE bool                            HasErrors() const { return _Glitches.ContainsByPredicate([](const SGlitch* Glitch) -> bool { return Glitch->_Result.IsError(); }); }
    VERSECOMPILER_API int32_t            GetErrorNum() const;

    // Any type of glitch including info, warnings and errors
    ULANG_FORCEINLINE bool                            HasGlitches() const { return _Glitches.IsFilled(); }
    ULANG_FORCEINLINE int32_t                         GetGlitchNum() const { return _Glitches.Num(); }
    ULANG_FORCEINLINE const TSRefArray<SGlitch>& GetGlitches() const { return _Glitches; }
    ULANG_FORCEINLINE bool                            IsGlitchWithId(uintptr_t VstIdentifier) const { return _Glitches.ContainsByPredicate([VstIdentifier](const SGlitch* Glitch) -> bool { return Glitch->_Locus._VstIdentifier == VstIdentifier; }); }

    // Experimental definition metrics.
    ULANG_FORCEINLINE bool HasUseOfExperimentalDefinition() const { return _bUsesExperimentalDefinition; }
    ULANG_FORCEINLINE const SBuildStatistics& GetStatistics() const
    {
        return _Statistics;
    }

    ULANG_FORCEINLINE void Reset()
    {
        _Glitches.Empty();
    }

    void AppendGlitch(const TSRef<SGlitch>& Glitch)
    {
        _Glitches.Add(Glitch);
        _OnGlitchEvent.Broadcast(Glitch);
    }

    void AppendGlitch(const TSPtr<SGlitch>& Glitch)
    {
        AppendGlitch(Glitch.AsRef());
    }

    ULANG_FORCEINLINE void AppendGlitch(SGlitchResult&& Result, SGlitchLocus&& Locus)
    {
        AppendGlitch(TSRef<SGlitch>::New(Move(Result), Move(Locus)));
    }

    ULANG_FORCEINLINE void AppendGlitch(SGlitchResult&& Result)
    {
        AppendGlitch(TSRef<SGlitch>::New(Move(Result), SGlitchLocus()));
    }

    ULANG_FORCEINLINE void AppendGlitches(TSRefArray<SGlitch>& Glitches)
    {
        for (const TSRef<SGlitch>& NewGlitch : Glitches)
        {
            _OnGlitchEvent.Broadcast(NewGlitch);
        }
        _Glitches.Append(Glitches);
    }

    ULANG_FORCEINLINE void Append(CDiagnostics&& Other)
    {
        for (const TSRef<SGlitch>& NewGlitch : Other._Glitches)
        {
            _OnGlitchEvent.Broadcast(NewGlitch);
        }
        _Glitches.Append(Move(Other._Glitches));

        for (uint32_t I = Other._Statistics.NumPersistentWeakMaps; I != 0; --I)
        {
            _OnBuildStatisticEvent.Broadcast({.Count = 1, .Type = EBuildEvent::PersistentWeakMapDefinition});
        }
        _Statistics.NumPersistentWeakMaps += Other._Statistics.NumPersistentWeakMaps;
        _Statistics.NumClasses += Other._Statistics.NumClasses;
        _Statistics.NumFunctions += Other._Statistics.NumFunctions;
        _Statistics.NumTopLevelDefinitions += Other._Statistics.NumTopLevelDefinitions;
        _Statistics.NumExperimentalDefinitions += Other._Statistics.NumExperimentalDefinitions;

        for (uint32_t I = Other._Statistics.NumExperimentalDefinitions; I != 0; --I)
        {
            _OnBuildStatisticEvent.Broadcast({.Count = 1, .Type = EBuildEvent::ExperimentalDefinition});
        }
        _bUsesExperimentalDefinition |= Other._bUsesExperimentalDefinition;
    }

    ULANG_FORCEINLINE void AppendPersistentWeakMap()
    {
        ++_Statistics.NumPersistentWeakMaps;
        _OnBuildStatisticEvent.Broadcast({.Count = 1, .Type = EBuildEvent::PersistentWeakMapDefinition});
    }

    ULANG_FORCEINLINE void AppendFunctionDefinition(const uint32_t Count)
    {
        _Statistics.NumFunctions += Count;
        _OnBuildStatisticEvent.Broadcast({.Count = 1, .Type = EBuildEvent::FunctionDefinition});
    }

    ULANG_FORCEINLINE void AppendClassDefinition(const uint32_t Count)
    {
        _Statistics.NumClasses += Count;
        _OnBuildStatisticEvent.Broadcast({.Count = 1, .Type = EBuildEvent::ClassDefinition});
    }

    ULANG_FORCEINLINE void AppendTopLevelDefinition(const uint32_t Count)
    {
        _Statistics.NumTopLevelDefinitions += Count;
        _OnBuildStatisticEvent.Broadcast({.Count = 1, .Type = EBuildEvent::TopLevelDefinition});
    }

    ULANG_FORCEINLINE void AppendUseOfExperimentalDefinition()
    {
        _bUsesExperimentalDefinition = true;
        _OnBuildStatisticEvent.Broadcast({.Count = 1, .Type = EBuildEvent::ExperimentalDefinition});
    }

    using COnGlitchEvent = TEvent<const TSRef<SGlitch>&>;
    COnGlitchEvent::Registrar& OnGlitchEvent() { return _OnGlitchEvent; }

    /// The type of event, followed by the count of that type of event.
    using COnBuildStatisticEvent = TEvent<const SBuildEventInfo&>;
    COnBuildStatisticEvent::Registrar& OnBuildStatisticEvent() { return _OnBuildStatisticEvent; }

protected:
    // All the issues encountered across all the phases (Parser and SemanticAnalyzer)
    TSRefArray<SGlitch> _Glitches;
 
    SBuildStatistics _Statistics{
        .NumExperimentalDefinitions = 0,
        .NumPersistentWeakMaps = 0,
        .NumFunctions = 0,
        .NumClasses = 0,
        .NumTopLevelDefinitions = 0
    };
    
    bool _bUsesExperimentalDefinition{false};

    COnGlitchEvent _OnGlitchEvent;
    COnBuildStatisticEvent _OnBuildStatisticEvent;
};
}
