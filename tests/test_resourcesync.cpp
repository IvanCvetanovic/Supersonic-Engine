// Tests for the decision that lets SyncResources skip an entity.
//
// Resolving an entity's mesh and texture ids means three hash-map lookups, and
// each builds its key by concatenating strings - a heap allocation per entity
// per lookup, every frame, to re-derive an answer that changes when someone
// edits a path and at no other time.
//
// Skipping that is only safe if the signature notices everything that can make
// the answer different, and the failure is silent in both directions: a
// signature that misses an input leaves an entity drawing the mesh it used to
// have, and one that never matches costs more than the lookups it replaced.
// Same reasoning as the shadow cache - the decision is separated from the work
// so the decision can be tested without a device.

#include "TestHarness.hpp"
#include "core/RenderSystem.hpp"
#include "core/Components.hpp"

#include <string>

using namespace Supersonic;

namespace {

MeshComponent mesh(const std::string& primitive, const std::string& path = {}) {
    MeshComponent component;
    component.primitiveType = primitive;
    component.filePath = path;
    return component;
}

MaterialComponent material(const std::string& albedo, const std::string& normal = {},
                          const std::string& orm = {}) {
    MaterialComponent component;
    component.albedoTexturePath = albedo;
    component.normalTexturePath = normal;
    component.ormTexturePath = orm;
    return component;
}

uint64_t sig(const MeshComponent* m, const MaterialComponent* mat,
             uint64_t meshGen = 1, uint64_t textureGen = 1, bool decodesColour = true) {
    return RenderSystem::ResourceSignature(m, mat, meshGen, textureGen, decodesColour);
}

} // namespace

static void testTheSameInputsGiveTheSameSignature() {
    // The half that makes the skip happen at all. Without this nothing is ever
    // skipped and the hash is pure cost.
    const MeshComponent m = mesh("Cube");
    const MaterialComponent mat = material("assets/textures/uv_grid.png");

    CHECK(sig(&m, &mat) == sig(&m, &mat));

    const MeshComponent same = mesh("Cube");
    const MaterialComponent sameMat = material("assets/textures/uv_grid.png");
    CHECK_MSG(sig(&m, &mat) == sig(&same, &sameMat),
              "equal values must agree, not merely the same objects");
}

static void testEveryPathThatSelectsAResourceIsInTheSignature() {
    const MeshComponent m = mesh("Cube");
    const MaterialComponent mat = material("a.png", "n.png");
    const uint64_t base = sig(&m, &mat);

    const MeshComponent otherPrimitive = mesh("Sphere");
    CHECK_MSG(base != sig(&otherPrimitive, &mat), "the primitive type selects a mesh");

    const MeshComponent fromFile = mesh("Cube", "assets/models/monument.gltf");
    CHECK_MSG(base != sig(&fromFile, &mat), "a file path overrides the primitive");

    const MaterialComponent otherAlbedo = material("b.png", "n.png");
    CHECK_MSG(base != sig(&m, &otherAlbedo), "the albedo path selects a texture");

    const MaterialComponent otherNormal = material("a.png", "m.png");
    CHECK_MSG(base != sig(&m, &otherNormal), "and so does the normal path");

    // Swapped, not merely different. The two paths are read into two different
    // ids with two different colour spaces, so a signature that concatenated
    // them without separation would call this unchanged.
    const MaterialComponent swapped = material("n.png", "a.png");
    CHECK_MSG(base != sig(&m, &swapped),
              "swapping the albedo and normal paths is a different pair of textures");
}

static void testGeneratedGeometryIsInTheSignature() {
    // A mesh the GAME uploaded, named by its cache key. It selects a mesh as
    // surely as a file path does, and it is the one input with no other trace
    // on the entity: a component switched from one uploaded key to another has
    // the same primitive, the same path and the same material, so a signature
    // blind to the key would keep drawing the first geometry for ever.
    MeshComponent generated = mesh("Cube");
    generated.meshKey = "mp:counter-text";
    const MaterialComponent mat = material("a.png");

    const MeshComponent plain = mesh("Cube");
    CHECK_MSG(sig(&plain, &mat) != sig(&generated, &mat),
              "naming uploaded geometry must re-resolve the entity");

    MeshComponent other = mesh("Cube");
    other.meshKey = "mp:crystal-text";
    CHECK_MSG(sig(&generated, &mat) != sig(&other, &mat),
              "and so must switching to different uploaded geometry");

    MeshComponent same = mesh("Cube");
    same.meshKey = "mp:counter-text";
    CHECK_MSG(sig(&generated, &mat) == sig(&same, &mat),
              "an unchanged key must keep its signature, or nothing is ever skipped");
}

static void testAnAbsentComponentIsNotAnEmptyOne() {
    // An entity with no MeshComponent falls back to the cube; one with a
    // MeshComponent holding empty strings asks the registry for the default
    // primitive. Those are different code paths and can be different meshes, so
    // they must not hash alike.
    const MeshComponent empty = mesh("", "");
    const MaterialComponent emptyMat = material("", "");

    CHECK_MSG(sig(nullptr, &emptyMat) != sig(&empty, &emptyMat),
              "no mesh component is not the same as an empty one");
    CHECK_MSG(sig(&empty, nullptr) != sig(&empty, &emptyMat),
              "no material component is not the same as an empty one");
    CHECK_MSG(sig(nullptr, nullptr) != sig(&empty, &emptyMat),
              "and neither is neither");
}

static void testAReloadedAssetChangesTheSignature() {
    // The case the whole generation counter exists for, and the one no amount
    // of looking at the components could catch: a hot reload leaves the entity
    // holding exactly the path it always held, while the id that path resolves
    // to - or the buffers behind that id - have been replaced.
    const MeshComponent m = mesh("Cube", "assets/models/monument.gltf");
    const MaterialComponent mat = material("a.png", "n.png");

    const uint64_t before = sig(&m, &mat, 1, 1);

    CHECK_MSG(before != sig(&m, &mat, 2, 1),
              "a mesh registry generation bump must invalidate every entity");
    CHECK_MSG(before != sig(&m, &mat, 1, 2),
              "and so must a texture registry one");
    CHECK_MSG(sig(&m, &mat, 2, 1) != sig(&m, &mat, 1, 2),
              "the two counters must not cancel each other out");
}

static void testTheSignatureIsNeverZero() {
    // Zero is what RenderableComponent::resourceSignature holds before anything
    // has been resolved, so it must not be an answer the function can give -
    // otherwise the one entity whose inputs hash to nothing is never resolved
    // at all and draws whatever the default happens to be, forever.
    //
    // Swept rather than argued, because the guarantee is a clamp at the end of
    // the function and a clamp is exactly the kind of thing that gets deleted.
    const char* paths[] = { "", "a", "assets/textures/floor_tiles.png", "\x01\x02", "Cube" };
    const int count = static_cast<int>(sizeof(paths) / sizeof(paths[0]));

    int checked = 0;
    for (int i = 0; i < count; ++i) {
        for (int j = 0; j < count; ++j) {
            const MeshComponent m = mesh(paths[i], paths[j]);
            const MaterialComponent mat = material(paths[j], paths[i]);
            for (uint64_t generation = 0; generation < 4; ++generation) {
                if (sig(&m, &mat, generation, generation + 1) == 0) {
                    CHECK_MSG(false, "a signature of zero collides with 'never resolved'");
                    return;
                }
                ++checked;
            }
        }
    }
    CHECK_MSG(checked == count * count * 4,
              "the sweep must have checked every combination: " + std::to_string(checked));
    CHECK(checked > 90);

    // And the degenerate case on its own, which is the likeliest input to hash
    // to something unusual.
    CHECK(sig(nullptr, nullptr, 0, 0) != 0);
}

static void testThePackedMapIsInTheSignatureToo() {
    // Every path that SELECTS a texture has to be in here, or the resolve is
    // skipped and the entity keeps sampling the map it used to have. The
    // occlusion/roughness/metallic path was the third to arrive and is the
    // one most easily forgotten, because a wrong answer looks like a
    // slightly differently shaded surface rather than like a bug.
    const MeshComponent m = mesh("Cube");

    const MaterialComponent none = material("albedo.png", "normal.png");
    const MaterialComponent packed = material("albedo.png", "normal.png", "orm.png");
    CHECK_MSG(sig(&m, &none) != sig(&m, &packed),
              "gaining a packed map must re-resolve the entity");

    const MaterialComponent other = material("albedo.png", "normal.png", "worn.png");
    CHECK_MSG(sig(&m, &packed) != sig(&m, &other),
              "and so must swapping it for a different one");

    // The control: the same three paths must still agree with themselves,
    // or the signature is simply noisy and nothing is ever skipped.
    const MaterialComponent again = material("albedo.png", "normal.png", "orm.png");
    CHECK_MSG(sig(&m, &packed) == sig(&m, &again),
              "an unchanged material must keep its signature");
}

static void testTheSceneColourSpaceIsInTheSignature() {
    // A scene that switches to display-encoded values asks for every albedo
    // again as a UNORM upload - the same path, a different texture. Nothing on
    // any entity changes, and no generation moves, so without the colour space
    // in the signature every sprite would keep sampling its sRGB copy and the
    // switch would look like it did nothing.
    const MeshComponent m = mesh("Quad");
    const MaterialComponent mat = material("assets/textures/uv_grid.png");

    CHECK_MSG(sig(&m, &mat, 1, 1, true) != sig(&m, &mat, 1, 1, false),
              "the same albedo decoded and not decoded are different textures");
    CHECK_MSG(sig(&m, &mat, 1, 1, false) == sig(&m, &mat, 1, 1, false),
              "and a scene that stays display-encoded must still skip");

    // Not folded into a generation: a mode switch on the same frame as a
    // reload must not land back on a signature some earlier frame resolved.
    const uint64_t a = sig(&m, &mat, 1, 1, true);
    const uint64_t b = sig(&m, &mat, 1, 2, true);
    const uint64_t c = sig(&m, &mat, 1, 1, false);
    const uint64_t d = sig(&m, &mat, 1, 2, false);
    CHECK_MSG(a != b && a != c && a != d && b != c && b != d && c != d,
              "the colour space and the texture generation must not cancel each other out");

    // Even an entity with no material changes: its texture ids are the
    // built-in ones either way, and a redundant re-resolve costs less than a
    // rule with an exception in it.
    CHECK(sig(&m, nullptr, 1, 1, true) != sig(&m, nullptr, 1, 1, false));
}

static void testTheOverlayIsInTheSignatureToo() {
    // The fourth path that selects a texture, and the one a 2D game changes
    // most: a sprite gains its level's lightmap when the level loads. Missed
    // here, the resolve is skipped and the sprite keeps adding the overlay it
    // had - or none - with nothing to say so.
    const MeshComponent m = mesh("Quad");

    MaterialComponent none = material("wall.png");
    MaterialComponent lit = material("wall.png");
    lit.overlayTexturePath = "lightmaps/level0/add696.png";
    CHECK_MSG(sig(&m, &none) != sig(&m, &lit), "gaining an overlay must re-resolve the entity");

    MaterialComponent other = lit;
    other.overlayTexturePath = "lightmaps/level1/add696.png";
    CHECK_MSG(sig(&m, &lit) != sig(&m, &other), "and so must swapping it for another");

    MaterialComponent again = material("wall.png");
    again.overlayTexturePath = "lightmaps/level0/add696.png";
    CHECK_MSG(sig(&m, &lit) == sig(&m, &again), "an unchanged overlay must keep its signature");

    // Moved between slots, not merely present: the same file named as the
    // packed map and as the overlay is two different bindings, one of them
    // data and one colour.
    MaterialComponent asOrm = material("wall.png", {}, "lightmaps/level0/add696.png");
    CHECK_MSG(sig(&m, &lit) != sig(&m, &asOrm), "the overlay's path is not the ORM map's");

    // Its colour space is the albedo's, already in the signature.
    CHECK(sig(&m, &lit, 1, 1, true) != sig(&m, &lit, 1, 1, false));

    // And nothing on a material that never names one moves by it: the 2D
    // settings select no texture, so they are not an input.
    MaterialComponent sprite = none;
    sprite.sprite2D.enabled = true;
    sprite.sprite2D.ambient = glm::vec3(0.35f);
    CHECK_MSG(sig(&m, &none) == sig(&m, &sprite),
              "a sprite's light settings are not a resource, so they do not re-resolve it");
}

static void runTests() {
    testTheSameInputsGiveTheSameSignature();
    testEveryPathThatSelectsAResourceIsInTheSignature();
    testGeneratedGeometryIsInTheSignature();
    testAnAbsentComponentIsNotAnEmptyOne();
    testAReloadedAssetChangesTheSignature();
    testTheSignatureIsNeverZero();
    testThePackedMapIsInTheSignatureToo();
    testTheSceneColourSpaceIsInTheSignature();
    testTheOverlayIsInTheSignatureToo();
}

TEST_MAIN("test_resourcesync", 29)
