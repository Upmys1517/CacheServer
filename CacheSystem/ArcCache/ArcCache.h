#pragma once

#include "../CachePolicy.h"
#include "ArcLruPart.h"
#include "ArcLfuPart.h"
#include <memory>

namespace KamaCache
{

template<typename Key, typename Value>
class KArcCache : public KICachePolicy<Key, Value>
{
public:
    explicit KArcCache(size_t capacity = 10, size_t transformThreshold = 2)
        : capacity_(capacity)
        , transformThreshold_(transformThreshold)
        , lruPart_(std::make_unique<ArcLruPart<Key, Value>>(capacity, transformThreshold))
        , lfuPart_(std::make_unique<ArcLfuPart<Key, Value>>(capacity, transformThreshold))
    {}

    ~KArcCache() override = default;

    void put(Key key, Value value) override
    {
        checkGhostCaches(key);

        // 先查 LFU（已被晋升的热 key），再查 LRU，都不存在则写入 LRU
        if (lfuPart_->updateIfExists(key, value))
            return;
        if (lruPart_->updateIfExists(key, value))
            return;
        lruPart_->put(key, value);
    }

    bool get(Key key, Value& value) override
    {
        checkGhostCaches(key);

        bool shouldTransform = false;
        typename ArcLruPart<Key, Value>::NodePtr extractedNode;

        // 先查 LRU，命中且达到晋升门槛则把节点从 LRU 移动到 LFU（非拷贝）
        if (lruPart_->get(key, value, shouldTransform, &extractedNode))
        {
            if (shouldTransform && extractedNode)
            {
                lfuPart_->addExistingNode(extractedNode);
            }
            return true;
        }
        return lfuPart_->get(key, value);
    }

    Value get(Key key) override
    {
        Value value{};
        get(key, value);
        return value;
    }

    void remove(Key key) override
    {
        lruPart_->remove(key);
        lfuPart_->remove(key);
    }

private:
    bool checkGhostCaches(Key key)
    {
        bool inGhost = false;
        if (lruPart_->checkGhost(key))
        {
            if (lfuPart_->decreaseCapacity())
            {
                lruPart_->increaseCapacity();
            }
            inGhost = true;
        }
        else if (lfuPart_->checkGhost(key))
        {
            if (lruPart_->decreaseCapacity())
            {
                lfuPart_->increaseCapacity();
            }
            inGhost = true;
        }
        return inGhost;
    }

private:
    size_t capacity_;
    size_t transformThreshold_;
    std::unique_ptr<ArcLruPart<Key, Value>> lruPart_;
    std::unique_ptr<ArcLfuPart<Key, Value>> lfuPart_;
};

} // namespace KamaCache
