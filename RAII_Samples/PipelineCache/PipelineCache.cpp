// Copyright(c) 2019, NVIDIA CORPORATION. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// VulkanHpp Samples : PipelineCache
//                     This sample tries to save and reuse pipeline cache data between runs.

#if defined( _MSC_VER )
// no need to ignore any warnings with MSVC
#elif defined( __clang__ )
#  pragma clang diagnostic ignored "-Wmissing-braces"
#elif defined( __GNUC__ )
#else
// unknow compiler... just ignore the warnings for yourselves ;)
#endif

#include "../../samples/utils/geometries.hpp"
#include "../../samples/utils/math.hpp"
#include "../utils/shaders.hpp"
#include "../utils/utils.hpp"
#include "SPIRV/GlslangToSpv.h"

#include <fstream>
#include <iomanip>
#include <thread>

// For timestamp code (getMilliseconds)
#ifdef WIN32
#  include <Windows.h>
#else
#  include <sys/time.h>
#endif

typedef unsigned long long timestamp_t;

timestamp_t getMilliseconds()
{
#ifdef WIN32
  LARGE_INTEGER frequency;
  BOOL          useQPC = QueryPerformanceFrequency( &frequency );
  if ( useQPC )
  {
    LARGE_INTEGER now;
    QueryPerformanceCounter( &now );
    return ( 1000LL * now.QuadPart ) / frequency.QuadPart;
  }
  else
  {
    return GetTickCount();
  }
#else
  struct timeval now;
  gettimeofday( &now, NULL );
  return ( now.tv_usec / 1000 ) + (timestamp_t)now.tv_sec;
#endif
}

static char const * AppName    = "PipelineCache";
static char const * EngineName = "Vulkan.hpp";

int main( int /*argc*/, char ** /*argv*/ )
{
  try
  {
    vk::raii::Context  context;
    vk::raii::Instance instance = vk::raii::su::makeInstance( context, AppName, EngineName, {}, vk::su::getInstanceExtensions() );
#if !defined( NDEBUG )
    vk::raii::DebugUtilsMessengerEXT debugUtilsMessenger( instance, vk::su::makeDebugUtilsMessengerCreateInfoEXT() );
#endif
    vk::raii::PhysicalDevice     physicalDevice = vk::raii::PhysicalDevices( instance ).front();
    vk::PhysicalDeviceProperties properties     = physicalDevice.getProperties();

    vk::raii::su::SurfaceData surfaceData( instance, AppName, vk::Extent2D( 500, 500 ) );

    std::pair<uint32_t, uint32_t> graphicsAndPresentQueueFamilyIndex =
      vk::raii::su::findGraphicsAndPresentQueueFamilyIndex( physicalDevice, surfaceData.surface );
    vk::raii::Device device = vk::raii::su::makeDevice( physicalDevice, graphicsAndPresentQueueFamilyIndex.first, vk::su::getDeviceExtensions() );

    vk::raii::CommandPool   commandPool   = vk::raii::CommandPool( device, { {}, graphicsAndPresentQueueFamilyIndex.first } );
    vk::raii::CommandBuffer commandBuffer = vk::raii::su::makeCommandBuffer( device, commandPool );

    vk::raii::Queue graphicsQueue( device, graphicsAndPresentQueueFamilyIndex.first, 0 );
    vk::raii::Queue presentQueue( device, graphicsAndPresentQueueFamilyIndex.second, 0 );

    vk::raii::su::SwapChainData swapChainData( physicalDevice,
                                               device,
                                               surfaceData.surface,
                                               surfaceData.extent,
                                               vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc,
                                               {},
                                               graphicsAndPresentQueueFamilyIndex.first,
                                               graphicsAndPresentQueueFamilyIndex.second );

    vk::raii::su::DepthBufferData depthBufferData( physicalDevice, device, vk::Format::eD16Unorm, surfaceData.extent );

    vk::raii::su::TextureData textureData( physicalDevice, device );

    commandBuffer.begin( vk::CommandBufferBeginInfo() );
    textureData.setImage( commandBuffer, vk::su::MonochromeImageGenerator( { 118, 185, 0 } ) );

    vk::raii::su::BufferData uniformBufferData( physicalDevice, device, sizeof( glm::mat4x4 ), vk::BufferUsageFlagBits::eUniformBuffer );
    glm::mat4x4              mvpcMatrix = vk::su::createModelViewProjectionClipMatrix( surfaceData.extent );
    vk::raii::su::copyToDevice( uniformBufferData.deviceMemory, mvpcMatrix );

    vk::raii::DescriptorSetLayout descriptorSetLayout =
      vk::raii::su::makeDescriptorSetLayout( device,
                                             { { vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex },
                                               { vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment } } );
    vk::raii::PipelineLayout pipelineLayout( device, { {}, *descriptorSetLayout } );

    vk::Format           colorFormat = vk::su::pickSurfaceFormat( physicalDevice.getSurfaceFormatsKHR( surfaceData.surface ) ).format;
    vk::raii::RenderPass renderPass  = vk::raii::su::makeRenderPass( device, colorFormat, depthBufferData.format );

    glslang::InitializeProcess();
    vk::raii::ShaderModule vertexShaderModule   = vk::raii::su::makeShaderModule( device, vk::ShaderStageFlagBits::eVertex, vertexShaderText_PT_T );
    vk::raii::ShaderModule fragmentShaderModule = vk::raii::su::makeShaderModule( device, vk::ShaderStageFlagBits::eFragment, fragmentShaderText_T_C );
    glslang::FinalizeProcess();

    std::vector<vk::raii::Framebuffer> framebuffers =
      vk::raii::su::makeFramebuffers( device, renderPass, swapChainData.imageViews, &depthBufferData.imageView, surfaceData.extent );

    vk::raii::su::BufferData vertexBufferData( physicalDevice, device, sizeof( texturedCubeData ), vk::BufferUsageFlagBits::eVertexBuffer );
    vk::raii::su::copyToDevice( vertexBufferData.deviceMemory, texturedCubeData, sizeof( texturedCubeData ) / sizeof( texturedCubeData[0] ) );

    vk::raii::DescriptorPool descriptorPool =
      vk::raii::su::makeDescriptorPool( device, { { vk::DescriptorType::eUniformBuffer, 1 }, { vk::DescriptorType::eCombinedImageSampler, 1 } } );
    vk::raii::DescriptorSet descriptorSet = std::move( vk::raii::DescriptorSets( device, { descriptorPool, *descriptorSetLayout } ).front() );

    vk::raii::su::updateDescriptorSets(
      device, descriptorSet, { { vk::DescriptorType::eUniformBuffer, uniformBufferData.buffer, VK_WHOLE_SIZE, nullptr } }, { textureData } );

    /* VULKAN_KEY_START */

    // Check disk for existing cache data
    size_t startCacheSize = 0;
    char * startCacheData = nullptr;

    std::string   cacheFileName = "pipeline_cache_data.bin";
    std::ifstream readCacheStream( cacheFileName, std::ios_base::in | std::ios_base::binary );
    if ( readCacheStream.good() )
    {
      // Determine cache size
      readCacheStream.seekg( 0, readCacheStream.end );
      startCacheSize = static_cast<size_t>( readCacheStream.tellg() );
      readCacheStream.seekg( 0, readCacheStream.beg );

      // Allocate memory to hold the initial cache data
      startCacheData = (char *)std::malloc( startCacheSize );

      // Read the data into our buffer
      readCacheStream.read( startCacheData, startCacheSize );

      // Clean up and print results
      readCacheStream.close();
      std::cout << "  Pipeline cache HIT!\n";
      std::cout << "  cacheData loaded from " << cacheFileName << "\n";
    }
    else
    {
      // No cache found on disk
      std::cout << "  Pipeline cache miss!\n";
    }

    if ( startCacheData != nullptr )
    {
      // Check for cache validity
      //
      // TODO: Update this as the spec evolves. The fields are not defined by the header.
      //
      // The code below supports SDK 0.10 Vulkan spec, which contains the following table:
      //
      // Offset	 Size            Meaning
      // ------    ------------    ------------------------------------------------------------------
      //      0               4    a device ID equal to VkPhysicalDeviceProperties::DeviceId written
      //                           as a stream of bytes, with the least significant byte first
      //
      //      4    VK_UUID_SIZE    a pipeline cache ID equal to VkPhysicalDeviceProperties::pipelineCacheUUID
      //
      //
      // The code must be updated for latest Vulkan spec, which contains the following table:
      //
      // Offset	 Size            Meaning
      // ------    ------------    ------------------------------------------------------------------
      //      0               4    length in bytes of the entire pipeline cache header written as a
      //                           stream of bytes, with the least significant byte first
      //      4               4    a VkPipelineCacheHeaderVersion value written as a stream of bytes,
      //                           with the least significant byte first
      //      8               4    a vendor ID equal to VkPhysicalDeviceProperties::vendorID written
      //                           as a stream of bytes, with the least significant byte first
      //     12               4    a device ID equal to VkPhysicalDeviceProperties::deviceID written
      //                           as a stream of bytes, with the least significant byte first
      //     16    VK_UUID_SIZE    a pipeline cache ID equal to VkPhysicalDeviceProperties::pipelineCacheUUID

      uint32_t headerLength                    = 0;
      uint32_t cacheHeaderVersion              = 0;
      uint32_t vendorID                        = 0;
      uint32_t deviceID                        = 0;
      uint8_t  pipelineCacheUUID[VK_UUID_SIZE] = {};

      memcpy( &headerLength, (uint8_t *)startCacheData + 0, 4 );
      memcpy( &cacheHeaderVersion, (uint8_t *)startCacheData + 4, 4 );
      memcpy( &vendorID, (uint8_t *)startCacheData + 8, 4 );
      memcpy( &deviceID, (uint8_t *)startCacheData + 12, 4 );
      memcpy( pipelineCacheUUID, (uint8_t *)startCacheData + 16, VK_UUID_SIZE );

      // Check each field and report bad values before freeing existing cache
      bool badCache = false;

      if ( headerLength <= 0 )
      {
        badCache = true;
        std::cout << "  Bad header length in " << cacheFileName << ".\n";
        std::cout << "    Cache contains: " << std::hex << std::setw( 8 ) << headerLength << "\n";
      }

      if ( cacheHeaderVersion != VK_PIPELINE_CACHE_HEADER_VERSION_ONE )
      {
        badCache = true;
        std::cout << "  Unsupported cache header version in " << cacheFileName << ".\n";
        std::cout << "    Cache contains: " << std::hex << std::setw( 8 ) << cacheHeaderVersion << "\n";
      }

      if ( vendorID != properties.vendorID )
      {
        badCache = true;
        std::cout << "  Vender ID mismatch in " << cacheFileName << ".\n";
        std::cout << "    Cache contains: " << std::hex << std::setw( 8 ) << vendorID << "\n";
        std::cout << "    Driver expects: " << std::hex << std::setw( 8 ) << properties.vendorID << "\n";
      }

      if ( deviceID != properties.deviceID )
      {
        badCache = true;
        std::cout << "  Device ID mismatch in " << cacheFileName << ".\n";
        std::cout << "    Cache contains: " << std::hex << std::setw( 8 ) << deviceID << "\n";
        std::cout << "    Driver expects: " << std::hex << std::setw( 8 ) << properties.deviceID << "\n";
      }

      if ( memcmp( pipelineCacheUUID, properties.pipelineCacheUUID, sizeof( pipelineCacheUUID ) ) != 0 )
      {
        badCache = true;
        std::cout << "  UUID mismatch in " << cacheFileName << ".\n";
        std::cout << "    Cache contains: " << vk::su::UUID( pipelineCacheUUID ) << "\n";
        std::cout << "    Driver expects: " << vk::su::UUID( properties.pipelineCacheUUID ) << "\n";
      }

      if ( badCache )
      {
        // Don't submit initial cache data if any version info is incorrect
        free( startCacheData );
        startCacheSize = 0;
        startCacheData = nullptr;

        // And clear out the old cache file for use in next run
        std::cout << "  Deleting cache entry " << cacheFileName << " to repopulate.\n";
        if ( remove( cacheFileName.c_str() ) != 0 )
        {
          std::cerr << "Reading error";
          exit( EXIT_FAILURE );
        }
      }
    }

    // Feed the initial cache data into cache creation
    vk::PipelineCacheCreateInfo pipelineCacheCreateInfo( {}, startCacheSize, startCacheData );
    vk::raii::PipelineCache     pipelineCache( device, pipelineCacheCreateInfo );

    // Free our initialData now that pipeline cache has been created
    free( startCacheData );
    startCacheData = NULL;

    // Time (roughly) taken to create the graphics pipeline
    timestamp_t        start            = getMilliseconds();
    vk::raii::Pipeline graphicsPipeline = vk::raii::su::makeGraphicsPipeline( device,
                                                                              pipelineCache,
                                                                              vertexShaderModule,
                                                                              nullptr,
                                                                              fragmentShaderModule,
                                                                              nullptr,
                                                                              sizeof( texturedCubeData[0] ),
                                                                              { { vk::Format::eR32G32B32A32Sfloat, 0 }, { vk::Format::eR32G32Sfloat, 16 } },
                                                                              vk::FrontFace::eClockwise,
                                                                              true,
                                                                              pipelineLayout,
                                                                              renderPass );
    timestamp_t        elapsed          = getMilliseconds() - start;
    std::cout << "  vkCreateGraphicsPipeline time: " << (double)elapsed << " ms\n";

    vk::raii::Semaphore imageAcquiredSemaphore( device, vk::SemaphoreCreateInfo() );

    // Get the index of the next available swapchain image:
    vk::Result result;
    uint32_t   imageIndex;
    std::tie( result, imageIndex ) = swapChainData.swapChain.acquireNextImage( vk::su::FenceTimeout, imageAcquiredSemaphore );
    assert( result == vk::Result::eSuccess );
    assert( imageIndex < swapChainData.images.size() );

    std::array<vk::ClearValue, 2> clearValues;
    clearValues[0].color        = vk::ClearColorValue( 0.2f, 0.2f, 0.2f, 0.2f );
    clearValues[1].depthStencil = vk::ClearDepthStencilValue( 1.0f, 0 );

    commandBuffer.beginRenderPass(
      vk::RenderPassBeginInfo( renderPass, framebuffers[imageIndex], vk::Rect2D( vk::Offset2D(), surfaceData.extent ), clearValues ),
      vk::SubpassContents::eInline );
    commandBuffer.bindPipeline( vk::PipelineBindPoint::eGraphics, graphicsPipeline );
    commandBuffer.bindDescriptorSets( vk::PipelineBindPoint::eGraphics, pipelineLayout, 0, { descriptorSet }, {} );

    commandBuffer.bindVertexBuffers( 0, { vertexBufferData.buffer }, { 0 } );
    commandBuffer.setViewport(
      0, vk::Viewport( 0.0f, 0.0f, static_cast<float>( surfaceData.extent.width ), static_cast<float>( surfaceData.extent.height ), 0.0f, 1.0f ) );
    commandBuffer.setScissor( 0, vk::Rect2D( vk::Offset2D( 0, 0 ), surfaceData.extent ) );

    commandBuffer.draw( 12 * 3, 1, 0, 0 );
    commandBuffer.endRenderPass();
    commandBuffer.end();

    vk::raii::Fence drawFence( device, vk::FenceCreateInfo() );

    vk::PipelineStageFlags waitDestinationStageMask( vk::PipelineStageFlagBits::eColorAttachmentOutput );
    vk::SubmitInfo         submitInfo( *imageAcquiredSemaphore, waitDestinationStageMask, *commandBuffer );
    graphicsQueue.submit( submitInfo, *drawFence );

    while ( vk::Result::eTimeout == device.waitForFences( { drawFence }, VK_TRUE, vk::su::FenceTimeout ) )
      ;

    vk::PresentInfoKHR presentInfoKHR( nullptr, *swapChainData.swapChain, imageIndex );
    result = presentQueue.presentKHR( presentInfoKHR );
    switch ( result )
    {
      case vk::Result::eSuccess: break;
      case vk::Result::eSuboptimalKHR: std::cout << "vk::Queue::presentKHR returned vk::Result::eSuboptimalKHR !\n"; break;
      default: assert( false );  // an unexpected result is returned !
    }
    std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );

    // Store away the cache that we've populated.  This could conceivably happen
    // earlier, depends on when the pipeline cache stops being populated
    // internally.
    std::vector<uint8_t> endCacheData = pipelineCache.getData();

    // Write the file to disk, overwriting whatever was there
    std::ofstream writeCacheStream( cacheFileName, std::ios_base::out | std::ios_base::binary );
    if ( writeCacheStream.good() )
    {
      writeCacheStream.write( reinterpret_cast<char const *>( endCacheData.data() ), endCacheData.size() );
      writeCacheStream.close();
      std::cout << "  cacheData written to " << cacheFileName << "\n";
    }
    else
    {
      // Something bad happened
      std::cout << "  Unable to write cache data to disk!\n";
    }

    /* VULKAN_KEY_END */
  }
  catch ( vk::SystemError & err )
  {
    std::cout << "vk::SystemError: " << err.what() << std::endl;
    exit( -1 );
  }
  catch ( std::exception & err )
  {
    std::cout << "std::exception: " << err.what() << std::endl;
    exit( -1 );
  }
  catch ( ... )
  {
    std::cout << "unknown error\n";
    exit( -1 );
  }
  return 0;
}
