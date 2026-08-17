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

#define SUPERSONIC_SCRIPT_API_VERSION 3

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

/* World queries, so a script can ask what is in front of it or whether it is
 * standing on anything.
 *
 * Same shape as the input block and for the same reason: engine-owned function
 * pointers, POD arguments, no ownership crossing. Entity ids travel as the same
 * unsigned int the context already uses, so a hit can be compared against
 * context->entityId directly.
 *
 * raycast returns 1 on a hit and fills the out parameters; every out pointer
 * may be null if the caller does not want that field. */
typedef struct SupersonicScriptPhysics {
    void* opaque;
    int (*raycast)(void* opaque,
                   const float origin[3], const float direction[3], float maxDistance,
                   unsigned int ignoreEntity,
                   float outPoint[3], float outNormal[3], float* outDistance,
                   unsigned int* outEntity);
    int (*isGrounded)(void* opaque, const float position[3], float distance,
                      unsigned int ignoreEntity);
} SupersonicScriptPhysics;

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
    const SupersonicScriptPhysics* physics;
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
