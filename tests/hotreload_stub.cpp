// A shared library that is NOT a script plugin, for test_hotreload: it loads on
// every platform and has none of the plugin entry points, which is exactly a
// plugin that fails for a reason no retry will cure.
//
// It exists because a system library stood in before, and on Windows that was a
// renamed copy of kernel32.dll: loading the copy runs a second instance of
// kernel32's initialisation in the process, which is unsupported and crashed the
// suite. A library of our own carries no such state.
#if defined(_WIN32)
#define STUB_EXPORT __declspec(dllexport)
#else
#define STUB_EXPORT __attribute__((visibility("default")))
#endif

extern "C" STUB_EXPORT int SupersonicHotReloadStubIsNotAPlugin() { return 1; }
