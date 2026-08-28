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

/* offsetof, for the layout pins at the bottom of this file. */
#include <stddef.h>

/* Version history. A plugin built against a different number is refused.
 *  10 - added getText and wasSubmitted to SupersonicScriptUI.
 *   9 - added mouseDelta and the cursor mode to SupersonicScriptInput.
 *   8 - added loadScene to SupersonicScriptWorld.
 *   7 - added SupersonicScriptWorld: contacts, spawn/destroy, velocity.
 *   6 - added SupersonicScriptData: authored parameters and per-entity state.
 *   5 - added the UI block.
 */
#define SUPERSONIC_SCRIPT_API_VERSION 11

/* Cursor modes, matching Supersonic::CursorMode. Plain ints because everything
 * across this boundary is POD, and named here so a plugin does not have to
 * remember which number means which.
 *
 * LOCKED is the one a mouse-look game wants: the pointer is hidden and held, so
 * it cannot reach the edge of the screen part-way through a turn. */
#define SUPERSONIC_CURSOR_NORMAL 0
#define SUPERSONIC_CURSOR_HIDDEN 1
#define SUPERSONIC_CURSOR_LOCKED 2

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

    /* How far the mouse moved this frame, in pixels, y growing downward.
     *
     * Not an axis: an axis is bipolar and clamped to -1..1, which is right for
     * a stick and wrong for a mouse, where the magnitude IS the movement and
     * there is no maximum. Without this a script could read every button on the
     * mouse and not one thing about where it went, so nothing a plugin could
     * write was able to look around. */
    void  (*mouseDelta)(void* opaque, float outDelta[2]);

    /* Ask for the pointer to be hidden, held, or given back. One of the
     * SUPERSONIC_CURSOR_* values above; anything else is ignored.
     *
     * A REQUEST. The editor takes the pointer back between plays and whenever
     * its viewport is not focused, and losing the window releases it outright -
     * otherwise a game that locked the pointer and offered no way out would trap
     * whoever ran it. The request is remembered across all of that, so a script
     * sets it once rather than fighting to hold it.
     *
     * cursorMode reports what is actually in force, which is what a script
     * should test before treating mouseDelta as a look. */
    void  (*setCursorMode)(void* opaque, int mode);
    int   (*cursorMode)(void* opaque);
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

/* Animation control.
 *
 * Cross-fading exists in the engine, and without this nothing in a running
 * game could trigger it: only the inspector could change an animator's clip,
 * which is no use to a character that should switch to a run when it starts
 * moving.
 *
 * isPlaying takes the name rather than returning one, so no pointer to engine
 * storage crosses the boundary and there is nothing for a plugin to hold on to
 * after the call returns.
 *
 * play does nothing for an entity with no animator, and requesting the clip
 * that is already playing is ignored rather than restarting it - otherwise a
 * script calling play every frame would hold the animation on frame zero. */
typedef struct SupersonicScriptAnimation {
    void* opaque;
    void (*play)(void* opaque, unsigned int entity, const char* clipName);
    int (*isPlaying)(void* opaque, unsigned int entity, const char* clipName);
} SupersonicScriptAnimation;

/* The user interface.
 *
 * A button could be pressed and nothing in the game could find out, and a score
 * could be drawn but never counted: the UI was one-way. Entities are named by
 * the same unsigned int the rest of this header uses, so a script can hold the
 * id of its menu button the way it holds any other.
 *
 * setText copies the string immediately; the pointer is not retained, so a
 * script may pass a stack buffer it is about to overwrite. */
typedef struct SupersonicScriptUI {
    void* opaque;
    int (*wasClicked)(void* opaque, unsigned int entity);
    int (*isHovered)(void* opaque, unsigned int entity);
    void (*setText)(void* opaque, unsigned int entity, const char* text);
    void (*setVisible)(void* opaque, unsigned int entity, int visible);
    void (*setFill)(void* opaque, unsigned int entity, float fill);

    /* Reads what somebody typed into a text field, into a buffer the CALLER
     * owns - nothing engine-side may be pointed at across this boundary, and a
     * std::string certainly may not.
     *
     * Copies as much as fits and always NUL-terminates. If the value is longer
     * than the buffer it is cut on a CHARACTER boundary, never through the
     * middle of one: a truncation that split a multi-byte letter would hand the
     * plugin bytes that are not valid UTF-8, and the plugin has no way to know.
     *
     * Returns the number of bytes written, not counting the terminator. */
    int (*getText)(void* opaque, unsigned int entity, char* out, int capacity);

    /* Enter was pressed in that field this frame. True for exactly one frame,
     * like wasClicked.
     *
     * Its own entry rather than riding wasClicked, even though both mean "the
     * player is done with this element": a plugin author reading the header has
     * no way to guess that a field reports a submit as a click, and saying what
     * it does is the header's whole job. */
    int (*wasSubmitted)(void* opaque, unsigned int entity);
} SupersonicScriptUI;

/* Authored parameters, and scratch that survives a reload.
 *
 * Both are ENGINE-owned storage. That is the whole point: the plugin is
 * unloaded and reloaded while the process keeps running, so a counter the
 * plugin allocated would dangle the moment the DLL is freed. Keeping it on this
 * side means a script keeps its state across a rebuild, which is what makes hot
 * reload feel like editing a running game rather than restarting one.
 *
 * Named, not indexed, matching how input actions are already passed. Adding a
 * parameter therefore needs no ABI change and no engine recompile.
 *
 * param() reads what the inspector authored and is read-only - a script that
 * could write its own configuration would fight the person editing it.
 * getState/setState are the script's own scratch, and are not serialised.
 */
typedef struct SupersonicScriptData {
    void* opaque;
    float (*param)(void* opaque, unsigned int entity, const char* name, float fallback);
    float (*getState)(void* opaque, unsigned int entity, const char* name, float fallback);
    void  (*setState)(void* opaque, unsigned int entity, const char* name, float value);
} SupersonicScriptData;

/* The world: what touched me, what I want to exist, and how I am moving.
 *
 * CONTACTS. PhysicsSystem has always produced these; nothing read them. They
 * went to the editor as a count, so a trigger volume could not fire - the
 * engine knew the player had entered it and threw the fact away.
 *
 * Queried rather than delivered by callback, deliberately. A second entry point
 * into the plugin would have to be resolved, checked and re-resolved on every
 * reload, and would fire at a different point in the frame from update(). A
 * script asks during its own update instead, which needs no new symbol and
 * cannot be half-swapped mid-reload.
 *
 * phase is 0 enter, 1 stay, 2 exit. Exit reports a zero normal, because the
 * bodies are apart and any direction would be invented. The normal always
 * points AWAY from the entity asking.
 *
 * SPAWN AND DESTROY are queued, not immediate. Both mutate the registry, and a
 * script runs inside a view over that registry - creating an entity there
 * invalidates the iteration the caller is in the middle of. They take effect at
 * the frame boundary, so spawn() returns no id: the entity does not exist yet.
 *
 * VELOCITY is read and written directly, because the solver reads it at the
 * start of the next step and nothing is iterating it. Note this is the one part
 * of this block that a deterministic fixed-tick simulation should NOT use: a
 * script pushing a body the sim also owns makes the outcome depend on the order
 * the two ran in.
 */
typedef struct SupersonicScriptWorld {
    void* opaque;

    int  (*contactCount)(void* opaque, unsigned int entity);
    int  (*contactAt)(void* opaque, unsigned int entity, int index,
                      unsigned int* outOther, float outNormal[3],
                      int* outPhase, int* outIsTrigger);

    void (*spawnPrefab)(void* opaque, const char* prefabPath, const float position[3]);
    void (*destroyEntity)(void* opaque, unsigned int entity);

    void (*getVelocity)(void* opaque, unsigned int entity, float outVelocity[3]);
    void (*setVelocity)(void* opaque, unsigned int entity, const float velocity[3]);
    void (*addForce)(void* opaque, unsigned int entity, const float force[3]);

    /* Queue a scene load. Takes effect at the same frame boundary spawn and
     * destroy do, and for a stronger version of the same reason: a load clears
     * and refills the registry, and a script runs inside a view over it.
     *
     * The path is copied, not retained. A script may hand over a string that
     * lives on its own stack.
     *
     * Returns nothing and reports nothing, because there is nothing useful to
     * say yet: the file is not opened until the boundary. A load that fails
     * leaves the current scene open and says so in the log. */
    void (*loadScene)(void* opaque, const char* scenePath);
} SupersonicScriptWorld;

/* Per-entity state handed to a script each frame. The engine copies values in
 * before the call and copies them back out afterwards. */
typedef struct SupersonicScriptContext {
    /* THE FIXED TICK, not the frame. Scripts run inside the simulation loop
     * as of API 11, so this is the authored tick length and is the same number
     * on every machine - which is what makes a script's motion reproducible
     * rather than a function of how fast the display is keeping up.
     *
     * The version bump is for this line and not for any struct member: the
     * layout is unchanged and the MEANING is not, which is worse, because a
     * version check cannot see it and nothing about a stale plugin would look
     * wrong. It would simply move at the wrong speed. */
    float deltaTime;      /* seconds of simulated time this tick             */
    float elapsed;        /* seconds this script has been running         */
    float position[3];    /* in/out                                        */
    float rotation[3];    /* in/out, radians, Euler XYZ                    */
    float scale[3];       /* in/out                                        */
    unsigned int entityId; /* opaque; stable for the lifetime of the entity */

    /* Never null. Valid only for the duration of the call. */
    const SupersonicScriptInput* input;
    const SupersonicScriptPhysics* physics;
    const SupersonicScriptAnimation* animation;
    const SupersonicScriptUI* ui;
    const SupersonicScriptData* data;
    const SupersonicScriptWorld* world;
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

/* ------------------------------------------------------------------------
 * Layout pins.
 *
 * The instruction at the top of this file - bump the version on any change to
 * the structs - was enforced by an integer comparison and nothing else. The
 * engine checks that the plugin reports the same SUPERSONIC_SCRIPT_API_VERSION
 * it was built with, which catches a plugin someone remembered to bump and is
 * silent about the case that actually happens.
 *
 * That case is the normal hot-reload workflow. Rebuilding ONLY the plugin while
 * the engine keeps running is the entire point of the feature, so editing a
 * struct here and rebuilding the plugin target gives two matching version
 * integers and two different memory layouts. position[3] is then copied in and
 * back out, every frame, per scripted entity, through fields that are no longer
 * where the other side believes they are.
 *
 * These pins make that a compile error in the plugin instead. Change a struct
 * without bumping the version and the plugin stops building, which is the only
 * moment anyone is in a position to notice.
 *
 * The sizes are expressed in sizeof(void*) rather than absolute bytes so a
 * 32-bit target is not held to 64-bit numbers. They do assume a function
 * pointer is the same width as a data pointer, which is true everywhere this
 * engine builds; if that ever stops being true, this failing is the signal to
 * look rather than something to widen.
 */
#ifdef __cplusplus
#  define SUPERSONIC_ABI_ASSERT(cond, msg) static_assert(cond, msg)
#else
#  define SUPERSONIC_ABI_ASSERT(cond, msg) _Static_assert(cond, msg)
#endif

#if SUPERSONIC_SCRIPT_API_VERSION == 10

SUPERSONIC_ABI_ASSERT(sizeof(SupersonicScriptInput) == 8 * sizeof(void*),
    "SupersonicScriptInput changed; bump SUPERSONIC_SCRIPT_API_VERSION");
SUPERSONIC_ABI_ASSERT(sizeof(SupersonicScriptPhysics) == 3 * sizeof(void*),
    "SupersonicScriptPhysics changed; bump SUPERSONIC_SCRIPT_API_VERSION");
SUPERSONIC_ABI_ASSERT(sizeof(SupersonicScriptAnimation) == 3 * sizeof(void*),
    "SupersonicScriptAnimation changed; bump SUPERSONIC_SCRIPT_API_VERSION");
SUPERSONIC_ABI_ASSERT(sizeof(SupersonicScriptUI) == 8 * sizeof(void*),
    "SupersonicScriptUI changed; bump SUPERSONIC_SCRIPT_API_VERSION");
SUPERSONIC_ABI_ASSERT(sizeof(SupersonicScriptData) == 4 * sizeof(void*),
    "SupersonicScriptData changed; bump SUPERSONIC_SCRIPT_API_VERSION");
SUPERSONIC_ABI_ASSERT(sizeof(SupersonicScriptWorld) == 9 * sizeof(void*),
    "SupersonicScriptWorld changed; bump SUPERSONIC_SCRIPT_API_VERSION");
SUPERSONIC_ABI_ASSERT(sizeof(SupersonicScriptHost) == 3 * sizeof(void*),
    "SupersonicScriptHost changed; bump SUPERSONIC_SCRIPT_API_VERSION");

/* The context is pinned member by member, not just by total size: reordering
 * two float[3] fields leaves sizeof unchanged and swaps rotation with scale. */
SUPERSONIC_ABI_ASSERT(offsetof(SupersonicScriptContext, deltaTime) == 0,  "context layout changed");
SUPERSONIC_ABI_ASSERT(offsetof(SupersonicScriptContext, elapsed)   == 4,  "context layout changed");
SUPERSONIC_ABI_ASSERT(offsetof(SupersonicScriptContext, position)  == 8,  "context layout changed");
SUPERSONIC_ABI_ASSERT(offsetof(SupersonicScriptContext, rotation)  == 20, "context layout changed");
SUPERSONIC_ABI_ASSERT(offsetof(SupersonicScriptContext, scale)     == 32, "context layout changed");
SUPERSONIC_ABI_ASSERT(offsetof(SupersonicScriptContext, entityId)  == 44, "context layout changed");
SUPERSONIC_ABI_ASSERT(offsetof(SupersonicScriptContext, input) == 48,
    "context layout changed; bump SUPERSONIC_SCRIPT_API_VERSION");
SUPERSONIC_ABI_ASSERT(
    sizeof(SupersonicScriptContext) == 48 + 6 * sizeof(void*),
    "SupersonicScriptContext changed; bump SUPERSONIC_SCRIPT_API_VERSION");

#endif /* SUPERSONIC_SCRIPT_API_VERSION == 10 */

#ifdef __cplusplus
}
#endif

#endif /* SUPERSONIC_SCRIPT_PLUGIN_API_H */
