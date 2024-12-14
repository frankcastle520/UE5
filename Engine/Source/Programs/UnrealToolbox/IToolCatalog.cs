// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealToolbox
{
	/// <summary>
	/// Interface for the tool catalog
	/// </summary>
	public interface IToolCatalog
	{
		/// <summary>
		/// Whether to auto-update tools to latest
		/// </summary>
		bool AutoUpdate { get; set; }

		/// <summary>
		/// List of available tools
		/// </summary>
		IReadOnlyList<IToolCatalogItem> Items { get; }

		/// <summary>
		/// Notification that some property of the items list has changed
		/// </summary>
		event Action? OnItemsChanged;
	}
}
