package org.openclaw.claw;

import android.app.Activity;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.OpenableColumns;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

// Launcher: the game needs CLAW.REZ from the player's own copy of Captain Claw
// (it is not in the APK). When it is not in the app's folder yet, this screen
// lets the player pick CLAW.REZ (and CLAW.FEC with the movies) with the
// system file picker and copies them into Android/data/org.openclaw.claw/files/,
// then starts the game. Copying by hand (USB) into that folder works too.
public class SetupActivity extends Activity {
    private static final int REQ_PICK = 1;
    private TextView mStatus;
    private Button mPick;

    private boolean polish() {
        return Locale.getDefault().getLanguage().equals("pl");
    }

    private String tr(String pl, String en) {
        return polish() ? pl : en;
    }

    private File dataDir() {
        File dir = getExternalFilesDir(null);
        return dir != null ? dir : getFilesDir();
    }

    private boolean hasGameData() {
        File[] files = dataDir().listFiles();
        if (files == null) return false;
        for (File f : files) {
            String n = f.getName().toLowerCase(Locale.ROOT);
            if (n.equals("claw.rez") && f.length() > 0) return true;
            if (n.equals("assets") && f.isDirectory()) return true;
        }
        return false;
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (hasGameData()) {
            startGame();
            return;
        }
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setGravity(Gravity.CENTER);
        layout.setPadding(48, 48, 48, 48);
        layout.setBackgroundColor(0xFF1C0E04);

        TextView title = new TextView(this);
        title.setText("Captain Claw");
        title.setTextSize(32);
        title.setTextColor(0xFFF6CD56);
        title.setGravity(Gravity.CENTER);
        layout.addView(title);

        mStatus = new TextView(this);
        mStatus.setTextSize(16);
        mStatus.setTextColor(0xFFFAECC8);
        mStatus.setGravity(Gravity.CENTER);
        mStatus.setPadding(0, 32, 0, 32);
        mStatus.setText(tr(
            "Gra potrzebuje pliku CLAW.REZ z Twojej kopii Captain Claw (1997).\n"
                + "Wskaż CLAW.REZ (i opcjonalnie CLAW.FEC z filmami).",
            "The game needs CLAW.REZ from your own copy of Captain Claw (1997).\n"
                + "Pick CLAW.REZ (and optionally CLAW.FEC with the movies)."));
        layout.addView(mStatus);

        mPick = new Button(this);
        mPick.setText(tr("Wskaż pliki gry", "Choose game files"));
        mPick.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { pick(); }
        });
        layout.addView(mPick);

        TextView hint = new TextView(this);
        hint.setTextSize(12);
        hint.setTextColor(0xFFBEAA82);
        hint.setGravity(Gravity.CENTER);
        hint.setPadding(0, 32, 0, 0);
        hint.setText(tr("Można też skopiować je przez USB do:\n", "Or copy them over USB to:\n")
            + dataDir().getAbsolutePath());
        layout.addView(hint);
        setContentView(layout);
    }

    private void pick() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        i.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
        startActivityForResult(i, REQ_PICK);
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (req != REQ_PICK || res != RESULT_OK || data == null) return;
        final List<Uri> uris = new ArrayList<>();
        if (data.getClipData() != null) {
            for (int i = 0; i < data.getClipData().getItemCount(); ++i) uris.add(data.getClipData().getItemAt(i).getUri());
        } else if (data.getData() != null) {
            uris.add(data.getData());
        }
        mPick.setEnabled(false);
        mStatus.setText(tr("Kopiowanie…", "Copying…"));
        final Handler ui = new Handler(Looper.getMainLooper());
        new Thread(new Runnable() {
            @Override public void run() {
                final String error = copyAll(uris);
                ui.post(new Runnable() {
                    @Override public void run() {
                        mPick.setEnabled(true);
                        if (hasGameData()) {
                            startGame();
                        } else {
                            mStatus.setText(error != null ? error
                                : tr("Nie wybrano pliku CLAW.REZ.", "CLAW.REZ was not selected."));
                        }
                    }
                });
            }
        }).start();
    }

    private String displayName(Uri uri) {
        try (Cursor c = getContentResolver().query(uri, new String[] { OpenableColumns.DISPLAY_NAME }, null, null, null)) {
            if (c != null && c.moveToFirst()) return c.getString(0);
        } catch (Exception ignored) { }
        return uri.getLastPathSegment();
    }

    // Copies the picked CLAW.REZ / CLAW.FEC; returns an error text or null.
    private String copyAll(List<Uri> uris) {
        for (Uri uri : uris) {
            String name = displayName(uri);
            if (name == null) continue;
            String upper = name.toUpperCase(Locale.ROOT);
            if (!upper.equals("CLAW.REZ") && !upper.equals("CLAW.FEC")) continue;
            File out = new File(dataDir(), upper);
            File tmp = new File(dataDir(), upper + ".part");
            try (InputStream in = getContentResolver().openInputStream(uri);
                 OutputStream os = new FileOutputStream(tmp)) {
                if (in == null) return tr("Nie można otworzyć ", "Cannot open ") + name;
                byte[] buf = new byte[1 << 16];
                int n;
                while ((n = in.read(buf)) > 0) os.write(buf, 0, n);
            } catch (Exception e) {
                tmp.delete();
                return tr("Błąd kopiowania: ", "Copy failed: ") + e.getMessage();
            }
            out.delete();
            if (!tmp.renameTo(out)) return tr("Błąd zapisu ", "Cannot save ") + upper;
        }
        return null;
    }

    private void startGame() {
        startActivity(new Intent(this, ClawActivity.class));
        finish();
    }
}
