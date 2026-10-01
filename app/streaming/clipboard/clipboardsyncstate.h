#pragma once

#include <cstdint>

// Pure decision logic for clipboard sync (no Qt windows, networking or AppKit).
// Mac -> host only when the stream window gains focus; host -> Mac on notification
// (small formats immediately, images when the stream window loses focus).
class ClipboardSyncState
{
public:
    enum class Action {
        None,
        ScheduleQuickFetch, // debounce, then GET without images (formats=7)
        FetchFull,          // GET all formats now
        Push,               // POST the Mac pasteboard now
    };

    Action onHostChanged(uint32_t seq, uint32_t formats);
    Action onFocusLost();
    Action onFocusGained(long macChangeCount, bool macHasSensitiveData);
    void onFetchSucceeded(uint32_t hostSeq, uint32_t receivedFormats, long macChangeCountAfterWrite, bool fullFetch);
    void onFetchEmpty(uint32_t hostSeq, bool fullFetch);
    void onPushSucceeded(long macChangeCount);
    void onPushSkipped(long macChangeCount);
    bool hostDataMissing() const;

private:
    static constexpr uint32_t kQuickFormats = 0x7;

    bool m_HasPending = false;
    uint32_t m_PendingSeq = 0;
    uint32_t m_PendingFormats = 0;
    bool m_HasApplied = false;
    uint32_t m_AppliedSeq = 0;
    uint32_t m_AppliedFormats = 0;
    long m_LastSentChangeCount = -1;
    long m_OwnChangeCount = -1;
    bool m_Focused = false;
    bool m_FocusKnown = false;
    bool m_HasSupersede = false;
    uint32_t m_SupersedeSeq = 0;
};
