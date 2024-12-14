// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointerFwd.h"

struct FStateTreeEditorNode;
enum class EStateTreeConditionEvaluationMode : uint8;

class SWidget;
class IPropertyHandle;
class IDetailLayoutBuilder;
class FDetailWidgetRow;
class FMenuBuilder;
class FStateTreeViewModel;
class UStateTreeEditorData;

namespace UE::StateTreeEditor::DebuggerExtensions
{

TSharedRef<SWidget> CreateStateWidget(TSharedPtr<IPropertyHandle> StateEnabledProperty, UStateTreeEditorData* TreeData);
void AppendStateMenuItems(FMenuBuilder& InMenuBuilder, TSharedPtr<IPropertyHandle> StateEnabledProperty, UStateTreeEditorData* TreeData);

TSharedRef<SWidget> CreateEditorNodeWidget(const TSharedPtr<IPropertyHandle>& StructPropertyHandle, UStateTreeEditorData* TreeData);
void AppendEditorNodeMenuItems(FMenuBuilder& InMenuBuilder, const TSharedPtr<IPropertyHandle>& StructPropertyHandle, UStateTreeEditorData* TreeData);

TSharedRef<SWidget> CreateTransitionWidget(const TSharedPtr<IPropertyHandle>& StructPropertyHandle, UStateTreeEditorData* TreeData);
void AppendTransitionMenuItems(FMenuBuilder& InMenuBuilder, const TSharedPtr<IPropertyHandle>& StructPropertyHandle, UStateTreeEditorData* TreeData);

}; // UE::StateTreeEditor::DebuggerExtensions
