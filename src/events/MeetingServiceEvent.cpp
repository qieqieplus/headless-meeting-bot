#include "MeetingServiceEvent.h"
#include "util/Logger.h"

MeetingServiceEvent::MeetingServiceEvent(std::function<void()> onJoin, std::function<void()> onEnd)
    : m_onMeetingJoin(onJoin), m_onMeetingEnd(onEnd) {
}
void MeetingServiceEvent::onMeetingStatusChanged(ZOOMSDK::MeetingStatus status, int iResult) {

    std::string message;
    std::string icon = "⏳";

    switch (status) {
        case ZOOMSDK::MEETING_STATUS_CONNECTING:
            message = "connecting to the meeting";
            break;
        case ZOOMSDK::MEETING_STATUS_RECONNECTING:
            message = "reconnecting to the meeting";
            break;
        case ZOOMSDK::MEETING_STATUS_DISCONNECTING:
            message = "disconnecting from the meeting";
            break;
        case ZOOMSDK::MEETING_STATUS_INMEETING:
            message = "joined meeting";
            Util::Logger::getInstance().success(message);
            if (m_onMeetingJoin) m_onMeetingJoin();
            return;
        case ZOOMSDK::MEETING_STATUS_ENDED:
            message = "meeting ended";
            Util::Logger::getInstance().success(message);
            if (m_onMeetingEnd) m_onMeetingEnd();
            return;
        case ZOOMSDK::MEETING_STATUS_FAILED:
            icon = "❌";
            message = "failed to connect to the meeting";
            break;
        case ZOOMSDK::MEETING_STATUS_WAITINGFORHOST:
            message = "waiting for the meeting to start";
            break;
        default:
            message = "meeting status: " + std::to_string(status);
            break;
    }

    if (!message.empty())
        Util::Logger::getInstance().info(message);
}

void MeetingServiceEvent::onMeetingParameterNotification(const ZOOMSDK::MeetingParameter *meeting_param) {
    // Callback not implemented
}

void MeetingServiceEvent::onMeetingStatisticsWarningNotification(ZOOMSDK::StatisticsWarningType type) {
    // Callback not implemented
}

void MeetingServiceEvent::onSuspendParticipantsActivities() {
    // Callback not implemented
}

void MeetingServiceEvent::onAICompanionActiveChangeNotice(bool bActive) {
    // Callback not implemented
}

// Callback setters are not implemented - callbacks are set directly in constructor


void MeetingServiceEvent::onMeetingTopicChanged(const zchar_t *sTopic) {

}

void MeetingServiceEvent::onMeetingFullToWatchLiveStream(const zchar_t *sLiveStreamUrl) {

}