/*
 * Copyright 2026, Kris Beazley (ablyss) RealTimeGUI@epluribusunix.net
 * All rights reserved. Distributed under the terms of the MIT license.
 */


#include <Application.h>
#include <Window.h>
#include <View.h>
#include <Button.h>
#include <StringView.h>
#include <TextView.h>
#include <TextControl.h>
#include <String.h>
#include <LayoutBuilder.h>
#include <Alert.h>
#include <Path.h>
#include <FindDirectory.h>
#include <MediaRoster.h>
#include <MediaDefs.h>
#include <MediaNode.h>
#include <Notification.h>

#include <sys/stat.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <thread>
#include <vector>


namespace AppInfo {
    static const char* const APP_NAME = "RealTimeGUI";
    static const char* const VERSION_STRING = "v1.0.2";
}

const char* kAppSignature = "application/x-vnd.realtimegui";

enum {
    MSG_APPLY   = 'aply',
    MSG_REFRESH = 'rfrh',
};


// =============================================================================
// Update Checker (same pattern as GLToogle's own -- a lightweight curl-based
// version check against this repo's own VERSION file, with a toast
// notification if a newer release exists).
// =============================================================================
static int32 BackgroundUpdateChecker(void* data) {
    snooze(5000000);

    const char* targetUrl =
        "https://raw.githubusercontent.com/ablyssx74/RealTimeGUI/refs/heads/main/VERSION";

    BString shellCmdString;
    shellCmdString.SetToFormat("curl -sL \"%s\"", targetUrl);

    BString remoteVersionStr;
    FILE* pipeStream = popen(shellCmdString.String(), "r");
    if (pipeStream != nullptr) {
        char buffer[128] = {0};
        if (fgets(buffer, sizeof(buffer), pipeStream) != nullptr)
            remoteVersionStr = buffer;
        pclose(pipeStream);
    }
    remoteVersionStr.Trim();

    if (remoteVersionStr.Length() == 0)
        return B_OK;

    BString currentVersionStr = AppInfo::VERSION_STRING;
    int32 curMajor = 0, curMinor = 0, curRevision = 0;
    int32 remMajor = 0, remMinor = 0, remRevision = 0;

    sscanf(currentVersionStr.String(), "%*[^0-9]%d.%d.%d", &curMajor, &curMinor, &curRevision);
    sscanf(remoteVersionStr.String(), "%*[^0-9]%d.%d.%d", &remMajor, &remMinor, &remRevision);

    int32 currentFlattened = (curMajor * 10000) + (curMinor * 100) + curRevision;
    int32 remoteFlattened  = (remMajor * 10000) + (remMinor * 100) + remRevision;

    if (remoteFlattened > currentFlattened) {
        BNotification updateAlert(B_INFORMATION_NOTIFICATION);
        updateAlert.SetGroup(AppInfo::APP_NAME);
        updateAlert.SetTitle("Update Available");
        BString alertContent;
        alertContent << "A newer version of " << AppInfo::APP_NAME << " is available! ("
            << remoteVersionStr << ")";
        updateAlert.SetContent(alertContent.String());
        updateAlert.Send();
    }

    return B_OK;
}


// =============================================================================
// Driver catalog
//
// Haiku's audio drivers are NOT standardized on a single settings-file
// convention -- each driver picks its own filename, and its own key names,
// independently. This table was built by reading every driver's own
// directory under haiku/haiku's
// src/add-ons/kernel/drivers/audio/ tree (github.com/haiku/haiku) directly,
// not by assuming one driver's convention applies to the rest -- an
// assumption that would have been wrong (see usb below).
//
// `devfsSegment` is the subdirectory name each driver actually publishes
// itself under in /dev/audio/hmulti/ -- confirmed per-driver from each
// driver's own publish_devices()/make_device_names() source, since it does
// NOT always match the driver's own internal name (usb_audio's own
// DRIVER_NAME is "usb_audio", but it publishes at /dev/audio/hmulti/usb/,
// not /dev/audio/hmulti/usb_audio/).
// =============================================================================
struct DriverProfile {
    const char* devfsSegment;      // subdirectory name under /dev/audio/hmulti/
    const char* label;             // friendly display name
    const char* settingsFileName;  // exact filename under ~/config/settings/kernel/drivers/
                                    // (nullptr if the driver has no settings file at all)
    bool supportsRealtimeBuffers;  // true only if a buffer-size/count key was actually found
    const char* framesKey;         // settings-file key controlling play buffer size, or nullptr
    const char* countKey;          // settings-file key controlling play buffer count, or nullptr
    const char* recordFramesKey;   // key controlling record buffer size, or nullptr if the
                                    // driver has no separate record-side key
    const char* recordCountKey;    // key controlling record buffer count, or nullptr
    const char* sourceNote;        // what was actually confirmed, and where
};

const DriverProfile kDriverProfiles[] = {
    { "hda", "Intel HD Audio (hda)", "hda.settings", true,
      "play_buffer_frames", "play_buffer_count",
      "record_buffer_frames", "record_buffer_count",
      "Confirmed via haiku/haiku's hda_multi_audio.cpp and hda.settings. The settings "
      "file's own comment: latency is roughly 2*buffer_frames/sample_rate at minimum, "
      "and its own worked example recommends 1024 frames at 192000Hz for under 15ms." },

    { "auich", "Intel AC'97 (auich)", "auich.settings", true,
      "buffer_frames", "buffer_count", nullptr, nullptr,
      "Confirmed via ac97/auich/auich.settings. That file also exposes its own "
      "sample_rate and use_thread keys, deliberately left untouched here -- your "
      "system's Media preferences already control the sample rate, and use_thread's "
      "effect wasn't something this app's own research pinned down with confidence. No "
      "separate record_* keys were found -- buffer_frames/buffer_count appear to be "
      "shared between play and record on this driver." },

    { "es1370", "Ensoniq ES1370 (es1370)", "es1370.settings", true,
      "buffer_frames", "buffer_count", nullptr, nullptr,
      "Confirmed via ac97/es1370/es1370.settings (shipped example: 512 frames, "
      "2 buffers, at 44100Hz). No separate record_* keys were found." },

    { "echo", "Echo Digital Audio (echo)", "echo.settings", true,
      "buffer_frames", "buffer_count", nullptr, nullptr,
      "Confirmed via audio/echo/echo.settings (shipped example: 512 frames, 2 buffers, "
      "48000Hz, 16-bit, 2 channels). No separate record_* keys were found. This driver's "
      "devfs segment name is inferred from its own DRIVER_NAME macro, not independently "
      "traced through its publish path the way hda/auich/es1370/emuxki/ice1712 were -- "
      "flagging that in case it's wrong for your specific card." },

    { "emuxki", "Creative Sound Blaster Live!/Audigy (emuxki)", "emuxki.settings", true,
      "buffer_frames", "buffer_count", nullptr, nullptr,
      "Confirmed via audio/emuxki/emuxki.settings and emuxki.c's own publish_devices() "
      "(shipped example: 512 frames, 2 buffers, 48000Hz, 16-bit, 2 channels). No "
      "separate record_* keys were found." },

    { "ice1712", "VIA Envy24 / ICE1712 (ice1712)", "ice1712.settings", true,
      "buffer_size", nullptr, nullptr, nullptr,
      "Confirmed via audio/ice1712/ice1712.settings and ice1712.cpp's own "
      "HMULTI_AUDIO_DEV_PATH. This driver only exposes a single buffer_size key -- no "
      "separate buffer-count or record_* key was found." },

    { "sis7018", "SiS 7018 (sis7018)", "sis7018", false, nullptr, nullptr, nullptr, nullptr,
      "Confirmed via ac97/sis7018/sis7018.settings.sample and Driver.cpp: this driver's "
      "settings file (note: the sample itself says to rename it to plain \"sis7018\", "
      "not \"sis7018.settings\") only controls debug tracing/logging, not buffer sizing." },

    { "usb", "USB Audio Class (usb_audio)", "usb_audio.settings", false, nullptr, nullptr,
      nullptr, nullptr,
      "Confirmed via audio/usb/Driver.h, Driver.cpp and usb_audio.settings: buffer size "
      "(2048 samples / 2 sub-buffers) is hardcoded in the driver itself, and the "
      "settings file only controls debug tracing/logging, not buffer sizing. This is "
      "likely what many real-time USB audio interfaces will show up as." },

    { "auvia", "VIA VT82xx AC'97 (auvia)", nullptr, false, nullptr, nullptr, nullptr, nullptr,
      "Confirmed via directory listing -- no .settings file at all ships with this "
      "driver." },

    { "geode", "AMD Geode (geode)", nullptr, false, nullptr, nullptr, nullptr, nullptr,
      "Confirmed via directory listing -- no .settings file at all ships with this "
      "driver." },

    { "sb16", "Sound Blaster 16 (sb16)", nullptr, false, nullptr, nullptr, nullptr, nullptr,
      "Confirmed via directory listing -- no .settings file at all ships with this "
      "driver." },

    { "virtio", "VirtIO Sound (virtual machines)", nullptr, false, nullptr, nullptr,
      nullptr, nullptr,
      "Confirmed via directory listing -- no .settings file at all ships with this "
      "driver." },

    { "null", "Null Audio (no real hardware)", nullptr, false, nullptr, nullptr,
      nullptr, nullptr,
      "A placeholder device Haiku can publish when nothing else claims the slot -- "
      "nothing to tune." },
};
const int32 kDriverProfileCount = sizeof(kDriverProfiles) / sizeof(kDriverProfiles[0]);

// Not cataloged: cmedia (its devfs segment name wasn't independently traced through
// its own publish path, unlike every entry above -- known to ship no .settings file
// either way, but left out rather than guess at a label/path that might be wrong),
// and the echo driver's own 24/3g/gals/indigo hardware sub-variants (assumed to share
// echo.settings/echo's own devfs segment, not checked individually).


// =============================================================================
// Detection: which driver(s) are actually active right now, and what
// frequency the system is actually running audio at.
// =============================================================================

// Scans /dev/audio/hmulti/ for published device subdirectories -- the same
// place every driver in the table above (and, per Haiku convention, every
// audio driver generally) publishes itself. Each subdirectory name is one
// driver's own devfsSegment.
static void DetectActiveDriverSegments(std::vector<BString>* outSegments) {
    DIR* dir = opendir("/dev/audio/hmulti");
    if (dir == nullptr)
        return;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        outSegments->push_back(BString(entry->d_name));
    }
    closedir(dir);
}

// Looks up a devfs segment name in the driver catalog above. Returns
// nullptr for anything not cataloged (an honest "don't know" rather than a
// guess).
static const DriverProfile* FindDriverProfile(const BString& segment) {
    for (int32 i = 0; i < kDriverProfileCount; i++) {
        if (segment == kDriverProfiles[i].devfsSegment)
            return &kDriverProfiles[i];
    }
    return nullptr;
}

// Queries the current physical audio output's actual negotiated sample
// rate via BMediaRoster's public API (GetAudioOutput() for the physical
// sink node, GetAllOutputsFor()/GetFormatFor() for its live format) --
// this reads whatever frequency is actually running right now, which is
// what Haiku's own Media preferences "Frequency" control sets.
static bool DetectCurrentSampleRate(double* outRate) {
    BMediaRoster* roster = BMediaRoster::Roster();
    if (roster == nullptr)
        return false;

    media_node audioOutputNode;
    if (roster->GetAudioOutput(&audioOutputNode) != B_OK)
        return false;

    media_output outputs[8];
    int32 outputCount = 0;
    bool found = false;
    if (roster->GetAllOutputsFor(audioOutputNode, outputs, 8, &outputCount) == B_OK) {
        for (int32 i = 0; i < outputCount && !found; i++) {
            media_format format;
            if (roster->GetFormatFor(outputs[i], &format) == B_OK
                    && format.type == B_MEDIA_RAW_AUDIO) {
                *outRate = format.u.raw_audio.frame_rate;
                found = true;
            }
        }
    }

    roster->ReleaseNode(audioOutputNode);
    return found;
}


// =============================================================================
// Recommendation
//
// Heuristic, not vendor-authoritative. Targets roughly the same per-buffer
// duration (~2.7ms) as the real-world-confirmed 128 frames @ 48000Hz
// setting this app's own recommendation logic was built around, scaled
// linearly to whatever sample rate is actually detected, and snapped to
// the nearest buffer size actually seen across every driver settings
// example gathered for the catalog above. This is a starting point, not a
// guarantee -- actual real-time headroom depends on the specific
// hardware and how much other work (effects processing, etc.) is
// competing for the same CPU core. If clicks/pops show up at the
// recommended setting, raising buffer_count first is cheaper in added
// latency than raising buffer_frames.
// =============================================================================
struct BufferRecommendation {
    int32 frames;
    int32 count;
    double perBufferMs;
    double totalLatencyMs;
};

const int32 kCommonBufferSizes[] = { 64, 128, 256, 512, 1024, 2048 };
const int32 kCommonBufferSizeCount = sizeof(kCommonBufferSizes) / sizeof(kCommonBufferSizes[0]);

static int32 SnapToCommonBufferSize(double target) {
    int32 best = kCommonBufferSizes[0];
    double bestDiff = 1e18;
    for (int32 i = 0; i < kCommonBufferSizeCount; i++) {
        double diff = fabs((double)kCommonBufferSizes[i] - target);
        if (diff < bestDiff) {
            bestDiff = diff;
            best = kCommonBufferSizes[i];
        }
    }
    return best;
}

static BufferRecommendation ComputeRecommendation(double sampleRate) {
    BufferRecommendation rec;
    const double kTargetPerBufferMs = 2.7;
    double targetFrames = sampleRate * kTargetPerBufferMs / 1000.0;
    rec.frames = SnapToCommonBufferSize(targetFrames);
    rec.count = 4;
    rec.perBufferMs = 1000.0 * rec.frames / sampleRate;
    rec.totalLatencyMs = rec.perBufferMs * rec.count;
    return rec;
}


// =============================================================================
// Settings file I/O
//
// Line-oriented and deliberately conservative, not a full settings-file
// parser: an existing key (commented out or not) has its value replaced
// in place; a key that isn't present yet is appended. Everything else in
// the file -- other keys, comments, formatting -- is left untouched, since
// this file may carry a driver's own shipped comments or a user's prior
// customization this app has no business erasing.
// =============================================================================

static BString ResolveDriverSettingsDir() {
    BPath path;
    if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK) {
        path.Append("kernel/drivers");
        return BString(path.Path());
    }
    // Fallback matching every driver's own settings-file header comment
    // ("This file should be moved to ~/config/settings/kernel/drivers/").
    return BString("/boot/home/config/settings/kernel/drivers");
}

// mkdir() only creates one level at a time, and there's no guarantee
// ~/config/settings/kernel/ already exists (only that ~/config/settings/
// itself does) -- most drivers' own settings files are only ever placed
// there by hand, so this app can easily be the first thing to ever need
// the "kernel" or "kernel/drivers" levels on a given system. Creates each
// missing path component in turn; EEXIST on any of them is expected and
// fine, not a failure.
static void EnsureDirectoryExists(const BString& fullPath) {
    BString soFar;
    int32 searchFrom = 1; // skip the leading '/' so the first split isn't empty
    while (true) {
        int32 slash = fullPath.FindFirst('/', searchFrom);
        BString component = (slash < 0) ? fullPath : BString(fullPath.String(), slash);
        mkdir(component.String(), 0755);
        if (slash < 0)
            break;
        searchFrom = slash + 1;
    }
}

static BString ReadFileToString(const char* path) {
    BString result;
    FILE* f = fopen(path, "r");
    if (f == nullptr)
        return result;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        result.Append(buf, n);
    fclose(f);
    return result;
}

static bool WriteStringToFile(const char* path, const BString& content) {
    FILE* f = fopen(path, "w");
    if (f == nullptr)
        return false;
    fwrite(content.String(), 1, content.Length(), f);
    fclose(f);
    return true;
}

// Updates one settings key to `value`, touching only genuinely ACTIVE
// (uncommented) lines. A commented-out line is inert to the driver either
// way, and this app's key-name matching can't reliably tell a real
// disabled setting apart from documentation prose that happens to mention
// the same key name inside a comment -- hda.settings's own header, for
// instance, shows "play_buffer_frames 1024" as a comment purely as a
// worked example. An earlier version of this matching logic stripped a
// leading '#' from *any* line and compared, so it matched that example
// (appearing earlier in the file) before ever reaching the real settings
// block further down -- confirmed via real-world testing, where it
// produced a duplicate active key instead of updating the real one.
//
// Every existing active line for this key, anywhere in the file, is
// removed (normally just one, but this also self-heals a file the bug
// above already left with more than one), then exactly one fresh active
// line is inserted at the first removed line's position -- or appended
// at the end of the file if the key wasn't active anywhere yet.
static void UpsertSettingKey(BString* content, const char* key, int32 value) {
    BString keyPattern(key);
    int32 lineStart = 0;
    int32 firstMatchPos = -1;

    while (lineStart < content->Length()) {
        int32 lineEnd = content->FindFirst('\n', lineStart);
        bool hasNewline = lineEnd >= 0;
        if (!hasNewline)
            lineEnd = content->Length();

        BString line;
        content->CopyInto(line, lineStart, lineEnd - lineStart);
        BString bare = line;
        bare.Trim();

        bool isActiveMatch = false;
        if (!bare.StartsWith("#") && bare.StartsWith(keyPattern)) {
            char afterKey = bare.Length() > keyPattern.Length()
                ? bare[keyPattern.Length()] : '\0';
            isActiveMatch = (afterKey == '\0' || afterKey == ' ' || afterKey == '\t');
        }

        if (isActiveMatch) {
            if (firstMatchPos < 0)
                firstMatchPos = lineStart;
            int32 removeLen = lineEnd - lineStart + (hasNewline ? 1 : 0);
            content->Remove(lineStart, removeLen);
            // Don't advance lineStart -- whatever followed this line has
            // just shifted into this same position.
            continue;
        }

        lineStart = hasNewline ? lineEnd + 1 : lineEnd;
    }

    BString newLine;
    newLine << key << "\t" << value << "\n";

    if (firstMatchPos >= 0) {
        content->Insert(newLine, firstMatchPos);
    } else {
        if (content->Length() > 0 && (*content)[content->Length() - 1] != '\n')
            content->Append("\n");
        content->Append(newLine);
    }
}


// =============================================================================
// Main window
// =============================================================================
class RealTimeWindow : public BWindow {
public:
    RealTimeWindow()
        : BWindow(BRect(0, 0, 560, 250), "RealTimeGUI -- Audio Real-Time Settings",
              B_DOCUMENT_WINDOW, B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS) {

        fDriverLabel = new BStringView("driver_label", "Detecting audio driver...");
        fDriverLabel->SetFont(be_bold_font);

        fFrequencyLabel = new BStringView("frequency_label", "Detecting current frequency...");

        fExplanationView = new BTextView("explanation_view");
        fExplanationView->MakeEditable(false);
        fExplanationView->MakeSelectable(true);
        fExplanationView->SetWordWrap(true);
        fExplanationView->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
        fExplanationView->SetLowUIColor(B_PANEL_BACKGROUND_COLOR);
        fExplanationView->SetExplicitMinSize(BSize(540.0, 45.0));

        fFramesControl = new BTextControl("frames_control", "Play buffer frames:", "", nullptr);
        fCountControl = new BTextControl("count_control", "Play buffer count:", "", nullptr);

        fRecordFramesControl = new BTextControl("record_frames_control",
            "Record buffer frames:", "", nullptr);
        fRecordCountControl = new BTextControl("record_count_control",
            "Record buffer count:", "", nullptr);

        fUnavailableLabel = new BStringView("unavailable_label", "");
        fUnavailableLabel->SetFont(be_bold_font);

        fTargetPathLabel = new BStringView("target_path_label", "");

        fApplyBtn = new BButton("apply_btn", "Apply", new BMessage(MSG_APPLY));
        fApplyBtn->SetEnabled(false);
        fRescanBtn = new BButton("rescan_btn", "Rescan", new BMessage(MSG_REFRESH));

        BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
            .AddGroup(B_VERTICAL, B_USE_ITEM_SPACING)
                .SetInsets(B_USE_WINDOW_INSETS)
                .Add(fDriverLabel)
                .Add(fFrequencyLabel)
                .Add(fUnavailableLabel)
                .Add(fExplanationView)
                .AddGroup(B_HORIZONTAL, B_USE_ITEM_SPACING)
                    .Add(fFramesControl)
                    .Add(fCountControl)
                .End()
                .AddGroup(B_HORIZONTAL, B_USE_ITEM_SPACING)
                    .Add(fRecordFramesControl)
                    .Add(fRecordCountControl)
                .End()
                .Add(fTargetPathLabel)
                .AddGlue()
                .AddGroup(B_HORIZONTAL, B_USE_ITEM_SPACING)
                    .Add(fRescanBtn)
                    .AddGlue()
                    .Add(fApplyBtn)
                .End()
            .End();

        thread_id updateThread = spawn_thread(BackgroundUpdateChecker, "UpdateCheckerThread",
            B_NORMAL_PRIORITY, this);
        if (updateThread >= 0)
            resume_thread(updateThread);

        CenterOnScreen();
        _Refresh();
    }

    bool QuitRequested() override {
        be_app->PostMessage(B_QUIT_REQUESTED);
        return true;
    }

    void MessageReceived(BMessage* message) override {
        switch (message->what) {
            case MSG_REFRESH:
                _Refresh();
                break;

            case MSG_APPLY:
                _ApplySettings();
                break;

            default:
                BWindow::MessageReceived(message);
                break;
        }
    }

private:
    void _Refresh() {
        std::vector<BString> segments;
        DetectActiveDriverSegments(&segments);

        fActiveProfile = nullptr;
        fUnknownSegment = "";
        for (size_t i = 0; i < segments.size(); i++) {
            const DriverProfile* profile = FindDriverProfile(segments[i]);
            if (profile != nullptr && profile->supportsRealtimeBuffers) {
                fActiveProfile = profile;
                break;
            }
            if (profile != nullptr && fActiveProfile == nullptr) {
                // Remember a known-but-unsupported driver so the message
                // below can name it specifically, but keep looking in case
                // a *tunable* one shows up too (e.g. an HDMI + a real
                // audio interface both present at once).
                fActiveProfile = profile;
            } else if (profile == nullptr && fUnknownSegment.Length() == 0) {
                fUnknownSegment = segments[i];
            }
        }

        double sampleRate = 0.0;
        bool haveSampleRate = DetectCurrentSampleRate(&sampleRate);
        fCurrentSampleRate = haveSampleRate ? sampleRate : 0.0;

        if (segments.empty()) {
            fDriverLabel->SetText("No audio device found under /dev/audio/hmulti/.");
        } else if (fActiveProfile != nullptr) {
            BString text("Detected: ");
            text << fActiveProfile->label;
            if (segments.size() > 1) {
                text << " (plus ";
                text << (int32)(segments.size() - 1);
                text << " other device(s) -- showing the first tunable one found)";
            }
            fDriverLabel->SetText(text.String());
        } else {
            BString text("Detected an audio device published as \"");
            text << fUnknownSegment << "\", not yet cataloged by this app.";
            fDriverLabel->SetText(text.String());
        }

        if (haveSampleRate) {
            BString freqText;
            freqText.SetToFormat("Current output frequency: %.0f Hz", sampleRate);
            fFrequencyLabel->SetText(freqText.String());
        } else {
            fFrequencyLabel->SetText(
                "Could not determine the current output frequency (is any audio device "
                "configured in Media preferences?).");
        }

        bool tunable = fActiveProfile != nullptr && fActiveProfile->supportsRealtimeBuffers;
        bool hasFrequency = haveSampleRate;

        if (tunable && hasFrequency) {
            fUnavailableLabel->SetText("");
            BufferRecommendation rec = ComputeRecommendation(sampleRate);

            BString explanation;
            explanation << "Recommended: " << rec.frames << " frames / " << rec.count
                << " buffers at your current " << (int32)sampleRate << " Hz -- about ";
            char msBuf[32];
            snprintf(msBuf, sizeof(msBuf), "%.2f", rec.perBufferMs);
            explanation << msBuf << "ms per buffer, ";
            snprintf(msBuf, sizeof(msBuf), "%.2f", rec.totalLatencyMs);
            explanation << msBuf << "ms of total buffered latency.";
            fExplanationView->SetText(explanation.String());

            BString framesStr;
            framesStr << rec.frames;
            fFramesControl->SetText(framesStr.String());

            if (fActiveProfile->countKey != nullptr) {
                BString countStr;
                countStr << rec.count;
                fCountControl->SetText(countStr.String());
                fCountControl->SetEnabled(true);
            } else {
                fCountControl->SetText("");
                fCountControl->SetEnabled(false);
            }

            if (fActiveProfile->recordFramesKey != nullptr) {
                BString recFramesStr;
                recFramesStr << rec.frames;
                fRecordFramesControl->SetText(recFramesStr.String());
                fRecordFramesControl->SetEnabled(true);
            } else {
                fRecordFramesControl->SetText("");
                fRecordFramesControl->SetEnabled(false);
            }

            if (fActiveProfile->recordCountKey != nullptr) {
                BString recCountStr;
                recCountStr << rec.count;
                fRecordCountControl->SetText(recCountStr.String());
                fRecordCountControl->SetEnabled(true);
            } else {
                fRecordCountControl->SetText("");
                fRecordCountControl->SetEnabled(false);
            }

            fFramesControl->SetEnabled(true);
            fApplyBtn->SetEnabled(true);

            BString settingsDir = ResolveDriverSettingsDir();
            BString targetPath;
            targetPath << "Will update: " << settingsDir << "/" << fActiveProfile->settingsFileName;
            fTargetPathLabel->SetText(targetPath.String());
        } else {
            fFramesControl->SetText("");
            fCountControl->SetText("");
            fRecordFramesControl->SetText("");
            fRecordCountControl->SetText("");
            fFramesControl->SetEnabled(false);
            fCountControl->SetEnabled(false);
            fRecordFramesControl->SetEnabled(false);
            fRecordCountControl->SetEnabled(false);
            fApplyBtn->SetEnabled(false);
            fTargetPathLabel->SetText("");

            if (fActiveProfile != nullptr && !fActiveProfile->supportsRealtimeBuffers) {
                fUnavailableLabel->SetText("Realtime settings not available.");
                BString explanation(fActiveProfile->sourceNote);
                fExplanationView->SetText(explanation.String());
            } else if (fActiveProfile == nullptr && !fUnknownSegment.IsEmpty()) {
                fUnavailableLabel->SetText("");
                fExplanationView->SetText(
                    "This driver isn't in this app's catalog yet, so whether it supports "
                    "a real-time settings file is unknown -- check "
                    "github.com/haiku/haiku's src/add-ons/kernel/drivers/audio/ tree "
                    "for a matching *.settings file manually if you'd like to try tuning "
                    "it by hand under ~/config/settings/kernel/drivers/.");
            } else if (!hasFrequency) {
                fUnavailableLabel->SetText("");
                fExplanationView->SetText(
                    "A recommendation needs to know your current output frequency first.");
            } else {
                fUnavailableLabel->SetText("Realtime settings not available.");
                fExplanationView->SetText(
                    "No audio device was found published under /dev/audio/hmulti/.");
            }
        }

        InvalidateLayout();
    }

    void _ApplySettings() {
        if (fActiveProfile == nullptr || !fActiveProfile->supportsRealtimeBuffers)
            return;

        int32 frames = atol(fFramesControl->Text());
        if (frames < 16 || frames > 65536) {
            BAlert* alert = new BAlert("Invalid value",
                "Buffer frames should be a reasonable number, e.g. between 16 and 65536.",
                "OK");
            alert->Go();
            return;
        }

        int32 count = -1;
        if (fActiveProfile->countKey != nullptr) {
            count = atol(fCountControl->Text());
            if (count < 2 || count > 64) {
                BAlert* alert = new BAlert("Invalid value",
                    "Buffer count should be a reasonable number, e.g. between 2 and 64.",
                    "OK");
                alert->Go();
                return;
            }
        }

        int32 recordFrames = -1;
        if (fActiveProfile->recordFramesKey != nullptr) {
            recordFrames = atol(fRecordFramesControl->Text());
            if (recordFrames < 16 || recordFrames > 65536) {
                BAlert* alert = new BAlert("Invalid value",
                    "Record buffer frames should be a reasonable number, e.g. between "
                    "16 and 65536.", "OK");
                alert->Go();
                return;
            }
        }

        int32 recordCount = -1;
        if (fActiveProfile->recordCountKey != nullptr) {
            recordCount = atol(fRecordCountControl->Text());
            if (recordCount < 2 || recordCount > 64) {
                BAlert* alert = new BAlert("Invalid value",
                    "Record buffer count should be a reasonable number, e.g. between "
                    "2 and 64.", "OK");
                alert->Go();
                return;
            }
        }

        BString settingsDir = ResolveDriverSettingsDir();
        EnsureDirectoryExists(settingsDir);

        BString targetPath;
        targetPath << settingsDir << "/" << fActiveProfile->settingsFileName;

        BString content = ReadFileToString(targetPath.String());
        if (content.Length() == 0) {
            content << "# " << fActiveProfile->settingsFileName
                << " -- written by RealTimeGUI (github.com/ablyssx74/RealTimeGUI)\n"
                << "# Restart Media Services (or reboot) for changes to take effect.\n\n";
        }

        UpsertSettingKey(&content, fActiveProfile->framesKey, frames);
        if (fActiveProfile->countKey != nullptr)
            UpsertSettingKey(&content, fActiveProfile->countKey, count);
        if (fActiveProfile->recordFramesKey != nullptr)
            UpsertSettingKey(&content, fActiveProfile->recordFramesKey, recordFrames);
        if (fActiveProfile->recordCountKey != nullptr)
            UpsertSettingKey(&content, fActiveProfile->recordCountKey, recordCount);

        if (WriteStringToFile(targetPath.String(), content)) {
            BString msg;
            msg << "Wrote " << frames;
            if (count > 0)
                msg << " frames / " << count << " play buffers";
            else
                msg << " play frames";
            if (recordFrames > 0) {
                msg << ", " << recordFrames;
                if (recordCount > 0)
                    msg << " frames / " << recordCount << " record buffers";
                else
                    msg << " record frames";
            }
            msg << " to " << targetPath << ".\n\nRestart Media Services (or reboot) for "
                "the new settings to take effect.";
            BAlert* alert = new BAlert("Settings updated", msg.String(), "OK");
            alert->Go();
        } else {
            BString msg;
            msg << "Could not write to " << targetPath << ".";
            BAlert* alert = new BAlert("Write failed", msg.String(), "OK");
            alert->Go();
        }
    }

    BStringView*  fDriverLabel;
    BStringView*  fFrequencyLabel;
    BStringView*  fUnavailableLabel;
    BStringView*  fTargetPathLabel;
    BTextView*    fExplanationView;
    BTextControl* fFramesControl;
    BTextControl* fCountControl;
    BTextControl* fRecordFramesControl;
    BTextControl* fRecordCountControl;
    BButton*      fApplyBtn;
    BButton*      fRescanBtn;

    const DriverProfile* fActiveProfile = nullptr;
    BString fUnknownSegment;
    double fCurrentSampleRate = 0.0;
};


class RealTimeApp : public BApplication {
public:
    RealTimeApp() : BApplication(kAppSignature) {}
    void ReadyToRun() override {
        RealTimeWindow* window = new RealTimeWindow();
        window->Show();
    }
};

int main() {
    RealTimeApp app;
    app.Run();
    return 0;
}
