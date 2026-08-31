---
id: 123
title: Add GitHub Release updater to AppImage and APK
status: resolved
symptom: Packaged AppImage and APK builds do not check GitHub Releases or offer an in-app update path.
tags: reported,release,appimage,android,updater
created: 2026-08-31
updated: 2026-08-31
---

REPORTED 2026-08-31. One release/version policy must serve both packages. AppImage may replace only a writable outer image and must preserve the current executable on any failure. Android must use the platform package-installer flow: download an APK signed by the same long-lived identity and request user-confirmed installation; silent install is not available to an ordinary app. Never bundle game files, never use GitHub Actions, and never commit generated packages.

### Note (2026-08-31)
IMPLEMENTED 2026-08-31: AppImages embed gh-releases-zsync metadata, bundle pinned AppImageUpdate, and emit a nonempty .zsync sidecar; the extracted package and embedded metadata were inspected. Android checks GitHub Releases automatically and manually from Settings, downloads only the version-named release APK, and refuses a wrong package, non-newer versionCode, or mismatched signing certificate before Android's user-confirmed installer. A signed release APK still requires the maintainer keystore and device acceptance.

### Resolution (2026-08-31)
GitHub update channels are active for AppImage and sideloaded Android builds. On codex_shared_api35, signed 0.1.0 automatically discovered public v0.1.5, downloaded the version-named APK, requested Android unknown-source permission, presented the system Update confirmation, and installed same-certificate v0.1.5 (versionCode 1005). GitHub remains the updater until a Play Store build uses Play-managed updates.
