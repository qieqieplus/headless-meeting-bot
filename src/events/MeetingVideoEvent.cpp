#include "MeetingVideoEvent.h"

#include <string>

#include "IUserEventSink.h"
#include "util/Logger.h"
MeetingVideoEvent::MeetingVideoEvent(IUserEventSink& sink) : sink_(sink) {}

void MeetingVideoEvent::onUserVideoStatusChange(unsigned int user_id, ZOOMSDK::VideoStatus status) {
  sink_.HandleVideoStatus(user_id, status);
}

void MeetingVideoEvent::onSpotlightedUserListChangeNotification(ZOOMSDK::IList<unsigned int>*) {}

void MeetingVideoEvent::onHostRequestStartVideo(ZOOMSDK::IRequestStartVideoHandler*) {}

void MeetingVideoEvent::onActiveSpeakerVideoUserChanged(unsigned int) {}

void MeetingVideoEvent::onActiveVideoUserChanged(unsigned int) {}

void MeetingVideoEvent::onHostVideoOrderUpdated(ZOOMSDK::IList<unsigned int>*) {}

void MeetingVideoEvent::onLocalVideoOrderUpdated(ZOOMSDK::IList<unsigned int>*) {}

void MeetingVideoEvent::onFollowHostVideoOrderChanged(bool) {}

void MeetingVideoEvent::onUserVideoQualityChanged(ZOOMSDK::VideoConnectionQuality, unsigned int) {}

void MeetingVideoEvent::onVideoAlphaChannelStatusChanged(bool) {}

void MeetingVideoEvent::onCameraControlRequestReceived(unsigned int,
                                                       ZOOMSDK::CameraControlRequestType,
                                                       ZOOMSDK::ICameraControlRequestHandler*) {}

void MeetingVideoEvent::onCameraControlRequestResult(unsigned int,
                                                     ZOOMSDK::CameraControlRequestResult) {}
