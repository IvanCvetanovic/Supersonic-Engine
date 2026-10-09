package com.ivancvetanovic.supersonic;

import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.content.Context;
import android.content.SharedPreferences;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.os.Build;
import android.util.DisplayMetrics;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.RandomAccessFile;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Locale;

/**
 * WHAT THE LAST RUN LEFT BEHIND, for a player whose game closed on them without a word.
 *
 * A game that dies on the native side (a signal in a vendor driver, an abort) shows the
 * player a black screen and then the launcher, and shows the developer nothing: no
 * exception reaches the game's own message box. From Android 11 the system keeps a record
 * of how each process ended (ApplicationExitInfo), and the game keeps a log whose last
 * line names the stage it had reached; this reads both, at the next start, before the
 * native side opens (and overwrites) that log, and words them for a screenshot or the
 * Share button. Nothing here can stop a start: every failure is a null report.
 *
 * Only what is certain is shown as fact. Whether a start "failed" is a judgement about
 * the record (see isUnexpected): a player's own Back, a swipe from Recents and the
 * system's routine trimming of a background process are not reported. An end is marked as
 * seen only once its dialog is on the screen (markShown), and a record whose dialog never
 * got that far is given up after a few tries, so a report that itself takes the process
 * down cannot come back for ever.
 */
final class PostMortem {

    /** What the activity shows: a title, the text for the dialog, the longer text to share. */
    static final class Report {
        final String title;
        final String shown;
        final String shared;
        final String shareLabel;
        /** The time of the end this report is about: what markShown remembers. */
        final long endedAt;

        Report(String title, String shown, String shared, String shareLabel, long endedAt) {
            this.title = title;
            this.shown = shown;
            this.shared = shared;
            this.shareLabel = shareLabel;
            this.endedAt = endedAt;
        }
    }

    private static final String PREFS = "supersonic_report";
    private static final String LAST_SHOWN = "lastShown";
    private static final String PENDING_AT = "pendingAt";
    private static final String PENDING_TRIES = "pendingTries";
    // How often one end is offered before it is given up: each offer that never reached markShown
    // was a start that did not get as far as a dialog on the screen.
    private static final int MAX_TRIES = 3;
    // IMPORTANCE_FOREGROUND_SERVICE: the process was on screen, or close to it, when it died.
    private static final int FOREGROUND_AT_MOST = 125;
    // The dialog has about one landscape screen: the facts, then the few log lines that name the
    // stage. Everything else is in the shared text, which is not cut.
    private static final int SHOWN_LOG_LINES = 7;
    private static final int SHOWN_LINE_CHARS = 120;
    private static final int SHARED_LOG_LINES = 60;
    // A line of the game's log is cut only when it is runaway (the engine's limits line is about
    // 350 characters, and every one of them is wanted).
    private static final int SHARED_LINE_CHARS = 600;
    private static final int TRACE_LINE_CHARS = 170;

    private PostMortem() {}

    /**
     * The report for the newest unexpected end of the game's last process that has not been
     * shown, or null when there is none, the phone is older than Android 11, or anything went
     * wrong. The caller shows it and then calls markShown.
     *
     * @param logPath the game's log, relative to the app's files directory ("" for none)
     */
    static Report collect(Context context, String logPath) {
        if (Build.VERSION.SDK_INT < 30) return null;
        try {
            ActivityManager manager = (ActivityManager) context.getSystemService(Context.ACTIVITY_SERVICE);
            if (manager == null) return null;
            List<ApplicationExitInfo> exits =
                    manager.getHistoricalProcessExitReasons(context.getPackageName(), 0, 8);
            if (exits == null || exits.isEmpty()) return null;

            SharedPreferences prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
            long lastShown = prefs.getLong(LAST_SHOWN, 0L);
            ApplicationExitInfo ended = null;
            int newer = 0;   // records newer than the one reported: runs that ended in an expected way since
            for (int i = 0; i < exits.size(); i++) {   // newest first
                ApplicationExitInfo info = exits.get(i);
                if (info.getTimestamp() <= lastShown) break;
                if (isUnexpected(info)) {
                    ended = info;
                    newer = i;
                    break;
                }
            }
            if (ended == null) return null;

            long when = ended.getTimestamp();
            int tries = (prefs.getLong(PENDING_AT, 0L) == when ? prefs.getInt(PENDING_TRIES, 0) : 0) + 1;
            if (tries > MAX_TRIES) {
                markShown(context, when);
                return null;
            }
            prefs.edit().putLong(PENDING_AT, when).putInt(PENDING_TRIES, tries).commit();
            return build(context, manager, ended, newer, logPath);
        } catch (Throwable t) {
            // A report is a courtesy; the game starts without it.
            return null;
        }
    }

    /** The end at {@code endedAt} is on the screen: it is not offered again. */
    static void markShown(Context context, long endedAt) {
        try {
            context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
                    .putLong(LAST_SHOWN, endedAt).remove(PENDING_AT).remove(PENDING_TRIES).commit();
        } catch (Throwable ignored) {
            // Offered again at the next start, at most MAX_TRIES times.
        }
    }

    private static boolean isUnexpected(ApplicationExitInfo info) {
        switch (info.getReason()) {
            case ApplicationExitInfo.REASON_CRASH:
            case ApplicationExitInfo.REASON_CRASH_NATIVE:
            case ApplicationExitInfo.REASON_ANR:
            case ApplicationExitInfo.REASON_INITIALIZATION_FAILURE:
            case ApplicationExitInfo.REASON_EXCESSIVE_RESOURCE_USAGE:
                return true;
            case ApplicationExitInfo.REASON_EXIT_SELF:
                // The game ending itself with an error status: a start it could not finish
                // (a clean quit, or the player leaving, is status 0).
                return info.getStatus() != 0;
            case ApplicationExitInfo.REASON_UNKNOWN:
            case ApplicationExitInfo.REASON_LOW_MEMORY:
            case ApplicationExitInfo.REASON_SIGNALED:
            case ApplicationExitInfo.REASON_OTHER:
            case ApplicationExitInfo.REASON_DEPENDENCY_DIED:
            case ApplicationExitInfo.REASON_PERMISSION_CHANGE:
                // Routine for a process nobody is looking at; only a process that was on the
                // screen is a game that was stopped.
                return info.getImportance() <= FOREGROUND_AT_MOST;
            default:
                // USER_REQUESTED, USER_STOPPED and whatever a newer Android adds.
                return false;
        }
    }

    private static Report build(Context context, ActivityManager manager, ApplicationExitInfo ended, int newer,
                                String logPath) {
        boolean portuguese = "pt".equals(Locale.getDefault().getLanguage());
        SimpleDateFormat time = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.US);

        // Facts the dialog carries: which build, which phone, how it ended.
        String app = appLine(context);
        StringBuilder device = new StringBuilder();
        device.append(Build.MANUFACTURER).append(' ').append(Build.MODEL).append(" (").append(Build.DEVICE)
                .append(", ").append(Build.HARDWARE);
        if (Build.VERSION.SDK_INT >= 31 && Build.SOC_MODEL != null && !Build.SOC_MODEL.isEmpty()) {
            device.append(", ").append(Build.SOC_MODEL);
        }
        device.append("), Android ").append(Build.VERSION.RELEASE).append(" (API ").append(Build.VERSION.SDK_INT)
                .append("), ").append(Build.DISPLAY).append('\n');

        long ago = Math.max(0L, (System.currentTimeMillis() - ended.getTimestamp()) / 1000L);
        StringBuilder outcome = new StringBuilder();
        outcome.append("Last run ended: ").append(reasonName(ended.getReason())).append(" (").append(ended.getReason())
                .append("), status ").append(ended.getStatus()).append(", ")
                .append(importanceName(ended.getImportance())).append(", ")
                .append(time.format(new Date(ended.getTimestamp()))).append(" (").append(ago).append(" s ago)\n");
        String description = ended.getDescription();
        if (description != null && !description.isEmpty()) outcome.append("Description: ").append(description).append('\n');
        if (newer > 0) {
            outcome.append("This was an EARLIER run: ").append(newer).append(" later run(s) ended normally.\n");
        }

        // Facts only the shared text carries.
        StringBuilder more = new StringBuilder();
        ActivityManager.MemoryInfo memory = new ActivityManager.MemoryInfo();
        manager.getMemoryInfo(memory);
        DisplayMetrics metrics = context.getResources().getDisplayMetrics();
        more.append("RAM ").append(memory.totalMem / (1024 * 1024)).append(" MB, screen ")
                .append(metrics.widthPixels).append('x').append(metrics.heightPixels).append(" at ")
                .append(metrics.densityDpi).append(" dpi\n");
        more.append("Memory then: pss ").append(ended.getPss() / 1024).append(" MB, rss ")
                .append(ended.getRss() / 1024).append(" MB\n");

        String trace = traceSummary(ended);

        // The log, and what ties it to that end: it is only replaced when the game gets as far as
        // opening it, so one that died before that leaves an earlier run's log in place.
        File logFile = logPath == null || logPath.isEmpty() ? null : new File(context.getFilesDir(), logPath);
        List<String> log = logTail(logFile, SHARED_LOG_LINES, SHARED_LINE_CHARS);
        StringBuilder logNote = new StringBuilder();
        if (logFile != null && logFile.isFile() && logFile.length() > 0) {
            long written = logFile.lastModified();
            long gap = (ended.getTimestamp() - written) / 1000L;
            logNote.append("Log last written ").append(time.format(new Date(written)));
            if (gap < 0) {
                logNote.append(" (AFTER that end: it belongs to a later run)");
            } else {
                logNote.append(" (").append(gap).append(" s before that end)");
            }
            logNote.append('\n');
        }

        String title = portuguese ? "O Penumbra fechou sozinho da última vez" : "Penumbra closed unexpectedly last time";
        String shareLabel = portuguese ? "Compartilhar" : "Share";
        String english = "Tap Share, or take screenshots (scroll down to the end), then tap OK.";
        String portugueseText = "Toque em Compartilhar, ou tire capturas (role até o fim), depois toque em OK.";

        StringBuilder shown = new StringBuilder();
        shown.append(portuguese ? portugueseText : english).append('\n');
        shown.append(portuguese ? english : portugueseText).append("\n\n");
        // By how much each line says about the end: the build, how it ended, where the log stops, then the phone.
        shown.append(app).append('\n').append(outcome).append(logNote);
        if (log.isEmpty()) {
            shown.append("No game log: the game stopped before it opened one.\n");
        } else {
            shown.append("End of the game's log:\n");
            int from = Math.max(0, log.size() - SHOWN_LOG_LINES);
            for (int i = from; i < log.size(); i++) {
                String line = log.get(i);
                if (line.startsWith("INFO ")) line = line.substring(5);   // the level that says nothing
                shown.append(cut(line, SHOWN_LINE_CHARS)).append('\n');
            }
        }
        shown.append(device);

        StringBuilder shared = new StringBuilder();
        shared.append(title).append("\n\n").append(app).append('\n').append(device).append(more).append(outcome)
                .append(logNote);
        if (!trace.isEmpty()) shared.append("\nSystem trace:\n").append(trace);
        if (log.isEmpty()) {
            shared.append("\nNo game log: the game stopped before it opened one.\n");
        } else {
            shared.append("\nEnd of the game's log:\n");
            for (String line : log) shared.append(line).append('\n');
        }
        return new Report(title, shown.toString(), shared.toString(), shareLabel, ended.getTimestamp());
    }

    private static String cut(String line, int chars) {
        return line.length() > chars ? line.substring(0, chars) + "..." : line;
    }

    @SuppressWarnings("deprecation")
    private static String appLine(Context context) {
        try {
            PackageInfo info = context.getPackageManager().getPackageInfo(context.getPackageName(), 0);
            long code = Build.VERSION.SDK_INT >= 28 ? info.getLongVersionCode() : info.versionCode;
            return context.getPackageName() + " " + info.versionName + " (build " + code + ")";
        } catch (PackageManager.NameNotFoundException e) {
            return context.getPackageName();
        }
    }

    private static String reasonName(int reason) {
        switch (reason) {
            case ApplicationExitInfo.REASON_EXIT_SELF: return "the game ended itself with an error";
            case ApplicationExitInfo.REASON_SIGNALED: return "killed by a signal";
            case ApplicationExitInfo.REASON_LOW_MEMORY: return "killed for memory";
            case ApplicationExitInfo.REASON_CRASH: return "crash (Java)";
            case ApplicationExitInfo.REASON_CRASH_NATIVE: return "crash (native)";
            case ApplicationExitInfo.REASON_ANR: return "not responding (ANR)";
            case ApplicationExitInfo.REASON_INITIALIZATION_FAILURE: return "failed to initialise";
            case ApplicationExitInfo.REASON_PERMISSION_CHANGE: return "permission change";
            case ApplicationExitInfo.REASON_EXCESSIVE_RESOURCE_USAGE: return "excessive resource use";
            case ApplicationExitInfo.REASON_DEPENDENCY_DIED: return "a dependency died";
            case ApplicationExitInfo.REASON_OTHER: return "other";
            case ApplicationExitInfo.REASON_UNKNOWN: return "unknown";
            default: return "reason " + reason;
        }
    }

    private static String importanceName(int importance) {
        if (importance <= ActivityManager.RunningAppProcessInfo.IMPORTANCE_FOREGROUND) return "in the foreground";
        if (importance <= ActivityManager.RunningAppProcessInfo.IMPORTANCE_FOREGROUND_SERVICE) return "just left the foreground";
        return "in the background (importance " + importance + ")";
    }

    /**
     * What the system kept of the end: an ANR's thread dump, or, on an Android that stores
     * one, a native crash's tombstone (binary on some versions: only its readable strings
     * that name a library are kept). Android 11 stores neither for a native crash.
     */
    private static String traceSummary(ApplicationExitInfo ended) {
        int reason = ended.getReason();
        if (reason != ApplicationExitInfo.REASON_ANR && reason != ApplicationExitInfo.REASON_CRASH_NATIVE) return "";
        InputStream in = null;
        try {
            in = ended.getTraceInputStream();
            if (in == null) return "";
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            byte[] buffer = new byte[8192];
            int n;
            while (bytes.size() < 256 * 1024 && (n = in.read(buffer)) > 0) bytes.write(buffer, 0, n);
            byte[] data = bytes.toByteArray();

            // Readable runs of at least six printable characters; a text trace is made of them.
            List<String> strings = new ArrayList<String>();
            StringBuilder run = new StringBuilder();
            for (byte b : data) {
                if (b >= 32 && b < 127) {
                    run.append((char) b);
                } else {
                    if (run.length() >= 6) strings.add(run.toString());
                    run.setLength(0);
                }
            }
            if (run.length() >= 6) strings.add(run.toString());

            StringBuilder out = new StringBuilder();
            int kept = 0;
            for (String s : strings) {
                boolean interesting = reason == ApplicationExitInfo.REASON_ANR
                        || s.contains(".so") || s.contains("signal") || s.contains("abort") || s.contains("Abort");
                if (!interesting) continue;
                out.append(cut(s, TRACE_LINE_CHARS)).append('\n');
                if (++kept >= 40) break;
            }
            return out.toString();
        } catch (Throwable t) {
            return "";
        } finally {
            if (in != null) {
                try { in.close(); } catch (IOException ignored) { }
            }
        }
    }

    /**
     * The end of the game's log as text, for a message the player shares on purpose (no unexpected end behind it): the last
     * {@code count} lines, each cut at {@code chars}. Empty when the game keeps no log or has not written one.
     *
     * @param logPath the game's log, relative to the app's files directory ("" for none)
     */
    static String logTailText(Context context, String logPath, int count, int chars) {
        if (logPath == null || logPath.isEmpty()) return "";
        StringBuilder out = new StringBuilder();
        for (String line : logTail(new File(context.getFilesDir(), logPath), count, chars)) out.append(line).append('
');
        return out.toString();
    }

    /** The last {@code count} lines of the game's log, each cut at {@code chars}; none when it has none. */
    private static List<String> logTail(File log, int count, int chars) {
        List<String> lines = new ArrayList<String>();
        if (log == null) return lines;
        RandomAccessFile file = null;
        try {
            if (!log.isFile() || log.length() == 0) return lines;
            file = new RandomAccessFile(log, "r");
            long size = file.length();
            int take = (int) Math.min(size, 48 * 1024L);
            byte[] data = new byte[take];
            file.seek(size - take);
            file.readFully(data);
            String text = new String(data, "UTF-8");
            String[] all = text.split("\n");
            // The first line of a window into the middle of a file may be cut.
            int from = size > take ? 1 : 0;
            for (int i = Math.max(from, all.length - count); i < all.length; i++) {
                String line = all[i].replace("\r", "");
                if (line.isEmpty()) continue;
                lines.add(line.length() > chars ? line.substring(0, chars) : line);
            }
        } catch (Throwable t) {
            lines.clear();
        } finally {
            if (file != null) {
                try { file.close(); } catch (IOException ignored) { }
            }
        }
        return lines;
    }
}
