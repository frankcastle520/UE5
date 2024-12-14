// Copyright Epic Games, Inc. All Rights Reserved.

using System;

namespace UnrealBuildTool
{
	/// <summary>
	/// Attribute which can be applied to a TargetRules-dervied class to indicate which target types it supports
	/// </summary>
	[AttributeUsage(AttributeTargets.Class, AllowMultiple = true)]
	public sealed class SupportedTargetTypesAttribute : Attribute
	{
		/// <summary>
		/// Array of supported target types
		/// </summary>
		public TargetType[] TargetTypes { get; }

		/// <summary>
		/// Initialize the attribute with a list of target types
		/// </summary>
		/// <param name="targetTypes">Variable-length array of target type arguments</param>
		public SupportedTargetTypesAttribute(params TargetType[] targetTypes)
		{
			TargetTypes = targetTypes;
		}
	}
}
