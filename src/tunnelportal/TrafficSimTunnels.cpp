#include "TrafficSimTunnels.h"

#include "GameAccess.h"
#include "GameAddresses.h"
#include "Logger.h"
#include "cISC4TrafficSimulator.h"
#include "cIGZUnknown.h"

#include <algorithm>
#include <cstdint>

namespace TunnelPortal::TrafficSimTunnels
{
	namespace
	{
		namespace Game = TunnelPortal::Game;
		namespace Field = TunnelPortal::Game::Field;

		Raw::TunnelListNode* GetListSentinel(cISC4TrafficSimulator* trafficSimulator)
		{
			return trafficSimulator
				? *reinterpret_cast<Raw::TunnelListNode**>(
					reinterpret_cast<uint8_t*>(trafficSimulator) + Field::kTrafficSimTunnelList)
				: nullptr;
		}

		const Raw::TunnelMapNode* FindRecord(
			cISC4TrafficSimulator* trafficSimulator,
			const Endpoint& endpoint)
		{
			const Raw::TunnelMap* const map = GetMap(trafficSimulator);
			if (!IsUsableMap(map))
			{
				return nullptr;
			}

			const uint16_t key = PackedCellKey(endpoint.x, endpoint.z);
			const uintptr_t bucketCount = static_cast<uintptr_t>(map->end - map->start);
			for (Raw::TunnelMapNode* node = map->start[key % bucketCount]; node; node = node->next)
			{
				if (node->key == key)
				{
					return node;
				}
			}

			return nullptr;
		}

		void FillListNode(
			Raw::TunnelListNode& node,
			const Endpoint& first,
			const Endpoint& second,
			const Raw::TunnelMapNode* sourceRecord,
			uint16_t mask)
		{
			std::fill(std::begin(node.value), std::end(node.value), 0);
			if (sourceRecord)
			{
				std::copy(std::begin(sourceRecord->value), std::end(sourceRecord->value), std::begin(node.value));
			}

			uint8_t* const bytes = reinterpret_cast<uint8_t*>(node.value);
			bytes[0] = static_cast<uint8_t>(first.x);
			bytes[1] = static_cast<uint8_t>(first.z);
			bytes[2] = static_cast<uint8_t>(second.x);
			bytes[3] = static_cast<uint8_t>(second.z);
			*reinterpret_cast<uint16_t*>(bytes + 12) = mask;
		}

		// Splices a forward/reverse connection record for one portal pair into the
		// simulator's tunnel list for the lifetime of the scope, then unsplices it.
		// Lets DoConnectionsChanged see the new pair before the registry is rebuilt.
		class TemporaryConnectionRecord
		{
		public:
			TemporaryConnectionRecord(
				cISC4TrafficSimulator* trafficSimulator,
				const Endpoint& first,
				const Endpoint& second)
				: sentinel(GetListSentinel(trafficSimulator)),
				  oldNext(nullptr),
				  oldPrevious(nullptr),
				  linked(false)
			{
				if (!sentinel)
				{
					return;
				}

				const Raw::TunnelMapNode* const forwardRecord = FindRecord(trafficSimulator, first);
				const Raw::TunnelMapNode* const reverseRecord = FindRecord(trafficSimulator, second);
				const uint16_t forwardMask = forwardRecord ? static_cast<uint16_t>(forwardRecord->value[0] >> 16) : 0x01FE;
				const uint16_t reverseMask = reverseRecord ? static_cast<uint16_t>(reverseRecord->value[0] >> 16) : 0x01FE;
				FillListNode(forwardNode, first, second, forwardRecord, forwardMask);
				FillListNode(reverseNode, second, first, reverseRecord, reverseMask);
				oldNext = sentinel->next;
				oldPrevious = sentinel->previous;
				if (!oldNext)
				{
					oldNext = sentinel;
				}
				if (!oldPrevious)
				{
					oldPrevious = sentinel;
				}

				forwardNode.next = &reverseNode;
				forwardNode.previous = sentinel;
				reverseNode.next = oldNext;
				reverseNode.previous = &forwardNode;
				oldNext->previous = &reverseNode;
				sentinel->next = &forwardNode;
				if (oldPrevious == sentinel)
				{
					sentinel->previous = &reverseNode;
				}
				linked = true;
			}

			~TemporaryConnectionRecord()
			{
				if (linked)
				{
					sentinel->next = oldNext;
					sentinel->previous = oldPrevious;
					if (oldNext)
					{
						oldNext->previous = sentinel;
					}
				}
			}

		private:
			Raw::TunnelListNode forwardNode{};
			Raw::TunnelListNode reverseNode{};
			Raw::TunnelListNode* sentinel;
			Raw::TunnelListNode* oldNext;
			Raw::TunnelListNode* oldPrevious;
			bool linked;
		};
	}

	const Raw::TunnelMap* GetMap(cISC4TrafficSimulator* trafficSimulator)
	{
		return trafficSimulator
			? reinterpret_cast<const Raw::TunnelMap*>(
				reinterpret_cast<const uint8_t*>(trafficSimulator) + Field::kTrafficSimTunnelMap)
			: nullptr;
	}

	bool IsUsableMap(const Raw::TunnelMap* map)
	{
		if (!map || !map->start || !map->end || map->end < map->start)
		{
			return false;
		}

		const uintptr_t bucketCount = static_cast<uintptr_t>(map->end - map->start);
		return bucketCount > 0 && bucketCount <= Raw::kMaxTunnelMapBuckets;
	}

	uint16_t PackedCellKey(uint32_t x, uint32_t z)
	{
		return static_cast<uint16_t>(((x & 0xff) << 8) | (z & 0xff));
	}

	void NotifyLinkedTunnels(
		const Endpoint& first,
		const Endpoint& second,
		cIGZUnknown* firstTunnel,
		cIGZUnknown* secondTunnel)
	{
		cISC4TrafficSimulator* const trafficSimulator = GameAccess::GetTrafficSimulator();

		if (!trafficSimulator)
		{
			Logger::GetInstance().WriteLine(LogLevel::Error, "TunnelPortalTool: cannot notify traffic simulator, no traffic simulator is available.");
			return;
		}

		Game::DoTunnelChanged(trafficSimulator, firstTunnel, false);
		Game::DoTunnelChanged(trafficSimulator, secondTunnel, false);
		Game::DoTunnelChanged(trafficSimulator, firstTunnel, true);
		Game::DoTunnelChanged(trafficSimulator, secondTunnel, true);

		TemporaryConnectionRecord temporaryConnectionRecord(trafficSimulator, first, second);
		Game::DoConnectionsChanged(trafficSimulator, first.x, first.z, first.x, first.z);
		Game::DoConnectionsChanged(trafficSimulator, second.x, second.z, second.x, second.z);
		Game::DoConnectionsChanged(
			trafficSimulator,
			std::min(first.x, second.x),
			std::min(first.z, second.z),
			std::max(first.x, second.x),
			std::max(first.z, second.z));
	}
}
