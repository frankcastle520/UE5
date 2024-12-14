// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeDelegates.h"

namespace UE::StateTree::Delegates
{
	
#if WITH_EDITOR
FOnIdentifierChanged OnIdentifierChanged;
FOnSchemaChanged OnSchemaChanged;
FOnParametersChanged OnParametersChanged;
FOnGlobalDataChanged OnGlobalDataChanged;
FOnVisualThemeChanged OnVisualThemeChanged;
FOnStateParametersChanged OnStateParametersChanged;
FOnBreakpointsChanged OnBreakpointsChanged;
FOnPostCompile OnPostCompile;
FOnRequestCompile OnRequestCompile;
FOnRequestEditorHash OnRequestEditorHash;
#endif // WITH_EDITOR

#if WITH_STATETREE_TRACE
FOnTracingStateChanged OnTracingStateChanged;
#endif // WITH_STATETREE_TRACE

}; // UE::StateTree::Delegates
