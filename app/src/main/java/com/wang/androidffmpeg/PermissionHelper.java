package com.wang.androidffmpeg;

import android.Manifest;
import android.app.Activity;
import android.content.Context;
import android.content.pm.PackageManager;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;

public final class PermissionHelper {

    private static final String PERMISSION
            = Manifest.permission.WRITE_EXTERNAL_STORAGE;
    private static final int CODE = 100;

    private final Activity mActivity;

    private Runnable mGrantCallback;

    public PermissionHelper(@NonNull Activity activity) {
        mActivity = activity;
    }

    public boolean hasAllPermissions() {
        return mActivity.checkSelfPermission(PERMISSION)
                == PackageManager.PERMISSION_GRANTED;
    }

    public void requestPermission() {
        mActivity.requestPermissions(new String[] {PERMISSION}, CODE);
    }

    public void checkPermission(@Nullable Runnable callback) {
        if (hasAllPermissions()) {
            if (callback != null) {
                callback.run();
            }
            return;
        }
        mGrantCallback = callback;
        requestPermission();
    }

    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        if (requestCode == CODE) {
            if (grantResults[0] == PackageManager.PERMISSION_GRANTED) {
                if (mGrantCallback != null) {
                    mGrantCallback.run();
                }
            }
        }
    }
}
