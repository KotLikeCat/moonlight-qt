#include "clipboardsyncstate.h"

ClipboardSyncState::Action ClipboardSyncState::onHostChanged(uint32_t seq, uint32_t formats)
{
    m_HasPending = true;
    m_PendingSeq = seq;
    m_PendingFormats = formats;
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
    m_Focused = false;
    return hostDataMissing() ? Action::FetchFull : Action::None;
}

ClipboardSyncState::Action ClipboardSyncState::onFocusGained(long macChangeCount, bool macHasSensitiveData)
{
    m_Focused = true;
    if (macHasSensitiveData || macChangeCount == m_LastSentChangeCount || macChangeCount == m_OwnChangeCount) {
        return Action::None;
    }
    return Action::Push;
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

void ClipboardSyncState::onFetchEmpty(bool fullFetch)
{
    m_HasApplied = true;
    m_AppliedSeq = m_PendingSeq;
    m_AppliedFormats = fullFetch ? m_PendingFormats : (m_PendingFormats & kQuickFormats);
}

void ClipboardSyncState::onPushSucceeded(long macChangeCount)
{
    m_LastSentChangeCount = macChangeCount;
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
