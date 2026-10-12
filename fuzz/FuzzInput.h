#pragma once

// Small helper to consume fuzzer input as structured fields.

#include <cstddef>
#include <cstdint>
#include <vector>

class FuzzInput
{
public:
    FuzzInput(const uint8_t *data, size_t size) : m_p(data), m_end(data + size) {}

    size_t remaining() const { return static_cast<size_t>(m_end - m_p); }
    bool empty() const { return m_p >= m_end; }

    uint8_t u8()
    {
        return m_p < m_end ? *m_p++ : 0;
    }
    uint16_t u16()
    {
        const uint16_t hi = u8();
        return static_cast<uint16_t>((hi << 8) | u8());
    }
    // Up to n bytes (fewer at the end of the input)
    std::vector<uint8_t> bytes(size_t n)
    {
        n = n < remaining() ? n : remaining();
        std::vector<uint8_t> v(m_p, m_p + n);
        m_p += n;
        return v;
    }

private:
    const uint8_t *m_p;
    const uint8_t *m_end;
};
