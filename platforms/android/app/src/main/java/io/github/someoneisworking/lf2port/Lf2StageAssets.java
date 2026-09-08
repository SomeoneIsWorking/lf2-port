package io.github.someoneisworking.lf2port;

import android.app.Activity;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

/** Publishes the title's bundled stage geometry before SDL/native startup. */
final class Lf2StageAssets {
    private static final int COPY_BUFFER_SIZE = 64 * 1024;
    private final Activity activity;

    Lf2StageAssets(Activity activity) {
        this.activity = activity;
    }

    void extract() throws IOException {
        File destination = new File(activity.getFilesDir(), "stages");
        if (!deleteRecursively(destination) || !destination.mkdir()) {
            throw new IOException("Cannot prepare private stage directory " + destination);
        }
        copyAssetDirectory("stages", destination);
    }

    private void copyAssetDirectory(String assetPath, File destination) throws IOException {
        String[] names = activity.getAssets().list(assetPath);
        if (names == null) {
            throw new IOException("Packaged asset directory is missing: " + assetPath);
        }
        for (String name : names) {
            validateLeafName(name);
            String childPath = assetPath + "/" + name;
            String[] children = activity.getAssets().list(childPath);
            File target = new File(destination, name);
            if (children != null && children.length > 0) {
                if (!target.mkdir()) {
                    throw new IOException("Cannot create private stage directory " + target);
                }
                copyAssetDirectory(childPath, target);
            } else {
                try (InputStream input = activity.getAssets().open(childPath);
                     OutputStream output = new FileOutputStream(target)) {
                    byte[] buffer = new byte[COPY_BUFFER_SIZE];
                    for (int count; (count = input.read(buffer)) >= 0; ) {
                        if (count > 0) output.write(buffer, 0, count);
                    }
                }
            }
        }
    }

    private static void validateLeafName(String name) throws IOException {
        if (name == null || name.isEmpty() || name.equals(".") || name.equals("..")
                || name.indexOf('/') >= 0 || name.indexOf('\\') >= 0) {
            throw new IOException("The selected provider returned an unsafe file name.");
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

}
