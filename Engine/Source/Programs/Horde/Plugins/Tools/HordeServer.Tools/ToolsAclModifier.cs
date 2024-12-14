// Copyright Epic Games, Inc. All Rights Reserved.

using HordeServer.Acls;
using HordeServer.Tools;
using HordeServer.Utilities;

namespace HordeServer
{
	class ToolsAclModifier : IDefaultAclModifier
	{
		/// <inheritdoc/>
		public void Apply(DefaultAclBuilder acl)
		{
			acl.AddCustomRole(HordeClaims.UploadToolsClaim, new[] { ToolAclAction.UploadTool });
		}
	}
}
