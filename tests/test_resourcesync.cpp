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

MaterialComponent material(const std::string& albedo, const std::string& normal = {}) {
    MaterialComponent component;
    component.albedoTexturePath = albedo;
    component.normalTexturePath = normal;
    return component;
}

uint64_t sig(const MeshComponent* m, const MaterialComponent* mat,
             uint64_t meshGen = 1, uint64_t textureGen = 1) {
    return RenderSystem::ResourceSignature(m, mat, meshGen, textureGen);
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

static void runTests() {
    testTheSameInputsGiveTheSameSignature();
    testEveryPathThatSelectsAResourceIsInTheSignature();
    testAnAbsentComponentIsNotAnEmptyOne();
    testAReloadedAssetChangesTheSignature();
    testTheSignatureIsNeverZero();
}

TEST_MAIN("test_resourcesync", 16)
