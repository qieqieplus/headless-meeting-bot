#include "MeetingServiceEvent.h"

#include "util/Logger.h"

MeetingServiceEvent::MeetingServiceEvent(std::function<void()> on_join,
                                         std::function<void()> on_end)
    : on_meeting_join_(on_join), on_meeting_end_(on_end) {}

void MeetingServiceEvent::SetOnStatusChanged(
    const std::function<void(ZOOMSDK::MeetingStatus, int)>& on_status_changed) {
  on_status_changed_ = on_status_changed;
}
void MeetingServiceEvent::onMeetingStatusChanged(ZOOMSDK::MeetingStatus status, int i_result) {
  if (on_status_changed_) {
    on_status_changed_(status, i_result);
  }

  std::string message;
  std::string icon = "⏳";

  switch (status) {
    case ZOOMSDK::MEETING_STATUS_CONNECTING:
      message = "Connecting to the meeting";
      break;
    case ZOOMSDK::MEETING_STATUS_RECONNECTING:
      message = "Reconnecting to the meeting";
      break;
    case ZOOMSDK::MEETING_STATUS_DISCONNECTING:
      message = "Disconnecting from the meeting";
      break;
    case ZOOMSDK::MEETING_STATUS_INMEETING:
      message = "Joined meeting";
      Logger::GetInstance().Success(message);
      if (on_meeting_join_) {
        on_meeting_join_();
      }
      return;
    case ZOOMSDK::MEETING_STATUS_ENDED:
      message = "Meeting ended";
      Logger::GetInstance().Success(message);
      if (on_meeting_end_) {
        on_meeting_end_();
      }
      return;
    case ZOOMSDK::MEETING_STATUS_FAILED:
      icon = "❌";
      message = "Failed to connect to the meeting";
      break;
    case ZOOMSDK::MEETING_STATUS_WAITINGFORHOST:
      message = "Waiting for the meeting to start";
      break;
    default:
      message = "Meeting status: " + std::to_string(status);
      break;
  }

  if (!message.empty()) {
    Logger::GetInstance().Info(message);
  }
}

void MeetingServiceEvent::onMeetingParameterNotification(
    const ZOOMSDK::MeetingParameter* meeting_param) {
  // Callback not implemented
}

void MeetingServiceEvent::onMeetingStatisticsWarningNotification(
    ZOOMSDK::StatisticsWarningType type) {
  // Callback not implemented
}

void MeetingServiceEvent::onSuspendParticipantsActivities() {
  // Callback not implemented
}

void MeetingServiceEvent::onAICompanionActiveChangeNotice(bool b_active) {
  // Callback not implemented
}

// Callback setters are not implemented - callbacks are set directly in
// constructor

void MeetingServiceEvent::onMeetingTopicChanged(const zchar_t* s_topic) {}

void MeetingServiceEvent::onMeetingFullToWatchLiveStream(const zchar_t* s_live_stream_url) {}
