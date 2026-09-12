package io.github.someoneisworking.lf2port;

import android.app.AlertDialog;
import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.os.Bundle;
import android.os.Build;

import io.github.someoneisworking.android.AndroidActivity;
import io.github.someoneisworking.android.AndroidDocumentImport;
import io.github.someoneisworking.android.AndroidImportProgress;

import java.io.File;
import java.io.IOException;
import java.util.Locale;

/** LF2 setup wording, native validation handoff, and title window/update policy. */
public final class Lf2Activity extends AndroidActivity {
    private static final String PICKER_STATE = "android-picker";
    private static final int REQUEST_GAME_TREE = 2001;
    private static final int REQUEST_GAME_FILE = 2002;
    private static final int MAX_FILES = 50_000;
    private static final long MAX_BYTES = 2L * 1024L * 1024L * 1024L;

    private boolean selectionPending;
    private UpdateManager updateManager;
    private AndroidDocumentImport importer;
    private AndroidImportProgress importProgress;
    // Main-thread callbacks publish this result before waking the native validation thread.
    private volatile AndroidDocumentImport.Result pendingImport;

    private static native void nativeGameTreeResult(String executable, String error);

    @Override
    protected String getMainFunction() {
        return "main";
    }

    @Override
    protected void onCreate(Bundle state) {
        importer = new AndroidDocumentImport(this,
                new AndroidDocumentImport.Limits(MAX_FILES, MAX_BYTES, 64 * 1024));
        selectionPending = importer.restorePickerState(
                state == null ? null : state.getBundle(PICKER_STATE), importCallback());
        importProgress = new AndroidImportProgress(this, 2003, "lf2_game_import",
                "Game File Installation", "Installing Little Fighter 2", Lf2Activity.class);
        importer.setProgressListener((entries, bytes, totalBytes, name) -> {
            if (importer.active()) {
                importProgress.update(entries + " files, " + bytes + " bytes — " + name,
                        bytes, totalBytes);
            }
        });
        importer.cleanStaleImports();
        try {
            new Lf2StageAssets(this).extract();
        } catch (IOException error) {
            throw new IllegalStateException("Could not prepare packaged LF2 stage geometry", error);
        }
        super.onCreate(state);
        if (Build.VERSION.SDK_INT >= 33
                && checkSelfPermission(android.Manifest.permission.POST_NOTIFICATIONS)
                    != android.content.pm.PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[] {android.Manifest.permission.POST_NOTIFICATIONS}, 2004);
        }
        updateManager = new UpdateManager(this);
        updateManager.check(false);
    }

    @Override
    protected void onSaveInstanceState(Bundle state) {
        super.onSaveInstanceState(state);
        state.putBundle(PICKER_STATE, importer.savePickerState());
    }

    @Override
    protected void onDestroy() {
        if (updateManager != null) updateManager.destroy();
        if (isFinishing()) {
            importer.cancel();
            importProgress.stop();
            finishSelection(null, "The Android activity closed before game-file setup completed.");
        }
        super.onDestroy();
    }

    private void finishSelection(String selectedPath, String error) {
        if (!selectionPending) return;
        selectionPending = false;
        if (selectedPath == null) importProgress.stop();
        nativeGameTreeResult(selectedPath, error);
    }

    public void requestLf2GameTree(String reason) {
        runOnUiThread(() -> {
            if (selectionPending) {
                finishSelection(null, "A game-file setup request is already active.");
                return;
            }
            selectionPending = true;
            try {
                discardPreviousImport();
            } catch (IOException error) {
                finishSelection(null, "Could not discard rejected game files: " + error.getMessage());
                return;
            }
            new AlertDialog.Builder(this)
                    .setTitle(R.string.game_files_title)
                    .setMessage(reason)
                    .setCancelable(false)
                    .setNegativeButton(R.string.quit, (dialog, which) -> finishSelection(null, null))
                    .setPositiveButton(R.string.browse, (dialog, which) ->
                            importer.pickDocument(REQUEST_GAME_FILE, importCallback()))
                    .show();
        });
    }

    private void discardPreviousImport() throws IOException {
        importProgress.stop();
        if (pendingImport != null) {
            importer.discard(pendingImport);
            pendingImport = null;
        }
    }

    private AndroidDocumentImport.Callback importCallback() {
        return new AndroidDocumentImport.Callback() {
            @Override
            public void onImported(AndroidDocumentImport.Result result) {
                acceptImported(result);
            }

            @Override
            public void onCancelled() {
                finishSelection(null, null);
            }

            @Override
            public void onFailed(String message) {
                finishSelection(null, message);
            }
        };
    }

    private void acceptImported(AndroidDocumentImport.Result result) {
        pendingImport = result;
        String name = result.documentName.toLowerCase(Locale.ROOT);
        if (!result.isTree && name.equals("lf2.exe")) {
            try {
                discardPreviousImport();
            } catch (IOException error) {
                finishSelection(null, "Could not discard the executable-only selection: " + error.getMessage());
                return;
            }
            new AlertDialog.Builder(this)
                    .setTitle(R.string.game_files_title)
                    .setMessage(R.string.exe_needs_folder)
                    .setCancelable(false)
                    .setNegativeButton(R.string.quit, (dialog, which) -> finishSelection(null, null))
                    .setPositiveButton(R.string.browse_folder, (dialog, which) ->
                            importer.pickTree(REQUEST_GAME_TREE, importCallback()))
                    .show();
            return;
        }
        if (!result.isTree && !name.endsWith(".zip") && !name.endsWith(".exe")) {
            finishSelection(null, "Choose the original LF2 v2.0a installer, lf2.exe, or a ZIP containing "
                    + "one complete LF2 tree.");
            return;
        }
        File selected = result.isTree ? result.stagingDirectory
                : new File(result.stagingDirectory, result.documentName);
        // The existing native resolver authenticates the executable and complete install;
        // only its subsequent commit callback may publish these private staged bytes.
        finishSelection(selected.getAbsolutePath(), null);
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        if (updateManager != null && updateManager.handleActivityResult(request)) return;
        if (importer.handleActivityResult(request, result, data)) {
            if (importer.active()) importProgress.start(getString(R.string.importing_game_files));
            return;
        }
        super.onActivityResult(request, result, data);
    }

    /** Called by the native validator after it authenticates a complete extracted/tree install. */
    public String commitLf2GameTree(String selectedPath) throws IOException {
        if (pendingImport == null) throw new IOException("No private import is waiting for validation.");
        File installed = importer.promoteValidated(pendingImport, new File(selectedPath), "game");
        runOnUiThread(() -> importProgress.stop());
        pendingImport = null;
        return installed.getAbsolutePath();
    }

    public void requestLf2Update() {
        runOnUiThread(() -> {
            if (updateManager == null) updateManager = new UpdateManager(this);
            updateManager.check(true);
        });
    }

    /**
     * SDL updates the activity orientation after it creates a resizable window. Reassert the
     * game contract at that boundary: either landscape rotation is valid, and both system bars
     * remain transient rather than reserving part of LF2's fixed game viewport.
     */
    public void enforceLf2WindowPolicy() {
        runOnUiThread(() -> {
            setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE);
            hideSystemUI();
        });
    }

}
