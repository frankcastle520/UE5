// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Horde.Acls;

namespace HordeServer.Symbols
{
	/// <summary>
	/// ACL actions which apply to symbols
	/// </summary>
	public static class SymbolStoreAclAction
	{
		/// <summary>
		/// Ability to download symbols
		/// </summary>
		public static AclAction ReadSymbols { get; } = new AclAction("ReadSymbols");
	}
}
