#include "MeetingReminderEvent.h"
#include "util/Logger.h"

void MeetingReminderEvent::onReminderNotify(IMeetingReminderContent* content, IMeetingReminderHandler* handle) {
    if (content) {
        Util::Logger::getInstance().info("Reminder Notification Received");
        Util::Logger::getInstance().debug("Type: " + std::to_string(content->GetType()));
        Util::Logger::getInstance().debug("Title: " + std::string(content->GetTitle()));
        Util::Logger::getInstance().debug("Content: " + std::string(content->GetContent()));
        Util::Logger::getInstance().debug("Is Blocking?: " + std::string(content->IsBlocking() ? "true" : "false"));
    }

    if (handle)
        handle->Accept();
}

void MeetingReminderEvent::onEnableReminderNotify(IMeetingReminderContent *content, IMeetingEnableReminderHandler *handle) {
    if (content) {
        Util::Logger::getInstance().info("Enable Reminder Notification Received");
        Util::Logger::getInstance().debug("Type: " + std::to_string(content->GetType()));
        Util::Logger::getInstance().debug("Title: " + std::string(content->GetTitle()));
        Util::Logger::getInstance().debug("Content: " + std::string(content->GetContent()));
        Util::Logger::getInstance().debug("Is Blocking?: " + std::string(content->IsBlocking() ? "true" : "false"));
    }

    if (handle)
        handle->Ignore();
}