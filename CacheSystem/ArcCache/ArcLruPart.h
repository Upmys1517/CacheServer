#pragma once

#include "ArcCacheNode.h"
#include <unordered_map>
#include <mutex>

namespace KamaCache
{

template<typename Key, typename Value>
class ArcLruPart
{
public:
    using NodeType = ArcNode<Key, Value>;
    using NodePtr = std::shared_ptr<NodeType>;
    using NodeMap = std::unordered_map<Key, NodePtr>;

    explicit ArcLruPart(size_t capacity, size_t transformThreshold)
        : capacity_(capacity)
        , ghostCapacity_(capacity)
        , transformThreshold_(transformThreshold)
    {
        initializeLists();
    }

    bool put(Key key, Value value)
    {
        if (capacity_ == 0) return false;

        std::lock_guard<std::mutex> lock(mutex_);
        auto it = mainCache_.find(key);
        if (it != mainCache_.end())
        {
            return updateExistingNode(it->second, value);
        }
        return addNewNode(key, value);
    }

    // 原有 get：不提取节点
    bool get(Key key, Value& value, bool& shouldTransform)
    {
        return getInternal(key, value, shouldTransform, nullptr);
    }

    // 带节点提取的 get：当 shouldTransform 时，节点从 LRU 移除并通过 outNode 返回
    bool get(Key key, Value& value, bool& shouldTransform, NodePtr* outNode)
    {
        return getInternal(key, value, shouldTransform, outNode);
    }

    bool contain(Key key)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return mainCache_.find(key) != mainCache_.end();
    }

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

    void remove(Key key)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = mainCache_.find(key);
        if (it != mainCache_.end())
        {
            removeFromMain(it->second);
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

    bool checkGhost(Key key)
    {
        auto it = ghostCache_.find(key);
        if (it != ghostCache_.end()) {
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
        if (mainCache_.size() == capacity_) {
            evictLeastRecent();
        }
        --capacity_;
        return true;
    }

private:
    void initializeLists()
    {
        mainHead_ = std::make_shared<NodeType>();
        mainTail_ = std::make_shared<NodeType>();
        mainHead_->next_ = mainTail_;
        mainTail_->prev_ = mainHead_;

        ghostHead_ = std::make_shared<NodeType>();
        ghostTail_ = std::make_shared<NodeType>();
        ghostHead_->next_ = ghostTail_;
        ghostTail_->prev_ = ghostHead_;
    }

    bool getInternal(Key key, Value& value, bool& shouldTransform, NodePtr* outNode)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = mainCache_.find(key);
        if (it != mainCache_.end())
        {
            shouldTransform = updateNodeAccess(it->second);
            value = it->second->getValue();
            if (shouldTransform && outNode)
            {
                *outNode = it->second;
                removeFromMain(*outNode);
                mainCache_.erase(it);
            }
            return true;
        }
        return false;
    }

    bool updateExistingNode(NodePtr node, const Value& value)
    {
        node->setValue(value);
        moveToFront(node);
        return true;
    }

    bool addNewNode(const Key& key, const Value& value)
    {
        if (mainCache_.size() >= capacity_)
        {
            evictLeastRecent();
        }

        NodePtr newNode = std::make_shared<NodeType>(key, value);
        mainCache_[key] = newNode;
        addToFront(newNode);
        return true;
    }

    bool updateNodeAccess(NodePtr node)
    {
        moveToFront(node);
        node->incrementAccessCount();
        return node->getAccessCount() >= transformThreshold_;
    }

    void moveToFront(NodePtr node)
    {
        if (!node->prev_.expired() && node->next_) {
            auto prev = node->prev_.lock();
            prev->next_ = node->next_;
            node->next_->prev_ = node->prev_;
            node->next_ = nullptr;
        }
        addToFront(node);
    }

    void addToFront(NodePtr node)
    {
        node->next_ = mainHead_->next_;
        node->prev_ = mainHead_;
        mainHead_->next_->prev_ = node;
        mainHead_->next_ = node;
    }

    void evictLeastRecent()
    {
        NodePtr leastRecent = mainTail_->prev_.lock();
        if (!leastRecent || leastRecent == mainHead_)
            return;

        removeFromMain(leastRecent);

        if (ghostCache_.size() >= ghostCapacity_)
        {
            removeOldestGhost();
        }
        addToGhost(leastRecent);

        mainCache_.erase(leastRecent->getKey());
    }

    void removeFromMain(NodePtr node)
    {
        if (!node->prev_.expired() && node->next_) {
            auto prev = node->prev_.lock();
            prev->next_ = node->next_;
            node->next_->prev_ = node->prev_;
            node->next_ = nullptr;
        }
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
        node->accessCount_ = 1;

        node->next_ = ghostHead_->next_;
        node->prev_ = ghostHead_;
        ghostHead_->next_->prev_ = node;
        ghostHead_->next_ = node;

        ghostCache_[node->getKey()] = node;
    }

    void removeOldestGhost()
    {
        NodePtr oldestGhost = ghostTail_->prev_.lock();
        if (!oldestGhost || oldestGhost == ghostHead_)
            return;

        removeFromGhost(oldestGhost);
        ghostCache_.erase(oldestGhost->getKey());
    }


private:
    size_t capacity_;
    size_t ghostCapacity_;
    size_t transformThreshold_;
    std::mutex mutex_;

    NodeMap mainCache_;
    NodeMap ghostCache_;

    NodePtr mainHead_;
    NodePtr mainTail_;
    NodePtr ghostHead_;
    NodePtr ghostTail_;
};

} // namespace KamaCache
