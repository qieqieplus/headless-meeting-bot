
#ifndef HEADLESS_ZOOM_BOT_MEETINGREMINDEREVENT_H
#define HEADLESS_ZOOM_BOT_MEETINGREMINDEREVENT_H


#include <iostream>
#include "meeting_service_components/meeting_reminder_ctrl_interface.h"


class MeetingReminderEvent : public ZOOMSDK::IMeetingReminderEvent
{
public:
    /**
     * Fires when the reminder dialog is shown
     * @param content content of the reminder
     * @param handle reminder handler for the reminder type
     */
    void onReminderNotify(ZOOMSDK::IMeetingReminderContent* content, ZOOMSDK::IMeetingReminderHandler* handle) override;
    void onEnableReminderNotify(ZOOMSDK::IMeetingReminderContent* content, ZOOMSDK::IMeetingEnableReminderHandler* handle) override;
};


#endif //HEADLESS_ZOOM_BOT_MEETINGREMINDEREVENT_H
