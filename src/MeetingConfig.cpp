#include "MeetingConfig.h"

MeetingConfig::MeetingConfig(const string& meetingId,
                             const string& password,
                             const string& displayName,
                             bool isMeetingStart,
                             const string& joinToken,
                             bool useRawAudio,
                             bool useRawVideo)
    : m_meetingId(meetingId)
    , m_password(password)
    , m_displayName(displayName)
    , m_isMeetingStart(isMeetingStart)
    , m_joinToken(joinToken)
    , m_useRawAudio(useRawAudio)
    , m_useRawVideo(useRawVideo) {
}
