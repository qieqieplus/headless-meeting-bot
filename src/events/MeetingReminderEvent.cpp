#include "MeetingReminderEvent.h"

#include "util/Logger.h"

void MeetingReminderEvent::onReminderNotify(ZOOMSDK::IMeetingReminderContent* content,
                                            ZOOMSDK::IMeetingReminderHandler* handle) {
  if (content) {
    Logger::GetInstance().Info("Reminder Notification Received");
    Logger::GetInstance().Debug("Type: " + std::to_string(content->GetType()));
    Logger::GetInstance().Debug("Title: " + std::string(content->GetTitle()));
    Logger::GetInstance().Debug("Content: " + std::string(content->GetContent()));
    Logger::GetInstance().Debug("Is Blocking?: " +
                                std::string(content->IsBlocking() ? "true" : "false"));
  }

  if (handle) {
    handle->Accept();
  }
}

void MeetingReminderEvent::onEnableReminderNotify(ZOOMSDK::IMeetingReminderContent* content,
                                                  ZOOMSDK::IMeetingEnableReminderHandler* handle) {
  if (content) {
    Logger::GetInstance().Info("Enable Reminder Notification Received");
    Logger::GetInstance().Debug("Type: " + std::to_string(content->GetType()));
    Logger::GetInstance().Debug("Title: " + std::string(content->GetTitle()));
    Logger::GetInstance().Debug("Content: " + std::string(content->GetContent()));
    Logger::GetInstance().Debug("Is Blocking?: " +
                                std::string(content->IsBlocking() ? "true" : "false"));
  }

  if (handle) {
    handle->Ignore();
  }
}