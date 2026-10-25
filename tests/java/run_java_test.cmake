# Runs tests/java/ReportTextTest.java: the Android report's wording, which is Java and has no android import, so a JDK
# alone can check it. ctest calls this as
#
#   cmake -DJAVAC=<javac> -DJAVA=<java> -DENGINE_DIR=<engine root> -DWORK=<scratch directory> -P run_java_test.cmake
#
# THREE OUTCOMES, and none of them is a quiet pass:
#
#   no JDK            (nothing found, or a javac or java that does not answer -version: a macOS stub, a cached
#                     path whose JDK was deleted) prints "SKIPPED: test_reporttext needs a JDK ..." and ends with
#                     status 0. The test is registered with SKIP_REGULAR_EXPRESSION on that sentence, so ctest lists
#                     it as Skipped and names it under "The following tests did not run" (its percentage line still
#                     counts it among the passed). (A cmake -P script cannot end with a chosen status such as 77,
#                     which is what SKIP_RETURN_CODE would need.)
#   a JDK, but javac or the test fails
#                     FATAL_ERROR: ctest lists it as Failed. A JDK that is there and cannot compile the class is a
#                     broken class, not a machine that cannot check it.
#   a JDK and every pin green
#                     Passed, and the pins' counts are in the output.
#
# The class is compiled the way the Android packager compiles it where that matters to the wording: --release 8 holds
# it to the Java 8 library (the packager compiles -source 8 against android.jar, so a call such as isBlank or strip,
# which only a newer host JDK has, would pass here under a plain javac and fail there), -encoding UTF-8 holds the
# sources to the encoding the packager reads them in, -Xlint:all,-options -Werror holds it to no warnings (a newer
# JDK may add a lint category and turn it red; it was measured on JDK 17 only). "options" is left out of the lint:
# from JDK 21 --release 8 is itself reported as obsolete there, which says nothing about the class.

if (NOT JAVAC OR NOT JAVA)
    message("SKIPPED: test_reporttext needs a JDK (javac and java) and found none on JAVA_HOME or PATH when CMake "
            "configured, so the Android report's wording was NOT checked on this machine. Install a JDK (the Android "
            "packager wants 17, the one this was measured on; --release needs 9), set JAVA_HOME, and configure again.")
    return()
endif()

# A launcher that is on the PATH is not yet a JDK: macOS is reported to ship /usr/bin/java and /usr/bin/javac stubs
# that find_program happily returns and that only print "Unable to locate a Java Runtime" (not tried on a Mac here); a
# cached path can also outlive the JDK it named.
# Either tool failing to answer -version means there is no JDK to check with, which is a skip (and the sentence carries
# what the launcher said). Once both answer, anything that goes wrong below is a failure.
foreach (_tool IN ITEMS JAVAC JAVA)
    execute_process(COMMAND "${${_tool}}" -version
        RESULT_VARIABLE _versionStatus OUTPUT_VARIABLE _versionOutput ERROR_VARIABLE _versionErrors)
    if (NOT _versionStatus EQUAL 0)
        message("SKIPPED: test_reporttext needs a JDK (javac and java) and ${_tool} (${${_tool}}) did not answer -version "
                "(status: ${_versionStatus}; it said: ${_versionOutput}${_versionErrors}), so the Android report's wording "
                "was NOT checked on this machine.")
        return()
    endif()
endforeach()

if (NOT WORK)
    message(FATAL_ERROR "run_java_test.cmake needs -DWORK=<a scratch directory>")
endif()

set(_sources
    "${ENGINE_DIR}/src/platform/android/java/com/ivancvetanovic/supersonic/ReportText.java"
    "${ENGINE_DIR}/tests/java/ReportTextTest.java")

# A fresh directory every run, so a class file left by an earlier build cannot stand in for one that no longer compiles.
# Only the directory this script names is removed, never WORK itself: WORK is whatever a person typed after -DWORK= when
# running this by hand, and may be a directory with something in it.
set(_classes "${WORK}/reporttext-classes")
file(REMOVE_RECURSE "${_classes}")
file(MAKE_DIRECTORY "${_classes}")

execute_process(
    COMMAND "${JAVAC}" --release 8 -encoding UTF-8 -Xlint:all,-options -Werror -d "${_classes}" ${_sources}
    RESULT_VARIABLE _compileStatus
    OUTPUT_VARIABLE _compileOutput
    ERROR_VARIABLE _compileErrors)
if (NOT _compileStatus EQUAL 0)
    message(FATAL_ERROR "javac (${JAVAC}) failed with status ${_compileStatus}:\n${_compileOutput}${_compileErrors}")
endif()

execute_process(
    COMMAND "${JAVA}" -cp "${_classes}" com.ivancvetanovic.supersonic.ReportTextTest
    RESULT_VARIABLE _runStatus
    OUTPUT_VARIABLE _runOutput
    ERROR_VARIABLE _runErrors)
message("${_runOutput}${_runErrors}")
if (NOT _runStatus EQUAL 0)
    message(FATAL_ERROR "ReportTextTest (${JAVA}) failed with status ${_runStatus}")
endif()
# The test prints its own summary line; a run that exited 0 without one did not run the test.
if (NOT _runOutput MATCHES "ReportTextTest: [0-9]+ checks, 0 failures")
    message(FATAL_ERROR "ReportTextTest exited 0 but printed no summary line: it did not run")
endif()
