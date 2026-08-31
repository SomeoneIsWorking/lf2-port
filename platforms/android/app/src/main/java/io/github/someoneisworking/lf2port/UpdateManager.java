package io.github.someoneisworking.lf2port;

import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.pm.Signature;
import android.net.Uri;
import android.os.Build;
import android.provider.Settings;
import android.util.Log;
import android.widget.Toast;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.File;
import java.io.ByteArrayOutputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.security.MessageDigest;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

final class UpdateManager {
    private static final String TAG = "Lf2Update";
    private static final String RELEASES_API =
            "https://api.github.com/repos/SomeoneIsWorking/lf2-port/releases?per_page=20";
    private static final Pattern VERSION = Pattern.compile("^v?(\\d+)\\.(\\d+)\\.(\\d+)$");
    private static final long MAX_APK_BYTES = 512L * 1024L * 1024L;
    private static final int REQUEST_INSTALL_PERMISSION = 2003;
    private static final int BUFFER_SIZE = 64 * 1024;

    private final Lf2Activity activity;
    private volatile boolean destroyed;
    private File pendingInstall;

    UpdateManager(Lf2Activity activity) {
        this.activity = activity;
    }

    void destroy() {
        destroyed = true;
    }

    void check(boolean manual) {
        Thread worker = new Thread(() -> checkInBackground(manual), "lf2-update-check");
        worker.start();
    }

    private void checkInBackground(boolean manual) {
        try {
            Release release = latestRelease();
            int comparison = compareVersions(release.version, installedVersion());
            if (comparison <= 0) {
                if (manual) showMessage("LF2 Port update", "This is the newest published version.");
                return;
            }
            activity.runOnUiThread(() -> {
                if (destroyed || activity.isFinishing()) return;
                new AlertDialog.Builder(activity)
                        .setTitle("LF2 Port update available")
                        .setMessage("Version " + release.version + " is available from the LF2 Port GitHub release.")
                        .setNegativeButton("Later", null)
                        .setPositiveButton("Download", (dialog, which) -> download(release))
                        .show();
            });
        } catch (Exception exception) {
            Log.w(TAG, "Could not check GitHub Releases", exception);
            if (manual) showError("Could not check GitHub Releases: " + exception.getMessage());
        }
    }

    private Release latestRelease() throws Exception {
        HttpURLConnection connection = open(RELEASES_API);
        try (InputStream input = checkedInput(connection)) {
            String json = new String(readSmallResponse(input), java.nio.charset.StandardCharsets.UTF_8);
            JSONArray releases = new JSONArray(json);
            Release newest = null;
            for (int index = 0; index < releases.length(); ++index) {
                JSONObject release = releases.getJSONObject(index);
                if (release.optBoolean("draft", true)) continue;
                String tag = release.getString("tag_name");
                String version = normalizedVersion(tag);
                String expected = "LF2-Port-" + version + "-android-arm64-release.apk";
                JSONArray assets = release.getJSONArray("assets");
                for (int assetIndex = 0; assetIndex < assets.length(); ++assetIndex) {
                    JSONObject asset = assets.getJSONObject(assetIndex);
                    if (expected.equals(asset.getString("name"))) {
                        Release candidate = new Release(version, asset.getString("browser_download_url"));
                        if (newest == null || compareVersions(candidate.version, newest.version) > 0) {
                            newest = candidate;
                        }
                    }
                }
            }
            if (newest != null) return newest;
        } finally {
            connection.disconnect();
        }
        throw new IOException("no signed Android release APK was found in the newest 20 releases");
    }

    private String installedVersion() throws Exception {
        PackageInfo info = activity.getPackageManager().getPackageInfo(activity.getPackageName(), 0);
        return normalizedVersion(info.versionName == null ? "" : info.versionName);
    }

    private static String normalizedVersion(String value) throws IOException {
        Matcher matcher = VERSION.matcher(value);
        if (!matcher.matches()) throw new IOException("unsupported release version " + value);
        return matcher.group(1) + "." + matcher.group(2) + "." + matcher.group(3);
    }

    private static int compareVersions(String left, String right) throws IOException {
        Matcher a = VERSION.matcher(left);
        Matcher b = VERSION.matcher(right);
        if (!a.matches() || !b.matches()) throw new IOException("release version is not semantic");
        for (int part = 1; part <= 3; ++part) {
            int comparison = Integer.compare(Integer.parseInt(a.group(part)), Integer.parseInt(b.group(part)));
            if (comparison != 0) return comparison;
        }
        return 0;
    }

    private void download(Release release) {
        Toast.makeText(activity, "Downloading LF2 Port " + release.version + "…", Toast.LENGTH_LONG).show();
        Thread worker = new Thread(() -> downloadInBackground(release), "lf2-update-download");
        worker.start();
    }

    private void downloadInBackground(Release release) {
        File output = UpdateFileProvider.updateFile(activity);
        File partial = new File(output.getParentFile(), output.getName() + ".download");
        try {
            if (!output.getParentFile().isDirectory() && !output.getParentFile().mkdirs()) {
                throw new IOException("cannot create private update cache");
            }
            if (partial.exists() && !partial.delete()) throw new IOException("cannot replace stale update download");
            HttpURLConnection connection = open(release.url);
            try (InputStream input = checkedInput(connection);
                 FileOutputStream file = new FileOutputStream(partial)) {
                long declared = connection.getContentLengthLong();
                if (declared > MAX_APK_BYTES) throw new IOException("release APK exceeds the 512 MiB limit");
                byte[] buffer = new byte[BUFFER_SIZE];
                long total = 0;
                for (int count; (count = input.read(buffer)) >= 0; ) {
                    if (count == 0) continue;
                    total += count;
                    if (total > MAX_APK_BYTES) throw new IOException("release APK exceeds the 512 MiB limit");
                    file.write(buffer, 0, count);
                }
            } finally {
                connection.disconnect();
            }
            validatePackage(partial);
            if (output.exists() && !output.delete()) throw new IOException("cannot replace cached update");
            if (!partial.renameTo(output)) throw new IOException("cannot accept completed update download");
            activity.runOnUiThread(() -> requestInstall(output));
        } catch (Exception exception) {
            if (partial.exists() && !partial.delete()) Log.w(TAG, "Could not remove partial update " + partial);
            Log.e(TAG, "Could not download update", exception);
            showError("Could not prepare the update: " + exception.getMessage());
        }
    }

    private static HttpURLConnection open(String location) throws IOException {
        HttpURLConnection connection = (HttpURLConnection) new URL(location).openConnection();
        connection.setConnectTimeout(15_000);
        connection.setReadTimeout(30_000);
        connection.setInstanceFollowRedirects(true);
        connection.setRequestProperty("Accept", "application/vnd.github+json");
        connection.setRequestProperty("User-Agent", "LF2-Port-Android-Updater");
        connection.setRequestProperty("X-GitHub-Api-Version", "2022-11-28");
        return connection;
    }

    private static InputStream checkedInput(HttpURLConnection connection) throws IOException {
        int status = connection.getResponseCode();
        if (status < 200 || status >= 300) throw new IOException("GitHub returned HTTP " + status);
        return connection.getInputStream();
    }

    private static byte[] readSmallResponse(InputStream input) throws IOException {
        final int limit = 4 * 1024 * 1024;
        ByteArrayOutputStream output = new ByteArrayOutputStream();
        byte[] buffer = new byte[16 * 1024];
        int total = 0;
        for (int count; (count = input.read(buffer)) >= 0; ) {
            if (count == 0) continue;
            total += count;
            if (total > limit) throw new IOException("GitHub release response exceeds 4 MiB");
            output.write(buffer, 0, count);
        }
        return output.toByteArray();
    }

    private void validatePackage(File apk) throws Exception {
        PackageManager manager = activity.getPackageManager();
        int flags = Build.VERSION.SDK_INT >= 28
                ? PackageManager.GET_SIGNING_CERTIFICATES : PackageManager.GET_SIGNATURES;
        PackageInfo installed = manager.getPackageInfo(activity.getPackageName(), flags);
        PackageInfo update = manager.getPackageArchiveInfo(apk.getAbsolutePath(), flags);
        if (update == null || !activity.getPackageName().equals(update.packageName)) {
            throw new IOException("download is not an LF2 Port APK");
        }
        if (versionCode(update) <= versionCode(installed)) throw new IOException("download is not a newer APK");
        if (!sameSigner(installed, update)) {
            throw new IOException("APK signing certificate does not match the installed app");
        }
    }

    private static long versionCode(PackageInfo info) {
        return Build.VERSION.SDK_INT >= 28 ? info.getLongVersionCode() : info.versionCode;
    }

    private static Signature[] signatures(PackageInfo info) {
        if (Build.VERSION.SDK_INT >= 28) {
            return info.signingInfo == null ? new Signature[0] : info.signingInfo.getApkContentsSigners();
        }
        return info.signatures == null ? new Signature[0] : info.signatures;
    }

    private static boolean sameSigner(PackageInfo installed, PackageInfo update) throws Exception {
        Signature[] current = signatures(installed);
        Signature[] candidate = signatures(update);
        if (current.length == 0 || current.length != candidate.length) return false;
        boolean[] matched = new boolean[candidate.length];
        for (Signature left : current) {
            boolean found = false;
            for (int index = 0; index < candidate.length; ++index) {
                if (!matched[index]
                        && MessageDigest.isEqual(left.toByteArray(), candidate[index].toByteArray())) {
                    matched[index] = true;
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        }
        return true;
    }

    private void requestInstall(File apk) {
        if (destroyed || activity.isFinishing()) return;
        if (Build.VERSION.SDK_INT >= 26 && !activity.getPackageManager().canRequestPackageInstalls()) {
            pendingInstall = apk;
            Intent settings = new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                    Uri.parse("package:" + activity.getPackageName()));
            activity.startActivityForResult(settings, REQUEST_INSTALL_PERMISSION);
            return;
        }
        launchInstaller(apk);
    }

    boolean handleActivityResult(int requestCode) {
        if (requestCode != REQUEST_INSTALL_PERMISSION) return false;
        File update = pendingInstall;
        pendingInstall = null;
        boolean permitted = Build.VERSION.SDK_INT < 26
                || activity.getPackageManager().canRequestPackageInstalls();
        if (update != null && update.isFile() && permitted) {
            launchInstaller(update);
        } else {
            showError("Android did not allow LF2 Port to request the update installation.");
        }
        return true;
    }

    private void launchInstaller(File apk) {
        Intent install = new Intent(Intent.ACTION_INSTALL_PACKAGE);
        install.setData(UpdateFileProvider.uriFor(activity));
        install.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        try {
            activity.startActivity(install);
        } catch (ActivityNotFoundException exception) {
            showError("No Android package installer is available.");
        }
    }

    private void showMessage(String title, String message) {
        activity.runOnUiThread(() -> {
            if (!destroyed && !activity.isFinishing()) {
                new AlertDialog.Builder(activity).setTitle(title).setMessage(message).setPositiveButton("OK", null).show();
            }
        });
    }

    private void showError(String message) {
        showMessage("LF2 Port update", message);
    }

    private static final class Release {
        final String version;
        final String url;

        Release(String version, String url) {
            this.version = version;
            this.url = url;
        }
    }
}
