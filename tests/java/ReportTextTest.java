package com.ivancvetanovic.supersonic;

import java.nio.charset.StandardCharsets;
import java.util.LinkedHashMap;
import java.util.Map;

/**
 * Host test for ReportText: the choice of the game's name and the words of the Android crash report's title.
 *
 * WHY IT EXISTS. PostMortem titled every game's report "Penumbra closed unexpectedly last time", so a tester of
 * another game was shown Penumbra's name. The wording moved into ReportText, a class with no android import, so that
 * this file can compile and run it with a JDK alone (tests/java/run_java_test.cmake, registered as the ctest
 * test_reporttext); PostMortem itself needs a device and is only compile-checked against android.jar.
 *
 * The same harness shape as tests/TestHarness.hpp: a pin is a named group of checks, a failure prints the pin's name,
 * and a floor on the number of checks turns a run that quietly checked nothing into a red one.
 *
 * This file avoids the backslash-u escape on purpose: the accent is built from its code (0xFA) so the source reads
 * the same under any default encoding, and the expected bytes of Penumbra's title are a hex string.
 */
public final class ReportTextTest {

    /** Below this a run is not a pass: it skipped work. The real count is far above it. */
    private static final int MIN_CHECKS = 60;

    private static final String U_ACUTE = String.valueOf((char) 0xFA);
    private static final String ENGLISH_SUFFIX = " closed unexpectedly last time";
    private static final String PORTUGUESE_SUFFIX = " fechou sozinho da " + U_ACUTE + "ltima vez";

    // HEAD's two strings, hand-copied from PostMortem.java before the name moved out of them, as UTF-8 bytes in hex
    // (the accent is C3 BA): what Penumbra's report must keep saying, to the byte.
    private static final String HEAD_PORTUGUESE_HEX =
            "4f2050656e756d62726120666563686f7520736f7a696e686f20646120c3ba6c74696d612076657a";
    private static final String HEAD_ENGLISH_HEX =
            "50656e756d62726120636c6f73656420756e65787065637465646c79206c6173742074696d65";
    private static final String HEAD_ENGLISH = "Penumbra closed unexpectedly last time";
    private static final String HEAD_PORTUGUESE = "O Penumbra fechou sozinho da " + U_ACUTE + "ltima vez";

    private interface Thunk {
        String get() throws Exception;
    }

    private static final class Pin {
        int checks;
        int failures;
    }

    private static final Map<String, Pin> PINS = new LinkedHashMap<String, Pin>();
    private static int checks;
    private static int failures;

    private ReportTextTest() {}

    public static void main(String[] args) {
        keyBeatsLabel();
        keyIsTrimmed();
        blankKeyFallsToLabel();
        labelIsTrimmed();
        noNameWhenBothAreAbsentOrBlank();
        innerTextIsKept();
        keyCase();
        labelCase();
        neutralCase();
        penumbraKeepsItsTitleToTheByte();
        portugueseAccentIsTheRealCharacter();
        portugueseArticleOnce();
        languagesDiffer();
        namesPassThroughUnformatted();
        aVeryLongNamePassesThrough();

        int pinsFailed = 0;
        for (Map.Entry<String, Pin> entry : PINS.entrySet()) {
            Pin pin = entry.getValue();
            if (pin.failures > 0) pinsFailed++;
            System.out.println("  pin " + entry.getKey() + ": " + pin.checks + " checks, " + pin.failures + " failed");
        }
        System.out.println("ReportTextTest: " + checks + " checks, " + failures + " failures, " + PINS.size()
                + " pins, " + pinsFailed + " pins red");
        if (checks < MIN_CHECKS) {
            System.out.println("  FAIL ReportTextTest ran " + checks + " checks, expected at least " + MIN_CHECKS
                    + " - the suite skipped work");
            System.exit(1);
        }
        System.exit(failures == 0 ? 0 : 1);
    }

    // ---- the choice of the name -------------------------------------------------------------------------------

    private static void keyBeatsLabel() {
        String pin = "key-beats-label";
        same(pin, "Alpha", () -> ReportText.chooseName("Alpha", "Beta"));
        same(pin, "Alpha", () -> ReportText.chooseName("Alpha", null));
        same(pin, "Alpha", () -> ReportText.chooseName("Alpha", "Alpha Enhanced"));
        same(pin, "Beta", () -> ReportText.chooseName("Beta", "Alpha"));
    }

    private static void keyIsTrimmed() {
        String pin = "key-is-trimmed";
        same(pin, "Alpha", () -> ReportText.chooseName("  Alpha ", "Beta"));
        same(pin, "Alpha", () -> ReportText.chooseName(ws() + "Alpha" + ws(), "Beta"));
        same(pin, "Alpha", () -> ReportText.chooseName("Alpha ", null));
    }

    private static void blankKeyFallsToLabel() {
        String pin = "blank-key-falls-to-label";
        same(pin, "Beta", () -> ReportText.chooseName("", "Beta"));
        same(pin, "Beta", () -> ReportText.chooseName(null, "Beta"));
        same(pin, "Beta", () -> ReportText.chooseName(" ", "Beta"));
        same(pin, "Beta", () -> ReportText.chooseName(ws(), "Beta"));
        same(pin, "Beta", () -> ReportText.chooseName(ws() + ws() + ws(), "Beta"));
    }

    private static void labelIsTrimmed() {
        String pin = "label-is-trimmed";
        same(pin, "Beta", () -> ReportText.chooseName(null, "  Beta  "));
        same(pin, "Beta", () -> ReportText.chooseName("", ws() + "Beta" + ws()));
        same(pin, "Beta", () -> ReportText.chooseName("   ", "Beta\n"));
    }

    private static void noNameWhenBothAreAbsentOrBlank() {
        String pin = "no-name-when-both-absent-or-blank";
        same(pin, "", () -> ReportText.chooseName(null, null));
        same(pin, "", () -> ReportText.chooseName("", null));
        same(pin, "", () -> ReportText.chooseName(null, ""));
        same(pin, "", () -> ReportText.chooseName("   ", ws()));
        same(pin, "", () -> ReportText.chooseName(ws(), "   "));
        same(pin, "", () -> ReportText.chooseName("", ""));
    }

    private static void innerTextIsKept() {
        String pin = "inner-text-is-kept";
        same(pin, "Magic  Portals", () -> ReportText.chooseName("Magic  Portals", null));
        same(pin, "Magic  Portals", () -> ReportText.chooseName(null, " Magic  Portals "));
        same(pin, "mAgIc pOrTaLs", () -> ReportText.chooseName("mAgIc pOrTaLs", "x"));
        same(pin, "a" + (char) 9 + "b", () -> ReportText.chooseName("a" + (char) 9 + "b", null));
        same(pin, "Magic Portals Enhanced", () -> ReportText.chooseName(null, "Magic Portals Enhanced"));
    }

    // ---- the title, in the three cases of the order of choice ------------------------------------------------------

    private static void keyCase() {
        String pin = "key-case";
        same(pin, "Magic Portals" + ENGLISH_SUFFIX,
                () -> ReportText.title(ReportText.chooseName("Magic Portals", "Magic Portals Enhanced"), false));
        same(pin, "O Magic Portals" + PORTUGUESE_SUFFIX,
                () -> ReportText.title(ReportText.chooseName("Magic Portals", "Magic Portals Enhanced"), true));
        same(pin, "Magic Portals closed unexpectedly last time",
                () -> ReportText.title(ReportText.chooseName("  Magic Portals  ", "label"), false));
    }

    private static void labelCase() {
        String pin = "label-case";
        same(pin, "Magic Portals Enhanced" + ENGLISH_SUFFIX,
                () -> ReportText.title(ReportText.chooseName(null, "Magic Portals Enhanced"), false));
        same(pin, "O Magic Portals Enhanced" + PORTUGUESE_SUFFIX,
                () -> ReportText.title(ReportText.chooseName(null, "Magic Portals Enhanced"), true));
        same(pin, "Magic Portals Enhanced closed unexpectedly last time",
                () -> ReportText.title(ReportText.chooseName("   ", " Magic Portals Enhanced "), false));
        same(pin, "O Magic Portals Enhanced fechou sozinho da " + U_ACUTE + "ltima vez",
                () -> ReportText.title(ReportText.chooseName("", " Magic Portals Enhanced "), true));
    }

    private static void neutralCase() {
        String pin = "neutral-case";
        same(pin, "The game closed unexpectedly last time",
                () -> ReportText.title(ReportText.chooseName(null, null), false));
        same(pin, "O jogo fechou sozinho da " + U_ACUTE + "ltima vez",
                () -> ReportText.title(ReportText.chooseName(null, null), true));
        same(pin, "The game closed unexpectedly last time",
                () -> ReportText.title(ReportText.chooseName("  ", ws()), false));
        same(pin, "O jogo fechou sozinho da " + U_ACUTE + "ltima vez",
                () -> ReportText.title(ReportText.chooseName(ws(), "   "), true));
        // title() reads a missing or blank name as no name, whoever passes it.
        same(pin, "The game closed unexpectedly last time", () -> ReportText.title(null, false));
        same(pin, "The game closed unexpectedly last time", () -> ReportText.title("", false));
        same(pin, "The game closed unexpectedly last time", () -> ReportText.title("   ", false));
        same(pin, "O jogo fechou sozinho da " + U_ACUTE + "ltima vez", () -> ReportText.title(null, true));
        same(pin, "O jogo fechou sozinho da " + U_ACUTE + "ltima vez", () -> ReportText.title("", true));
        same(pin, "O jogo fechou sozinho da " + U_ACUTE + "ltima vez", () -> ReportText.title(ws(), true));
        // The neutral words themselves, so that a change of wording is a decision and not an accident.
        same(pin, "The game", () -> ReportText.NEUTRAL_ENGLISH);
        same(pin, "jogo", () -> ReportText.NEUTRAL_PORTUGUESE);
    }

    // ---- Penumbra, which sets nothing and must not notice ------------------------------------------------------

    private static void penumbraKeepsItsTitleToTheByte() {
        String pin = "penumbra-identical-to-head";
        // As strings.
        same(pin, HEAD_ENGLISH, () -> ReportText.title("Penumbra", false));
        same(pin, HEAD_PORTUGUESE, () -> ReportText.title("Penumbra", true));
        // As bytes: the hex of HEAD's strings, so that a wrong-encoding compile of this very file cannot agree with a
        // wrong-encoding compile of ReportText.
        same(pin, HEAD_ENGLISH_HEX, () -> hex(ReportText.title("Penumbra", false)));
        same(pin, HEAD_PORTUGUESE_HEX, () -> hex(ReportText.title("Penumbra", true)));
        // Through the choice, as Penumbra reaches it: no key, the label "Penumbra".
        same(pin, HEAD_ENGLISH, () -> ReportText.title(ReportText.chooseName(null, "Penumbra"), false));
        same(pin, HEAD_PORTUGUESE, () -> ReportText.title(ReportText.chooseName(null, "Penumbra"), true));
        same(pin, HEAD_ENGLISH, () -> ReportText.title(ReportText.chooseName("", "Penumbra"), false));
        same(pin, HEAD_PORTUGUESE, () -> ReportText.title(ReportText.chooseName("  ", " Penumbra "), true));
        // And with a key that says the same.
        same(pin, HEAD_ENGLISH, () -> ReportText.title(ReportText.chooseName("Penumbra", "whatever"), false));
        same(pin, HEAD_PORTUGUESE, () -> ReportText.title(ReportText.chooseName("Penumbra", "whatever"), true));
    }

    private static void portugueseAccentIsTheRealCharacter() {
        String pin = "portuguese-accent-is-utf8-c3-ba";
        String title = ReportText.title("Penumbra", true);
        int at = title.indexOf("ltima vez") - 1;
        check(pin, at > 0 && title.charAt(at) == (char) 0xFA, "the character before \"ltima vez\" is not U+00FA");
        check(pin, title.indexOf((char) 0xC3) < 0, "a mojibake lead character (U+00C3) is in the title");
        check(pin, title.indexOf((char) 0xFFFD) < 0, "a replacement character (U+FFFD) is in the title");
        check(pin, title.indexOf('?') < 0, "a question mark stands where the accent should be");
        byte[] bytes = title.getBytes(StandardCharsets.UTF_8);
        int pairs = 0;
        for (int i = 0; i + 1 < bytes.length; i++) {
            if ((bytes[i] & 0xFF) == 0xC3 && (bytes[i + 1] & 0xFF) == 0xBA) pairs++;
        }
        check(pin, pairs == 1, "UTF-8 pair C3 BA appears " + pairs + " times, expected once");
        check(pin, bytes.length == HEAD_PORTUGUESE_HEX.length() / 2, "title is " + bytes.length + " bytes, expected "
                + HEAD_PORTUGUESE_HEX.length() / 2);
    }

    private static void portugueseArticleOnce() {
        String pin = "portuguese-article-once";
        String[] names = {"Penumbra", "Magic Portals Enhanced", "A"};
        for (String name : names) {
            String title = ReportText.title(name, true);
            check(pin, title.startsWith("O " + name + " "), "\"" + title + "\" does not start with the article and the name");
            check(pin, !title.startsWith("O O "), "\"" + title + "\" doubles the article");
        }
        String neutral = ReportText.title(null, true);
        check(pin, neutral.startsWith("O jogo "), "\"" + neutral + "\" does not start with \"O jogo\"");
        check(pin, !neutral.startsWith("O O "), "\"" + neutral + "\" doubles the article");
        check(pin, !ReportText.title("Penumbra", false).startsWith("O "), "the English title starts with the Portuguese article");
    }

    private static void languagesDiffer() {
        String pin = "languages-differ";
        String[] names = {"Penumbra", "Magic Portals Enhanced"};
        for (String name : names) {
            String english = ReportText.title(name, false);
            String portuguese = ReportText.title(name, true);
            check(pin, !english.equals(portuguese), "both languages gave \"" + english + "\"");
            check(pin, english.endsWith(ENGLISH_SUFFIX), "\"" + english + "\" does not end with the English sentence");
            check(pin, portuguese.endsWith(PORTUGUESE_SUFFIX), "\"" + portuguese + "\" does not end with the Portuguese sentence");
        }
        check(pin, !ReportText.title(null, false).equals(ReportText.title(null, true)), "the neutral titles are one language");
    }

    // ---- a name is data, never a format pattern ----------------------------------------------------------------

    private static String[] awkwardNames() {
        String bs = String.valueOf((char) 92);
        return new String[] {
            "It's \"quoted\" 'twice'",
            "'''",
            "\"",
            "100% sure",
            "%s %d %n %% %",
            "%1$s %2$s %-5.3f",
            "{0} {1} {name} {",
            "}{ '{0}' ''",
            "Pok" + (char) 0xE9 + "mon " + (char) 0xC1 + "rvore Cora" + (char) 0xE7 + (char) 0xE3 + "o",
            "back" + bs + "slash " + bs + bs + " " + bs + "n",
            "tab" + (char) 9 + "inside",
            "line" + (char) 10 + "break",
            new String(Character.toChars(0x1F3AE)) + " game " + new String(Character.toChars(0x1F3AE)),
        };
    }

    private static void namesPassThroughUnformatted() {
        // One pin for each kind of character that a format pattern would eat or reject, so a join through
        // String.format or MessageFormat is caught by the kind it breaks.
        String[] names = awkwardNames();
        int index = 0;
        for (final String name : names) {
            String pin;
            if (index < 3) pin = "verbatim-quotes";
            else if (index < 6) pin = "verbatim-percent";
            else if (index < 8) pin = "verbatim-braces";
            else if (index < 9) pin = "verbatim-accents";
            else pin = "verbatim-control-and-backslash";
            same(pin, name + ENGLISH_SUFFIX, () -> ReportText.title(name, false));
            same(pin, "O " + name + PORTUGUESE_SUFFIX, () -> ReportText.title(name, true));
            // Through the choice as well: a name that comes out of the manifest key keeps its characters.
            if (name.trim().equals(name)) {
                same(pin, name, () -> ReportText.chooseName(name, "label"));
                same(pin, name, () -> ReportText.chooseName(null, name));
            }
            index++;
        }
    }

    private static void aVeryLongNamePassesThrough() {
        String pin = "verbatim-very-long";
        int[] lengths = {300, 5000, 200000};
        for (int length : lengths) {
            StringBuilder builder = new StringBuilder(length);
            for (int i = 0; i < length; i++) builder.append((char) ('a' + i % 26));
            final String name = builder.toString();
            same(pin, name + ENGLISH_SUFFIX, () -> ReportText.title(name, false));
            same(pin, "O " + name + PORTUGUESE_SUFFIX, () -> ReportText.title(name, true));
            same(pin, name, () -> ReportText.chooseName(name, null));
        }
    }

    // ---- the harness -------------------------------------------------------------------------------------------

    /** Spaces and the control whitespace a manifest value can carry around a name. */
    private static String ws() {
        return " " + (char) 9 + (char) 10 + (char) 13 + " ";
    }

    private static String hex(String text) {
        StringBuilder out = new StringBuilder();
        for (byte b : text.getBytes(StandardCharsets.UTF_8)) {
            out.append(Character.forDigit((b >> 4) & 0xF, 16)).append(Character.forDigit(b & 0xF, 16));
        }
        return out.toString();
    }

    /** Only printable ASCII reaches the output, whatever the console's encoding: anything else as U+XXXX. */
    private static String ascii(String text) {
        if (text == null) return "null";
        StringBuilder out = new StringBuilder();
        int shown = Math.min(text.length(), 160);
        for (int i = 0; i < shown; i++) {
            char c = text.charAt(i);
            if (c >= 32 && c < 127) out.append(c);
            else out.append("<U+").append(String.format("%04X", (int) c)).append('>');
        }
        if (text.length() > shown) out.append("... (").append(text.length()).append(" chars)");
        return out.toString();
    }

    private static void check(String pin, boolean ok, String detail) {
        Pin entry = PINS.get(pin);
        if (entry == null) {
            entry = new Pin();
            PINS.put(pin, entry);
        }
        entry.checks++;
        checks++;
        if (ok) return;
        entry.failures++;
        failures++;
        System.out.println("  FAIL " + pin + ": " + ascii(detail));
    }

    /** The value must equal {@code expected}; an exception is a failure of the pin, not the end of the run. */
    private static void same(String pin, String expected, Thunk actual) {
        String got;
        try {
            got = actual.get();
        } catch (Throwable thrown) {
            check(pin, false, "threw " + thrown + " (expected [" + expected + "])");
            return;
        }
        check(pin, expected.equals(got), "expected [" + expected + "] got [" + got + "]");
    }
}
