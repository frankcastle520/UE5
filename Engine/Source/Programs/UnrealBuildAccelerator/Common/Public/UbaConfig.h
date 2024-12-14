// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UbaStringBuffer.h"

namespace uba
{
	class Config;
	class Logger;


	class ConfigTable
	{
	public:
		bool GetValueAsString(const tchar*& out, const tchar* key) const;
		bool GetValueAsString(TString& out, const tchar* key) const;
		bool GetValueAsU32(u32& out, const tchar* key) const;
		bool GetValueAsInt(int& out, const tchar* key) const;
		bool GetValueAsBool(bool& out, const tchar* key) const;

		const ConfigTable* GetTable(const tchar* name) const;

		void AddValue(const tchar* key, int value);
		void AddValue(const tchar* key, u32 value);
		void AddValue(const tchar* key, bool value);
		void AddValue(const tchar* key, const tchar* str);

	private:
		ConfigTable* m_parent = nullptr;
		enum ValueType { ValueType_Value, ValueType_String };
		struct Value { ValueType type; TString string; };
		Map<TString, Value> m_values;
		UnorderedMap<TString, ConfigTable> m_tables;
		friend Config;
	};


	class Config : public ConfigTable
	{
	public:
		bool LoadFromFile(Logger& logger, const tchar* configFile);
		bool LoadFromText(Logger& logger, const char* text, u64 textLen);
		bool IsLoaded() const;

		bool SaveToFile(Logger& logger, const tchar* configFile);

		bool m_isLoaded = false;
	};
}
