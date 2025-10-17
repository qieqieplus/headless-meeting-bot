#include "MeetingConfig.h"

MeetingConfig::MeetingConfig(const std::string& meetingId,
                             const std::string& password,
                             const std::string& displayName,
                             bool isMeetingStart,
                             const std::string& joinToken,
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
