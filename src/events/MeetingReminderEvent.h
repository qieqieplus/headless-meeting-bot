
#ifndef HEADLESS_ZOOM_BOT_MEETINGREMINDEREVENT_H
#define HEADLESS_ZOOM_BOT_MEETINGREMINDEREVENT_H


#include <iostream>
#include "meeting_service_components/meeting_reminder_ctrl_interface.h"

using namespace std;
using namespace ZOOMSDK;

class MeetingReminderEvent : public IMeetingReminderEvent
{
public:
    /**
     * Fires when the reminder dialog is shown
     * @param content content of the reminder
     * @param handle reminder handler for the reminder type
     */
    void onReminderNotify(IMeetingReminderContent* content, IMeetingReminderHandler* handle) override;
    void onEnableReminderNotify(IMeetingReminderContent* content, IMeetingEnableReminderHandler* handle) override;
};


#endif //HEADLESS_ZOOM_BOT_MEETINGREMINDEREVENT_H
