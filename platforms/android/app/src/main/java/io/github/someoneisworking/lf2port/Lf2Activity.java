package io.github.someoneisworking.lf2port;

import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.ContentResolver;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.util.Log;
import android.widget.Toast;

import io.github.someoneisworking.lucent.LucentActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.HashSet;
import java.util.Locale;
import java.util.Set;

public final class Lf2Activity extends LucentActivity {
    private static final String TAG = "Lf2Activity";
    private static final int REQUEST_GAME_TREE = 2001;
    private static final int REQUEST_GAME_FILE = 2002;
    private static final int COPY_BUFFER_SIZE = 64 * 1024;
    private static final int MAX_FILES = 50_000;
    private static final long MAX_BYTES = 2L * 1024L * 1024L * 1024L;
    private static final String IMPORT_PREFIX = "game-import-";

    private boolean pickerActive;
    private boolean selectionPending;

    private static native void nativeGameTreeResult(String executable, String error);

    @Override
    protected String getMainFunction() {
        return "main";
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        cleanStaleImports();
        try {
            extractPortStages();
        } catch (IOException exception) {
            Log.e(TAG, "Could not extract packaged stage geometry", exception);
        }
        super.onCreate(savedInstanceState);
    }

    @Override
    protected void onDestroy() {
        finishSelection(null, "The Android activity closed before game-file setup completed.");
        super.onDestroy();
    }

    private synchronized void finishSelection(String selectedPath, String error) {
        if (!selectionPending) {
            return;
        }
        selectionPending = false;
        pickerActive = false;
        nativeGameTreeResult(selectedPath, error);
    }

    private void extractPortStages() throws IOException {
        File destination = new File(getFilesDir(), "stages");
        if (!deleteRecursively(destination) || !destination.mkdir()) {
            throw new IOException("Cannot prepare private stage directory " + destination);
        }
        copyAssetDirectory("stages", destination);
    }

    private void copyAssetDirectory(String assetPath, File destination) throws IOException {
        String[] names = getAssets().list(assetPath);
        if (names == null) {
            throw new IOException("Packaged asset directory is missing: " + assetPath);
        }
        for (String name : names) {
            validateLeafName(name);
            String childPath = assetPath + "/" + name;
            String[] children = getAssets().list(childPath);
            File target = new File(destination, name);
            if (children != null && children.length > 0) {
                if (!target.mkdir()) {
                    throw new IOException("Cannot create private stage directory " + target);
                }
                copyAssetDirectory(childPath, target);
            } else {
                try (InputStream input = getAssets().open(childPath);
                     OutputStream output = new FileOutputStream(target)) {
                    byte[] buffer = new byte[COPY_BUFFER_SIZE];
                    for (int count; (count = input.read(buffer)) >= 0; ) {
                        if (count > 0) output.write(buffer, 0, count);
                    }
                }
            }
        }
    }

    public void requestLf2GameTree(String reason) {
        runOnUiThread(() -> {
            if (selectionPending) {
                finishSelection(null, "A game-file setup request is already active.");
                return;
            }
            selectionPending = true;
            cleanStaleImports();
            new AlertDialog.Builder(this)
                    .setTitle(R.string.game_files_title)
                    .setMessage(reason)
                    .setCancelable(false)
                    .setNegativeButton(R.string.quit, (dialog, which) -> finishSelection(null, null))
                    .setPositiveButton(R.string.browse, (dialog, which) -> openGameFilePicker())
                    .show();
        });
    }

    private void openGameFilePicker() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        pickerActive = true;
        try {
            startActivityForResult(intent, REQUEST_GAME_FILE);
        } catch (ActivityNotFoundException exception) {
            finishSelection(null, "No Android document picker is available.");
        }
    }

    private void openGameTreePicker() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PREFIX_URI_PERMISSION);
        pickerActive = true;
        try {
            startActivityForResult(intent, REQUEST_GAME_TREE);
        } catch (ActivityNotFoundException exception) {
            finishSelection(null, "No Android folder picker is available.");
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQUEST_GAME_TREE && requestCode != REQUEST_GAME_FILE) {
            return;
        }
        pickerActive = false;
        Uri tree = resultCode == RESULT_OK && data != null ? data.getData() : null;
        if (tree == null) {
            finishSelection(null, null);
            return;
        }

        if (requestCode == REQUEST_GAME_FILE) {
            handleSelectedFile(tree);
            return;
        }

        Toast.makeText(this, R.string.importing_game_files, Toast.LENGTH_LONG).show();
        Thread worker = new Thread(() -> importGameTree(tree), "lf2-game-import");
        worker.start();
    }

    private void handleSelectedFile(Uri source) {
        String displayName = displayName(source);
        String lowerName = displayName == null ? "" : displayName.toLowerCase(Locale.ROOT);
        if (lowerName.equals("lf2.exe")) {
            runOnUiThread(() -> new AlertDialog.Builder(this)
                    .setTitle(R.string.game_files_title)
                    .setMessage(R.string.exe_needs_folder)
                    .setCancelable(false)
                    .setNegativeButton(R.string.quit,
                            (dialog, which) -> finishSelection(null, null))
                    .setPositiveButton(R.string.browse_folder,
                            (dialog, which) -> openGameTreePicker())
                    .show());
            return;
        }
        String privateName;
        String description;
        if (lowerName.endsWith(".zip")) {
            privateName = "game.zip";
            description = "ZIP";
        } else if (lowerName.endsWith(".exe")) {
            privateName = "installer.exe";
            description = "LF2 installer";
        } else {
            finishSelection(null, "Choose the original LF2 v2.0a installer, lf2.exe, or a ZIP containing "
                    + "one complete LF2 tree.");
            return;
        }
        Toast.makeText(this, R.string.importing_game_files, Toast.LENGTH_LONG).show();
        Thread worker = new Thread(
                () -> importGameFile(source, privateName, description), "lf2-file-import");
        worker.start();
    }

    private String displayName(Uri source) {
        try (Cursor cursor = getContentResolver().query(
                source, new String[] {OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                return cursor.getString(0);
            }
        }
        return source.getLastPathSegment();
    }

    private void importGameFile(Uri source, String privateName, String description) {
        File staging = new File(getFilesDir(), IMPORT_PREFIX + System.nanoTime());
        try {
            if (!staging.mkdir()) {
                throw new IOException("Cannot create private import directory " + staging);
            }
            File selectedFile = new File(staging, privateName);
            ImportBudget budget = new ImportBudget();
            budget.addFile(-1);
            copyFile(source, selectedFile, budget);
            finishSelection(selectedFile.getAbsolutePath(), null);
        } catch (IOException | SecurityException | IllegalArgumentException exception) {
            deleteRecursively(staging);
            finishSelection(null, "Could not copy that " + description + " into private app storage: "
                    + exception.getMessage());
        }
    }

    private void importGameTree(Uri tree) {
        File staging = new File(getFilesDir(), IMPORT_PREFIX + System.nanoTime());
        try {
            if (!staging.mkdir()) {
                throw new IOException("Cannot create private import directory " + staging);
            }
            ImportBudget budget = new ImportBudget();
            String rootDocument = DocumentsContract.getTreeDocumentId(tree);
            copyChildren(tree, rootDocument, staging, budget);
            finishSelection(staging.getAbsolutePath(), null);
        } catch (IOException | SecurityException | IllegalArgumentException exception) {
            deleteRecursively(staging);
            finishSelection(null, "Could not copy that folder into private app storage: "
                    + exception.getMessage());
        }
    }

    private void copyChildren(Uri tree, String parentDocument, File destination, ImportBudget budget)
            throws IOException {
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, parentDocument);
        String[] columns = {
                DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                DocumentsContract.Document.COLUMN_MIME_TYPE,
                DocumentsContract.Document.COLUMN_SIZE,
        };
        Set<String> names = new HashSet<>();
        try (Cursor cursor = getContentResolver().query(children, columns, null, null, null)) {
            if (cursor == null) {
                throw new IOException("The selected provider did not return the folder contents.");
            }
            while (cursor.moveToNext()) {
                String documentId = cursor.getString(0);
                String name = cursor.getString(1);
                String mimeType = cursor.getString(2);
                long declaredSize = cursor.isNull(3) ? -1 : cursor.getLong(3);
                validateLeafName(name);
                if (!names.add(name)) {
                    throw new IOException("The selected folder contains duplicate name: " + name);
                }
                budget.addFile(declaredSize);
                File target = new File(destination, name);
                Uri document = DocumentsContract.buildDocumentUriUsingTree(tree, documentId);
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mimeType)) {
                    if (!target.mkdir()) {
                        throw new IOException("Cannot create private directory " + target);
                    }
                    copyChildren(tree, documentId, target, budget);
                } else {
                    copyFile(document, target, budget);
                }
            }
        }
    }

    private void copyFile(Uri source, File target, ImportBudget budget) throws IOException {
        ContentResolver resolver = getContentResolver();
        try (InputStream input = resolver.openInputStream(source);
             OutputStream output = new FileOutputStream(target)) {
            if (input == null) {
                throw new IOException("The selected provider could not open " + source);
            }
            byte[] buffer = new byte[COPY_BUFFER_SIZE];
            for (int count; (count = input.read(buffer)) >= 0; ) {
                if (count == 0) {
                    continue;
                }
                budget.addBytes(count);
                output.write(buffer, 0, count);
            }
        }
    }

    private static void validateLeafName(String name) throws IOException {
        if (name == null || name.isEmpty() || name.equals(".") || name.equals("..")
                || name.indexOf('/') >= 0 || name.indexOf('\\') >= 0) {
            throw new IOException("The selected provider returned an unsafe file name.");
        }
    }

    public String commitLf2GameTree(String stagingPath) throws IOException {
        File privateRoot = getFilesDir().getCanonicalFile();
        File selectedRoot = new File(stagingPath).getCanonicalFile();
        File staging = selectedRoot;
        while (staging.getParentFile() != null && !privateRoot.equals(staging.getParentFile())) {
            staging = staging.getParentFile();
        }
        if (!privateRoot.equals(staging.getParentFile()) || !staging.getName().startsWith(IMPORT_PREFIX)
                || !selectedRoot.isDirectory()) {
            throw new IOException("Refusing an import outside LF2 private staging storage.");
        }

        File game = new File(privateRoot, "game");
        File previous = new File(privateRoot, "game-previous");
        deleteRecursively(previous);
        boolean movedPrevious = false;
        if (game.exists()) {
            movedPrevious = game.renameTo(previous);
            if (!movedPrevious) {
                throw new IOException("Could not preserve the current private game tree.");
            }
        }
        if (!selectedRoot.renameTo(game)) {
            if (movedPrevious && !previous.renameTo(game)) {
                throw new IOException("The new import failed and the previous tree could not be restored.");
            }
            throw new IOException("Could not accept the validated game tree.");
        }
        if (!staging.equals(selectedRoot) && !deleteRecursively(staging)) {
            Log.w(TAG, "Validated nested import succeeded, but its staging wrapper remains at " + staging);
        }
        if (movedPrevious && !deleteRecursively(previous)) {
            Log.w(TAG, "Validated import succeeded, but stale backup remains at " + previous);
        }
        cleanStaleImports();
        return game.getAbsolutePath();
    }

    private void cleanStaleImports() {
        File[] files = getFilesDir().listFiles();
        if (files == null) {
            return;
        }
        for (File file : files) {
            if (file.getName().startsWith(IMPORT_PREFIX) || file.getName().equals("game-previous")) {
                if (!deleteRecursively(file)) {
                    Log.w(TAG, "Could not remove stale private import " + file);
                }
            }
        }
    }

    private static boolean deleteRecursively(File file) {
        if (!file.exists()) {
            return true;
        }
        File[] children = file.listFiles();
        if (children != null) {
            for (File child : children) {
                if (!deleteRecursively(child)) {
                    return false;
                }
            }
        }
        return file.delete();
    }

    private static final class ImportBudget {
        int files;
        long bytes;

        void addFile(long declaredSize) throws IOException {
            files++;
            if (files > MAX_FILES) {
                throw new IOException("The selected tree contains more than " + MAX_FILES + " entries.");
            }
            if (declaredSize > 0 && declaredSize > MAX_BYTES - bytes) {
                throw new IOException("The selected tree is larger than the 2 GiB import limit.");
            }
        }

        void addBytes(int count) throws IOException {
            bytes += count;
            if (bytes > MAX_BYTES) {
                throw new IOException("The selected tree is larger than the 2 GiB import limit.");
            }
        }
    }
}
