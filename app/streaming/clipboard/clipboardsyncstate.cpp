#include "clipboardsyncstate.h"

ClipboardSyncState::Action ClipboardSyncState::onHostChanged(uint32_t seq, uint32_t formats)
{
    m_HasPending = true;
    m_PendingSeq = seq;
    m_PendingFormats = formats;
    if (!m_FocusKnown) {
        // Session greeting before the stream window exists: the Mac clipboard wins
        // (decided on the first focus event).
        return Action::None;
    }
    if (!hostDataMissing()) {
        return Action::None;
    }
    if (!m_Focused) {
        return Action::FetchFull;
    }
    return (formats & kQuickFormats) ? Action::ScheduleQuickFetch : Action::None;
}

ClipboardSyncState::Action ClipboardSyncState::onFocusLost()
{
    m_FocusKnown = true;
    m_Focused = false;
    return hostDataMissing() ? Action::FetchFull : Action::None;
}

ClipboardSyncState::Action ClipboardSyncState::onFocusGained(long macChangeCount, bool macHasSensitiveData)
{
    const bool firstFocus = !m_FocusKnown;
    m_FocusKnown = true;
    m_Focused = true;
    if (!macHasSensitiveData && macChangeCount != m_LastSentChangeCount && macChangeCount != m_OwnChangeCount) {
        // The push will supersede the host version pending right now.
        m_HasSupersede = m_HasPending;
        m_SupersedeSeq = m_PendingSeq;
        return Action::Push;
    }
    if (firstFocus && hostDataMissing() && (m_PendingFormats & kQuickFormats)) {
        return Action::ScheduleQuickFetch;
    }
    return Action::None;
}

void ClipboardSyncState::onFetchSucceeded(uint32_t hostSeq, uint32_t receivedFormats, long macChangeCountAfterWrite, bool fullFetch)
{
    // A full fetch delivers everything the host could send for this version
    // (an image dropped by the size limit will not appear on a retry either).
    const bool samePending = m_HasPending && hostSeq == m_PendingSeq;
    m_HasApplied = true;
    m_AppliedSeq = hostSeq;
    m_AppliedFormats = (fullFetch && samePending) ? (receivedFormats | m_PendingFormats) : receivedFormats;
    m_OwnChangeCount = macChangeCountAfterWrite;
}

void ClipboardSyncState::onFetchEmpty(uint32_t hostSeq, bool fullFetch)
{
    m_HasApplied = true;
    m_AppliedSeq = hostSeq;
    if (m_HasPending && hostSeq == m_PendingSeq) {
        m_AppliedFormats = fullFetch ? m_PendingFormats : (m_PendingFormats & kQuickFormats);
    } else {
        m_AppliedFormats = 0;
    }
}

void ClipboardSyncState::onPushSucceeded(long macChangeCount)
{
    m_LastSentChangeCount = macChangeCount;
    // The host clipboard now holds our data and will not notify us about our own write.
    if (m_HasSupersede && m_HasPending && m_PendingSeq == m_SupersedeSeq) {
        m_HasApplied = true;
        m_AppliedSeq = m_PendingSeq;
        m_AppliedFormats = m_PendingFormats;
    }
    m_HasSupersede = false;
}

void ClipboardSyncState::onPushSkipped(long macChangeCount)
{
    m_LastSentChangeCount = macChangeCount;
    m_HasSupersede = false;
}

bool ClipboardSyncState::hostDataMissing() const
{
    if (!m_HasPending) {
        return false;
    }
    if (!m_HasApplied || m_AppliedSeq != m_PendingSeq) {
        return true;
    }
    return (m_PendingFormats & ~m_AppliedFormats) != 0;
}
