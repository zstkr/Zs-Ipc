#include <string.h>
#include <stdint.h>
#include <jni.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <pthread.h>

// 引入 OpenCV 和 YOLOv8 头文件
#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include "yolov8.h"
extern "C" void __kmpc_dispatch_deinit(void* loc, int gtid) {}
GST_DEBUG_CATEGORY_STATIC (debug_category);
#define GST_CAT_DEFAULT debug_category

/*
 * These macros provide a way to store the native pointer to CustomData, which might be 32 or 64 bits, into
 * a jlong, which is always 64 bits, without warnings.
 */
#if GLIB_SIZEOF_VOID_P == 8
#define GET_CUSTOM_DATA(env, thiz, fieldID) (CustomData *)env->GetLongField(thiz, fieldID)
#define SET_CUSTOM_DATA(env, thiz, fieldID, data) env->SetLongField(thiz, fieldID, (jlong)data)
#else
# define GET_CUSTOM_DATA(env, thiz, fieldID) (CustomData *)(jint)env->GetLongField (thiz, fieldID)
# define SET_CUSTOM_DATA(env, thiz, fieldID, data) env->SetLongField (thiz, fieldID, (jlong)(jint)data)
#endif

/* Structure to contain all our information, so we can pass it to callbacks */
typedef struct _CustomData
{
    jobject app;                  /* Application instance, used to call its methods. A global reference is kept. */
    GstElement *pipeline;         /* The running pipeline */
    GMainContext *context;        /* GLib context used to run the main loop */
    GMainLoop *main_loop;         /* GLib main loop */
    gboolean initialized;         /* To avoid informing the UI multiple times about the initialization */
    GstElement *video_sink;       /* The video sink element (now appsink) */
    ANativeWindow *native_window; /* The Android native window where video will be rendered */
} CustomData;

/* These global variables cache values which are not changing during execution */
static pthread_t gst_app_thread;
static pthread_key_t current_jni_env;
static JavaVM *java_vm;
static jfieldID custom_data_field_id;
static jmethodID set_message_method_id;
static jmethodID on_gstreamer_initialized_method_id;
static ncnn::Mutex lock;
// 定义全局的 YOLOv8 实例
static YOLOv8* yolov8 = nullptr;

/*
 * Private methods
 */

/* appsink 的回调函数声明 */
static GstFlowReturn on_new_sample (GstElement * sink, CustomData * data);

/* Register this thread with the VM */
static JNIEnv *
attach_current_thread (void)
{
  JNIEnv *env;
  JavaVMAttachArgs args;

  GST_DEBUG ("Attaching thread %p", g_thread_self ());
  args.version = JNI_VERSION_1_4;
  args.name = NULL;
  args.group = NULL;

  if (java_vm->AttachCurrentThread (&env, &args) < 0) {
    GST_ERROR ("Failed to attach current thread");
    return NULL;
  }

  return env;
}

/* Unregister this thread from the VM */
static void
detach_current_thread (void *env)
{
  GST_DEBUG ("Detaching thread %p", g_thread_self ());
  java_vm->DetachCurrentThread ();
}

/* Retrieve the JNI environment for this thread */
static JNIEnv *
get_jni_env (void)
{
  JNIEnv *env;

  if ((env = (JNIEnv *)pthread_getspecific (current_jni_env)) == NULL) {
    env = attach_current_thread ();
    pthread_setspecific (current_jni_env, env);
  }

  return env;
}

/* Change the content of the UI's TextView */
static void
set_ui_message (const gchar * message, CustomData * data)
{
  JNIEnv *env = get_jni_env ();
  GST_DEBUG ("Setting message to: %s", message);
  jstring jmessage = env->NewStringUTF (message);
  env->CallVoidMethod ( data->app, set_message_method_id, jmessage);
  if (env->ExceptionCheck ()) {
    GST_ERROR ("Failed to call Java method");
    env->ExceptionClear ();
  }
  env->DeleteLocalRef (jmessage);
}

/* Retrieve errors from the bus and show them on the UI */
static void
error_cb (GstBus * bus, GstMessage * msg, CustomData * data)
{
  GError *err;
  gchar *debug_info;
  gchar *message_string;

  gst_message_parse_error (msg, &err, &debug_info);
  message_string =
          g_strdup_printf ("Error received from element %s: %s",
                           GST_OBJECT_NAME (msg->src), err->message);
  g_clear_error (&err);
  g_free (debug_info);
  set_ui_message (message_string, data);
  g_free (message_string);
  gst_element_set_state (data->pipeline, GST_STATE_NULL);
}

/* Notify UI about pipeline state changes */
static void
state_changed_cb (GstBus * bus, GstMessage * msg, CustomData * data)
{
  GstState old_state, new_state, pending_state;
  gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
  /* Only pay attention to messages coming from the pipeline, not its children */
  if (GST_MESSAGE_SRC (msg) == GST_OBJECT (data->pipeline)) {
    gchar *message = g_strdup_printf ("State changed to %s",
                                      gst_element_state_get_name (new_state));
    set_ui_message (message, data);
    g_free (message);
  }
}

/* Check if all conditions are met to report GStreamer as initialized. */
static void
check_initialization_complete (CustomData * data)
{
  JNIEnv *env = get_jni_env ();
  if (!data->initialized && data->native_window && data->main_loop) {
    GST_DEBUG
    ("Initialization complete, notifying application. native_window:%p main_loop:%p",
     data->native_window, data->main_loop);

    env->CallVoidMethod (data->app, on_gstreamer_initialized_method_id);
    if (env->ExceptionCheck ()) {
      GST_ERROR ("Failed to call Java method");
      env->ExceptionClear ();
    }
    data->initialized = TRUE;
  }
}

/* GStreamer 原始图像截获与 YOLOv8 目标检测核心回调函数 */
static GstFlowReturn on_new_sample (GstElement * sink, CustomData * data) {
  GstSample *sample = nullptr;
  g_signal_emit_by_name (sink, "pull-sample", &sample);
  if (!sample) {
    return GST_FLOW_OK;
  }

  GstBuffer *buffer = gst_sample_get_buffer (sample);
  GstCaps *caps = gst_sample_get_caps (sample);
  GstStructure *s = gst_caps_get_structure (caps, 0);

  int width, height;
  gst_structure_get_int (s, "width", &width);
  gst_structure_get_int (s, "height", &height);

  GstMapInfo map;
  if (gst_buffer_map (buffer, &map, GST_MAP_READ)) {
    // 1. 获取原始 4 通道 RGBA 图像
    cv::Mat rgba_mat(height, width, CV_8UC4, map.data);

    // 定义最终用于投递显示屏幕的画面矩阵
    cv::Mat draw_mat;

    // 【优化逻辑】：先加锁检查当前是否有模型激活
    bool has_active_model = false;
    {
      ncnn::MutexLockGuard g(lock);
      if (yolov8 != nullptr) {
        has_active_model = true;
      }
    }

    if (has_active_model) {
      // ==========================================
      // A. 有模型：需要进行转码、推理、画框并转回 RGBA
      // ==========================================
      cv::Mat rgb_mat;
      cv::cvtColor(rgba_mat, rgb_mat, cv::COLOR_RGBA2RGB);

      std::vector<Object> objects;
      {
        ncnn::MutexLockGuard g(lock);
        // 双重检查保护，防止在多线程等待锁期间模型被 set_task 销毁而引发空指针异常
        if (yolov8) {
          yolov8->detect(rgb_mat, objects);
          yolov8->draw(rgb_mat, objects);
        }
      }
      // 转回 4 通道供显示
      cv::cvtColor(rgb_mat, draw_mat, cv::COLOR_RGB2RGBA);
    } else {
      // ==========================================
      // B. 无模型（task_id == -1）：旁路设计，零拷贝零格式转换，丝滑播放！
      // ==========================================
      draw_mat = rgba_mat;
    }

    // 2. 将画面渲染到安卓手机的 Native Window 屏幕上
    if (data->native_window) {
      ANativeWindow_Buffer window_buffer;
      ANativeWindow_setBuffersGeometry(data->native_window, width, height, WINDOW_FORMAT_RGBA_8888);
      if (ANativeWindow_lock(data->native_window, &window_buffer, NULL) == 0) {
        uint8_t *src = draw_mat.data;
        uint8_t *dst = (uint8_t *)window_buffer.bits;
        int src_stride = draw_mat.step[0];
        int dst_stride = window_buffer.stride * 4;

        // 逐行拷贝，防止花屏
        for (int i = 0; i < height; i++) {
          memcpy(dst + i * dst_stride, src + i * src_stride, width * 4);
        }
        ANativeWindow_unlockAndPost(data->native_window);
      }
    }
    gst_buffer_unmap (buffer, &map);
  }
  gst_sample_unref (sample);
  return GST_FLOW_OK;
}

/* Main method for the native code. This is executed on its own thread. */
static void *
app_function (void *userdata)
{
  GstBus *bus;
  CustomData *data = (CustomData *) userdata;
  GSource *bus_source;
  GError *error = NULL;

  GST_DEBUG ("Creating pipeline in CustomData at %p", data);

  /* Create our own GLib Main Context and make it the default one */
  data->context = g_main_context_new ();
  g_main_context_push_thread_default (data->context);

  /* Build pipeline */
  data->pipeline =
          gst_parse_launch("udpsrc address=0.0.0.0 port=5004 buffer-size=2097152 caps=\"application/x-rtp, media=(string)video, clock-rate=(int)90000, encoding-name=(string)H264\" ! "
                           "rtpjitterbuffer latency=0 ! rtph264depay ! h264parse ! avdec_h264 ! "
                           "queue max-size-buffers=1 leaky=downstream ! videoconvert ! video/x-raw,format=RGBA ! "
                           "appsink name=mysink emit-signals=true max-buffers=1 drop=true sync=false", &error);
  if (error) {
    gchar *message =
            g_strdup_printf ("Unable to build pipeline: %s", error->message);
    g_clear_error (&error);
    set_ui_message (message, data);
    g_free (message);
    return NULL;
  }

  /* Set the pipeline to READY */
  gst_element_set_state (data->pipeline, GST_STATE_READY);

  data->video_sink = gst_bin_get_by_name (GST_BIN (data->pipeline), "mysink");
  if (!data->video_sink) {
    GST_ERROR ("Could not retrieve appsink");
    return NULL;
  }

  /* Instruct the bus to emit signals for each received message, and connect to the interesting signals */
  bus = gst_element_get_bus (data->pipeline);
  bus_source = gst_bus_create_watch (bus);
  g_source_set_callback (bus_source, (GSourceFunc) gst_bus_async_signal_func,
                         NULL, NULL);
  g_source_attach (bus_source, data->context);
  g_source_unref (bus_source);
  g_signal_connect (G_OBJECT (bus), "message::error", (GCallback) error_cb,
                    data);
  g_signal_connect (G_OBJECT (bus), "message::state-changed",
                    (GCallback) state_changed_cb, data);
  gst_object_unref (bus);

  g_signal_connect (data->video_sink, "new-sample", G_CALLBACK (on_new_sample), data);

  /* Create a GLib Main Loop and set it to run */
  GST_DEBUG ("Entering main loop... (CustomData:%p)", data);
  data->main_loop = g_main_loop_new (data->context, FALSE);
  check_initialization_complete (data);
  g_main_loop_run (data->main_loop);
  GST_DEBUG ("Exited main loop");
  g_main_loop_unref (data->main_loop);
  data->main_loop = NULL;

  /* Free resources */
  g_main_context_pop_thread_default (data->context);
  g_main_context_unref (data->context);
  gst_element_set_state (data->pipeline, GST_STATE_NULL);
  gst_object_unref (data->video_sink);
  gst_object_unref (data->pipeline);

  return NULL;
}

/*
 * Java Bindings
 */

/* Instruct the native code to create its internal data structure, pipeline and thread */
static void
gst_native_init (JNIEnv * env, jobject thiz, jobject asset_manager)
{
  CustomData *data = g_new0 (CustomData, 1);
  SET_CUSTOM_DATA (env, thiz, custom_data_field_id, data);
  GST_DEBUG_CATEGORY_INIT (debug_category, "tutorial-3", 0,
                           "Android tutorial 3");
  gst_debug_set_threshold_for_name ("tutorial-3", GST_LEVEL_DEBUG);
  GST_DEBUG ("Created CustomData at %p", data);
  data->app = env->NewGlobalRef ( thiz);
  GST_DEBUG ("Created GlobalRef for app object at %p", data->app);

  AAssetManager* mgr = AAssetManager_fromJava(env, asset_manager);
  if (!yolov8) {
    yolov8 = new YOLOv8_det_coco();
    yolov8->load(mgr, "yolov8n.ncnn.param", "yolov8n.ncnn.bin", false);
    yolov8->set_det_target_size(320);
  }

  pthread_create (&gst_app_thread, NULL, &app_function, data);
}

/* Quit the main loop, remove the native thread and free resources */
static void
gst_native_finalize (JNIEnv * env, jobject thiz)
{
  CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);
  if (!data)
    return;
  GST_DEBUG ("Quitting main loop...");
  g_main_loop_quit (data->main_loop);
  GST_DEBUG ("Waiting for thread to finish...");
  pthread_join (gst_app_thread, NULL);
  GST_DEBUG ("Deleting GlobalRef for app object at %p", data->app);
  env->DeleteGlobalRef (data->app);
  GST_DEBUG ("Freeing CustomData at %p", data);
  g_free (data);
  SET_CUSTOM_DATA (env, thiz, custom_data_field_id, NULL);
  GST_DEBUG ("Done finalizing");
}

/* Set pipeline to PLAYING state */
static void
gst_native_play (JNIEnv * env, jobject thiz)
{
  CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);
  if (!data)
    return;
  GST_DEBUG ("Setting state to PLAYING");
  gst_element_set_state (data->pipeline, GST_STATE_PLAYING);
}

/* Set pipeline to PAUSED state */
static void
gst_native_pause (JNIEnv * env, jobject thiz)
{
  CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);
  if (!data)
    return;
  GST_DEBUG ("Setting state to PAUSED");
  gst_element_set_state (data->pipeline, GST_STATE_PAUSED);
}

/* Static class initializer: retrieve method and field IDs */
static jboolean
gst_native_class_init (JNIEnv * env, jclass klass)
{
  custom_data_field_id =
          env->GetFieldID (klass, "native_custom_data", "J");
  set_message_method_id =
          env->GetMethodID (klass, "setMessage", "(Ljava/lang/String;)V");
  on_gstreamer_initialized_method_id =
          env->GetMethodID ( klass, "onGStreamerInitialized", "()V");

  if (!custom_data_field_id || !set_message_method_id
      || !on_gstreamer_initialized_method_id) {
    return JNI_FALSE;
  }
  return JNI_TRUE;
}

static void
gst_native_surface_init (JNIEnv * env, jobject thiz, jobject surface)
{
  CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);
  if (!data)
    return;
  ANativeWindow *new_native_window = ANativeWindow_fromSurface (env, surface);
  GST_DEBUG ("Received surface %p (native window %p)", surface,
             new_native_window);

  if (data->native_window) {
    ANativeWindow_release (data->native_window);
    if (data->native_window == new_native_window) {
      GST_DEBUG ("New native window is the same as the previous one %p",
                 data->native_window);
      return;
    } else {
      GST_DEBUG ("Released previous native window %p", data->native_window);
      data->initialized = FALSE;
    }
  }
  data->native_window = new_native_window;

  check_initialization_complete (data);
}

static void
gst_native_surface_finalize (JNIEnv * env, jobject thiz)
{
  CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);
  if (!data)
    return;
  GST_DEBUG ("Releasing Native Window %p", data->native_window);

  ANativeWindow_release (data->native_window);
  data->native_window = NULL;
  data->initialized = FALSE;
}

/* 【关键修改】：支持切换到 -1 (不使用任何模型) 模式 */
static void
gst_native_set_task (JNIEnv * env, jobject thiz, jobject asset_manager, jint task_id)
{
  // 加锁，让视频流线程先暂停一下
  ncnn::MutexLockGuard g(lock);

  // 1. 销毁旧模型
  if (yolov8) {
    delete yolov8;
    yolov8 = nullptr;
  }

  // 如果传入 -1，表示直接禁用模型，我们直接置空后返回，不加载任何模型
  if (task_id == -1) {
    return;
  }

  // 2. 实例化新模型
  if (task_id == 0) yolov8 = new YOLOv8_det_coco();
  if (task_id == 2) yolov8 = new YOLOv8_seg();
  if (task_id == 3) yolov8 = new YOLOv8_pose();

  // 3. 动态拼接文件名
  std::string task_suffix = "";
  if (task_id == 2) task_suffix = "_seg";
  if (task_id == 3) task_suffix = "_pose";

  std::string param_name = "yolov8n" + task_suffix + ".ncnn.param";
  std::string bin_name = "yolov8n" + task_suffix + ".ncnn.bin";

  // 4. 加载新模型
  AAssetManager* mgr = AAssetManager_fromJava(env, asset_manager);
  if (yolov8) {
    yolov8->load(mgr, param_name.c_str(), bin_name.c_str(), false);
    yolov8->set_det_target_size(320);
  }
}
/*动态切换视频流源头（0 = 远端板载 IPC, 1 = 手机本地摄像头） */
static void
gst_native_set_source (JNIEnv * env, jobject thiz, jint source_id)
{
  CustomData *data = GET_CUSTOM_DATA (env, thiz, custom_data_field_id);
  if (!data) return;

  // 1. 加锁，暂停视频流并彻底清理旧管道
  ncnn::MutexLockGuard g(lock);

  gst_element_set_state (data->pipeline, GST_STATE_NULL);

  // 解绑 appsink 信号，释放旧节点
  g_signal_handlers_disconnect_by_func (data->video_sink, (gpointer) on_new_sample, data);
  gst_object_unref (data->video_sink);
  gst_object_unref (data->pipeline);

  // 2. 根据用户选择，构建对应的 GStreamer 管道
  GError *error = NULL;
  if (source_id == 0) {
    // 🎥 方案 A：网络 IPC 摄像头流
    data->pipeline = gst_parse_launch(
            "udpsrc address=0.0.0.0 port=5004 buffer-size=2097152 caps=\"application/x-rtp, media=(string)video, clock-rate=(int)90000, encoding-name=(string)H264\" ! "
            "rtpjitterbuffer latency=0 ! rtph264depay ! h264parse ! avdec_h264 ! "
            "queue max-size-buffers=1 leaky=downstream ! videoconvert ! video/x-raw,format=RGBA ! "
            "appsink name=mysink emit-signals=true max-buffers=1 drop=true sync=false", &error);
  }  else {
    // 📱 方案 B：手机本地摄像头（ 移除了不兼容的 camera=0 属性，恢复自适应纯净的 ahcsrc 驱动）
    data->pipeline = gst_parse_launch(
            "ahcsrc ! queue max-size-buffers=1 leaky=downstream ! videoconvert ! video/x-raw,format=RGBA ! "
            "appsink name=mysink emit-signals=true max-buffers=1 drop=true sync=false", &error);
  }

  if (error) {
    __android_log_print(ANDROID_LOG_ERROR, "GStreamer", "❌ 切换管道失败: %s", error->message);
    g_clear_error (&error);
    return;
  }

  // 3. 重新抓取并关联我们的 "mysink" appsink 节点并挂载 YOLO 回调
  data->video_sink = gst_bin_get_by_name (GST_BIN (data->pipeline), "mysink");
  g_signal_connect (data->video_sink, "new-sample", G_CALLBACK (on_new_sample), data);

  /* 4. 重新建立并接管 Bus 总线监听（换了新管道，Bus 变了！） */
  GstBus *bus = gst_element_get_bus (data->pipeline);
  GSource *bus_source = gst_bus_create_watch (bus);
  g_source_set_callback (bus_source, (GSourceFunc) gst_bus_async_signal_func, NULL, NULL);
  g_source_attach (bus_source, data->context);
  g_source_unref (bus_source);
  g_signal_connect (G_OBJECT (bus), "message::error", (GCallback) error_cb, data);
  g_signal_connect (G_OBJECT (bus), "message::state-changed", (GCallback) state_changed_cb, data);
  gst_object_unref (bus);

  // 5.强行命令新管道直接进入 PLAYING 播放状态，确保画面立刻唤醒！
  gst_element_set_state (data->pipeline, GST_STATE_PLAYING);
}
/* List of implemented native methods */
static JNINativeMethod native_methods[] = {
        {"nativeInit", "(Landroid/content/res/AssetManager;)V", (void *) gst_native_init},
        {"nativeFinalize", "()V", (void *) gst_native_finalize},
        {"nativePlay", "()V", (void *) gst_native_play},
        {"nativePause", "()V", (void *) gst_native_pause},
        {"nativeSurfaceInit", "(Ljava/lang/Object;)V",
                                                                (void *) gst_native_surface_init},
        {"nativeSurfaceFinalize", "()V", (void *) gst_native_surface_finalize},
        {"nativeClassInit", "()Z", (void *) gst_native_class_init},
        {"nativeSetTask", "(Landroid/content/res/AssetManager;I)V", (void *) gst_native_set_task},
        {"nativeSetSource", "(I)V", (void *) gst_native_set_source}
};

/* Library initializer */
extern "C" JNIEXPORT jint JNICALL
JNI_OnLoad (JavaVM * vm, void *reserved)
{
  JNIEnv *env = NULL;

  java_vm = vm;

  if (vm->GetEnv((void **)&env, JNI_VERSION_1_4) != JNI_OK) {
    __android_log_print (ANDROID_LOG_ERROR, "tutorial-3",
                         "Could not retrieve JNIEnv");
    return 0;
  }
  jclass klass = env->FindClass (
          "org/freedesktop/gstreamer/tutorials/tutorial_3/Tutorial3");
  env->RegisterNatives (klass, native_methods,
                        G_N_ELEMENTS (native_methods));

  pthread_key_create (&current_jni_env, detach_current_thread);

  return JNI_VERSION_1_4;
}