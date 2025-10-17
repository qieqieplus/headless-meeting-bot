#ifndef HEADLESS_ZOOM_BOT_MEETING_CONFIG_H
#define HEADLESS_ZOOM_BOT_MEETING_CONFIG_H

#include <string>


class MeetingConfig {
private:
    std::string m_meetingId;
    std::string m_password;
    bool m_isMeetingStart;
    std::string m_joinToken;
    bool m_useRawAudio;
    bool m_useRawVideo;  // Always records share when enabled
    std::string m_displayName;

public:
    MeetingConfig(const std::string& meetingId = "",
                  const std::string& password = "",
                  const std::string& displayName = "",
                  bool isMeetingStart = false,
                  const std::string& joinToken = "",
                  bool useRawAudio = false,
                  bool useRawVideo = false);

    // Getters
    const std::string& meetingId() const { return m_meetingId; }
    const std::string& password() const { return m_password; }
    const std::string& displayName() const { return m_displayName; }
    const std::string& joinToken() const { return m_joinToken; }
    bool isMeetingStart() const { return m_isMeetingStart; }
    bool useRawAudio() const { return m_useRawAudio; }
    bool useRawVideo() const { return m_useRawVideo; }
    bool useRawRecording() const { return m_useRawAudio || m_useRawVideo; }

    // Setters
    void setMeetingId(const std::string& meetingId) { m_meetingId = meetingId; }
    void setPassword(const std::string& password) { m_password = password; }
    void setDisplayName(const std::string& displayName) { m_displayName = displayName; }
    void setJoinToken(const std::string& joinToken) { m_joinToken = joinToken; }
    void setMeetingStart(bool isMeetingStart) { m_isMeetingStart = isMeetingStart; }
    void setUseRawAudio(bool useRawAudio) { m_useRawAudio = useRawAudio; }
    void setUseRawVideo(bool useRawVideo) { m_useRawVideo = useRawVideo; }
    
    
    // Validation
    bool isValidForJoining() const {
        return !m_meetingId.empty() && !m_password.empty() && !m_isMeetingStart;
    }
    
    bool isValidForStarting() const {
        return m_isMeetingStart;
    }
    
    bool isValid() const {
        return isValidForJoining() || isValidForStarting();
    }
};

#endif //HEADLESS_ZOOM_BOT_MEETING_CONFIG_H
