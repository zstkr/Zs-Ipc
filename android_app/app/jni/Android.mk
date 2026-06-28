LOCAL_PATH := $(call my-dir)

# ==========================================
# 1. 纯手工引入 OpenCV (静态库)
# ==========================================
OPENCV_PATH := $(LOCAL_PATH)/opencv-mobile-4.13.0-android/sdk/native

include $(CLEAR_VARS)
LOCAL_MODULE := opencv_core
LOCAL_SRC_FILES := $(OPENCV_PATH)/staticlibs/$(TARGET_ARCH_ABI)/libopencv_core.a
LOCAL_EXPORT_C_INCLUDES := $(OPENCV_PATH)/jni/include
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := opencv_imgproc
LOCAL_SRC_FILES := $(OPENCV_PATH)/staticlibs/$(TARGET_ARCH_ABI)/libopencv_imgproc.a
LOCAL_EXPORT_C_INCLUDES := $(OPENCV_PATH)/jni/include
include $(PREBUILT_STATIC_LIBRARY)

# ==========================================
# 2. 手工引入 OpenCV 的 KleidiCV 底层图像加速库
#    (还原被 Android Studio 缩写合并的 3层 真实路径！)
# ==========================================
OPENCV_3RDPARTY_PATH := $(OPENCV_PATH)/3rdparty/libs/$(TARGET_ARCH_ABI)

include $(CLEAR_VARS)
LOCAL_MODULE := kleidicv
LOCAL_SRC_FILES := $(OPENCV_3RDPARTY_PATH)/libkleidicv.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := kleidicv_hal
LOCAL_SRC_FILES := $(OPENCV_3RDPARTY_PATH)/libkleidicv_hal.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := kleidicv_thread
LOCAL_SRC_FILES := $(OPENCV_3RDPARTY_PATH)/libkleidicv_thread.a
include $(PREBUILT_STATIC_LIBRARY)

# ==========================================
# 3. 引入 NCNN 作为预编译静态库
# ==========================================
include $(CLEAR_VARS)
LOCAL_MODULE := ncnn
LOCAL_SRC_FILES := $(LOCAL_PATH)/ncnn-20260113-android-vulkan/$(TARGET_ARCH_ABI)/lib/libncnn.a
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/ncnn-20260113-android-vulkan/$(TARGET_ARCH_ABI)/include/ncnn
include $(PREBUILT_STATIC_LIBRARY)

# ==========================================
# 4. 编译你的主程序 (GStreamer + YOLOv8)
# ==========================================
include $(CLEAR_VARS)

LOCAL_MODULE    := tutorial-3
LOCAL_SRC_FILES := tutorial-3.cpp yolov8.cpp yolov8_det.cpp yolov8_pose.cpp yolov8_seg.cpp yolov8_cls.cpp

# 链接所有的静态库 (注意顺序：被依赖的放后面)
LOCAL_STATIC_LIBRARIES := ncnn opencv_imgproc opencv_core kleidicv_hal kleidicv kleidicv_thread
LOCAL_SHARED_LIBRARIES := gstreamer_android

# 开启 OpenMP 多线程支持，并静态链接
LOCAL_CPPFLAGS := -std=c++11 -frtti -fexceptions -fopenmp
LOCAL_LDFLAGS  := -fopenmp -static-openmp

# 链接 Vulkan (glslang) 的底层依赖库（去掉了不支持的 -lOGLCompiler）
NCNN_LIB_DIR := $(LOCAL_PATH)/ncnn-20260113-android-vulkan/$(TARGET_ARCH_ABI)/lib
LOCAL_LDLIBS := -llog -landroid -lvulkan -ljnigraphics -lz \
                -L$(NCNN_LIB_DIR) -lglslang -lSPIRV -lMachineIndependent -lOSDependent -lGenericCodeGen

include $(BUILD_SHARED_LIBRARY)


# ==========================================
# 5. GStreamer 原生编译规则（从你提供的完美版本中合并过来）
# ==========================================
ifndef GSTREAMER_ROOT_ANDROID
$(error GSTREAMER_ROOT_ANDROID is not defined!)
endif

ifeq ($(TARGET_ARCH_ABI),armeabi)
GSTREAMER_ROOT        := $(GSTREAMER_ROOT_ANDROID)/arm
else ifeq ($(TARGET_ARCH_ABI),armeabi-v7a)
GSTREAMER_ROOT        := $(GSTREAMER_ROOT_ANDROID)/armv7
else ifeq ($(TARGET_ARCH_ABI),arm64-v8a)
GSTREAMER_ROOT        := $(GSTREAMER_ROOT_ANDROID)/arm64
else ifeq ($(TARGET_ARCH_ABI),x86)
GSTREAMER_ROOT        := $(GSTREAMER_ROOT_ANDROID)/x86
else ifeq ($(TARGET_ARCH_ABI),x86_64)
GSTREAMER_ROOT        := $(GSTREAMER_ROOT_ANDROID)/x86_64
else
$(error Target arch ABI not supported: $(TARGET_ARCH_ABI))
endif

GSTREAMER_NDK_BUILD_PATH  := $(GSTREAMER_ROOT)/share/gst-android/ndk-build/
include $(GSTREAMER_NDK_BUILD_PATH)/plugins.mk
GSTREAMER_PLUGINS := $(GSTREAMER_PLUGINS_CORE) $(GSTREAMER_PLUGINS_SYS) $(GSTREAMER_PLUGINS_EFFECTS) udp rtp videoparsersbad androidmedia opengl playback rtpmanager libav
GSTREAMER_EXTRA_DEPS      := gstreamer-video-1.0 gobject-2.0
GSTREAMER_EXTRA_LIBS      := -liconv
include $(GSTREAMER_NDK_BUILD_PATH)/gstreamer-1.0.mk