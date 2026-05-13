#pragma once

#include "ArcCacheNode.h"
#include <unordered_map>
#include <map>
#include <list>
#include <mutex>

namespace KamaCache
{

template<typename Key, typename Value>
class ArcLfuPart
{
public:
    using NodeType = ArcNode<Key, Value>;
    using NodePtr = std::shared_ptr<NodeType>;
    using NodeMap = std::unordered_map<Key, NodePtr>;
    using FreqList = std::list<NodePtr>;
    using FreqMap = std::map<size_t, FreqList>;
    using KeyToIter = std::unordered_map<Key, typename FreqList::iterator>;

    explicit ArcLfuPart(size_t capacity, size_t transformThreshold)
        : capacity_(capacity)
        , ghostCapacity_(capacity)
        , transformThreshold_(transformThreshold)
        , minFreq_(0)
    {
        initializeLists();
    }

    bool put(Key key, Value value)
    {
        if (capacity_ == 0)
            return false;

        std::lock_guard<std::mutex> lock(mutex_);
        auto it = mainCache_.find(key);
        if (it != mainCache_.end())
        {
            return updateExistingNode(it->second, value);
        }
        return addNewNode(key, value);
    }

    bool get(Key key, Value& value)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = mainCache_.find(key);
        if (it != mainCache_.end())
        {
            updateNodeFrequency(it->second);
            value = it->second->getValue();
            return true;
        }
        return false;
    }

    void remove(Key key)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = mainCache_.find(key);
        if (it != mainCache_.end())
        {
            size_t freq = it->second->getAccessCount();
            auto& freqList = freqMap_[freq];
            auto iter = keyToIter_.find(key);
            if (iter != keyToIter_.end())
            {
                freqList.erase(iter->second);          // O(1)
                keyToIter_.erase(iter);
            }
            if (freqList.empty())
            {
                freqMap_.erase(freq);
                if (freq == minFreq_ && !freqMap_.empty())
                    minFreq_ = freqMap_.begin()->first;
            }
            mainCache_.erase(it);
            return;
        }
        auto git = ghostCache_.find(key);
        if (git != ghostCache_.end())
        {
            removeFromGhost(git->second);
            ghostCache_.erase(git);
        }
    }

    bool contain(Key key)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return mainCache_.find(key) != mainCache_.end();
    }

    bool checkGhost(Key key)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = ghostCache_.find(key);
        if (it != ghostCache_.end())
        {
            removeFromGhost(it->second);
            ghostCache_.erase(it);
            return true;
        }
        return false;
    }

    void increaseCapacity()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++capacity_;
    }

    bool decreaseCapacity()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (capacity_ <= 0) return false;
        if (mainCache_.size() == capacity_)
        {
            evictLeastFrequent();
        }
        --capacity_;
        return true;
    }

    // 仅更新已存在的 key，不存在则返回 false（用于避免新 key 误入 LFU）
    bool updateIfExists(Key key, const Value& value)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = mainCache_.find(key);
        if (it != mainCache_.end())
        {
            updateExistingNode(it->second, value);
            return true;
        }
        return false;
    }

    // 接收从 LRU 部分晋升过来的已存在节点（非拷贝）
    void addExistingNode(NodePtr node)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (mainCache_.find(node->getKey()) != mainCache_.end())
        {
            return;  // 已存在，忽略（理论上不应发生）
        }
        if (mainCache_.size() >= capacity_)
        {
            evictLeastFrequent();
        }

        node->accessCount_ = 1;  // 在新 part 中重新计数
        mainCache_[node->getKey()] = node;

        if (freqMap_.find(1) == freqMap_.end())
        {
            freqMap_[1] = FreqList();
        }
        freqMap_[1].push_back(node);
        keyToIter_[node->getKey()] = std::prev(freqMap_[1].end());
        if (minFreq_ == 0) minFreq_ = 1;
        else minFreq_ = std::min(minFreq_, size_t(1));
    }

private:
    void initializeLists()
    {
        ghostHead_ = std::make_shared<NodeType>();
        ghostTail_ = std::make_shared<NodeType>();
        ghostHead_->next_ = ghostTail_;
        ghostTail_->prev_ = ghostHead_;
    }

    bool updateExistingNode(NodePtr node, const Value& value)
    {
        node->setValue(value);
        updateNodeFrequency(node);
        return true;
    }

    bool addNewNode(const Key& key, const Value& value)
    {
        if (mainCache_.size() >= capacity_)
        {
            evictLeastFrequent();
        }

        NodePtr newNode = std::make_shared<NodeType>(key, value);
        mainCache_[key] = newNode;

        if (freqMap_.find(1) == freqMap_.end())
        {
            freqMap_[1] = FreqList();
        }
        freqMap_[1].push_back(newNode);
        keyToIter_[key] = std::prev(freqMap_[1].end());  // O(1) 记录迭代器
        if (minFreq_ == 0) minFreq_ = 1;
        else minFreq_ = std::min(minFreq_, size_t(1));

        return true;
    }

    void updateNodeFrequency(NodePtr node)
    {
        size_t oldFreq = node->getAccessCount();
        node->incrementAccessCount();
        size_t newFreq = node->getAccessCount();

        // O(1)：用迭代器直接从旧频率链表中删除
        auto iter = keyToIter_.find(node->getKey());
        if (iter != keyToIter_.end())
        {
            auto& oldList = freqMap_[oldFreq];
            oldList.erase(iter->second);
            if (oldList.empty())
            {
                freqMap_.erase(oldFreq);
                if (oldFreq == minFreq_)
                {
                    minFreq_ = newFreq;
                }
            }
        }

        // 添加到新频率链表
        if (freqMap_.find(newFreq) == freqMap_.end())
        {
            freqMap_[newFreq] = FreqList();
        }
        freqMap_[newFreq].push_back(node);
        keyToIter_[node->getKey()] = std::prev(freqMap_[newFreq].end());
    }

    void evictLeastFrequent()
    {
        if (freqMap_.empty())
            return;

        auto& minFreqList = freqMap_[minFreq_];
        if (minFreqList.empty())
            return;

        NodePtr leastNode = minFreqList.front();
        minFreqList.pop_front();
        keyToIter_.erase(leastNode->getKey());

        if (minFreqList.empty())
        {
            freqMap_.erase(minFreq_);
            if (!freqMap_.empty())
            {
                minFreq_ = freqMap_.begin()->first;
            }
        }

        if (ghostCache_.size() >= ghostCapacity_)
        {
            removeOldestGhost();
        }
        addToGhost(leastNode);

        mainCache_.erase(leastNode->getKey());
    }

    void removeFromGhost(NodePtr node)
    {
        if (!node->prev_.expired() && node->next_) {
            auto prev = node->prev_.lock();
            prev->next_ = node->next_;
            node->next_->prev_ = node->prev_;
            node->next_ = nullptr;
        }
    }

    void addToGhost(NodePtr node)
    {
        node->next_ = ghostTail_;
        node->prev_ = ghostTail_->prev_;
        if (!ghostTail_->prev_.expired()) {
            ghostTail_->prev_.lock()->next_ = node;
        }
        ghostTail_->prev_ = node;
        ghostCache_[node->getKey()] = node;
    }

    void removeOldestGhost()
    {
        NodePtr oldestGhost = ghostHead_->next_;
        if (oldestGhost != ghostTail_)
        {
            removeFromGhost(oldestGhost);
            ghostCache_.erase(oldestGhost->getKey());
        }
    }

private:
    size_t capacity_;
    size_t ghostCapacity_;
    size_t transformThreshold_;
    size_t minFreq_;
    std::mutex mutex_;

    NodeMap mainCache_;
    NodeMap ghostCache_;
    FreqMap freqMap_;
    KeyToIter keyToIter_;

    NodePtr ghostHead_;
    NodePtr ghostTail_;
};

} // namespace KamaCache
