package io.github.someoneisworking.lf2port;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;

import java.io.File;
import java.io.FileNotFoundException;

public final class UpdateFileProvider extends ContentProvider {
    static final String FILE_NAME = "LF2-Port-update.apk";

    static Uri uriFor(Lf2Activity activity) {
        return new Uri.Builder()
                .scheme("content")
                .authority(activity.getPackageName() + ".updates")
                .appendPath(FILE_NAME)
                .build();
    }

    static File updateFile(Lf2Activity activity) {
        return new File(new File(activity.getCacheDir(), "updates"), FILE_NAME);
    }

    private File requestedFile(Uri uri) {
        if (getContext() == null || uri.getPathSegments().size() != 1
                || !FILE_NAME.equals(uri.getLastPathSegment())) {
            throw new IllegalArgumentException("Unknown LF2 update path");
        }
        return new File(new File(getContext().getCacheDir(), "updates"), FILE_NAME);
    }

    @Override
    public boolean onCreate() {
        return true;
    }

    @Override
    public String getType(Uri uri) {
        return "application/vnd.android.package-archive";
    }

    @Override
    public Cursor query(Uri uri, String[] projection, String selection, String[] selectionArgs,
                        String sortOrder) {
        File file = requestedFile(uri);
        MatrixCursor cursor = new MatrixCursor(new String[] {OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE});
        cursor.addRow(new Object[] {FILE_NAME, file.length()});
        return cursor;
    }

    @Override
    public ParcelFileDescriptor openFile(Uri uri, String mode) throws FileNotFoundException {
        if (!"r".equals(mode)) throw new FileNotFoundException("LF2 updates are read-only");
        return ParcelFileDescriptor.open(requestedFile(uri), ParcelFileDescriptor.MODE_READ_ONLY);
    }

    @Override
    public Uri insert(Uri uri, ContentValues values) {
        throw new UnsupportedOperationException("LF2 updates are read-only");
    }

    @Override
    public int delete(Uri uri, String selection, String[] selectionArgs) {
        throw new UnsupportedOperationException("LF2 updates are read-only");
    }

    @Override
    public int update(Uri uri, ContentValues values, String selection, String[] selectionArgs) {
        throw new UnsupportedOperationException("LF2 updates are read-only");
    }
}
