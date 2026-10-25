package com.ivancvetanovic.supersonic;

/**
 * THE WORDS OF THE REPORT THAT NAME THE GAME, apart from PostMortem so that a plain JDK can run them.
 *
 * The report's title used to say "Penumbra closed unexpectedly last time" for every game built on the engine, so
 * a tester of another game was shown Penumbra's name. The title now carries the game's own name; choosing that name
 * and wording the title are two pure functions here, with no android import, and tests/java/ReportTextTest.java
 * compiles and runs them with javac and java alone (it is registered as the ctest test_reporttext). PostMortem and
 * SupersonicActivity are the glue: they fetch the manifest's key and the application's label, and hand them on.
 *
 * Everything here must stay Java 8 (the packager compiles with -source 8 against the platform's android.jar, whose
 * level is not the host JDK's): trim().isEmpty(), not isBlank or strip.
 */
final class ReportText {

    // What the title says when nothing names the game. The Portuguese one is the bare noun: its article is part of
    // the sentence, so that "O Penumbra" and "O jogo" are one sentence with a different name in it.
    static final String NEUTRAL_ENGLISH = "The game";
    static final String NEUTRAL_PORTUGUESE = "jogo";

    private ReportText() {}

    /**
     * The name the title uses, in this order: {@code declared} (the manifest meta-data supersonic.reportTitle) when
     * it is present and not blank, else {@code label} (the application's label as the system answers it: its
     * android:label, or the class or package name when the manifest declares none) when that is present and not
     * blank, else the empty string, which {@link #title} reads as "no name" and answers with the neutral word; that
     * is, the neutral word is for a label that could not be read. Both are trimmed; nothing else is touched, so a
     * name keeps its inner spacing, case and punctuation.
     *
     * The neutral word is not chosen here because it is not one word: it depends on the language, and the Portuguese
     * sentence puts an article in front of it.
     */
    static String chooseName(String declared, String label) {
        String fromManifest = trimmed(declared);
        if (!fromManifest.isEmpty()) return fromManifest;
        return trimmed(label);
    }

    /**
     * The title of the report: "{name} closed unexpectedly last time", or in Portuguese "O {name} fechou sozinho da
     * \u00FAltima vez" (the article stays in front of the name, which is what keeps Penumbra's title, whose android:label
     * is "Penumbra", byte for byte what it was when the name was written into the sentence). A null or blank
     * {@code name} gives the neutral word instead: "The game" and "O jogo".
     *
     * The name is joined by plain concatenation. It comes from a manifest a game author wrote, and may hold a quote,
     * a percent sign or a brace; used as a format pattern (String.format, MessageFormat) any of them would throw or
     * be eaten. The accent of the last word is written as a unicode escape (the code of u with an acute) so that the source
     * reads the same under any default encoding of the machine that compiles it.
     */
    static String title(String name, boolean portuguese) {
        boolean named = name != null && !name.trim().isEmpty();
        if (portuguese) {
            return "O " + (named ? name : NEUTRAL_PORTUGUESE) + " fechou sozinho da \u00FAltima vez";
        }
        return (named ? name : NEUTRAL_ENGLISH) + " closed unexpectedly last time";
    }

    private static String trimmed(String text) {
        return text == null ? "" : text.trim();
    }
}
