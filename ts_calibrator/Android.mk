# Copyright 2005 The Android Open Source Project
#
# Android.mk for ts_calibrator
#

ifeq ($(TARGET_TS_CALIBRATION),true)

LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
        five_wire_calib.c \
	ts_calibrator.c

LOCAL_CFLAGS += -DTS_DEVICE=$(TARGET_TS_DEVICE)

LOCAL_MODULE := ts_calibrator
LOCAL_MODULE_TAGS := eng

LOCAL_MODULE_PATH := $(TARGET_ROOT_OUT_SBIN)
LOCAL_FORCE_STATIC_EXECUTABLE := true
LOCAL_STATIC_LIBRARIES += libcutils libc libm

include $(BUILD_EXECUTABLE)

endif
