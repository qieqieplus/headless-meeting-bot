#ifndef HEADLESS_ZOOM_BOT_MEETINGVIDEOEVENT_H
#define HEADLESS_ZOOM_BOT_MEETINGVIDEOEVENT_H

#include "meeting_service_components/meeting_video_interface.h"

class IUserEventSink;

class MeetingVideoEvent : public ZOOMSDK::IMeetingVideoCtrlEvent {
 public:
  explicit MeetingVideoEvent(IUserEventSink& sink);

  void onUserVideoStatusChange(unsigned int user_id, ZOOMSDK::VideoStatus status) override;
  void onSpotlightedUserListChangeNotification(
      ZOOMSDK::IList<unsigned int>* lst_spotlighted_user_id) override;
  void onHostRequestStartVideo(ZOOMSDK::IRequestStartVideoHandler* handler) override;
  void onActiveSpeakerVideoUserChanged(unsigned int userid) override;
  void onActiveVideoUserChanged(unsigned int userid) override;
  void onHostVideoOrderUpdated(ZOOMSDK::IList<unsigned int>* order_list) override;
  void onLocalVideoOrderUpdated(ZOOMSDK::IList<unsigned int>* local_order_list) override;
  void onFollowHostVideoOrderChanged(bool b_follow) override;
  void onUserVideoQualityChanged(ZOOMSDK::VideoConnectionQuality quality,
                                 unsigned int userid) override;
  void onVideoAlphaChannelStatusChanged(bool is_alpha_mode_on) override;
  void onCameraControlRequestReceived(unsigned int user_id,
                                      ZOOMSDK::CameraControlRequestType request_type,
                                      ZOOMSDK::ICameraControlRequestHandler* p_handler) override;
  void onCameraControlRequestResult(unsigned int user_id,
                                    ZOOMSDK::CameraControlRequestResult result) override;

 private:
  IUserEventSink& sink_;
};

#endif  // HEADLESS_ZOOM_BOT_MEETINGVIDEOEVENT_H
