// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

namespace uba
{
	template<typename Key, typename Value>
	struct HashMap
	{
		struct Entry
		{
			Key key;
			u32 next; // If this is 0 it means that it is unused, ~0u means it is the end of a chain, otherwise offset into entry array
		};

		void Init(MemoryBlock& memory, u64 maxSize)
		{
			u32 v = u32(maxSize);
			v--;
			v |= v >> 1;
			v |= v >> 2;
			v |= v >> 4;
			v |= v >> 8;
			v |= v >> 16;
			v++;
			u32 lookupSize = v;// << 1u;
			mask = lookupSize - 1;
			nextAvailableEntry = 1;

			// This code relies on that allocated memory is zeroed out
			lookup = (u32*)memory.Allocate(lookupSize*sizeof(u32), 1, TC(""));
			entries = (Entry*)memory.Allocate((maxSize+1)*sizeof(Entry), alignof(Entry), TC(""));
			values = (Value*)memory.Allocate((maxSize+1)*sizeof(Value), alignof(Value), TC(""));
		}

		Value& Insert(const Key& key)
		{
			u32 index = std::hash<Key>()(key) & mask;
			u32 entryIndex = lookup[index];

			if (entryIndex == 0)
			{
				entryIndex = nextAvailableEntry++;
				lookup[index] = entryIndex;
				Entry& entry = entries[entryIndex];
				entry.key = key;
				entry.next = ~0u;
				return values[entryIndex];
			}

			while (true)
			{
				Entry& entry = entries[entryIndex];
				if (entry.key == key)
					return values[entryIndex];

				if (entry.next != ~0u)
				{
					entryIndex = entry.next;
					//static u32 counter = 0;
					//printf("MULTIPLE: %u\n", counter++);
					continue;
				}

				u32 newEntryIndex = entry.next = nextAvailableEntry++;
				Entry& newEntry = entries[newEntryIndex];
				newEntry.key = key;
				newEntry.next = ~0u;
				return values[newEntryIndex];
			}
		}

		Value* Find(const Key& key) const
		{
			u32 index = std::hash<Key>()(key) & mask;
			u32 entryIndex = lookup[index];
			if (entryIndex == 0)
				return nullptr;
			Entry* entry = entries + entryIndex;
			while (true)
			{
				if (entry->key == key)
					return values + entryIndex;
				entryIndex = entry->next;
				if (entryIndex == ~0u)
					return nullptr;
				entry = entries + entryIndex;
			}
		}

		const Key* GetKey(Value* value)
		{
			u32 pos = u32(value - values);
			auto& entry = entries[pos];
			if (entry.next == 0)
				return nullptr;
			return &entry.key;
		}

		u32 Size()
		{
			return nextAvailableEntry - 1;
		}

		void Erase(const Key& key)
		{
			u32 index = std::hash<Key>()(key) & mask;
			u32 entryIndex = lookup[index];
			if (entryIndex == 0)
				return;
			Entry* entry = entries + entryIndex;
			u32* prevNext = nullptr;
			while (true)
			{
				if (entry->key == key)
				{
					if (!prevNext)
					{
						if (entry->next == ~0u)
							lookup[index] = 0;
						else
							lookup[index] = entry->next;
					}
					else
						*prevNext = entry->next;
					entry->next = 0;

					// TODO: We could swap with last and reduce size

					return;
				}
				entryIndex = entry->next;
				if (entryIndex == ~0u)
					return;
				prevNext = &entry->next;
				entry = entries + entryIndex;
			}
		}

		Value* ValuesBegin() { return values + 1; }
		Value* ValuesEnd() { return values + nextAvailableEntry; }

		Entry* entries;
		Value* values;

		u32* lookup;
		u32 mask;
		u32 nextAvailableEntry;
	};

	template<typename Key, typename Value>
	struct HashMap2
	{
		struct Entry
		{
			Key key;
			Value value;
			u32 next; // If this is 0 it means that it is unused, ~0u means it is the end of a chain, otherwise offset into entry array
		};

		void Init(MemoryBlock& memory, u64 maxSize)
		{
			u32 v = u32(maxSize);
			v--;
			v |= v >> 1;
			v |= v >> 2;
			v |= v >> 4;
			v |= v >> 8;
			v |= v >> 16;
			v++;
			u32 lookupSize = v;// << 1u;
			mask = lookupSize - 1;
			nextAvailableEntry = 1;

			// This code relies on that allocated memory is zeroed out
			lookup = (u32*)memory.Allocate(lookupSize*sizeof(u32), 1, TC(""));
			entries = (Entry*)memory.Allocate((maxSize+1)*sizeof(Entry), alignof(Entry), TC(""));
		}

		Value& Insert(const Key& key)
		{
			u32 index = std::hash<Key>()(key) & mask;
			u32 entryIndex = lookup[index];

			if (entryIndex == 0)
			{
				entryIndex = nextAvailableEntry++;
				lookup[index] = entryIndex;
				Entry& entry = entries[entryIndex];
				entry.key = key;
				entry.next = ~0u;
				return entry.value;
			}

			while (true)
			{
				Entry& entry = entries[entryIndex];
				if (entry.key == key)
					return entry.value;

				if (entry.next != ~0u)
				{
					entryIndex = entry.next;
					//static u32 counter = 0;
					//printf("MULTIPLE: %u\n", counter++);
					continue;
				}

				u32 newEntryIndex = entry.next = nextAvailableEntry++;
				Entry& newEntry = entries[newEntryIndex];
				newEntry.key = key;
				newEntry.next = ~0u;
				return newEntry.value;
			}
		}

		Value* Find(const Key& key) const
		{
			u32 index = std::hash<Key>()(key) & mask;
			u32 entryIndex = lookup[index];
			if (entryIndex == 0)
				return nullptr;
			Entry* entry = entries + entryIndex;
			while (true)
			{
				if (entry->key == key)
					return &entry->value;
				entryIndex = entry->next;
				if (entryIndex == ~0u)
					return nullptr;
				entry = entries + entryIndex;
			}
		}

		u32 Size()
		{
			return nextAvailableEntry - 1;
		}

		Entry* entries;
		u32* lookup;
		u32 mask;
		u32 nextAvailableEntry;
	};
}