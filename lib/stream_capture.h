/* Whole-stream recording for HLS and FLAC. GPL-2.0-or-later. */
#ifndef STREAM_CAPTURE_H
#define STREAM_CAPTURE_H
#include "srtypes.h"
#include "errors.h"
#define SR_SUCCESS_END_OF_STREAM 2
error_code stream_capture_prepare(RIP_MANAGER_INFO* rmi);
error_code stream_capture_rip(RIP_MANAGER_INFO* rmi);
BOOL stream_capture_is_hls(RIP_MANAGER_INFO* rmi);
int stream_capture_file_type(RIP_MANAGER_INFO* rmi);
void stream_capture_destroy(RIP_MANAGER_INFO* rmi);
#endif
