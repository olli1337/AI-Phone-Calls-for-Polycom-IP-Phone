#pragma once

// This build never captures or renders audio on the desktop.
#define PJMEDIA_HAS_VIDEO 0
#define PJMEDIA_HAS_SRTP 0
#define PJMEDIA_TRANSPORT_SWITCH_REMOTE_ADDR 0
#define PJMEDIA_HAS_WEBRTC_AEC 0
#define PJ_HAS_IPV6 0
