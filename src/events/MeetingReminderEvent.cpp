#include "MeetingReminderEvent.h"
#include "util/Logger.h"

void MeetingReminderEvent::onReminderNotify(
    ZOOMSDK::IMeetingReminderContent *content,
    ZOOMSDK::IMeetingReminderHandler *handle) {
  if (content) {
    Logger::getInstance().info("Reminder Notification Received");
    Logger::getInstance().debug("Type: " + std::to_string(content->GetType()));
    Logger::getInstance().debug("Title: " + std::string(content->GetTitle()));
    Logger::getInstance().debug("Content: " +
                                std::string(content->GetContent()));
    Logger::getInstance().debug(
        "Is Blocking?: " +
        std::string(content->IsBlocking() ? "true" : "false"));
  }

  if (handle)
    handle->Accept();
}

void MeetingReminderEvent::onEnableReminderNotify(
    ZOOMSDK::IMeetingReminderContent *content,
    ZOOMSDK::IMeetingEnableReminderHandler *handle) {
  if (content) {
    Logger::getInstance().info("Enable Reminder Notification Received");
    Logger::getInstance().debug("Type: " + std::to_string(content->GetType()));
    Logger::getInstance().debug("Title: " + std::string(content->GetTitle()));
    Logger::getInstance().debug("Content: " +
                                std::string(content->GetContent()));
    Logger::getInstance().debug(
        "Is Blocking?: " +
        std::string(content->IsBlocking() ? "true" : "false"));
  }

  if (handle)
    handle->Ignore();
}