#ifndef HEADLESS_ZOOM_BOT_MEETING_CONFIG_H
#define HEADLESS_ZOOM_BOT_MEETING_CONFIG_H

#include <string>

using namespace std;

class MeetingConfig {
private:
    string m_meetingId;
    string m_password;
    bool m_isMeetingStart;
    string m_joinToken;
    bool m_useRawAudio;
    bool m_useRawVideo;  // Always records share when enabled
    string m_displayName;

public:
    MeetingConfig(const string& meetingId = "",
                  const string& password = "",
                  const string& displayName = "",
                  bool isMeetingStart = false,
                  const string& joinToken = "",
                  bool useRawAudio = false,
                  bool useRawVideo = false);

    // Getters
    const string& meetingId() const { return m_meetingId; }
    const string& password() const { return m_password; }
    const string& displayName() const { return m_displayName; }
    const string& joinToken() const { return m_joinToken; }
    bool isMeetingStart() const { return m_isMeetingStart; }
    bool useRawAudio() const { return m_useRawAudio; }
    bool useRawVideo() const { return m_useRawVideo; }
    bool useRawRecording() const { return m_useRawAudio || m_useRawVideo; }

    // Setters
    void setMeetingId(const string& meetingId) { m_meetingId = meetingId; }
    void setPassword(const string& password) { m_password = password; }
    void setDisplayName(const string& displayName) { m_displayName = displayName; }
    void setJoinToken(const string& joinToken) { m_joinToken = joinToken; }
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
