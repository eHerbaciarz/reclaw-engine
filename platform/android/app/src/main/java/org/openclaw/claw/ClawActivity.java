package org.openclaw.claw;

import android.content.Context;
import android.net.wifi.WifiManager;
import android.os.Bundle;

import org.libsdl.app.SDLActivity;

// SDL's activity running the game (libmain.so).
public class ClawActivity extends SDLActivity {
    private WifiManager.MulticastLock mMulticastLock;

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "SDL2_image", "SDL2_mixer", "SDL2_ttf", "main" };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // The network race finds other players with UDP broadcasts, which
        // many phones drop unless a multicast lock is held.
        try {
            WifiManager wifi = (WifiManager) getApplicationContext().getSystemService(Context.WIFI_SERVICE);
            if (wifi != null) {
                mMulticastLock = wifi.createMulticastLock("claw-race");
                mMulticastLock.setReferenceCounted(false);
                mMulticastLock.acquire();
            }
        } catch (Exception e) {
            mMulticastLock = null;
        }
    }

    @Override
    protected void onDestroy() {
        if (mMulticastLock != null && mMulticastLock.isHeld()) {
            mMulticastLock.release();
        }
        super.onDestroy();
    }
}
