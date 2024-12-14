// Copyright Epic Games, Inc. All Rights Reserved.

using MongoDB.Bson;

namespace EpicGames.MongoDB
{
	static class ObjectIdHelpers
	{
		public static ObjectId ToObjectId(this string text)
		{
			if (text.Length == 0)
			{
				return ObjectId.Empty;
			}
			else
			{
				return ObjectId.Parse(text);
			}
		}
	}
}
