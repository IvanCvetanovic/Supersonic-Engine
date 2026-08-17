#ifndef SUPERSONIC_SCRIPT_PLUGIN_API_H
#define SUPERSONIC_SCRIPT_PLUGIN_API_H

/*
 * Stable C ABI between the engine and a hot-reloadable script plugin.
 *
 * Deliberately POD-only and C-linkage. Nothing with a C++ layout, no
 * std::string, no virtuals and no ownership crosses this boundary, because the
 * plugin is unloaded and reloaded while the process keeps running: anything
 * whose layout or vtable lives in the plugin would dangle the moment the DLL is
 * freed. Scripts read and write a flat context and return.
 *
 * Bump SUPERSONIC_SCRIPT_API_VERSION on any change to the structs below. The engine
 * refuses to load a plugin whose version does not match, which turns a stale
 * build into a clear log line instead of undefined behaviour.
 */

#define SUPERSONIC_SCRIPT_API_VERSION 2

#if defined(_WIN32)
#  define SUPERSONIC_SCRIPT_EXPORT __declspec(dllexport)
#else
#  define SUPERSONIC_SCRIPT_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Input, exposed to scripts as function pointers rather than as a snapshot
 * struct.
 *
 * The pointers are ENGINE functions, so unloading the plugin cannot dangle
 * them - the direction that matters here, since the plugin is the thing that
 * gets swapped. Names are passed as plain const char*, which keeps the
 * boundary POD and means adding an action needs no ABI change at all. */
typedef struct SupersonicScriptInput {
    void* opaque;
    int   (*isDown)(void* opaque, const char* action);
    int   (*wasPressed)(void* opaque, const char* action);
    int   (*wasReleased)(void* opaque, const char* action);
    float (*axis)(void* opaque, const char* axis);
} SupersonicScriptInput;

/* Per-entity state handed to a script each frame. The engine copies values in
 * before the call and copies them back out afterwards. */
typedef struct SupersonicScriptContext {
    float deltaTime;      /* seconds since the previous update            */
    float elapsed;        /* seconds this script has been running         */
    float position[3];    /* in/out                                        */
    float rotation[3];    /* in/out, radians, Euler XYZ                    */
    float scale[3];       /* in/out                                        */
    unsigned int entityId; /* opaque; stable for the lifetime of the entity */

    /* Never null. Valid only for the duration of the call. */
    const SupersonicScriptInput* input;
} SupersonicScriptContext;

typedef void (*SupersonicScriptUpdateFn)(SupersonicScriptContext* context);

/* Passed to the plugin so it can announce what it provides. */
typedef struct SupersonicScriptHost {
    int apiVersion;
    void* opaque;
    void (*registerScript)(void* opaque, const char* name, SupersonicScriptUpdateFn update);
} SupersonicScriptHost;

/* Every plugin must export exactly these two symbols. */
typedef int  (*SupersonicScriptPluginVersionFn)(void);
typedef void (*SupersonicScriptPluginRegisterFn)(SupersonicScriptHost* host);

#define SUPERSONIC_SCRIPT_PLUGIN_VERSION_SYMBOL  "SupersonicScriptPluginVersion"
#define SUPERSONIC_SCRIPT_PLUGIN_REGISTER_SYMBOL "SupersonicScriptPluginRegister"

#ifdef __cplusplus
}
#endif

#endif /* SUPERSONIC_SCRIPT_PLUGIN_API_H */
