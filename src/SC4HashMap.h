#pragma once

template<typename Key, typename Value> class SC4HashMap
{
public:
	struct HashMapNode
	{
		HashMapNode* next;
		std::pair<const Key, Value> item;
	};
	static_assert(sizeof(HashMapNode) == 4 + ((sizeof(Key)-1)/4 + 1) * 4 + ((sizeof(Value)-1)/4 + 1) * 4);

	// a vector of buckets each storing a singly-linked list of key-value pairs
	HashMapNode** mpStart;
	HashMapNode** mpEnd;
	uint32_t RESERVED;  // probably capacity from SC4Vector
	uint32_t mSize;

private:
	constexpr size_t get_hash_code(const Key& key) const { return key; }  // NOTE only supports unsigned integer types for now

public:
	size_t size() const {
		// auto count = 0;
		// for (auto pBucket = mpStart; pBucket != mpEnd; pBucket++) {
		// 	for (auto pNode = *pBucket; pNode != nullptr; pNode = pNode->next) {
		// 		count++;
		// 	}
		// }
		// return count;
		return mSize;
	}

	class iterator {
		friend class SC4HashMap;
		HashMapNode** pBucket;
		HashMapNode** pEnd;
		HashMapNode* pNode;
		constexpr void advance_while_null() {
			while (pNode == nullptr) {
				pBucket++;
				if (pBucket == pEnd) {
					break;
				} else {
					pNode = *pBucket;
				}
			}
		}
	public:
		iterator(HashMapNode** pBucket, HashMapNode** pEnd, HashMapNode* pNode) : pEnd(pEnd), pBucket(pBucket), pNode(pNode) {}
		iterator(HashMapNode** pBucket, HashMapNode** pEnd) : pEnd(pEnd), pBucket(pBucket) {
			if (pBucket != pEnd) {
				this->pNode = *pBucket;
				advance_while_null();
			} else {
				this->pNode = nullptr;
			}
		}
		iterator& operator++() {
			if (pNode != nullptr) {  // else no more elements
				pNode = pNode->next;
				advance_while_null();
			}
			return *this;
		}
		iterator operator++(int) { iterator retval = *this; ++(*this); return retval; }
		bool operator==(iterator other) const { return pNode == other.pNode; }
		bool operator!=(iterator other) const { return pNode != other.pNode; }
		std::pair<const Key, Value>& operator*() const { return pNode->item; }
		std::pair<const Key, Value>* operator->() const { return &(pNode->item); }
	};

	iterator begin() const { return iterator(mpStart, mpEnd); }
	iterator end() const { return iterator(mpEnd, mpEnd, nullptr); }

	Value& at(const Key& key) const {
		if (mpEnd != mpStart) {
			for (auto pNode = mpStart[get_hash_code(key) % (mpEnd - mpStart)]; pNode != nullptr; pNode = pNode->next) {
				if (pNode->item.first == key) {
					return pNode->item.second;
				}
			}
		}
		throw std::out_of_range("Key is not contained in map.");
	}

	iterator find(const Key& key) const {
		if (mpEnd != mpStart) {
			auto pBucket = mpStart + (get_hash_code(key) % (mpEnd - mpStart));
			for (auto pNode = *pBucket; pNode != nullptr; pNode = pNode->next) {
				if (pNode->item.first == key) {
					return iterator(pBucket, mpEnd, pNode);
				}
			}
		}
		return end();
	}

	iterator erase(iterator pos) {
		if (pos.pNode == nullptr) {  // end
			return pos;
		} else {
			auto pNodePred = *(pos.pBucket);
			if (pNodePred == pos.pNode) { // node was first in bucket, so drop it
				(pos.pBucket)[0] = pos.pNode->next;
				this->mSize = this->mSize - 1;
			} else {
				for (; pNodePred != nullptr; pNodePred = pNodePred->next) {
					if (pNodePred->next == pos.pNode) {
						pNodePred->next = pos.pNode->next;  // skip it
						this->mSize = this->mSize - 1;
						break;
					}
				}
				// if end of loop is reached, pos did not contain valid data, as item is not contained in bucket
			}
			if (pos.pNode->next != nullptr) {
				return iterator(pos.pBucket, pos.pEnd, pos.pNode->next);
			} else {
				return iterator(pos.pBucket + 1, pos.pEnd);
			}
		}
	}
};
