#ifndef HEADLESS_ZOOM_BOT_MEETINGSERVICEEVENT_H
#define HEADLESS_ZOOM_BOT_MEETINGSERVICEEVENT_H

#include <iostream>
#include <string>
#include <functional>
#include "meeting_service_interface.h"

using namespace std;
using namespace ZOOMSDK;

class MeetingServiceEvent : public IMeetingServiceEvent {
    function<void()> m_onMeetingJoin;
    function<void()> m_onMeetingEnd;

public:
    MeetingServiceEvent(function<void()> onJoin, function<void()> onEnd);

    /**
     * Meeting status changed callback
     * @param status value of the current meeting status
     * @param iResult detailed reasons for special meeting statuses
     */
    void onMeetingStatusChanged(MeetingStatus status, int iResult) override;

    /**
     * callback will be triggered right before the meeting starts
     * The meeting_param will be destroyed once the function calls end
     * @param meeting_param holds parameters for a newly created meeting
     */
    void onMeetingParameterNotification(const MeetingParameter* meeting_param) override;

    /**
     * callback used when there are Meeting statistics warning notifications
     * @param type type of statistics warning
     */
    void onMeetingStatisticsWarningNotification(StatisticsWarningType type) override;

    /**
     * Callback event when a meeting is suspended
     */
    void onSuspendParticipantsActivities() override;

    /**
     * Callback event used when the AI Companion active status changed.
     * @param bActive true if the AI Companion is active
     */
    void onAICompanionActiveChangeNotice(bool bActive) override;

    void onMeetingTopicChanged(const zchar_t* sTopic) override;

    void onMeetingFullToWatchLiveStream(const zchar_t* sLiveStreamUrl) override;


};

#endif //HEADLESS_ZOOM_BOT_MEETINGSERVICEEVENT_H
