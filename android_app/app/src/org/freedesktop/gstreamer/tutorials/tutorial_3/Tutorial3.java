package org.freedesktop.gstreamer.tutorials.tutorial_3;

import android.app.Activity;
import android.os.Bundle;
import android.util.Log;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.widget.TextView;
import android.widget.Toast;
import android.content.res.AssetManager;
import org.freedesktop.gstreamer.GStreamer;

public class Tutorial3 extends Activity implements SurfaceHolder.Callback {
    // C/C++ Native Functions
    private native void nativeInit(AssetManager assetManager);
    private native void nativeFinalize();
    private native void nativePlay();
    private native void nativePause();
    private static native boolean nativeClassInit();
    private native void nativeSurfaceInit(Object surface);
    private native void nativeSurfaceFinalize();
    private long native_custom_data;
    private native void nativeSetTask(android.content.res.AssetManager assetManager, int taskId);
    //底层切换视频源的方法声明
    private native void nativeSetSource(int sourceId);

    // All our OSD TextViews
    private TextView osdTopText, osdBottomLeft, osdBottomRight;
    private android.os.Handler osdHandler = new android.os.Handler();

    // Simulated Telemetry Data
    private float mockVoltage = 16.8f;
    private int mockAltitude = 0;

    // Called when the activity is first created.
    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // Initialize GStreamer
        try {
            GStreamer.init(this);
        } catch (Exception e) {
            Toast.makeText(this, e.getMessage(), Toast.LENGTH_LONG).show();
            finish();
            return;
        }

        // Set our new FPV-style layout
        setContentView(R.layout.main); // Ensure your layout file is named "main.xml"

        // Android 运行时动态申请摄像头权限
        if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.M) {
            if (checkSelfPermission(android.Manifest.permission.CAMERA) != android.content.pm.PackageManager.PERMISSION_GRANTED) {
                requestPermissions(new String[]{android.Manifest.permission.CAMERA}, 100);
            }
        }

        // Find all OSD TextViews from the layout
        osdTopText = (TextView) findViewById(R.id.osd_top_text);
        osdBottomLeft = (TextView) findViewById(R.id.osd_bottom_left);
        osdBottomRight = (TextView) findViewById(R.id.osd_bottom_right);

        // Find the video SurfaceView and set up its callbacks
        SurfaceView sv = (SurfaceView) this.findViewById(R.id.surface_video);
        SurfaceHolder sh = sv.getHolder();
        sh.addCallback(this);

        // Start the OSD simulation
        startOsdSimulation();

        // Initialize native GStreamer code
        nativeInit(getAssets());

        // 找到我们刚才在 XML 里加的下拉菜单
        android.widget.Spinner spinner = findViewById(R.id.spinner_task);
        spinner.setOnItemSelectedListener(new android.widget.AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(android.widget.AdapterView<?> parent, android.view.View view, int position, long id) {
                // 把文字颜色改成绿色，配合你的 OSD 风格
                if (parent.getChildAt(0) != null) {
                    ((android.widget.TextView) parent.getChildAt(0)).setTextColor(android.graphics.Color.GREEN);
                }

                // position 映射：0=检测, 1=分割, 2=姿态, 3=关闭模型
                int taskId = position;
                if (position == 1) taskId = 2; // 对应底层的 taskid_seg
                if (position == 2) taskId = 3; // 对应底层的 taskid_pose
                if (position == 3) taskId = -1; //对应底层禁用模型

                // 调用 C++ 方法，把 AssetManager 传过去！
                nativeSetTask(getAssets(), taskId);
            }

            @Override
            public void onNothingSelected(android.widget.AdapterView<?> parent) {}
        });

        // 将开关监听完美收纳在 onCreate 方法的末尾！
        android.widget.ToggleButton cameraSwitch = findViewById(R.id.switch_camera);
        if (cameraSwitch != null) {
            cameraSwitch.setOnCheckedChangeListener(new android.widget.CompoundButton.OnCheckedChangeListener() {
                @Override
                public void onCheckedChanged(android.widget.CompoundButton buttonView, boolean isChecked) {
                    // isChecked 改变时：true 代表选中（使用本地摄像头），false 代表未选中（使用远端 IPC）
                    onSwitchCameraClicked(isChecked);
                }
            });
        }
    } 

    //用来中转触发底层的 C++ 视频源切换
    public void onSwitchCameraClicked(boolean useLocalCamera) {
        if (useLocalCamera) {
            nativeSetSource(1); // 📱 切换到手机本地摄像头，直接对着现实世界运行 YOLO！
        } else {
            nativeSetSource(0); // 🎥 切换回 Luckfox 开发板发来的 IPC 远端画面
        }
    }

    private void onGStreamerInitialized () {
        Log.i ("GStreamer", "Gst Initialized. Setting to PLAYING state.");
        nativePlay();
    }

    private void setMessage(final String message) {
        Log.e("GStreamer-Message", message);
        if (osdTopText != null) {
            runOnUiThread(() -> osdTopText.setText("ERROR: " + message));
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (native_custom_data != 0) {
            nativePlay();
        }
    }

    @Override
    protected void onPause() {
        if (native_custom_data != 0) {
            nativePause();
        }
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        // Stop the OSD timer to prevent memory leaks
        osdHandler.removeCallbacksAndMessages(null);
        nativeFinalize();
        super.onDestroy();
    }

    static {
        System.loadLibrary("gstreamer_android");
        System.loadLibrary("tutorial-3");
        nativeClassInit();
    }

    // SurfaceHolder.Callback implementation
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        Log.d("GStreamer", "Surface changed to format " + format + " width " + width + " height " + height);
        nativeSurfaceInit(holder.getSurface());
    }

    public void surfaceCreated(SurfaceHolder holder) {
        Log.d("GStreamer", "Surface created: " + holder.getSurface());
    }

    public void surfaceDestroyed(SurfaceHolder holder) {
        Log.d("GStreamer", "Surface destroyed");
        nativeSurfaceFinalize();
    }

    // This is our advanced OSD simulation timer
    private void startOsdSimulation() {
        osdHandler.postDelayed(new Runnable() {
            @Override
            public void run() {
                // 1. Simulate data updates
                mockAltitude += 1;
                mockVoltage -= 0.01f;
                if (mockVoltage < 14.0f) mockVoltage = 16.8f;
                int mockSpeed = (int) (Math.random() * 5 + 70);
                int mockLQ = (int) (Math.random() * 5 + 95);
                float mockBitrate = (float) (Math.random() * 10 + 20);

                // 2. Format all OSD strings
                String topText = String.format("LQ:%d%% | %.1fMbps | FPS:60", mockLQ, mockBitrate);
                String bottomLeftText = String.format("ACRO\nARMED\n%.1f Volt", mockVoltage);
                String bottomRightText = String.format("%d km/h\n%d m", mockSpeed, mockAltitude);

                // 3. Update all TextViews on the screen
                if (osdTopText != null) osdTopText.setText(topText);
                if (osdBottomLeft != null) osdBottomLeft.setText(bottomLeftText);
                if (osdBottomRight != null) osdBottomRight.setText(bottomRightText);

                // 4. Schedule the next update in 200 milliseconds
                osdHandler.postDelayed(this, 200);
            }
        }, 200);
    }
}