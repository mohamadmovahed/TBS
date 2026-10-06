#include "BbFrameExtractor.h"

#include "TsAnalyzer.h"

void BbFrameExtractor::reset()
{
    m_state = State::Idle;
    m_frame.clear();
    m_nextCounter = 0;
    m_carrierPackets = m_frames = m_payloadBytes = 0;
    m_crcErrors = m_sequenceErrors = m_lengthErrors = 0;
}

bool BbFrameExtractor::tryEmit(const FrameCallback &cb, bool final)
{
    if (m_frame.size() < BbHeader::kSize) {
        if (final) {
            ++m_lengthErrors;
            m_state = State::Idle;
        }
        return false;
    }

    BbHeader hdr;
    if (!hdr.parse(m_frame.data())) {
        ++m_crcErrors;
        m_state = State::Idle;
        return false;
    }

    const size_t needed = BbHeader::kSize + hdr.dataFieldBytes();
    if (m_frame.size() < needed) {
        if (final) {
            ++m_lengthErrors;
            m_state = State::Idle;
        }
        return false;
    }

    ++m_frames;
    m_payloadBytes += hdr.dataFieldBytes();
    if (cb)
        cb(m_frame.data(), needed, hdr);
    // Anything after DFL is BBFrame padding; skip the rest of this frame.
    m_state = final ? State::Idle : State::Draining;
    return true;
}

void BbFrameExtractor::pushPacket(const uint8_t *pkt, const FrameCallback &cb)
{
    if (!isBbFramePid(TsAnalyzer::pidOf(pkt)))
        return;
    ++m_carrierPackets;

    const uint8_t sectionLength = pkt[7];
    if (sectionLength < 1 || sectionLength > kFullSectionLength) {
        ++m_lengthErrors;
        m_state = State::Idle;
        return;
    }
    const uint8_t *payload = pkt + 9;
    const size_t payloadLen = sectionLength - 1u;
    const bool lastPacket = sectionLength < kFullSectionLength;

    if (pkt[8] == kStartMarker) {
        if (m_state == State::Collecting)
            tryEmit(cb, true); // previous frame ended exactly on a packet boundary
        m_frame.assign(payload, payload + payloadLen);
        m_nextCounter = 1;
        m_state = State::Collecting;
    } else {
        if (m_state == State::Idle)
            return; // wait for the next frame start
        if (pkt[8] != m_nextCounter) {
            if (m_state == State::Collecting)
                ++m_sequenceErrors;
            m_state = State::Idle;
            return;
        }
        ++m_nextCounter;
        if (m_state == State::Draining) {
            if (lastPacket)
                m_state = State::Idle;
            return;
        }
        m_frame.insert(m_frame.end(), payload, payload + payloadLen);
    }

    tryEmit(cb, lastPacket);
}
