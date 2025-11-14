#pragma once

#include <functional>

#include "meeting_service_interface.h"

class MeetingServiceEvent : public ZOOMSDK::IMeetingServiceEvent {
  std::function<void()> on_meeting_join_;
  std::function<void()> on_meeting_end_;
  std::function<void(ZOOMSDK::MeetingStatus, int)> on_status_changed_;

 public:
  MeetingServiceEvent(std::function<void()> on_join, std::function<void()> on_end);

  void SetOnStatusChanged(
      const std::function<void(ZOOMSDK::MeetingStatus, int)>& on_status_changed);

  /**
   * Meeting status changed callback
   * @param status value of the current meeting status
   * @param iResult detailed reasons for special meeting statuses
   */
  void onMeetingStatusChanged(ZOOMSDK::MeetingStatus status, int i_result) override;

  /**
   * callback will be triggered right before the meeting starts
   * The meeting_param will be destroyed once the function calls end
   * @param meeting_param holds parameters for a newly created meeting
   */
  void onMeetingParameterNotification(const ZOOMSDK::MeetingParameter* meeting_param) override;

  /**
   * callback used when there are Meeting statistics warning notifications
   * @param type type of statistics warning
   */
  void onMeetingStatisticsWarningNotification(ZOOMSDK::StatisticsWarningType type) override;

  /**
   * Callback event when a meeting is suspended
   */
  void onSuspendParticipantsActivities() override;

  /**
   * Callback event used when the AI Companion active status changed.
   * @param bActive true if the AI Companion is active
   */
  void onAICompanionActiveChangeNotice(bool b_active) override;

  void onMeetingTopicChanged(const zchar_t* s_topic) override;

  void onMeetingFullToWatchLiveStream(const zchar_t* s_live_stream_url) override;
};
