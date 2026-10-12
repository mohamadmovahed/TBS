#pragma once

// Bounded hand-over buffer between the capture (reader) thread and the
// processing thread. The reader never blocks: when the buffer is full the
// chunk is dropped and counted. Chunks are recycled to avoid allocations.

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

namespace tbs {

class ChunkQueue
{
public:
    struct Chunk
    {
        std::vector<uint8_t> data;
        size_t len = 0;
        bool reset = false; // control marker: retune happened, reset the parsers
    };

    explicit ChunkQueue(size_t maxBytes) : m_maxBytes(std::max<size_t>(maxBytes, 1u << 20)) {}

    // ---- reader side ----
    std::unique_ptr<Chunk> acquire(size_t capacity)
    {
        std::unique_ptr<Chunk> c;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_pool.empty()) {
                c = std::move(m_pool.back());
                m_pool.pop_back();
            }
        }
        if (!c)
            c = std::make_unique<Chunk>();
        if (c->data.size() < capacity)
            c->data.resize(capacity);
        c->len = 0;
        c->reset = false;
        return c;
    }

    // Returns false if the chunk was dropped because the buffer is full
    bool push(std::unique_ptr<Chunk> c, bool control = false)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!control && m_queued + c->len > m_maxBytes) {
                m_droppedBytes += c->len;
                ++m_droppedChunks;
                recycleLocked(std::move(c));
                return false;
            }
            m_queued += c->len;
            m_peak = std::max(m_peak, m_queued);
            m_queue.push_back(std::move(c));
        }
        m_cv.notify_one();
        return true;
    }

    void close()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_closed = true;
        }
        m_cv.notify_all();
    }

    // ---- processing side ----
    // Next chunk, or nullptr on timeout / when closed and drained
    std::unique_ptr<Chunk> pop(int timeoutMs, bool *finished)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this] { return !m_queue.empty() || m_closed; });
        if (m_queue.empty()) {
            if (finished)
                *finished = m_closed;
            return nullptr;
        }
        std::unique_ptr<Chunk> c = std::move(m_queue.front());
        m_queue.pop_front();
        m_queued -= c->len;
        if (finished)
            *finished = false;
        return c;
    }

    void recycle(std::unique_ptr<Chunk> c)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        recycleLocked(std::move(c));
    }

    struct Stats
    {
        size_t capacity = 0;
        size_t queued = 0;
        size_t peak = 0;
        uint64_t droppedBytes = 0;
        uint64_t droppedChunks = 0;
    };
    Stats stats() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return {m_maxBytes, m_queued, m_peak, m_droppedBytes, m_droppedChunks};
    }

private:
    void recycleLocked(std::unique_ptr<Chunk> c)
    {
        if (m_pool.size() < 64)
            m_pool.push_back(std::move(c));
    }

    const size_t m_maxBytes;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<std::unique_ptr<Chunk>> m_queue;
    std::vector<std::unique_ptr<Chunk>> m_pool;
    size_t m_queued = 0;
    size_t m_peak = 0;
    uint64_t m_droppedBytes = 0;
    uint64_t m_droppedChunks = 0;
    bool m_closed = false;
};

} // namespace tbs
