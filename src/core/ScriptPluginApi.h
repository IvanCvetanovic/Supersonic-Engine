#ifndef ENGINE_SCRIPT_PLUGIN_API_H
#define ENGINE_SCRIPT_PLUGIN_API_H

/*
 * Stable C ABI between the engine and a hot-reloadable script plugin.
 *
 * Deliberately POD-only and C-linkage. Nothing with a C++ layout, no
 * std::string, no virtuals and no ownership crosses this boundary, because the
 * plugin is unloaded and reloaded while the process keeps running: anything
 * whose layout or vtable lives in the plugin would dangle the moment the DLL is
 * freed. Scripts read and write a flat context and return.
 *
 * Bump ENGINE_SCRIPT_API_VERSION on any change to the structs below. The engine
 * refuses to load a plugin whose version does not match, which turns a stale
 * build into a clear log line instead of undefined behaviour.
 */

#define ENGINE_SCRIPT_API_VERSION 1

#if defined(_WIN32)
#  define ENGINE_SCRIPT_EXPORT __declspec(dllexport)
#else
#  define ENGINE_SCRIPT_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Per-entity state handed to a script each frame. The engine copies values in
 * before the call and copies them back out afterwards. */
typedef struct EngineScriptContext {
    float deltaTime;      /* seconds since the previous update            */
    float elapsed;        /* seconds this script has been running         */
    float position[3];    /* in/out                                        */
    float rotation[3];    /* in/out, radians, Euler XYZ                    */
    float scale[3];       /* in/out                                        */
    unsigned int entityId; /* opaque; stable for the lifetime of the entity */
} EngineScriptContext;

typedef void (*EngineScriptUpdateFn)(EngineScriptContext* context);

/* Passed to the plugin so it can announce what it provides. */
typedef struct EngineScriptHost {
    int apiVersion;
    void* opaque;
    void (*registerScript)(void* opaque, const char* name, EngineScriptUpdateFn update);
} EngineScriptHost;

/* Every plugin must export exactly these two symbols. */
typedef int  (*EngineScriptPluginVersionFn)(void);
typedef void (*EngineScriptPluginRegisterFn)(EngineScriptHost* host);

#define ENGINE_SCRIPT_PLUGIN_VERSION_SYMBOL  "EngineScriptPluginVersion"
#define ENGINE_SCRIPT_PLUGIN_REGISTER_SYMBOL "EngineScriptPluginRegister"

#ifdef __cplusplus
}
#endif

#endif /* ENGINE_SCRIPT_PLUGIN_API_H */
