// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;

namespace EpicGames.UHT.Parsers
{

	/// <summary>
	/// Collection of UENUM specifiers
	/// </summary>
	[UnrealHeaderTool]
	[SuppressMessage("CodeQuality", "IDE0051:Remove unused private members", Justification = "Attribute accessed method")]
	public static class UhtFieldSpecifiers
	{
		[UhtSpecifier(Extends = UhtTableNames.Struct, ValueType = UhtSpecifierValueType.KeyValuePairList)]
		[UhtSpecifier(Extends = UhtTableNames.Class, ValueType = UhtSpecifierValueType.KeyValuePairList)]
		private static void VerseSpecifier(UhtSpecifierContext specifierContext, List<KeyValuePair<StringView, StringView>> values)
		{
			UhtField fieldObj = (UhtField)specifierContext.Type;

			foreach (KeyValuePair<StringView, StringView> kvp in values)
			{
				ReadOnlySpan<char> key = kvp.Key.Span;
				if (key.Equals("name", StringComparison.OrdinalIgnoreCase))
				{
					fieldObj.VerseName = kvp.Value.ToString();
				}
				else if (key.Equals("module", StringComparison.OrdinalIgnoreCase))
				{
					fieldObj.VerseModule = kvp.Value.ToString();
				}
				else
				{
					fieldObj.LogError($"Verse specifier option '{key}' is unknown");
				}
			}

			if (!fieldObj.IsVerseField)
			{
				fieldObj.LogError($"Verse specifier must include the name");
			}
			else if(fieldObj.Outer is not UhtPackage)
			{
				fieldObj.LogError($"Verse specifier can only appear on top level classes and structures");
			}
			else
			{
				if (String.IsNullOrEmpty(fieldObj.VerseModule))
				{
					fieldObj.EngineName = fieldObj.VerseName!;
				}
				else
				{
					fieldObj.EngineName = $"{fieldObj.VerseModule}_{fieldObj.VerseName!}";
				}

				using BorrowStringBuilder borrowBuilder = new(StringBuilderCache.Small);
				borrowBuilder.StringBuilder.AppendVerseUEVNIPackageName(fieldObj);
				fieldObj.Outer = fieldObj.Module.CreatePackage(borrowBuilder.StringBuilder.ToString());
			}
		}
	}
}
