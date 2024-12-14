// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;

#pragma warning disable CA2227

namespace EpicGames.Horde.Ugs
{
	/// <summary>
	/// Information about a Badge
	/// </summary>
	public class UgsBadgeMetadata
	{
		/// <summary>
		/// Name of the Badge
		/// </summary>
		public string Name { get; set; } = "";

		/// <summary>
		/// URL of the Badge setter
		/// </summary>
		public Uri? Url { get; set; } = null;

		/// <summary>
		/// Status state of the Badge
		/// </summary>
		public string State { get; set; } = "";
	}

	/// <summary>
	/// Information about a User from UGS
	/// </summary>
	public class UgsUserMetadata
	{
		/// <summary>
		/// User name
		/// </summary>
		public string User { get; set; } = "";

		/// <summary>
		/// Time user synced change in UGS
		/// </summary>
		public long SyncTime { get; set; }

		/// <summary>
		/// User vote state
		/// </summary>
		public string Vote { get; set; } = "";
	}

	/// <summary>
	/// Information about UGS Metadata
	/// </summary>
	public class UgsMetadataItem
	{
		/// <summary>
		/// Change number
		/// </summary>
		public int Change { get; set; }

		/// <summary>
		/// Project name
		/// </summary>
		public string Project { get; set; } = "";

		/// <summary>
		/// Badges associated with Change
		/// </summary>
		public List<UgsBadgeMetadata> Badges { get; set; } = new();

		/// <summary>
		/// Users of this change
		/// </summary>
		public List<UgsUserMetadata> Users { get; set; } = new();
	}

	/// <summary>
	/// Information about a collection of Metadata
	/// </summary>
	public class GetUgsMetadataResponse
	{
		/// <summary>
		/// Sequence number identifier
		/// </summary>
		public long SequenceNumber { get; set; }

		/// <summary>
		/// Metadata items
		/// </summary>
		public List<UgsMetadataItem> Items { get; set; } = new();
	}
}
