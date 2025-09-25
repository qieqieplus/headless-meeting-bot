package com.zoom.jna;

import com.sun.jna.Callback;
import com.sun.jna.Library;
import com.sun.jna.Native;
import com.sun.jna.Pointer;
import com.sun.jna.PointerType;

public interface ZoomSDK extends Library {

    ZoomSDK INSTANCE = Native.load("zoomsdk_c", ZoomSDK.class);

    // Opaque handles
    class ZoomSDKHandle extends PointerType {}
    class MeetingHandle extends PointerType {}

    // Return codes
    int ZOOM_SDK_SUCCESS = 0;
    int ZOOM_SDK_ERROR = -1;

    // Audio type constants
    int ZOOM_AUDIO_TYPE_MIXED = 0;
    int ZOOM_AUDIO_TYPE_ONE_WAY = 1;
    int ZOOM_AUDIO_TYPE_SHARE = 2;

    // Audio callback
    interface OnAudioDataReceivedCallback extends Callback {
        void invoke(MeetingHandle meeting_handle, Pointer data, int length, int type, int node_id);
    }

    // --- C API Functions ---

    ZoomSDKHandle zoom_sdk_create(String sdk_key, String sdk_secret);

    void zoom_sdk_destroy(ZoomSDKHandle handle);

    MeetingHandle zoom_meeting_create_and_join(ZoomSDKHandle sdk_handle,
                                               String meeting_id,
                                               String password,
                                               String display_name,
                                               int enable_audio);

    void zoom_meeting_destroy(MeetingHandle meeting_handle);

    int zoom_meeting_set_audio_callback(MeetingHandle meeting_handle, OnAudioDataReceivedCallback callback);

    void zoom_sdk_run_loop();

    void zoom_sdk_stop_loop();
}
