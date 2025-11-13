
#ifndef ZOOM_BOT_AUDIO_DELEGATE_H
#define ZOOM_BOT_AUDIO_DELEGATE_H

#include "rawdata/rawdata_audio_helper_interface.h"
#include "zoom_bot_c.h"
#include "zoom_sdk_raw_data_def.h"

// Forward declaration
class MediaController;

class ZoomBotAudioRawDataDelegate : public ZOOMSDK::IZoomSDKAudioRawDataDelegate {
 public:
  explicit ZoomBotAudioRawDataDelegate(MediaController* media_controller)
      : media_controller_(media_controller) {}

  void onMixedAudioRawDataReceived(AudioRawData* data) override;
  void onOneWayAudioRawDataReceived(AudioRawData* data, uint32_t user_id) override;
  void onShareAudioRawDataReceived(AudioRawData* data, uint32_t user_id) override;
  void onOneWayInterpreterAudioRawDataReceived(AudioRawData* data, const zchar_t* lang) override;

 private:
  MediaController* media_controller_;
};

#endif  // ZOOM_BOT_AUDIO_DELEGATE_H
