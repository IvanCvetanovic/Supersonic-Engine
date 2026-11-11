// The engine against a real Vulkan device.
//
// Everything in this suite runs on whatever device the machine has - a GPU, or
// Mesa's lavapipe in software - and is SKIPPED (exit code 77, which ctest lists as
// Skipped) where there is none. See DeviceFixture.hpp.
//
// What is checked is the engine's own behaviour, not a driver's: the code in the
// renderer that talks to the device had no test at all, and the hardening pass and
// the review after it fixed a use-after-free, a texture registry that handed its
// own fallback image to a caller, and a slot leak - all "from reading the code",
// with nothing able to run them.

#include "TestHarness.hpp"
#include "DeviceFixture.hpp"

#include "core/MeshData.hpp"
#include "core/ModelLoader.hpp"
#include "renderer/MeshRegistry.hpp"
#include "renderer/PipelineCache.hpp"
#include "renderer/TextureRegistry.hpp"
#include "renderer/VulkanBuffer.hpp"
#include "renderer/VulkanImage.hpp"
#include "renderer/VulkanPipeline.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using namespace Supersonic;
using Supersonic::test::DeviceFixture;
namespace fs = std::filesystem;

namespace {

// The layout the material descriptor sets use: kMaterialBindingCount combined image
// samplers, read in the fragment stage. Built here rather than taken from
// VulkanPipeline, which would need a render pass and shaders to exist.
vk::DescriptorSetLayout makeMaterialLayout(vk::Device device) {
    std::array<vk::DescriptorSetLayoutBinding, VulkanPipeline::kMaterialBindingCount> bindings{};
    for (uint32_t i = 0; i < bindings.size(); ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = vk::DescriptorType::eCombinedImageSampler;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = vk::ShaderStageFlagBits::eFragment;
    }
    vk::DescriptorSetLayoutCreateInfo info{};
    info.bindingCount = static_cast<uint32_t>(bindings.size());
    info.pBindings = bindings.data();
    return device.createDescriptorSetLayout(info);
}

// Reads one mip level of an image that is in ShaderReadOnlyOptimal back to the host.
std::vector<uint8_t> readBack(DeviceFixture& f, vk::Image image, uint32_t width, uint32_t height,
                              uint32_t level) {
    const uint32_t w = std::max(1u, width >> level);
    const uint32_t h = std::max(1u, height >> level);
    const vk::DeviceSize bytes = static_cast<vk::DeviceSize>(w) * h * 4;

    VulkanBuffer host(f.device->GetAllocator(), bytes, vk::BufferUsageFlagBits::eTransferDst,
                      VMA_MEMORY_USAGE_GPU_TO_CPU, VMA_ALLOCATION_CREATE_MAPPED_BIT);

    vk::Device device = f.device->GetDevice();
    vk::CommandBufferAllocateInfo allocate{};
    allocate.commandPool = f.pool;
    allocate.level = vk::CommandBufferLevel::ePrimary;
    allocate.commandBufferCount = 1;
    vk::CommandBuffer cmd = device.allocateCommandBuffers(allocate)[0];

    cmd.begin(vk::CommandBufferBeginInfo{vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

    vk::ImageMemoryBarrier toSource{};
    toSource.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    toSource.newLayout = vk::ImageLayout::eTransferSrcOptimal;
    toSource.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toSource.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toSource.image = image;
    toSource.subresourceRange = {vk::ImageAspectFlagBits::eColor, level, 1, 0, 1};
    toSource.srcAccessMask = vk::AccessFlagBits::eShaderRead;
    toSource.dstAccessMask = vk::AccessFlagBits::eTransferRead;
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eAllCommands, vk::PipelineStageFlagBits::eTransfer,
                        {}, {}, {}, toSource);

    vk::BufferImageCopy region{};
    region.imageSubresource = {vk::ImageAspectFlagBits::eColor, level, 0, 1};
    region.imageExtent = vk::Extent3D{w, h, 1};
    cmd.copyImageToBuffer(image, vk::ImageLayout::eTransferSrcOptimal, host.GetBuffer(), region);

    vk::BufferMemoryBarrier toHost{};
    toHost.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
    toHost.dstAccessMask = vk::AccessFlagBits::eHostRead;
    toHost.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toHost.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toHost.buffer = host.GetBuffer();
    toHost.size = VK_WHOLE_SIZE;
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eHost,
                        {}, {}, toHost, {});
    cmd.end();

    vk::SubmitInfo submit{};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    f.device->GetGraphicsQueue().submit(submit);
    f.device->GetGraphicsQueue().waitIdle();
    device.freeCommandBuffers(f.pool, cmd);

    // Memory the GPU wrote and the host reads: invalidated where it is not coherent.
    vmaInvalidateAllocation(f.device->GetAllocator(), host.GetAllocation(), 0, VK_WHOLE_SIZE);

    std::vector<uint8_t> out(static_cast<size_t>(bytes));
    std::memcpy(out.data(), host.GetMappedData(), out.size());
    return out;
}

} // namespace

static void testAnUploadedImageReadsBackWhatWasUploaded(DeviceFixture& f) {
    // The upload path every texture takes: staging buffer, a layout transition, a
    // copy, a transition to shader-read. Four by four, every texel a different
    // value, UNORM so no transfer function touches the bytes - what comes back is
    // what went in, on any device.
    constexpr uint32_t kSize = 4;
    std::vector<uint8_t> pixels(kSize * kSize * 4);
    for (size_t i = 0; i < pixels.size(); ++i) pixels[i] = static_cast<uint8_t>(i * 7 + 3);

    VulkanBuffer staging(f.device->GetAllocator(), pixels.size(),
                         vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_ONLY);
    staging.UploadData(pixels.data(), pixels.size());

    VulkanImage image(*f.device, kSize, kSize, vk::Format::eR8G8B8A8Unorm,
                      vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst |
                          vk::ImageUsageFlagBits::eTransferSrc,
                      vk::ImageAspectFlagBits::eColor, 1, vk::SampleCountFlagBits::e1, false,
                      /*generateMipmaps*/ false);

    VulkanImage::TransitionLayout(*f.device, f.pool, image.GetImage(), vk::ImageLayout::eUndefined,
                                  vk::ImageLayout::eTransferDstOptimal);
    VulkanImage::CopyBufferToImage(*f.device, f.pool, staging.GetBuffer(), image.GetImage(), kSize, kSize);
    VulkanImage::TransitionLayout(*f.device, f.pool, image.GetImage(),
                                  vk::ImageLayout::eTransferDstOptimal,
                                  vk::ImageLayout::eShaderReadOnlyOptimal);

    const std::vector<uint8_t> back = readBack(f, image.GetImage(), kSize, kSize, 0);
    CHECK_EQ(back.size(), pixels.size());
    CHECK_MSG(back == pixels, "an uploaded 4x4 image reads back byte for byte");
}

static void testAMipChainEndsInTheAverageOfWhatItWasBuiltFrom(DeviceFixture& f) {
    // GenerateMipmaps blits each level from the one above and leaves every level in
    // shader-read. A flat colour has the same average at every level, so the 1x1 at
    // the bottom is that colour - to within the one count a filtered blit may round
    // by, which differs between drivers and is why this is not an exact compare.
    constexpr uint32_t kSize = 8;
    constexpr uint32_t kLevels = 4;   // 8, 4, 2, 1
    std::vector<uint8_t> pixels(kSize * kSize * 4);
    for (size_t i = 0; i < pixels.size(); i += 4) {
        pixels[i + 0] = 200; pixels[i + 1] = 100; pixels[i + 2] = 50; pixels[i + 3] = 255;
    }

    VulkanBuffer staging(f.device->GetAllocator(), pixels.size(),
                         vk::BufferUsageFlagBits::eTransferSrc, VMA_MEMORY_USAGE_CPU_ONLY);
    staging.UploadData(pixels.data(), pixels.size());

    VulkanImage image(*f.device, kSize, kSize, vk::Format::eR8G8B8A8Unorm,
                      vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst |
                          vk::ImageUsageFlagBits::eTransferSrc,
                      vk::ImageAspectFlagBits::eColor, 1, vk::SampleCountFlagBits::e1, false,
                      /*generateMipmaps*/ true);
    CHECK_EQ(image.GetMipLevels(), kLevels);

    VulkanImage::TransitionLayout(*f.device, f.pool, image.GetImage(), vk::ImageLayout::eUndefined,
                                  vk::ImageLayout::eTransferDstOptimal);
    VulkanImage::CopyBufferToImage(*f.device, f.pool, staging.GetBuffer(), image.GetImage(), kSize, kSize);
    const bool filtered = VulkanImage::GenerateMipmaps(*f.device, f.pool, image.GetImage(),
                                                       vk::Format::eR8G8B8A8Unorm, kSize, kSize,
                                                       image.GetMipLevels());
    CHECK_MSG(filtered, "R8G8B8A8Unorm can be linearly filtered, so the chain is built");

    const std::vector<uint8_t> top = readBack(f, image.GetImage(), kSize, kSize, kLevels - 1);
    CHECK_EQ(top.size(), size_t{4});
    if (top.size() == 4) {
        CHECK_MSG(std::abs(int(top[0]) - 200) <= 1 && std::abs(int(top[1]) - 100) <= 1 &&
                      std::abs(int(top[2]) - 50) <= 1 && top[3] == 255,
                  "the 1x1 level of a flat colour is that colour");
    }
}

static void testAReplacedTextureLeavesTheTableTheSizeItWas(DeviceFixture& f) {
    // ReplaceRGBA uploads the new pixels as a scratch texture, moves its image under
    // the id being replaced, and used to leave the scratch slot behind holding
    // nothing: one dead slot per call, which for a texture replaced every frame is a
    // slot a frame.
    const vk::DescriptorSetLayout layout = makeMaterialLayout(f.device->GetDevice());
    {
        TextureRegistry textures(*f.device, f.pool, layout);
        std::vector<uint8_t> red(4 * 4 * 4, 0);
        for (size_t i = 0; i < red.size(); i += 4) { red[i] = 255; red[i + 3] = 255; }
        std::vector<uint8_t> blue(4 * 4 * 4, 0);
        for (size_t i = 0; i < blue.size(); i += 4) { blue[i + 2] = 255; blue[i + 3] = 255; }

        const uint32_t id = textures.UploadRGBA("device-test:dynamic", red.data(), 4, 4, false);
        const size_t before = textures.Size();
        for (int i = 0; i < 5; ++i) {
            CHECK_MSG(textures.ReplaceRGBA(id, (i % 2 == 0 ? blue : red).data(), 4, 4, false),
                      "a replace of a user texture succeeds");
        }
        CHECK_EQ(textures.Size(), before);
    }
    f.device->GetDevice().waitIdle();
    f.device->FlushDeferredDestroys();
    f.device->GetDevice().destroyDescriptorSetLayout(layout);
}

static void testARefusedUploadDoesNotStealTheFallbackImage(DeviceFixture& f) {
    // An image larger than the device can make is refused, and UploadRGBA answers a
    // refusal with the CHECKERBOARD's id. ReplaceRGBA took that answer for its
    // scratch texture, moved the checker's own image under the id being replaced,
    // and left the checker's slot empty - so every later fallback to it threw
    // mid-frame. A refusal is now a refusal, and the checker still has its image.
    const vk::DescriptorSetLayout layout = makeMaterialLayout(f.device->GetDevice());
    {
        TextureRegistry textures(*f.device, f.pool, layout);
        const uint32_t checker = textures.GetCheckerTexture();

        const std::array<uint8_t, 4> one = {10, 20, 30, 255};
        const uint32_t id = textures.UploadRGBA("device-test:victim", one.data(), 1, 1, false);

        const uint32_t tooWide =
            f.device->GetPhysicalDevice().getProperties().limits.maxImageDimension2D + 1;
        std::vector<uint8_t> unread(4, 0);   // refused before a pixel is read
        CHECK_MSG(!textures.ReplaceRGBA(id, unread.data(), tooWide, 1, false),
                  "an image the device cannot hold is refused");

        // The checker still has an image: a descriptor set that names it builds. A
        // slot left empty throws "Material references a texture that does not exist".
        bool built = true;
        try {
            textures.AcquireMaterialSet(checker, textures.GetCheckerTexture(), checker, checker, checker);
        } catch (const std::exception&) {
            built = false;
        }
        CHECK_MSG(built, "the fallback texture still has its image after a refused replace");
    }
    f.device->GetDevice().waitIdle();
    f.device->FlushDeferredDestroys();
    f.device->GetDevice().destroyDescriptorSetLayout(layout);
}

static void testAReplacedMeshKeepsItsId(DeviceFixture& f) {
    MeshRegistry meshes(*f.device, f.pool);

    MeshData cube;
    CHECK(ModelLoader::GenerateCube(1.0f, cube));
    const uint32_t id = meshes.Upload("device-test:mesh", cube);
    CHECK_MSG(id != MeshRegistry::kInvalidMesh, "a mesh uploads");
    const GpuMesh* first = meshes.Get(id);
    CHECK_MSG(first != nullptr && first->indexCount == cube.indices.size(), "with its index count");

    MeshData bigger;
    CHECK(ModelLoader::GenerateCube(2.0f, bigger));
    bigger.indices.insert(bigger.indices.end(), cube.indices.begin(), cube.indices.end());
    CHECK_MSG(meshes.Replace(id, bigger), "its geometry can be replaced");
    const GpuMesh* second = meshes.Get(id);
    CHECK_MSG(second != nullptr && second->indexCount == bigger.indices.size(),
              "behind the same id, with the new index count");
    CHECK_MSG(meshes.Find("device-test:mesh") == id, "and the key still answers to it");

    f.device->GetDevice().waitIdle();
    f.device->FlushDeferredDestroys();
}

static void testThePipelineCacheWritesAFileAndReadsItBack(DeviceFixture& f) {
    // Unique per run: two copies of the suite (the release and the sanitizer trees, run
    // side by side) would otherwise write and delete one file under each other.
    const fs::path path = fs::temp_directory_path() /
        ("supersonic-device-test-pipeline-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".cache");
    std::error_code ec;
    fs::remove(path, ec);

    {
        PipelineCache cache(*f.device, path.string());
        CHECK_MSG(static_cast<bool>(cache.Get()), "a cache is created");
        cache.Save();
    }
    CHECK_MSG(fs::exists(path, ec), "Save writes the cache to disk");
    // The 32-byte header every Vulkan pipeline cache blob starts with.
    CHECK_MSG(fs::file_size(path, ec) >= 32, "and it holds at least the header");

    {
        // The next run reads it: validated against vendor, device and driver, and
        // accepted for the same device.
        PipelineCache again(*f.device, path.string());
        CHECK_MSG(static_cast<bool>(again.Get()), "a second cache is created");
        // Get() cannot tell loading the file from starting empty - a blob the header
        // check refuses still gives a valid, empty cache - so ask how much it took.
        std::error_code sizeError;
        const auto onDisk = static_cast<size_t>(fs::file_size(path, sizeError));
        CHECK_MSG(again.LoadedBytes() == onDisk && onDisk >= 32,
                  "and it accepted the file it was handed: " + std::to_string(again.LoadedBytes()) +
                      " bytes loaded of " + std::to_string(onDisk));
    }

    // A file from another device is refused, not handed to the driver: flip a byte of
    // the vendor id in the header, and the cache starts empty. This is the negative
    // control for the check above - without it that check passes on a loader that
    // accepts everything.
    {
        std::vector<char> blob;
        {
            std::ifstream in(path, std::ios::binary);
            blob.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        if (blob.size() >= 32) {
            blob[8] = static_cast<char>(blob[8] ^ 0x5A);   // VkPipelineCacheHeaderVersionOne::vendorID
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out.write(blob.data(), static_cast<std::streamsize>(blob.size()));
        }
        PipelineCache foreign(*f.device, path.string());
        CHECK_MSG(static_cast<bool>(foreign.Get()), "a cache is still made from a file for another device");
        CHECK_MSG(foreign.LoadedBytes() == 0, "but none of that file is used");
    }
    fs::remove(path, ec);
}

static void testADeferredDestroyWaitsForTheFrameItWasQueuedOn(DeviceFixture& f) {
    // The rule that makes freeing a resource safe while earlier frames may still be
    // reading it: a deleter queued on frame N runs once frame N is complete, not
    // before. Everything that frees a GPU object mid-run (UIImageStore::Destroy,
    // texture and mesh replacement, a material set going back to its pool) rides on
    // it.
    int ran = 0;
    f.device->SetFrameNumber(100);
    f.device->DeferDestroy([&ran] { ++ran; });
    f.device->DeferDestroy([&ran] { ++ran; });
    CHECK_EQ(f.device->PendingDestroyCount(), size_t{2});

    f.device->CollectGarbage(99);
    CHECK_MSG(ran == 0, "a frame earlier than the one it was queued on frees nothing");
    f.device->CollectGarbage(100);
    CHECK_MSG(ran == 2, "the frame it was queued on frees it");
    CHECK_EQ(f.device->PendingDestroyCount(), size_t{0});

    // Queued, then flushed at shutdown without the frame ever completing.
    f.device->SetFrameNumber(200);
    f.device->DeferDestroy([&ran] { ++ran; });
    f.device->FlushDeferredDestroys();
    CHECK_MSG(ran == 3, "a flush runs what is still queued");
}

int main() {
    std::string whyNot;
    bool machineHasNone = false;
    const std::unique_ptr<DeviceFixture> fixture = DeviceFixture::Create(whyNot, machineHasNone);
    if (!fixture) {
        // A device is there and the engine failed to use it: a failure, whatever the
        // environment says. The message avoids the word ctest matches below, or it
        // would be reported Skipped anyway.
        if (!machineHasNone) {
            std::printf("FAILED: test_device could not set up a device the machine offers: %s\n",
                        whyNot.c_str());
            std::fflush(stdout);
            std::_Exit(1);
        }

        // A machine that is MEANT to have a device says so (SUPERSONIC_REQUIRE_DEVICE,
        // set by `tools/check.sh device` and by CI's lavapipe job), and for it a missing
        // device is a failure too: the machine's setup is what broke (the ICD path, a
        // package that did not install), and green by skipping would hide it.
        const char* required = std::getenv("SUPERSONIC_REQUIRE_DEVICE");
        if (required != nullptr && required[0] != '\0' && std::strcmp(required, "0") != 0) {
            std::printf("FAILED: test_device was required to run (SUPERSONIC_REQUIRE_DEVICE is set) "
                        "and could not get a Vulkan device and a window: %s\n", whyNot.c_str());
            std::fflush(stdout);
            std::_Exit(1);
        }

        // The text ctest matches (SKIP_REGULAR_EXPRESSION) and the code it also
        // honours (SKIP_RETURN_CODE): either marks this Skipped, never green by default.
        std::printf("SKIPPED: test_device needs a Vulkan device and somewhere to open a window (%s)\n",
                    whyNot.c_str());
        std::fflush(stdout);
        // _Exit, not return: nothing ran, and under LeakSanitizer a return runs the leak
        // check over what a FAILED window creation left behind inside libX11 (GLFW's
        // _glfwConnectX11 leaks 12 KB when there is no display), which turned this
        // Skipped into a failure in `tools/check.sh asan` on any machine without a screen.
        std::_Exit(77);
    }

    testAnUploadedImageReadsBackWhatWasUploaded(*fixture);
    testAMipChainEndsInTheAverageOfWhatItWasBuiltFrom(*fixture);
    testAReplacedTextureLeavesTheTableTheSizeItWas(*fixture);
    testARefusedUploadDoesNotStealTheFallbackImage(*fixture);
    testAReplacedMeshKeepsItsId(*fixture);
    testThePipelineCacheWritesAFileAndReadsItBack(*fixture);
    testADeferredDestroyWaitsForTheFrameItWasQueuedOn(*fixture);

    // Everything above ran under whatever validation layers the build asked for
    // (SUPERSONIC_ENABLE_VALIDATION), whose callback only COUNTS errors - it is the
    // engine's main that turns the count into an exit code. This suite has no main of
    // the engine's, so it asks. Zero in a build with no layers, trivially.
    CHECK_MSG(VulkanContext::ValidationErrorCount() == 0,
              "the validation layers reported " + std::to_string(VulkanContext::ValidationErrorCount()) +
                  " error(s) during the suite");

    return ::test::summary("test_device", 20);
}
