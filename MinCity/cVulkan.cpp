#include "pch.h"
#define VKU_IMPLEMENTATION		// required in the one cpp file
#define VMA_IMPLEMENTATION		// required in the one cpp file
#include "cVulkan.h"			// <--- inclusion of vku_framework.hpp which includes vku.hpp VKU_IMPLEMENTATION must be defined & VMA_IMPLEMENTATION must be defined ONLY here
#include "MinCity.h"
#include "cNuklear.h"
#include "cPostProcess.h"
#include "cVoxelWorld.h"
#include "IsoVoxel.h"
#include "voxelAlloc.h"

#include <Utility/mem.h>
#pragma intrinsic(memcpy)
#pragma intrinsic(memset)

// for the texture indices used in large texture array
#include "../Data/Shaders/texturearray.glsl"

#if !defined(NDEBUG) && defined(LIVESHADER_MODE)
#include "liveshader.hpp"
#endif

inline VmaAllocator vku::vma_;
inline tbb::concurrent_unordered_map<VkCommandPool, vku::CommandBufferContainer<1>> vku::pool_;

inline cVulkan::sPOSTAADATA cVulkan::_aaData;
inline cVulkan::sDEPTHRESOLVEDATA cVulkan::_depthData;
inline cVulkan::sCOMPUTEDATA cVulkan::_comData;
inline cVulkan::sNUKLEARDATA cVulkan::_nkData;
inline cVulkan::sRTSHARED_DATA cVulkan::_rtSharedData;

inline tbb::concurrent_vector< vku::DynamicVertexBuffer* > cVulkan::_vbos;

inline cVulkan::sRTSHARED_DESCSET cVulkan::_rtSharedDescSet[eVoxelDescSharedLayoutSet::_size()]{
	cVulkan::sRTSHARED_DESCSET(eVoxelDescSharedLayout::VOXEL_COMMON),
	cVulkan::sRTSHARED_DESCSET(eVoxelDescSharedLayout::VOXEL_CLEAR)
};
inline vk::Pipeline cVulkan::_rtSharedPipeline[eVoxelSharedPipeline::_size()];
inline cVulkan::sRTDATA cVulkan::_rtData[eVoxelPipeline::_size()]{};
	 
inline cVulkan::sRTDATA_CHILD cVulkan::_rtDataChild[eVoxelPipelineCustomized::_size()]{ 

	// shaders //
	cVulkan::sRTDATA_CHILD(cVulkan::_rtData[eVoxelPipeline::VOXEL_DYNAMIC], true, false), // // VOXEL_SHADER_RAIN 

	// ### main transparent - must be last shader do not move from here //
	cVulkan::sRTDATA_CHILD(cVulkan::_rtData[eVoxelPipeline::VOXEL_DYNAMIC], true, false), // VOXEL_SHADER_TRANS 

	//------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------//

	// clears (mask clearing, all grouped together) //
	cVulkan::sRTDATA_CHILD(cVulkan::_rtData[eVoxelPipeline::VOXEL_DYNAMIC], false, true), // VOXEL_CLEAR_RAIN

	// ### main transparent - must be last shader do not move from here //
	cVulkan::sRTDATA_CHILD(cVulkan::_rtData[eVoxelPipeline::VOXEL_DYNAMIC], false, true), // VOXEL_CLEAR_TRANS

// ###################################################################################################################################################################################
}; // cVulkan,h : NUM_CHILD_MASKS   // ### update if adding transparecy shader that uses the clear mask functioality. Equals # of VOXEL_CLEAR's above
// ###################################################################################################################################################################################

inline cVulkan::sVOLUMETRICLIGHTDATA cVulkan::_volData( _rtSharedData._ubo );
inline cVulkan::sUPSAMPLEDATA cVulkan::_upData[eUpsamplePipeline::_size()];

cVulkan::cVulkan()
	: _fw{}, _window{ nullptr }, _device{}, _frameTimingAverage{}, _OffscreenCopied{}, _indirectActiveCount{ nullptr, nullptr }
{

}

bool const cVulkan::LoadVulkanFramework()
{
	if (VK_SUCCESS == volkInitialize()) { // first !!!!

		// Initialise the Vookoo framework.
		_fw.FrameworkCreate(Globals::TITLE);
		if (!_fw.ok()) {
			return(false);
		}

		// Get a device from the framework.
		_device = _fw.device();

		return(true);
	}
	else {
		fmt::print(fg(fmt::color::red), "[Vulkan] Cannot locate Vulkan loader, does system have Vulkan?\n");
	}
	return(false);
}

bool const cVulkan::isFullScreenExclusiveExtensionSupported() const // needed to query support during window creation
{
	return(_fw.isFullScreenExclusiveExtensionSupported());
}
bool const cVulkan::isFullScreenExclusive() const // after the window has been created, this indicates if fullscreen exclusive mode is on.
{
	return(_window->isFullScreenExclusive());
}
void cVulkan::setFullScreenExclusiveEnabled(bool const bEnabled)
{
	_fw.setFullScreenExclusiveEnabled(bEnabled);
}
bool const cVulkan::isHDR() const // after the window has been created, this indicates if HDR is on.
{
	return(_window->isHDR());
}
uint32_t const cVulkan::getMaximumNits() const
{
	return(_fw.getMaximumNits());
}
void cVulkan::setHDREnabled(bool const bEnabled, uint32_t const max_nits)
{
	_fw.setHDREnabled(bEnabled, max_nits);
}

bool const cVulkan::LoadVulkanWindow(GLFWwindow* const glfwwindow)
{
	// Create a window to draw into
	_window = new vku::Window( _fw, _device, _fw.physicalDevice(), _fw.graphicsQueueFamilyIndex(), _fw.computeQueueFamilyIndex(), _fw.transferQueueFamilyIndex(), glfwwindow);
	
	if (!_window->ok()) {
		return(false);
	}

	// Commonly used samplers  //
	vku::SamplerMaker sm;
	// default is vk::SamplerAddressMode::eClampToEdge

	sm.magFilter(vk::Filter::eNearest);
	sm.minFilter(vk::Filter::eNearest);
	sm.mipmapMode(vk::SamplerMipmapMode::eNearest);
	sm.addressModeUVW(vk::SamplerAddressMode::eClampToEdge);
	_sampNearest[eSamplerAddressing::CLAMP] = sm.createUnique(_device);
	sm.addressModeUVW(vk::SamplerAddressMode::eRepeat);
	_sampNearest[eSamplerAddressing::REPEAT] = sm.createUnique(_device);
	sm.addressModeUVW(vk::SamplerAddressMode::eMirroredRepeat);
	_sampNearest[eSamplerAddressing::MIRRORED_REPEAT] = sm.createUnique(_device);
	sm.addressModeUVW(vk::SamplerAddressMode::eClampToBorder);
	_sampNearest[eSamplerAddressing::BORDER] = sm.createUnique(_device);

	sm.magFilter(vk::Filter::eLinear);
	sm.minFilter(vk::Filter::eLinear);
	sm.mipmapMode(vk::SamplerMipmapMode::eLinear);
	sm.addressModeUVW(vk::SamplerAddressMode::eClampToEdge);
	_sampLinear[eSamplerAddressing::CLAMP] = sm.createUnique(_device);
	sm.addressModeUVW(vk::SamplerAddressMode::eRepeat);
	_sampLinear[eSamplerAddressing::REPEAT] = sm.createUnique(_device);
	sm.addressModeUVW(vk::SamplerAddressMode::eMirroredRepeat);
	_sampLinear[eSamplerAddressing::MIRRORED_REPEAT] = sm.createUnique(_device);
	sm.addressModeUVW(vk::SamplerAddressMode::eClampToBorder);
	_sampLinear[eSamplerAddressing::BORDER] = sm.createUnique(_device);

	sm.anisotropyEnable(VK_TRUE);
	sm.maxAnisotropy(Globals::DEFAULT_ANISOTROPIC_LEVEL);
	sm.addressModeUVW(vk::SamplerAddressMode::eClampToEdge);
	_sampAnisotropic[eSamplerAddressing::CLAMP] = sm.createUnique(_device);
	sm.addressModeUVW(vk::SamplerAddressMode::eRepeat);
	_sampAnisotropic[eSamplerAddressing::REPEAT] = sm.createUnique(_device);
	sm.addressModeUVW(vk::SamplerAddressMode::eMirroredRepeat);
	_sampAnisotropic[eSamplerAddressing::MIRRORED_REPEAT] = sm.createUnique(_device);
	sm.addressModeUVW(vk::SamplerAddressMode::eClampToBorder);
	_sampAnisotropic[eSamplerAddressing::BORDER] = sm.createUnique(_device);

	// for nuklear gui shader/pipeline compatbility
	_offscreenImageView2DArray = std::move(_window->offscreenImage().createImageView(_device, vk::ImageViewType::e2DArray));

	return(true);
}

// Nuklear pass is pre-multiply alpha, Depth test is on, depth writes are off for correct transpareency rendering of back to front order.
// should also be used for any alpha / translucent rendering (leverage this pass)
// PRE_MULTIPLY ALPHA must be done for any rendering in this pass ( done in shaders is best )
// eg.) color.rgb = color.rgb * color.a; // color.a passes thru
// if a custom pipeline / shader is needed, 
// the pipeline must be created referencing the correct renderpass (_overlaypass)
// and should match the nuklear pipeline blending state (see below)
void cVulkan::CreateNuklearResources()
{
	std::vector< vku::SpecializationConstant > constants_fs;

	MinCity::VoxelWorld->SetSpecializationConstants_Nuklear_FS(constants_fs);

	// Create two shaders, vertex and fragment. 
	vku::ShaderModule const vert_{ _device, SHADER_BINARY_DIR "Nuklear.vert.bin" };
	vku::ShaderModule const frag_{ _device, SHADER_BINARY_DIR "Nuklear.frag.bin", constants_fs };

	// Build a template for descriptor sets that use these shaders.
	size_t const imageCount(MinCity::Nuklear->getImageCount());

	std::vector<vk::Sampler> samplers; // dynamically replicated to conform with function below for images
	samplers.reserve(imageCount);
	for (size_t i = 0 ; i < imageCount ; ++i ) {
		samplers.emplace_back(getLinearSampler()); // all gui textures use linear samplers
	}

	vku::DescriptorSetLayoutMaker	dslm;
	dslm.buffer(0U, vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex, 1);
	dslm.image(1U, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, (uint32_t)imageCount, samplers.data());

	_nkData.descLayout = dslm.createUnique(_device);

	// Make a default pipeline layout. This shows how pointers
	// to resources are layed out.
	// 
	vku::PipelineLayoutMaker		plm;
	plm.descriptorSetLayout(*_nkData.descLayout);
	plm.pushConstantRange(vk::ShaderStageFlagBits::eFragment, 0, sizeof(UniformDecl::NuklearPushConstants));
	_nkData.pipelineLayout = plm.createUnique(_device);

	{
		vk::DeviceSize const gpuSize(Globals::NK_MAX_VERTEX_BUFFER_SZ);
		for (uint32_t resource_index = 0; resource_index < vku::double_buffer<uint32_t>::count; ++resource_index) {
			_nkData._vbo[resource_index] = vku::DynamicVertexBuffer(gpuSize);
		}
	}
	{
		vk::DeviceSize const gpuSize(Globals::NK_MAX_INDEX_BUFFER_SZ);
		for (uint32_t resource_index = 0; resource_index < vku::double_buffer<uint32_t>::count; ++resource_index) {
			_nkData._ibo[resource_index] = vku::DynamicIndexBuffer(gpuSize);
		}
	}

	// Make a pipeline to use the vertex format and shaders.
	
	vku::PipelineMaker pm(MinCity::getFramebufferSize().x, MinCity::getFramebufferSize().y);
	pm.topology(vk::PrimitiveTopology::eTriangleList);
	pm.shader(vk::ShaderStageFlagBits::eVertex, vert_);
	pm.shader(vk::ShaderStageFlagBits::eFragment, frag_);
	pm.vertexBinding(0, (uint32_t)sizeof(VertexDecl::nk_vertex));
	pm.vertexAttribute(0, 0, vk::Format::eR32G32B32A32Sfloat, (uint32_t)offsetof(VertexDecl::nk_vertex, position_uv));
	pm.vertexAttribute(1, 0, vk::Format::eR8G8B8A8Uint, (uint32_t)offsetof(VertexDecl::nk_vertex, color));

	pm.depthCompareOp(vk::CompareOp::eAlways);			// For GUI, depth is completely disabled, always infront and doesn't need to test depth either
	pm.depthClampEnable(VK_FALSE);
	pm.depthTestEnable(VK_FALSE);
	pm.depthWriteEnable(VK_FALSE);
	pm.cullMode(vk::CullModeFlagBits::eBack);
	pm.frontFace(vk::FrontFace::eClockwise);

	// gui renders to its own surface, which is then blended at the end of the post processing renderpass
	pm.blendBegin(VK_TRUE);
	pm.blendSrcColorBlendFactor(vk::BlendFactor::eOne); // this is pre-multiplied alpha
	pm.blendDstColorBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha);
	pm.blendColorBlendOp(vk::BlendOp::eAdd);
	pm.blendSrcAlphaBlendFactor(vk::BlendFactor::eOne);
	pm.blendDstAlphaBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha);
	pm.blendAlphaBlendOp(vk::BlendOp::eAdd);
	typedef vk::ColorComponentFlagBits ccbf;
	pm.blendColorWriteMask( ccbf::eR | ccbf::eG | ccbf::eB | ccbf::eA );
	
	pm.dynamicState(vk::DynamicState::eScissor);

	// Create a pipeline using a seperate renderPass built for overlay.
	// leveraging overlay render pass, subpass index 2
	pm.subPass(2);
	pm.rasterizationSamples(vku::DefaultSampleCount);

	auto &cache = _fw.pipelineCache();
	_nkData.pipeline = pm.create(_device, cache, *_nkData.pipelineLayout, _window->overlayPass());

	// Create a single entry uniform buffer.
	// We cannot update this buffers with normal memory writes
	// because reading the buffer may happen at any time.
	_nkData._ubo = vku::UniformBuffer{ sizeof(UniformDecl::nk_uniform) };

	// We need to create a descriptor set to tell the shader where
	// our buffers are.
	vku::DescriptorSetMaker			dsm;
	dsm.layout(*_nkData.descLayout);
	_nkData.sets = dsm.create(_device, _fw.descriptorPool());
	_nkData.sets.shrink_to_fit();
}

void cVulkan::CreateComputeResources()
{
	// light -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	{
		{ // descriptor set shared between JFA and SEED, different pipelines //
			// Build a template for descriptor sets that use these shaders.
			vku::DescriptorSetLayoutMaker	dslm;

			auto const samplers{ getSamplerArray
				<eSamplerAddressing::BORDER>(
					eSamplerSampling::LINEAR, eSamplerSampling::LINEAR
				)
			};
			dslm.image(0U, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eCompute, 2, samplers); // 3d volume seed (lightprobes)
			dslm.image(1U, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eCompute, 2, samplers); // 3d volume pingpong input
			dslm.image(2U, vk::DescriptorType::eStorageImage, vk::ShaderStageFlagBits::eCompute, 2); // 3d volume pingpong output
			/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
			dslm.image(3U, vk::DescriptorType::eStorageImage, vk::ShaderStageFlagBits::eCompute, 1); // final output (distance & direction)
			dslm.image(4U, vk::DescriptorType::eStorageImage, vk::ShaderStageFlagBits::eCompute, 1); // final 16bpc output (light color)
			dslm.image(5U, vk::DescriptorType::eStorageImage, vk::ShaderStageFlagBits::eCompute, 1); // final 8bpc output (reflection color)
			
			_comData.light.descLayout = dslm.createUnique(_device);
		}

		// We need to create a descriptor set to tell the shader where
			// our buffers are.
		vku::DescriptorSetMaker			dsm;
		dsm.layout(*_comData.light.descLayout);

		_comData.light.sets = dsm.create(_device, _fw.descriptorPool());
		_comData.light.sets.shrink_to_fit();

		std::vector< vku::SpecializationConstant > constants;
		MinCity::VoxelWorld->SetSpecializationConstants_ComputeLight(constants);

		// SEED & JFA  //

		vku::PipelineLayoutMaker		plm;
		plm.descriptorSetLayout(*_comData.light.descLayout);
		// pipeline layout is the same/shared across all stages
		plm.pushConstantRange(vk::ShaderStageFlagBits::eCompute, 0, sizeof(UniformDecl::ComputeLightPushConstants));

		_comData.light.pipelineLayout = plm.createUnique(_device);
		{
			vku::ShaderModule const comp{ _device, SHADER_BINARY_DIR "light_seed.comp.bin", constants };

			// Make a pipeline to use the vertex format and shaders.
			vku::ComputePipelineMaker pm{};
			pm.shader(vk::ShaderStageFlagBits::eCompute, comp);

			// Create a pipeline using a renderPass built for our window.
			auto& cache = _fw.pipelineCache();
			_comData.light.pipeline[eComputeLightPipeline::SEED] = pm.createUnique(_device, cache, *_comData.light.pipelineLayout);
		}
		{
			vku::ShaderModule const comp{ _device, SHADER_BINARY_DIR "light_jfa.comp.bin", constants };

			// Make a pipeline to use the vertex format and shaders.
			vku::ComputePipelineMaker pm{};
			pm.shader(vk::ShaderStageFlagBits::eCompute, comp);

			// Create a pipeline using a renderPass built for our window.
			auto& cache = _fw.pipelineCache();
			_comData.light.pipeline[eComputeLightPipeline::JFA] = pm.createUnique(_device, cache, *_comData.light.pipelineLayout);
		}

		// FILTER //
		{
			vku::ShaderModule const comp{ _device, SHADER_BINARY_DIR "light_mip.comp.bin", constants };

			// Make a pipeline to use the vertex format and shaders.
			vku::ComputePipelineMaker pm{};
			pm.shader(vk::ShaderStageFlagBits::eCompute, comp);

			// Create a pipeline using a renderPass built for our window.
			auto& cache = _fw.pipelineCache();
			_comData.light.pipeline[eComputeLightPipeline::FILTER] = pm.createUnique(_device, cache, *_comData.light.pipelineLayout);
		}
	}

	// [[deprecated]] texture shaders -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
	/* {
		// shared/common among texture shaders:
		{
			// Build a template for descriptor sets that use these shaders.
			vku::DescriptorSetLayoutMaker	dslm;

			dslm.image(0U, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eCompute, 1); // input texture - not using immutable sampler, so it can be customized per texture shaders decriptor set.
			dslm.image(1U, vk::DescriptorType::eStorageImage, vk::ShaderStageFlagBits::eCompute, 1); // output texture

			_comData.texture.descLayout = dslm.createUnique(_device);
		}

		// We need to create a descriptor set to tell the shader where
				// our buffers are.
		vku::DescriptorSetMaker			dsm;
		dsm.layout(*_comData.texture.descLayout);

		for (uint32_t shader = 0; shader < eTextureShader::_size(); ++shader)
		{
			_comData.texture.sets[shader] = dsm.create(_device, _fw.descriptorPool());
		}

		vku::PipelineLayoutMaker		plm;
		plm.descriptorSetLayout(*_comData.texture.descLayout);
		plm.pushConstantRange(vk::ShaderStageFlagBits::eCompute, 0, sizeof(UniformDecl::TextureShaderPushConstants));

		_comData.texture.pipelineLayout = plm.createUnique(_device);

		for (uint32_t shader = 0; shader < eTextureShader::_size(); ++shader)
		{
			std::string const szFile(fmt::format(FMT_STRING("textureshader_{:03d}.comp.bin"), shader));
			std::wstring wszFile(szFile.begin(), szFile.end());
			wszFile = SHADER_BINARY_DIR + wszFile;

			std::vector< vku::SpecializationConstant > constants_textureshader;
			MinCity::VoxelWorld->SetSpecializationConstants_TextureShader(constants_textureshader, shader);	// each textureshader can have distinct specialization constants

			vku::ShaderModule const comp{ _device, wszFile, constants_textureshader };

			// Make a pipeline to use the vertex format and shaders.
			vku::ComputePipelineMaker pm{};
			pm.shader(vk::ShaderStageFlagBits::eCompute, comp);

			// Create a pipeline using a renderPass built for our window.
			auto& cache = _fw.pipelineCache();
			_comData.texture.pipeline[shader] = pm.createUnique(_device, cache, *_comData.texture.pipelineLayout);
		}
	}*/
}

void cVulkan::CreateVolumetricResources()
{
	std::vector< vku::SpecializationConstant > constantsVS, constantsFS;
		
	// Create two shaders, vertex and fragment. 
	MinCity::VoxelWorld->SetSpecializationConstants_VolumetricLight_VS(constantsVS);
	vku::ShaderModule const vert_{ _device, SHADER_BINARY_DIR "volumetric.vert.bin", constantsVS };

	MinCity::VoxelWorld->SetSpecializationConstants_VolumetricLight_FS(constantsFS);
	vku::ShaderModule const frag_{ _device, SHADER_BINARY_DIR "volumetric.frag.bin", constantsFS };

	// Build a template for descriptor sets that use these shaders.
	auto const samplers{ getSamplerArray
			<eSamplerAddressing::CLAMP>(
				eSamplerSampling::LINEAR, eSamplerSampling::LINEAR, eSamplerSampling::LINEAR, eSamplerSampling::LINEAR
			)
	};
	vku::DescriptorSetLayoutMaker	dslm;
	dslm.buffer(0U, vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex, 1);
	dslm.image(1U, vk::DescriptorType::eInputAttachment, vk::ShaderStageFlagBits::eFragment, 1);  // half-res depth
	dslm.image(2U, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1, &getNearestSampler<eSamplerAddressing::REPEAT>());  // blue noise
	dslm.image(3U, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 4, samplers);  // lightmap volume textures (distance & direction), (color) + opacity volume texture
	dslm.image(4U, vk::DescriptorType::eStorageImage, vk::ShaderStageFlagBits::eFragment, 2); // writeonly bounce light (reflection), volumetrics output
#ifdef DEBUG_VOLUMETRIC
	dslm.buffer(10U, vk::DescriptorType::eStorageBuffer, vk::ShaderStageFlagBits::eFragment, 1);
#endif	
	_volData.descLayout = dslm.createUnique(_device);

	// We need to create a descriptor set to tell the shader where
	// our buffers are.
	vku::DescriptorSetMaker			dsm;
	dsm.layout(*_volData.descLayout);

	// double buffered
	_volData.sets.emplace_back(dsm.create(_device, _fw.descriptorPool())[0]);
	_volData.sets.emplace_back(dsm.create(_device, _fw.descriptorPool())[0]);
	_volData.sets.shrink_to_fit();

	// Make a default pipeline layout. This shows how pointers
	// to resources are layed out.
	// 
	vku::PipelineLayoutMaker		plm;
	plm.descriptorSetLayout(*_volData.descLayout);

	_volData.pipelineLayout = plm.createUnique(_device);

	// todo:  Unit Cube static vbo & ibo here
	// Setup vertices indices for a cube
	{
		VertexDecl::just_position const vertices[] = {
			{ { 0.0f,  0.0f,  1.0f }, },
			{ { 1.0f,  0.0f,  1.0f }, },
			{ { 1.0f,  1.0f,  1.0f }, },
			{ { 0.0f,  1.0f,  1.0f }, },
			{ { 0.0f,  0.0f,  0.0f }, },
			{ { 1.0f,  0.0f,  0.0f }, },
			{ { 1.0f,  1.0f,  0.0f }, },
			{ { 0.0f,  1.0f,  0.0f }, },
		};

		uint16_t const indices[] = {
			0,1,2, 2,3,0, 1,5,6, 6,2,1, 7,6,5, 5,4,7, 4,0,3, 3,7,4, 4,5,1, 1,0,4, 3,2,6, 6,7,3,
		};
		
		{
			vk::DeviceSize gpuSize = _countof(vertices) * sizeof(VertexDecl::just_position);
			_volData._vbo = vku::VertexBuffer(gpuSize);
			_volData._vbo.upload(_device, dmaTransferPool(vku::eCommandPools::DMA_TRANSFER_POOL_PRIMARY), transferQueue(), vertices, gpuSize);
		}
		{
			vk::DeviceSize gpuSize = _countof(indices) * sizeof(uint16_t);
			_volData._ibo = vku::IndexBuffer(gpuSize);
			_volData._ibo.upload(_device, dmaTransferPool(vku::eCommandPools::DMA_TRANSFER_POOL_PRIMARY), transferQueue(), indices, gpuSize);
			_volData.index_count = _countof(indices);
		}
	}

	// Make a pipeline to use the vertex format and shaders.
	point2D const frameBufferSz(MinCity::getFramebufferSize());
	point2D_t const downResFrameBufferSz(vku::getDownResolution(frameBufferSz));

	vku::PipelineMaker pm(uint32_t(downResFrameBufferSz.x), uint32_t(downResFrameBufferSz.y));
	pm.topology(vk::PrimitiveTopology::eTriangleList);
	pm.shader(vk::ShaderStageFlagBits::eVertex, vert_);
	pm.shader(vk::ShaderStageFlagBits::eFragment, frag_);
	pm.vertexBinding(0, (uint32_t)sizeof(VertexDecl::just_position));
	pm.vertexAttribute(0, 0, vk::Format::eR32G32B32A32Sfloat, (uint32_t)offsetof(VertexDecl::just_position, position));

	pm.depthCompareOp(vk::CompareOp::eAlways);								// volumetric raymarch - half resolution framebuffer, and additionally checkerboarded. [1/8 rays / fullresolution pixel]
	pm.depthClampEnable(VK_FALSE);											// * * * * * superior speed and quality (blue noise temporal checkerboard is flawless reconstruction), additionally bilateraly upscale to full resolution framebuffer used in final blending.
	pm.depthTestEnable(VK_FALSE);											// for volume rendering, for *best* results:
	pm.depthWriteEnable(VK_FALSE);											//  - depth testing and writing disabled.
	pm.cullMode(vk::CullModeFlagBits::eBack);								//  - back & clockwise works, and is common to the engine usage for all pipelines. -c-u-l-l-i-n-g- -f-r-o-n-t- -f-a-c-e-s- -a-n-d- -c-o-u-n-t-e-r- -c--lo-c-k--wi--se- -m-u-s--t -b-e-- u-s-e--d
	pm.frontFace(vk::FrontFace::eClockwise);								//    (chosen based on whether the volume intersects the near plane or far plane. volume closest to near plane. 
																			//  - in the volumetric shader:
																			//     - top of file: layout(early_fragment_tests) in;
																			//		 (working optimization, and if not enabled flickering of transparent surfaces witnessed)
																			//
	// ################################										// - stencil buffer enabled
	pm.stencilTestEnable(VK_TRUE); // only stencil							//   (used for alternating checkerboard rendering)
	vk::StencilOpState const stencilOp{
		/*vk::StencilOp failOp_ =*/ vk::StencilOp::eKeep,
		/*vk::StencilOp passOp_ =*/ vk::StencilOp::eKeep,
		/*vk::StencilOp depthFailOp_ =*/ vk::StencilOp::eKeep,
		/*vk::CompareOp compareOp_ =*/ vk::CompareOp::eEqual,
		/*uint32_t compareMask_ =*/ (uint32_t)0xff,
		/*uint32_t writeMask_ =*/ (uint32_t)0,
		/*uint32_t reference_ =*/ (uint32_t)vku::STENCIL_CHECKER_REFERENCE
	};
	pm.front(stencilOp);
	pm.back(stencilOp);

	pm.rasterizationSamples(vk::SampleCountFlagBits::e1);
	pm.sampleShadingEnable(VK_TRUE);
	pm.minSampleShading(0.25f);
	pm.blendBegin(VK_FALSE);
	pm.blendColorWriteMask((vk::ColorComponentFlagBits)0); // no color writes, all imageStores

#if !defined(NDEBUG) && defined(LIVESHADER_MODE) && (LIVE_SHADER == LIVE_SHADER_VOLUMETRIC)
	liveshader::cache_pipeline_creation(pm);
#endif

	// Create a pipeline using a seperate renderPass built for half resolution
	auto& cache = _fw.pipelineCache();
	_volData.pipeline = pm.create(_device, cache, *_volData.pipelineLayout, _window->downPass());
}

void cVulkan::CreateUpsampleResources()
{
	{
		vku::ShaderModule const vert_{ _device, SHADER_BINARY_DIR "postquad_resolve.vert.bin" }; // common for resolve stage & upsample stage

		// Create two shaders, vertex and fragment. 
		{ constexpr uint32_t const index = eUpsamplePipeline::RESOLVE;

			std::vector< vku::SpecializationConstant > constants;

			MinCity::VoxelWorld->SetSpecializationConstants_Resolve(constants);

			vku::ShaderModule const frag_{ _device, SHADER_BINARY_DIR "upsample_resolve.frag.bin", constants };

			// Build a template for descriptor sets that use these shaders.
			auto const samplers{ getSamplerArray
				<eSamplerAddressing::CLAMP>(
					eSamplerSampling::LINEAR, eSamplerSampling::LINEAR
				)
			};

			vku::DescriptorSetLayoutMaker	dslm;
			dslm.buffer(0U, vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex, 1);
			dslm.image(1U, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1, &getNearestSampler<eSamplerAddressing::REPEAT>());  // bluenoise
			dslm.image(2U, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 2, samplers);  // checkered volumetrics, reflection to resolve
			dslm.image(3U, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 2, samplers);  // full resolution volumetrics & reflection (effectively last frames volumetric & reflection full resolution output)

			_upData[index].descLayout = dslm.createUnique(_device);
			// We need to create a descriptor set to tell the shader where
			// our buffers are.
			vku::DescriptorSetMaker			dsm;
			dsm.layout(*_upData[index].descLayout);

			// double buffered
			_upData[index].sets.emplace_back(dsm.create(_device, _fw.descriptorPool())[0]);
			_upData[index].sets.emplace_back(dsm.create(_device, _fw.descriptorPool())[0]);
			_upData[index].sets.shrink_to_fit();

			// Make a default pipeline layout. This shows how pointers
			// to resources are layed out.
			// 
			vku::PipelineLayoutMaker		plm;
			plm.descriptorSetLayout(*_upData[index].descLayout);
			_upData[index].pipelineLayout = plm.createUnique(_device);

			// Make a pipeline to use the vertex format and shaders.
			point2D const frameBufferSz(MinCity::getFramebufferSize());
			point2D_t const downResFrameBufferSz(vku::getDownResolution(frameBufferSz));

			vku::PipelineMaker pm(uint32_t(downResFrameBufferSz.x), uint32_t(downResFrameBufferSz.y));
			pm.shader(vk::ShaderStageFlagBits::eVertex, vert_);
			pm.shader(vk::ShaderStageFlagBits::eFragment, frag_);

			pm.depthCompareOp(vk::CompareOp::eAlways);
			pm.depthClampEnable(VK_FALSE);
			pm.depthTestEnable(VK_FALSE);
			pm.depthWriteEnable(VK_FALSE);
			pm.cullMode(vk::CullModeFlagBits::eBack);
			pm.frontFace(vk::FrontFace::eClockwise);

			// 2 color attachments (out) requires 2 blend states to be emplaced
			pm.blendBegin(VK_FALSE);
			pm.blendBegin(VK_FALSE);

			// Create a pipeline using a renderPass
			// leveraging down render pass, subpass index 1
			pm.subPass(1);
			pm.rasterizationSamples(vk::SampleCountFlagBits::e1);

			auto& cache = _fw.pipelineCache();
			_upData[index].pipeline = pm.create(_device, cache, *_upData[index].pipelineLayout, _window->downPass());
		}

		{ constexpr uint32_t const index = eUpsamplePipeline::UPSAMPLE;

			std::vector< vku::SpecializationConstant > constants;

			MinCity::VoxelWorld->SetSpecializationConstants_Upsample(constants);
			vku::ShaderModule const frag_{ _device, SHADER_BINARY_DIR "upsample.frag.bin", constants };

			// Build a template for descriptor sets that use these shaders.
			vku::DescriptorSetLayoutMaker	dslm;
			dslm.buffer(0U, vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex, 1);
			dslm.image(1U, vk::DescriptorType::eInputAttachment, vk::ShaderStageFlagBits::eFragment, 1);  // full resolution depth
			dslm.image(2U, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1, &getNearestSampler());  // half resolution depth
			dslm.image(3U, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1, &getNearestSampler<eSamplerAddressing::REPEAT>());  // bluenoise
			dslm.image(4U, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1, &getLinearSampler());  // half resolution volumetric color source
			dslm.image(5U, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1, &getLinearSampler());  // half resolution bounce light (reflection) source
			dslm.buffer(6U, vk::DescriptorType::eStorageBuffer, vk::ShaderStageFlagBits::eFragment, 1);

			_upData[index].descLayout = dslm.createUnique(_device);
			// We need to create a descriptor set to tell the shader where
			// our buffers are.
			vku::DescriptorSetMaker			dsm;
			dsm.layout(*_upData[index].descLayout);

			// double buffered
			_upData[index].sets.emplace_back(dsm.create(_device, _fw.descriptorPool())[0]);
			_upData[index].sets.emplace_back(dsm.create(_device, _fw.descriptorPool())[0]);
			_upData[index].sets.shrink_to_fit();

			// Make a default pipeline layout. This shows how pointers
			// to resources are layed out.
			// 
			vku::PipelineLayoutMaker		plm;
			plm.descriptorSetLayout(*_upData[index].descLayout);
			_upData[index].pipelineLayout = plm.createUnique(_device);

			// Make a pipeline to use the vertex format and shaders.

			vku::PipelineMaker pm(MinCity::getFramebufferSize().x, MinCity::getFramebufferSize().y);
			pm.shader(vk::ShaderStageFlagBits::eVertex, vert_);
			pm.shader(vk::ShaderStageFlagBits::eFragment, frag_);

			pm.depthCompareOp(vk::CompareOp::eAlways);
			pm.depthClampEnable(VK_FALSE);
			pm.depthTestEnable(VK_FALSE);
			pm.depthWriteEnable(VK_FALSE);
			pm.cullMode(vk::CullModeFlagBits::eBack);
			pm.frontFace(vk::FrontFace::eClockwise);

			// 2 color attachments (out) requires 2 blend states to be emplaced
			pm.blendBegin(VK_FALSE);
			pm.blendBegin(VK_FALSE);

			// Create a pipeline using a renderPass
			// leveraging up render pass, subpass index 0
			pm.subPass(0);
			pm.rasterizationSamples(vk::SampleCountFlagBits::e1);

	#if !defined(NDEBUG) && defined(LIVESHADER_MODE) && (LIVE_SHADER == LIVE_SHADER_UPSAMPLE)
			liveshader::cache_pipeline_creation(pm);
	#endif
			auto& cache = _fw.pipelineCache();
			_upData[index].pipeline = pm.create(_device, cache, *_upData[index].pipelineLayout, _window->upPass());
		}
	}
	{ constexpr uint32_t const index = eUpsamplePipeline::BLEND;

		vku::ShaderModule const vert_{ _device, SHADER_BINARY_DIR "postquad.vert.bin" };
		vku::ShaderModule const frag_{ _device, SHADER_BINARY_DIR "upsample_blend.frag.bin" };

		// Build a template for descriptor sets that use these shaders.
		vku::DescriptorSetLayoutMaker	dslm;
		dslm.image(0U, vk::DescriptorType::eInputAttachment, vk::ShaderStageFlagBits::eFragment, 1);  // full resolution volumetrics upsample

		_upData[index].descLayout = dslm.createUnique(_device);
		// We need to create a descriptor set to tell the shader where
		// our buffers are.
		vku::DescriptorSetMaker			dsm;
		dsm.layout(*_upData[index].descLayout);
		_upData[index].sets = dsm.create(_device, _fw.descriptorPool());
		_upData[index].sets.shrink_to_fit();

		// Make a default pipeline layout. This shows how pointers
		// to resources are layed out.
		// 
		vku::PipelineLayoutMaker		plm;
		plm.descriptorSetLayout(*_upData[index].descLayout);
		_upData[index].pipelineLayout = plm.createUnique(_device);

		// Make a pipeline to use the vertex format and shaders.

		vku::PipelineMaker pm(MinCity::getFramebufferSize().x, MinCity::getFramebufferSize().y);
		pm.shader(vk::ShaderStageFlagBits::eVertex, vert_);
		pm.shader(vk::ShaderStageFlagBits::eFragment, frag_);

		pm.depthCompareOp(vk::CompareOp::eAlways);
		pm.depthClampEnable(VK_FALSE);
		pm.depthTestEnable(VK_FALSE);
		pm.depthWriteEnable(VK_FALSE);
		pm.cullMode(vk::CullModeFlagBits::eBack);
		pm.frontFace(vk::FrontFace::eClockwise);

		pm.blendBegin(VK_TRUE);
		pm.blendSrcColorBlendFactor(vk::BlendFactor::eOne); // this is pre-multiplied alpha *specific* to handle transmission properly. *do not change* https://patapom.com/topics/Revision2013/Revision%202013%20-%20Real-time%20Volumetric%20Rendering%20Course%20Notes.pdf
		pm.blendDstColorBlendFactor(vk::BlendFactor::eSrcAlpha);
		pm.blendColorBlendOp(vk::BlendOp::eAdd);
		pm.blendSrcAlphaBlendFactor(vk::BlendFactor::eOne);
		pm.blendDstAlphaBlendFactor(vk::BlendFactor::eSrcAlpha);
		pm.blendAlphaBlendOp(vk::BlendOp::eAdd);
		typedef vk::ColorComponentFlagBits ccbf;
		pm.blendColorWriteMask(ccbf::eR | ccbf::eG | ccbf::eB); // must preserve alpha transparency mask) which is used by overlay pass

		// Create a pipeline using a renderPass
		// leveraging mid render pass, subpass index 1
		pm.subPass(1);
		pm.rasterizationSamples(vku::DefaultSampleCount);

		auto& cache = _fw.pipelineCache();
		_upData[index].pipeline = pm.create(_device, cache, *_upData[index].pipelineLayout, _window->midPass());
	}
}

void cVulkan::CreateDepthResolveResources()
{
	// Create two shaders, vertex and fragment. 
	vku::ShaderModule const vert_{ _device, SHADER_BINARY_DIR "postquad.vert.bin" };
	vku::ShaderModule const frag_{ _device, SHADER_BINARY_DIR "depthresolve.frag.bin" };

	// Build a template for descriptor sets that use these shaders.
	vku::DescriptorSetLayoutMaker	dslm;
	dslm.image(0U, vk::DescriptorType::eInputAttachment, vk::ShaderStageFlagBits::eFragment, 1);  // full resolution depthbuffer in
	dslm.image(1U, vk::DescriptorType::eStorageImage, vk::ShaderStageFlagBits::eFragment, 1);  // half resolution depthbuffer out

	_depthData.descLayout = dslm.createUnique(_device);
	// We need to create a descriptor set to tell the shader where
	// our buffers are.
	vku::DescriptorSetMaker			dsm;
	dsm.layout(*_depthData.descLayout);
	_depthData.sets = dsm.create(_device, _fw.descriptorPool());
	_depthData.sets.shrink_to_fit();

	// Make a default pipeline layout. This shows how pointers
	// to resources are layed out.
	// 
	vku::PipelineLayoutMaker		plm;
	plm.descriptorSetLayout(*_depthData.descLayout);
	_depthData.pipelineLayout = plm.createUnique(_device);

	// Make a pipeline to use the vertex format and shaders.

	vku::PipelineMaker pm(MinCity::getFramebufferSize().x, MinCity::getFramebufferSize().y);
	pm.shader(vk::ShaderStageFlagBits::eVertex, vert_);
	pm.shader(vk::ShaderStageFlagBits::eFragment, frag_);

	pm.depthCompareOp(vk::CompareOp::eAlways);
	pm.depthClampEnable(VK_FALSE); 
	pm.depthTestEnable(VK_FALSE);
	pm.depthWriteEnable(VK_FALSE);
	pm.cullMode(vk::CullModeFlagBits::eBack);
	pm.frontFace(vk::FrontFace::eClockwise);

	// Create a pipeline using a renderPass
	// leveraging z render pass, subpass index 1
	pm.subPass(1U);
	pm.rasterizationSamples(vk::SampleCountFlagBits::e1);

	auto& cache = _fw.pipelineCache();
	_depthData.pipeline = pm.create(_device, cache, *_depthData.pipelineLayout, _window->zPass());
}

void cVulkan::CreatePostAAResources()
{
	// Build a template for descriptor sets that use these shaders. *same descriptor set used across all post-process shaders*
	vku::DescriptorSetLayoutMaker	dslm;
	dslm.buffer(0U, vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eVertex, 1);
	dslm.image(1U, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1, &getLinearSampler());  // full resolution backbuffer color source
	dslm.image(2U, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1, &getNearestSampler<eSamplerAddressing::REPEAT>());  // 2d blue noise

	dslm.image(3U, vk::DescriptorType::eStorageImage, vk::ShaderStageFlagBits::eFragment, 1, nullptr);										// full resolution out temporal
	dslm.image(4U, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1, &getLinearSampler());	// full resolution temporal source 
	
	dslm.image(5U, vk::DescriptorType::eStorageImage, vk::ShaderStageFlagBits::eFragment, 1, nullptr);										// full resolution out last color
	dslm.image(6U, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1, &getLinearSampler());  // last color source

	auto const samplers{ getSamplerArray
			<eSamplerAddressing::CLAMP>(
				eSamplerSampling::LINEAR, eSamplerSampling::LINEAR
			)
	};

	dslm.image(7U, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 2, samplers);	// full resolution blurstep color source 
	dslm.image(8U, vk::DescriptorType::eStorageImage, vk::ShaderStageFlagBits::eFragment, 2, nullptr);										// full resolution out blurstep
	
	dslm.image(9U, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 2, samplers);							// anamorphic flare array source
	dslm.image(10U, vk::DescriptorType::eStorageImage, vk::ShaderStageFlagBits::eFragment, 2, nullptr);										// anamorphic flare array out
	dslm.image(11U, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1, samplers);					// 3d lut
	dslm.image(12U, vk::DescriptorType::eInputAttachment, vk::ShaderStageFlagBits::eFragment, 1, nullptr);									// input gui
	dslm.image(13U, vk::DescriptorType::eInputAttachment, vk::ShaderStageFlagBits::eFragment, 1, nullptr);									// input presentation 0
	dslm.image(14U, vk::DescriptorType::eInputAttachment, vk::ShaderStageFlagBits::eFragment, 1, nullptr);									// input presentation 1
	
	_aaData.descLayout = dslm.createUnique(_device);	// *same descriptor set used across all post - process shaders *
	// We need to create a descriptor set to tell the shader where
	// our buffers are.
	vku::DescriptorSetMaker			dsm;	// *same descriptor set used across all post-process shaders*
	dsm.layout(*_aaData.descLayout);

	// double buffered
	_aaData.sets.emplace_back(dsm.create(_device, _fw.descriptorPool())[0]);
	_aaData.sets.emplace_back(dsm.create(_device, _fw.descriptorPool())[0]);
	_aaData.sets.shrink_to_fit();

	// Make a default pipeline layout. This shows how pointers
	// to resources are layed out.
	// 
	vku::PipelineLayoutMaker		plm;
	plm.descriptorSetLayout(*_aaData.descLayout);			// *same pipeline layout used across all post - process shaders *
	plm.pushConstantRange(vk::ShaderStageFlagBits::eFragment, 0, sizeof(UniformDecl::PostPushConstants));
	_aaData.pipelineLayout = plm.createUnique(_device);

	std::vector< vku::SpecializationConstant > constants;

	// Create two unique shaders per pass, vertex and fragment. 
	MinCity::VoxelWorld->SetSpecializationConstants_PostAA(constants);

	// Make 5 distict pipelines to use the vertex format and shaders.
	vku::ShaderModule const vertcommon_{ _device, SHADER_BINARY_DIR "postquad.vert.bin", constants }; // common except for last 2 passes

	{ // temporal resolve subpass + anamorphic mask downsample + blur mask downsample
		vku::PipelineMaker pm(MinCity::getFramebufferSize().x, MinCity::getFramebufferSize().y);
		pm.depthCompareOp(vk::CompareOp::eAlways);
		pm.depthClampEnable(VK_FALSE); 
		pm.depthTestEnable(VK_FALSE);
		pm.depthWriteEnable(VK_FALSE);
		pm.cullMode(vk::CullModeFlagBits::eBack);
		pm.frontFace(vk::FrontFace::eClockwise);
		pm.subPass(0);

		vku::ShaderModule const frag_{ _device, SHADER_BINARY_DIR "postaatmp.frag.bin", constants };
		pm.shader(vk::ShaderStageFlagBits::eVertex, vertcommon_);
		pm.shader(vk::ShaderStageFlagBits::eFragment, frag_);

		// Create a pipeline using a renderPass
		pm.rasterizationSamples(vk::SampleCountFlagBits::e1);
		auto& cache = _fw.pipelineCache();
		_aaData.pipeline[0] = pm.create(_device, cache, *_aaData.pipelineLayout, _window->finalPass());
	}
	{ // blur downsampled horizontally + anamorphic reduction
		vku::PipelineMaker pm(MinCity::getFramebufferSize().x, MinCity::getFramebufferSize().y);
		pm.depthCompareOp(vk::CompareOp::eAlways);
		pm.depthClampEnable(VK_FALSE); 
		pm.depthTestEnable(VK_FALSE);
		pm.depthWriteEnable(VK_FALSE);
		pm.cullMode(vk::CullModeFlagBits::eBack);
		pm.frontFace(vk::FrontFace::eClockwise);
		pm.subPass(0);

		vku::ShaderModule const frag_{ _device, SHADER_BINARY_DIR "postaapp0.frag.bin", constants };
		pm.shader(vk::ShaderStageFlagBits::eVertex, vertcommon_);
		pm.shader(vk::ShaderStageFlagBits::eFragment, frag_);

		// Create a pipeline using a renderPass
		pm.rasterizationSamples(vk::SampleCountFlagBits::e1);
		auto& cache = _fw.pipelineCache();
		_aaData.pipeline[1] = pm.create(_device, cache, *_aaData.pipelineLayout, _window->finalPass());
	}
	{ // blur downsampled vertically
		vku::PipelineMaker pm(MinCity::getFramebufferSize().x, MinCity::getFramebufferSize().y);
		pm.depthCompareOp(vk::CompareOp::eAlways);
		pm.depthClampEnable(VK_FALSE);
		pm.depthTestEnable(VK_FALSE);
		pm.depthWriteEnable(VK_FALSE);
		pm.cullMode(vk::CullModeFlagBits::eBack);
		pm.frontFace(vk::FrontFace::eClockwise);
		pm.subPass(0);

		vku::ShaderModule const frag_{ _device, SHADER_BINARY_DIR "postaapp1.frag.bin", constants };
		pm.shader(vk::ShaderStageFlagBits::eVertex, vertcommon_);
		pm.shader(vk::ShaderStageFlagBits::eFragment, frag_);

		// Create a pipeline using a renderPass
		pm.rasterizationSamples(vk::SampleCountFlagBits::e1);
		auto& cache = _fw.pipelineCache();
		_aaData.pipeline[2] = pm.create(_device, cache, *_aaData.pipelineLayout, _window->finalPass());
	}

	if (isHDR()) {
		// *** this clears the current vector of constants, and replaces it with the spec constants w/Maximum Nits defined
		constants.clear();
		// required for the last 2 passes / pipelines if HDR is on
		MinCity::VoxelWorld->SetSpecializationConstants_PostAA_HDR(constants);
	}

	{ // final pass aa + blur upsample + dithering + anamorphic flare	
		vku::PipelineMaker pm(MinCity::getFramebufferSize().x, MinCity::getFramebufferSize().y);
		pm.depthCompareOp(vk::CompareOp::eAlways);
		pm.depthClampEnable(VK_FALSE); 
		pm.depthTestEnable(VK_FALSE);
		pm.depthWriteEnable(VK_FALSE);
		pm.cullMode(vk::CullModeFlagBits::eBack);
		pm.frontFace(vk::FrontFace::eClockwise);
		pm.subPass(0);

		std::wstring szFragShader;
		if (isHDR()) { // select shader
			szFragShader = SHADER_BINARY_DIR "postaapp2_hdr.frag.bin";
		}
		else {
			szFragShader = SHADER_BINARY_DIR "postaapp2.frag.bin";
		}
		vku::ShaderModule const vert_{ _device, SHADER_BINARY_DIR "postquadpp2.vert.bin" };
		vku::ShaderModule const frag_{ _device, szFragShader, constants };
		pm.shader(vk::ShaderStageFlagBits::eVertex, vert_);
		pm.shader(vk::ShaderStageFlagBits::eFragment, frag_);

		// Create a pipeline using a renderPass
		pm.rasterizationSamples(vk::SampleCountFlagBits::e1);
		auto& cache = _fw.pipelineCache();
		_aaData.pipeline[3] = pm.create(_device, cache, *_aaData.pipelineLayout, _window->finalPass());
	}
	{ // overlay final(actual) subpass(gui overlay)
		vku::PipelineMaker pm(MinCity::getFramebufferSize().x, MinCity::getFramebufferSize().y);
		pm.depthCompareOp(vk::CompareOp::eAlways);
		pm.depthClampEnable(VK_FALSE); 
		pm.depthTestEnable(VK_FALSE);
		pm.depthWriteEnable(VK_FALSE);
		pm.cullMode(vk::CullModeFlagBits::eBack);
		pm.frontFace(vk::FrontFace::eClockwise);
		
		std::wstring szFragShader;
		if (isHDR()) { // select shader
			szFragShader = SHADER_BINARY_DIR "postaaoverlay_hdr.frag.bin";
		}
		else {
			szFragShader = SHADER_BINARY_DIR "postaaoverlay.frag.bin";
		}
		vku::ShaderModule const vert_{ _device, SHADER_BINARY_DIR "postquad_overlay.vert.bin" };
		vku::ShaderModule const frag_{ _device, szFragShader, constants };
		pm.shader(vk::ShaderStageFlagBits::eVertex, vert_);
		pm.shader(vk::ShaderStageFlagBits::eFragment, frag_);

		pm.blendBegin(VK_TRUE);
		pm.blendSrcColorBlendFactor(vk::BlendFactor::eOne); // this is pre-multiplied alpha
		pm.blendDstColorBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha);
		pm.blendColorBlendOp(vk::BlendOp::eAdd);
		pm.blendSrcAlphaBlendFactor(vk::BlendFactor::eOne);
		pm.blendDstAlphaBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha);
		pm.blendAlphaBlendOp(vk::BlendOp::eAdd);

		typedef vk::ColorComponentFlagBits ccbf;
		pm.blendColorWriteMask(ccbf::eR | ccbf::eG | ccbf::eB | ccbf::eA);

		// Create a pipeline using a renderPass
		pm.rasterizationSamples(vk::SampleCountFlagBits::e1);
		pm.subPass(1U);

		auto& cache = _fw.pipelineCache();
		_aaData.pipeline[4] = pm.create(_device, cache, *_aaData.pipelineLayout, _window->finalPass());
	}
	
	{ // presentation
		vku::PipelineMaker pm(MinCity::getFramebufferSize().x, MinCity::getFramebufferSize().y);
		pm.depthCompareOp(vk::CompareOp::eAlways);
		pm.depthClampEnable(VK_FALSE);
		pm.depthTestEnable(VK_FALSE);
		pm.depthWriteEnable(VK_FALSE);
		pm.cullMode(vk::CullModeFlagBits::eBack);
		pm.frontFace(vk::FrontFace::eClockwise);
		
		vku::ShaderModule const frag_{ _device, SHADER_BINARY_DIR "postaapp3.frag.bin", constants };
		pm.shader(vk::ShaderStageFlagBits::eVertex, vertcommon_);
		pm.shader(vk::ShaderStageFlagBits::eFragment, frag_);

		// Create a pipeline using a renderPass
		pm.rasterizationSamples(vk::SampleCountFlagBits::e1);
		pm.subPass(2U);
		
		auto& cache = _fw.pipelineCache();
		_aaData.pipeline[5] = pm.create(_device, cache, *_aaData.pipelineLayout, _window->finalPass());
	}
}

void cVulkan::CreateVoxelResources()
{
	CreateSharedVoxelResources();	//MUST be 1st

	vk::DeviceSize totalSize{};
	vku::ShaderModule const geom_null_{}, frag_null_{};
	vku::ShaderModule const frag_basic_{ _device, SHADER_BINARY_DIR "uniforms_basic.frag.bin" };

	{ // terrain voxels //

		std::vector< vku::SpecializationConstant > constants_terrain_basic_vs, constants_terrain_vs, constants_terrain_gs, constants_terrain_fs;
		MinCity::VoxelWorld->SetSpecializationConstants_VoxelTerrain_Basic_VS(constants_terrain_basic_vs);
		MinCity::VoxelWorld->SetSpecializationConstants_VoxelTerrain_VS(constants_terrain_vs);
		MinCity::VoxelWorld->SetSpecializationConstants_VoxelTerrain_GS(constants_terrain_gs);
		MinCity::VoxelWorld->SetSpecializationConstants_VoxelTerrain_FS(constants_terrain_fs);

		vku::ShaderModule const vert_{ _device, SHADER_BINARY_DIR "uniforms_height.vert.bin", constants_terrain_vs };
		vku::ShaderModule const geom_{ _device, SHADER_BINARY_DIR "uniforms_height.geom.bin", constants_terrain_gs };
		vku::ShaderModule const frag_{ _device, SHADER_BINARY_DIR "uniforms_t2d.frag.bin", constants_terrain_fs };

		vk::DeviceSize gpuSize{};

		// main vertex buffer for terrain
		gpuSize = Volumetric::Allocation::VOXEL_GRID_VISIBLE_TOTAL * sizeof(VertexDecl::VoxelNormal);
		for (uint32_t resource_index = 0; resource_index < vku::double_buffer<uint32_t>::count; ++resource_index) {
			totalSize += gpuSize;
			(*_rtData[eVoxelPipeline::VOXEL_TERRAIN]._vbo[resource_index]) = new vku::DynamicVertexBuffer(gpuSize, true);
			(*_rtData[eVoxelPipeline::VOXEL_TERRAIN_BASIC_ZONLY]._vbo[resource_index]) = (*_rtData[eVoxelPipeline::VOXEL_TERRAIN]._vbo[resource_index]); // basic refers to vertex buffer
			(*_rtData[eVoxelPipeline::VOXEL_TERRAIN_BASIC]._vbo[resource_index]) = (*_rtData[eVoxelPipeline::VOXEL_TERRAIN]._vbo[resource_index]); // basic refers to vertex buffer
			(*_rtData[eVoxelPipeline::VOXEL_TERRAIN_BASIC_CLEAR]._vbo[resource_index]) = (*_rtData[eVoxelPipeline::VOXEL_TERRAIN]._vbo[resource_index]); // basic refers to vertex buffer

			VKU_SET_OBJECT_NAME(vk::ObjectType::eBuffer, (VkBuffer)(*_rtData[eVoxelPipeline::VOXEL_TERRAIN]._vbo[resource_index])->buffer(), vkNames::Buffer::TERRAIN);
		}
		// pipeline for terrain
		CreateVoxelResource(_rtData[eVoxelPipeline::VOXEL_TERRAIN],
			_window->midPass(), MinCity::getFramebufferSize().x, MinCity::getFramebufferSize().y,
			vert_, geom_, frag_, 0U);

		{
			vku::ShaderModule const vert_basic_terrain_{ _device, SHADER_BINARY_DIR "uniforms_basic_height.vert.bin", constants_terrain_basic_vs };
			vku::ShaderModule const geom_basic_terrain_{ _device, SHADER_BINARY_DIR "uniforms_basic_height.geom.bin" };

			CreateVoxelResource<false, true>(_rtData[eVoxelPipeline::VOXEL_TERRAIN_BASIC_ZONLY],
				_window->zPass(), MinCity::getFramebufferSize().x, MinCity::getFramebufferSize().y,
				vert_basic_terrain_, geom_basic_terrain_, frag_null_, 0U);

			CreateVoxelResource<false, true>(_rtData[eVoxelPipeline::VOXEL_TERRAIN_BASIC],
				_window->zPass(), MinCity::getFramebufferSize().x, MinCity::getFramebufferSize().y,
				vert_basic_terrain_, geom_basic_terrain_, frag_basic_, 0U);

			vku::ShaderModule const vert_basic_clear_terrain_{ _device, SHADER_BINARY_DIR "uniforms_basic_clear_height.vert.bin", constants_terrain_basic_vs }; // clear from opacity map

			CreateVoxelResource<false, true, true>(_rtData[eVoxelPipeline::VOXEL_TERRAIN_BASIC_CLEAR],
				_window->clearPass(), MinCity::getFramebufferSize().x, MinCity::getFramebufferSize().y,
				vert_basic_clear_terrain_, geom_null_, frag_null_, 0U);
		}
	}

	{ // road voxels //

		std::vector< vku::SpecializationConstant > constants_road_basic_vs, constants_road_vs, constants_road_gs, constants_road_fs;
		MinCity::VoxelWorld->SetSpecializationConstants_VoxelRoad_Basic_VS(constants_road_basic_vs);
		MinCity::VoxelWorld->SetSpecializationConstants_VoxelRoad_VS(constants_road_vs);
		MinCity::VoxelWorld->SetSpecializationConstants_VoxelRoad_GS(constants_road_gs);
		MinCity::VoxelWorld->SetSpecializationConstants_VoxelRoad_FS(constants_road_fs);

		vk::DeviceSize gpuSize{};

		// main vertex buffer for road
		gpuSize = Volumetric::Allocation::VOXEL_GRID_VISIBLE_TOTAL * sizeof(VertexDecl::VoxelNormal); 
		for (uint32_t resource_index = 0; resource_index < vku::double_buffer<uint32_t>::count; ++resource_index) {
			totalSize += gpuSize;
			(*_rtData[eVoxelPipeline::VOXEL_ROAD]._vbo[resource_index]) = new vku::DynamicVertexBuffer(gpuSize, true);
			(*_rtData[eVoxelPipeline::VOXEL_ROAD_BASIC_ZONLY]._vbo[resource_index]) = (*_rtData[eVoxelPipeline::VOXEL_ROAD]._vbo[resource_index]); // basic refers to vertex buffer
			(*_rtData[eVoxelPipeline::VOXEL_ROAD_BASIC]._vbo[resource_index]) = (*_rtData[eVoxelPipeline::VOXEL_ROAD]._vbo[resource_index]); // basic refers to vertex buffer
			(*_rtData[eVoxelPipeline::VOXEL_ROAD_TRANS]._vbo[resource_index]) = (*_rtData[eVoxelPipeline::VOXEL_ROAD]._vbo[resource_index]); // trans refers to vertex buffer
			(*_rtData[eVoxelPipeline::VOXEL_ROAD_CLEARMASK]._vbo[resource_index]) = (*_rtData[eVoxelPipeline::VOXEL_ROAD]._vbo[resource_index]); // clearmask refers to vertex buffer
			(*_rtData[eVoxelPipeline::VOXEL_ROAD_OFFSCREEN]._vbo[resource_index]) = (*_rtData[eVoxelPipeline::VOXEL_ROAD]._vbo[resource_index]); // clearmask refers to vertex buffer

			// partition the shared vertex buffer *first*
			(*_rtData[eVoxelPipeline::VOXEL_ROAD]._vbo[resource_index])->createPartitions((uint32_t)eVoxelRoadVertexBufferPartition::_size());

			VKU_SET_OBJECT_NAME(vk::ObjectType::eBuffer, (VkBuffer)(*_rtData[eVoxelPipeline::VOXEL_ROAD]._vbo[resource_index])->buffer(), vkNames::Buffer::ROAD);
		}
		
		// opaque road
		{
			vku::ShaderModule const vert_{ _device, SHADER_BINARY_DIR "uniforms_road.vert.bin", constants_road_vs };
			vku::ShaderModule const geom_{ _device, SHADER_BINARY_DIR "uniforms_road.geom.bin", constants_road_gs };
			vku::ShaderModule const frag_{ _device, SHADER_BINARY_DIR "uniforms_road.frag.bin", constants_road_fs };
			CreateVoxelResource<false, false, false, false, true>(_rtData[eVoxelPipeline::VOXEL_ROAD],  // *bugfix - sample shading enabled for roads only. combats texture/shader aliasing.
				_window->midPass(), MinCity::getFramebufferSize().x, MinCity::getFramebufferSize().y,
				vert_, geom_, frag_, 0U);

			CreateVoxelResource<false, false, false, false, true>(_rtData[eVoxelPipeline::VOXEL_ROAD_OFFSCREEN], // *bugfix - sample shading enabled for roads only. combats texture/shader aliasing.
				_window->offscreenPass(), MinCity::getFramebufferSize().x, MinCity::getFramebufferSize().y,
				vert_, geom_, frag_, 0U);

			{ // specialization for static road
				vku::ShaderModule const vert_basic_road_{ _device, SHADER_BINARY_DIR "uniforms_basic_road.vert.bin", constants_road_basic_vs };
				vku::ShaderModule const geom_basic_road_{ _device, SHADER_BINARY_DIR "uniforms_basic_road.geom.bin" };

				CreateVoxelResource<false, true>(_rtData[eVoxelPipeline::VOXEL_ROAD_BASIC_ZONLY],
					_window->zPass(), MinCity::getFramebufferSize().x, MinCity::getFramebufferSize().y,
					vert_basic_road_, geom_basic_road_, frag_null_, 0U);

				CreateVoxelResource<false, true>(_rtData[eVoxelPipeline::VOXEL_ROAD_BASIC],
					_window->zPass(), MinCity::getFramebufferSize().x, MinCity::getFramebufferSize().y,
					vert_basic_road_, geom_basic_road_, frag_basic_, 0U);
			}
		}
		// transparent road
		{
			vku::ShaderModule const vert_{ _device, SHADER_BINARY_DIR "uniforms_trans_road.vert.bin", constants_road_vs };
			vku::ShaderModule const geom_{ _device, SHADER_BINARY_DIR "uniforms_trans_road.geom.bin", constants_road_gs };
			vku::ShaderModule const frag_{ _device, SHADER_BINARY_DIR "uniforms_trans_road.frag.bin", constants_road_fs };
			CreateVoxelResource<false, false, false, true, true>(_rtData[eVoxelPipeline::VOXEL_ROAD_TRANS], // *bugfix - sample shading enabled for roads only. combats texture/shader aliasing.
				_window->overlayPass(), MinCity::getFramebufferSize().x, MinCity::getFramebufferSize().y,
				vert_, geom_, frag_, 0U);

			{ // specialization for static road
				vku::ShaderModule const vert_basic_road_trans_{ _device, SHADER_BINARY_DIR "uniforms_trans_basic_road.vert.bin", constants_road_basic_vs };
				vku::ShaderModule const geom_basic_road_trans_{ _device, SHADER_BINARY_DIR "uniforms_trans_basic_road.geom.bin" };

				CreatePipeline_VoxelClear_Static(_rtData[eVoxelPipeline::VOXEL_ROAD_CLEARMASK],
					vert_basic_road_trans_, geom_basic_road_trans_);
			}
		}
	}

	{ // static & dynamic voxels //

		std::vector< vku::SpecializationConstant > constants_voxel_basic_vs, constants_voxel_vs, constants_voxel_gs, constants_voxel_fs;
		MinCity::VoxelWorld->SetSpecializationConstants_Voxel_Basic_VS(constants_voxel_basic_vs);
		MinCity::VoxelWorld->SetSpecializationConstants_Voxel_VS(constants_voxel_vs);
		MinCity::VoxelWorld->SetSpecializationConstants_Voxel_GS(constants_voxel_gs);
		MinCity::VoxelWorld->SetSpecializationConstants_Voxel_FS(constants_voxel_fs);

		vku::ShaderModule const geom_basic_{ _device, SHADER_BINARY_DIR "uniforms_basic.geom.bin" };
		vku::ShaderModule const geom_{ _device, SHADER_BINARY_DIR "uniforms.geom.bin", constants_voxel_gs };
		vku::ShaderModule const frag_{ _device, SHADER_BINARY_DIR "uniforms.frag.bin", constants_voxel_fs };

		{ // static //

			vku::ShaderModule const vert_{ _device, SHADER_BINARY_DIR "uniforms.vert.bin", constants_voxel_vs };

			vk::DeviceSize gpuSize{};
			
			gpuSize = Volumetric::Allocation::VOXEL_MINIGRID_VISIBLE_TOTAL * sizeof(VertexDecl::VoxelNormal);
			for (uint32_t resource_index = 0; resource_index < vku::double_buffer<uint32_t>::count; ++resource_index) {
				totalSize += gpuSize;
				(*_rtData[eVoxelPipeline::VOXEL_STATIC]._vbo[resource_index]) = new vku::DynamicVertexBuffer(gpuSize, true);
				(*_rtData[eVoxelPipeline::VOXEL_STATIC_BASIC_ZONLY]._vbo[resource_index]) = (*_rtData[eVoxelPipeline::VOXEL_STATIC]._vbo[resource_index]); // basic refers to vertex buffer
				(*_rtData[eVoxelPipeline::VOXEL_STATIC_BASIC]._vbo[resource_index]) = (*_rtData[eVoxelPipeline::VOXEL_STATIC]._vbo[resource_index]); // basic refers to vertex buffer
				(*_rtData[eVoxelPipeline::VOXEL_STATIC_BASIC_CLEAR]._vbo[resource_index]) = (*_rtData[eVoxelPipeline::VOXEL_STATIC]._vbo[resource_index]); // basic refers to vertex buffer
				(*_rtData[eVoxelPipeline::VOXEL_STATIC_OFFSCREEN]._vbo[resource_index]) = (*_rtData[eVoxelPipeline::VOXEL_STATIC]._vbo[resource_index]); // basic refers to vertex buffer

				VKU_SET_OBJECT_NAME(vk::ObjectType::eBuffer, (VkBuffer)(*_rtData[eVoxelPipeline::VOXEL_STATIC]._vbo[resource_index])->buffer(), vkNames::Buffer::VOXEL_STATIC);
			}
			// pipeline for static voxels
			CreateVoxelResource(_rtData[eVoxelPipeline::VOXEL_STATIC],
				_window->midPass(), MinCity::getFramebufferSize().x, MinCity::getFramebufferSize().y,
				vert_, geom_, frag_, 0U);

			CreateVoxelResource(_rtData[eVoxelPipeline::VOXEL_STATIC_OFFSCREEN],
				_window->offscreenPass(), MinCity::getFramebufferSize().x, MinCity::getFramebufferSize().y,
				vert_, geom_, frag_, 0U);
			{
				vku::ShaderModule const vert_basic_{ _device, SHADER_BINARY_DIR "uniforms_basic.vert.bin", constants_voxel_basic_vs };

				CreateVoxelResource<false, true>(_rtData[eVoxelPipeline::VOXEL_STATIC_BASIC_ZONLY],
					_window->zPass(), MinCity::getFramebufferSize().x, MinCity::getFramebufferSize().y,
					vert_basic_, geom_basic_, frag_null_, 0U);

				CreateVoxelResource<false, true>(_rtData[eVoxelPipeline::VOXEL_STATIC_BASIC],
					_window->zPass(), MinCity::getFramebufferSize().x, MinCity::getFramebufferSize().y,
					vert_basic_, geom_basic_, frag_basic_, 0U);

				vku::ShaderModule const vert_basic_clear_{ _device, SHADER_BINARY_DIR "uniforms_basic_clear.vert.bin", constants_voxel_basic_vs }; // clear from opacity map

				CreateVoxelResource<false, true, true>(_rtData[eVoxelPipeline::VOXEL_STATIC_BASIC_CLEAR],
					_window->clearPass(), MinCity::getFramebufferSize().x, MinCity::getFramebufferSize().y,
					vert_basic_clear_, geom_null_, frag_null_, 0U);
			}
		}
		{ // dynamic //
			vku::ShaderModule const vert_dynamic_{ _device, SHADER_BINARY_DIR "uniforms_dynamic.vert.bin", constants_voxel_vs };
				
			vk::DeviceSize gpuSize{};

			gpuSize = Volumetric::Allocation::VOXEL_DYNAMIC_MINIGRID_VISIBLE_TOTAL * sizeof(VertexDecl::VoxelDynamic); 
			for (uint32_t resource_index = 0; resource_index < vku::double_buffer<uint32_t>::count; ++resource_index) {
				totalSize += gpuSize;
				(*_rtData[eVoxelPipeline::VOXEL_DYNAMIC]._vbo[resource_index]) = new vku::DynamicVertexBuffer(gpuSize, true);
				(*_rtData[eVoxelPipeline::VOXEL_DYNAMIC_BASIC_ZONLY]._vbo[resource_index]) = (*_rtData[eVoxelPipeline::VOXEL_DYNAMIC]._vbo[resource_index]); // basic refers to vertex buffer
				(*_rtData[eVoxelPipeline::VOXEL_DYNAMIC_BASIC]._vbo[resource_index]) = (*_rtData[eVoxelPipeline::VOXEL_DYNAMIC]._vbo[resource_index]); // basic refers to vertex buffer
				(*_rtData[eVoxelPipeline::VOXEL_DYNAMIC_BASIC_CLEAR]._vbo[resource_index]) = (*_rtData[eVoxelPipeline::VOXEL_DYNAMIC]._vbo[resource_index]); // basic refers to vertex buffer
				(*_rtData[eVoxelPipeline::VOXEL_DYNAMIC_OFFSCREEN]._vbo[resource_index]) = (*_rtData[eVoxelPipeline::VOXEL_DYNAMIC]._vbo[resource_index]); // basic refers to vertex buffer

				// partition the shared vertex buffer *first*
				(*_rtData[eVoxelPipeline::VOXEL_DYNAMIC]._vbo[resource_index])->createPartitions((uint32_t)eVoxelDynamicVertexBufferPartition::_size());

				VKU_SET_OBJECT_NAME(vk::ObjectType::eBuffer, (VkBuffer)(*_rtData[eVoxelPipeline::VOXEL_DYNAMIC]._vbo[resource_index])->buffer(), vkNames::Buffer::VOXEL_DYNAMIC);
			}

			// pipeline for dynamic voxels
			CreateVoxelResource<true>(_rtData[eVoxelPipeline::VOXEL_DYNAMIC],
				_window->midPass(), MinCity::getFramebufferSize().x, MinCity::getFramebufferSize().y,
				vert_dynamic_, geom_, frag_, 0U);

			CreateVoxelResource<true>(_rtData[eVoxelPipeline::VOXEL_DYNAMIC_OFFSCREEN],
				_window->offscreenPass(), MinCity::getFramebufferSize().x, MinCity::getFramebufferSize().y,
				vert_dynamic_, geom_, frag_, 0U);

			{
				vku::ShaderModule const vert_dynamic_basic_{ _device, SHADER_BINARY_DIR "uniforms_basic_dynamic.vert.bin", constants_voxel_basic_vs };
				
				CreateVoxelResource<true, true>(_rtData[eVoxelPipeline::VOXEL_DYNAMIC_BASIC_ZONLY],
					_window->zPass(), MinCity::getFramebufferSize().x, MinCity::getFramebufferSize().y,
					vert_dynamic_basic_, geom_basic_, frag_null_, 0U);

				CreateVoxelResource<true, true>(_rtData[eVoxelPipeline::VOXEL_DYNAMIC_BASIC],
					_window->zPass(), MinCity::getFramebufferSize().x, MinCity::getFramebufferSize().y,
					vert_dynamic_basic_, geom_basic_, frag_basic_, 0U);

				vku::ShaderModule const vert_dynamic_basic_clear_{ _device, SHADER_BINARY_DIR "uniforms_basic_clear_dynamic.vert.bin", constants_voxel_basic_vs }; // clear from opacity map

				CreateVoxelResource<true, true, true>(_rtData[eVoxelPipeline::VOXEL_DYNAMIC_BASIC_CLEAR],
					_window->clearPass(), MinCity::getFramebufferSize().x, MinCity::getFramebufferSize().y,
					vert_dynamic_basic_clear_, geom_null_, frag_null_, 0U);
			}

			// child custom pipeline "shader voxels"
			// levrages the same pipelinelayout, descriptor sets, vertexbuffer etc. *except has a custom pipeline*
			// always dynamic parent type
				
			// for transparency rendering only
			vku::ShaderModule const vert_dynamic_trans_{ _device, SHADER_BINARY_DIR "uniforms_trans_dynamic.vert.bin", constants_voxel_vs };
			vku::ShaderModule const geom_trans_{ _device, SHADER_BINARY_DIR "uniforms_trans.geom.bin", constants_voxel_gs };

			// VOXEL_SHADER_TRANS
			{							
				vku::ShaderModule const frag_voxeltrans_{ _device, SHADER_BINARY_DIR "uniforms_trans.frag.bin", constants_voxel_fs };
				CreateVoxelChildResource<eVoxelPipelineCustomized::VOXEL_SHADER_TRANS>(
					_window->overlayPass(), MinCity::getFramebufferSize().x, MinCity::getFramebufferSize().y,	// overlaypass, subpass 0
					vert_dynamic_trans_, geom_trans_, frag_voxeltrans_, 0U);
				CreateVoxelChildResource<eVoxelPipelineCustomized::VOXEL_CLEAR_TRANS>( // renderpass and subpass set appropriastely in CreateSharedPipeline_VoxelClear()
					_rtSharedPipeline[eVoxelSharedPipeline::VOXEL_CLEAR_MOUSE], std::forward<vku::double_buffer<vku::VertexBufferPartition const*>&&>(_rtDataChild[eVoxelPipelineCustomized::VOXEL_SHADER_TRANS].vbo_partition_info));

			}
			
			// special effects voxel shaders :

			// VOXEL_SHADER_RAIN
			{	
				// rain specializes voxel size
				std::vector< vku::SpecializationConstant > constants_voxel_rain_vs;
				MinCity::VoxelWorld->SetSpecializationConstants_VoxelRain_VS(constants_voxel_rain_vs);

				vku::ShaderModule const vert_dynamic_trans_rain_{ _device, SHADER_BINARY_DIR "uniforms_trans_dynamic.vert.bin", constants_voxel_rain_vs };

				// ***inherits the same constants as base voxel fragment shader //
				vku::ShaderModule const frag_voxelrain_{ _device, SHADER_BINARY_DIR "voxel_rain.frag.bin", constants_voxel_fs };
				CreateVoxelChildResource<eVoxelPipelineCustomized::VOXEL_SHADER_RAIN>(
					_window->overlayPass(), MinCity::getFramebufferSize().x, MinCity::getFramebufferSize().y,	// overlaypass, subpass 0
					vert_dynamic_trans_rain_, geom_trans_, frag_voxelrain_, 0U);
				CreateVoxelChildResource<eVoxelPipelineCustomized::VOXEL_CLEAR_RAIN>( // renderpass and subpass set appropriastely in CreateSharedPipeline_VoxelClear()
					_rtSharedPipeline[eVoxelSharedPipeline::VOXEL_CLEAR], std::forward<vku::double_buffer<vku::VertexBufferPartition const*>&&>(_rtDataChild[eVoxelPipelineCustomized::VOXEL_SHADER_RAIN].vbo_partition_info));

			}

			// todo:
			// VOXEL_SHADER_WATER
		}
	}
	FMT_LOG(GPU_LOG, "visible voxel allocation: {:n} bytes", (size_t)totalSize);
	
	//vku::ShaderModule frag_{ _device, SHADER_BINARY_DIR "uniforms_basic.frag.bin" };
	//CreateOffscreenResource(vert_basic_, vert_basic_terrain_, geom_basic_, geom_basic_terrain_, frag_);
}
void cVulkan::CreateSharedVoxelResources()
{
	// Create a single entry uniform buffer.
	// We cannot update this buffers with normal memory writes
	// because reading the buffer may happen at any time.
	for (uint32_t resource_index = 0; resource_index < vku::double_buffer<uint32_t>::count; ++resource_index) {
		_rtSharedData._ubo[resource_index] = vku::UniformBuffer{ sizeof(UniformDecl::VoxelSharedUniform) };
		VKU_SET_OBJECT_NAME(vk::ObjectType::eBuffer, (VkBuffer)_rtSharedData._ubo[resource_index].buffer(), vkNames::Buffer::VOXEL_SHARED_UNIFORM);
	}
	// Build a template for descriptor sets that use these shaders.
	
	// TEXTURE_ARRAY_LENGTH is purposely fixed as defined for glsl shader which cannot be dynamic //
	vk::Sampler const samplers_tex_array[TEXTURE_ARRAY_LENGTH]{ 
		TEX_NOISE_SAMPLER, 
		TEX_BLUE_NOISE_SAMPLER,
		TEX_TERRAIN_SAMPLER, 
		TEX_GRID_SAMPLER,
		TEX_ROAD_SAMPLER,
		TEX_BLACKBODY_SAMPLER
	};// first texture always has repeat addressing, and is a mix of noises on each channel (see textureboy)
	
	auto const samplers_common{ getSamplerArray
			<eSamplerAddressing::CLAMP>(
				eSamplerSampling::LINEAR, eSamplerSampling::LINEAR, eSamplerSampling::LINEAR	// should all be linear (3d texture samplers, etc)
			)
	};


	{ // voxels common ("one descriptor set")
		vku::DescriptorSetLayoutMaker	dslm;
		dslm.buffer(0U, vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eGeometry, 1);
		dslm.buffer(1U, vk::DescriptorType::eStorageBuffer, vk::ShaderStageFlagBits::eVertex, 1);
		dslm.image(3U, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 3, samplers_common);							// 3d textures (light & opacity)
		dslm.image(4U, vk::DescriptorType::eInputAttachment, vk::ShaderStageFlagBits::eFragment, 1);											// 2d texture(ambient light reflection)
		dslm.image(5U, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, TEXTURE_ARRAY_LENGTH, samplers_tex_array);			// 2d texture array
		dslm.image(6U, vk::DescriptorType::eCombinedImageSampler, vk::ShaderStageFlagBits::eFragment, 1, &samplers_common[0]);					// 2d texture (last color backbuffer)

		_rtSharedData.descLayout[eVoxelDescSharedLayout::VOXEL_COMMON] = dslm.createUnique(_device);
	}

	{ // voxels clear and clear mask
		vku::DescriptorSetLayoutMaker	dslm;
		dslm.buffer(0U, vk::DescriptorType::eUniformBuffer, vk::ShaderStageFlagBits::eGeometry, 1);
		dslm.image(1U, vk::DescriptorType::eStorageImage, vk::ShaderStageFlagBits::eVertex, 1, nullptr);									// 3d image (opacity) required for transparents
		dslm.buffer(2U, vk::DescriptorType::eStorageBuffer, vk::ShaderStageFlagBits::eFragment, 1);
		dslm.buffer(3U, vk::DescriptorType::eStorageBuffer, vk::ShaderStageFlagBits::eFragment, 1);
		_rtSharedData.descLayout[eVoxelDescSharedLayout::VOXEL_CLEAR] = dslm.createUnique(_device);
	}

	// Shared Sets //
	for (uint32_t i = 0; i < eVoxelDescSharedLayoutSet::_size(); ++i) {
		// We need to create a unique descriptor set to tell the shader where
		// our buffers are.
		vku::DescriptorSetMaker			dsm;
		dsm.layout(*_rtSharedDescSet[i].layout);
		// double buffered
		_rtSharedDescSet[i].sets.emplace_back(dsm.create(_device, _fw.descriptorPool())[0]);
		_rtSharedDescSet[i].sets.emplace_back(dsm.create(_device, _fw.descriptorPool())[0]);
		_rtSharedDescSet[i].sets.shrink_to_fit();

		// Make a default pipeline layout. This shows how pointers
		// to resources are layed out.
		// 
		vku::PipelineLayoutMaker		plm;
		plm.descriptorSetLayout(*_rtSharedDescSet[i].layout);
		_rtSharedDescSet[i].pipelineLayout = plm.createUnique(_device);
	}

	// Shared Pipelines //
	CreateSharedPipeline_VoxelClear();  // clear mask
}

void cVulkan::CreateSharedPipeline_VoxelClear()  // clear mask
{
	std::vector< vku::SpecializationConstant > constants_voxel_vs, constants_voxel_fs;
	MinCity::VoxelWorld->SetSpecializationConstants_Voxel_Basic_VS_Common(constants_voxel_vs, true);
	MinCity::VoxelWorld->SetSpecializationConstants_Voxel_ClearMask_FS(constants_voxel_fs);

	vku::ShaderModule const vert_{ _device, SHADER_BINARY_DIR "uniforms_trans_basic_dynamic.vert.bin", constants_voxel_vs };
	vku::ShaderModule const geom_{ _device, SHADER_BINARY_DIR "uniforms_basic.geom.bin" };

	{ // VOXEL_CLEAR
		vku::ShaderModule const frag_{ _device, SHADER_BINARY_DIR "voxel_clear.frag.bin", constants_voxel_fs };

		// Make a pipeline to use the vertex format and shaders.
		vku::PipelineMaker pm(MinCity::getFramebufferSize().x, MinCity::getFramebufferSize().y);
		pm.topology(vk::PrimitiveTopology::ePointList);
		pm.shader(vk::ShaderStageFlagBits::eVertex, vert_);
		pm.shader(vk::ShaderStageFlagBits::eGeometry, geom_);
		pm.shader(vk::ShaderStageFlagBits::eFragment, frag_);

		// dynamic voxels only
		pm.vertexBinding(0, (uint32_t)sizeof(VertexDecl::VoxelDynamic));

		pm.vertexAttribute(0, 0, vk::Format::eR32G32B32A32Sfloat, (uint32_t)offsetof(VertexDecl::VoxelDynamic, worldPos));
		pm.vertexAttribute(1, 0, vk::Format::eR32G32B32A32Sfloat, (uint32_t)offsetof(VertexDecl::VoxelDynamic, uv_vr));
		pm.vertexAttribute(2, 0, vk::Format::eR32G32B32A32Sfloat, (uint32_t)offsetof(VertexDecl::VoxelDynamic, orient_reserved));

		// ###***###
		// alpha must be cleared to 1.0f on main rendertarget / color attachment
		// all opaque geometry must be rendered first 
		// no further writes to the alpha channel, preserve until overlay/transparency pass
		pm.depthCompareOp(vk::CompareOp::eLessOrEqual);
		pm.depthClampEnable(VK_FALSE);
		pm.depthTestEnable(VK_TRUE);	// dependent on depth test, all opaque geometry must be rendered first
		pm.depthWriteEnable(VK_FALSE);  // no depth writes on voxel clear alpha mask *important*

		pm.cullMode(vk::CullModeFlagBits::eBack);
		pm.frontFace(vk::FrontFace::eClockwise);

		// accumulating output alpha *only* //
		pm.blendBegin(VK_TRUE);
		pm.blendSrcColorBlendFactor(vk::BlendFactor::eOne);		// this is additive blending
		pm.blendDstColorBlendFactor(vk::BlendFactor::eZero);
		pm.blendColorBlendOp(vk::BlendOp::eAdd);
		pm.blendSrcAlphaBlendFactor(vk::BlendFactor::eOne);
		pm.blendDstAlphaBlendFactor(vk::BlendFactor::eOne);
		pm.blendAlphaBlendOp(vk::BlendOp::eAdd);

		pm.blendColorWriteMask(vk::ColorComponentFlagBits::eA);

		// no output to second color attachment
		pm.blendBegin(VK_FALSE);
		pm.blendColorWriteMask((vk::ColorComponentFlagBits)0);

		// Create a pipeline using a renderPass built for our window.
		// transparent dynamic voxels in overlay renderpass, subpass 0
		// transparent "mask" dynamic voxels in zpass, subpass 0
		pm.subPass(0U);
		pm.rasterizationSamples(vku::DefaultSampleCount);

		auto& cache = _fw.pipelineCache();
		_rtSharedPipeline[eVoxelSharedPipeline::VOXEL_CLEAR] = pm.create(_device, cache,
			*_rtSharedDescSet[eVoxelDescSharedLayoutSet::VOXEL_CLEAR].pipelineLayout, _window->zPass());
	}

	{ // VOXEL_CLEAR_MOUSE
		vku::ShaderModule const frag_{ _device, SHADER_BINARY_DIR "voxel_clear_mouse.frag.bin", constants_voxel_fs };

		// Make a pipeline to use the vertex format and shaders.
		vku::PipelineMaker pm(MinCity::getFramebufferSize().x, MinCity::getFramebufferSize().y);
		pm.topology(vk::PrimitiveTopology::ePointList);
		pm.shader(vk::ShaderStageFlagBits::eVertex, vert_);
		pm.shader(vk::ShaderStageFlagBits::eGeometry, geom_);
		pm.shader(vk::ShaderStageFlagBits::eFragment, frag_);

		// dynamic voxels only
		pm.vertexBinding(0, (uint32_t)sizeof(VertexDecl::VoxelDynamic));

		pm.vertexAttribute(0, 0, vk::Format::eR32G32B32A32Sfloat, (uint32_t)offsetof(VertexDecl::VoxelDynamic, worldPos));
		pm.vertexAttribute(1, 0, vk::Format::eR32G32B32A32Sfloat, (uint32_t)offsetof(VertexDecl::VoxelDynamic, uv_vr));
		pm.vertexAttribute(2, 0, vk::Format::eR32G32B32A32Sfloat, (uint32_t)offsetof(VertexDecl::VoxelDynamic, orient_reserved));

		// ###***###
		// alpha must be cleared to 1.0f on main rendertarget / color attachment
		// all opaque geometry must be rendered first 
		// no further writes to the alpha channel, preserve until overlay/transparency pass
		pm.depthCompareOp(vk::CompareOp::eLessOrEqual);
		pm.depthClampEnable(VK_FALSE);
		pm.depthTestEnable(VK_TRUE);	// dependent on depth test, all opaque geometry must be rendered first
		pm.depthWriteEnable(VK_FALSE);  // no depth writes on voxel clear alpha mask *important*

		pm.cullMode(vk::CullModeFlagBits::eBack);
		pm.frontFace(vk::FrontFace::eClockwise);

		// accumulating output alpha *only* //
		pm.blendBegin(VK_TRUE);
		pm.blendSrcColorBlendFactor(vk::BlendFactor::eOne);		// this is additive blending
		pm.blendDstColorBlendFactor(vk::BlendFactor::eZero);
		pm.blendColorBlendOp(vk::BlendOp::eAdd);
		pm.blendSrcAlphaBlendFactor(vk::BlendFactor::eOne);
		pm.blendDstAlphaBlendFactor(vk::BlendFactor::eOne);
		pm.blendAlphaBlendOp(vk::BlendOp::eAdd);

		pm.blendColorWriteMask(vk::ColorComponentFlagBits::eA);

		// output to second color attachment
		// mouse buffer, transparents just output black (required for mouse occlusion queries)
		typedef vk::ColorComponentFlagBits ccbf;
		pm.blendBegin(VK_FALSE);
		pm.blendColorWriteMask((vk::ColorComponentFlagBits)ccbf::eR | ccbf::eG | ccbf::eB | ccbf::eA);

		// Create a pipeline using a renderPass built for our window.
		// transparent dynamic voxels in overlay renderpass, subpass 0
		// transparent "mask" dynamic voxels in zpass, subpass 0
		pm.subPass(0U);
		pm.rasterizationSamples(vku::DefaultSampleCount);

		auto& cache = _fw.pipelineCache();
		_rtSharedPipeline[eVoxelSharedPipeline::VOXEL_CLEAR_MOUSE] = pm.create(_device, cache,
			*_rtSharedDescSet[eVoxelDescSharedLayoutSet::VOXEL_CLEAR].pipelineLayout, _window->zPass());
	}
}

void cVulkan::CreatePipeline_VoxelClear_Static( // clearmask (for roads as they are static) there are no other static transparents
	cVulkan::sRTDATA& rtData,
	vku::ShaderModule const& __restrict vert,
	vku::ShaderModule const& __restrict geom)  // clear mask
{
	vku::ShaderModule const frag{ _device, SHADER_BINARY_DIR "voxel_clear.frag.bin" };

	// Make a pipeline to use the vertex format and shaders.
	vku::PipelineMaker pm(MinCity::getFramebufferSize().x, MinCity::getFramebufferSize().y);
	pm.topology(vk::PrimitiveTopology::ePointList);
	pm.shader(vk::ShaderStageFlagBits::eVertex, vert);
	pm.shader(vk::ShaderStageFlagBits::eGeometry, geom);
	pm.shader(vk::ShaderStageFlagBits::eFragment, frag);

	// static voxels only
	pm.vertexBinding(0, (uint32_t)sizeof(VertexDecl::VoxelNormal));

	pm.vertexAttribute(0, 0, vk::Format::eR32G32B32A32Sfloat, (uint32_t)offsetof(VertexDecl::VoxelNormal, worldPos));
	pm.vertexAttribute(1, 0, vk::Format::eR32G32B32A32Sfloat, (uint32_t)offsetof(VertexDecl::VoxelNormal, uv_vr));

	// ###***###
	// alpha must be cleared to 1.0f on main rendertarget / color attachment
	// all opaque geometry must be rendered first 
	// no further writes to the alpha channel, preserve until overlay/transparency pass
	pm.depthCompareOp(vk::CompareOp::eLessOrEqual);
	pm.depthClampEnable(VK_FALSE); 
	pm.depthTestEnable(VK_TRUE);	// dependent on depth test, all opaque geometry must be rendered first
	pm.depthWriteEnable(VK_FALSE);  // no depth writes on voxel clear alpha mask *important*

	pm.cullMode(vk::CullModeFlagBits::eBack);
	pm.frontFace(vk::FrontFace::eClockwise);

	// accumulating output alpha *only* //
	pm.blendBegin(VK_TRUE);
	pm.blendSrcColorBlendFactor(vk::BlendFactor::eOne);		// this is additive blending
	pm.blendDstColorBlendFactor(vk::BlendFactor::eZero);
	pm.blendColorBlendOp(vk::BlendOp::eAdd);
	pm.blendSrcAlphaBlendFactor(vk::BlendFactor::eOne);
	pm.blendDstAlphaBlendFactor(vk::BlendFactor::eOne);
	pm.blendAlphaBlendOp(vk::BlendOp::eAdd);

	pm.blendColorWriteMask(vk::ColorComponentFlagBits::eA); // clearmask channel

	// static transparents (roads currently) does not modify the mouse buffer
	typedef vk::ColorComponentFlagBits ccbf;
	pm.blendBegin(VK_FALSE);
	pm.blendColorWriteMask((vk::ColorComponentFlagBits)0);

	// Create a pipeline using a renderPass built for our window.
	pm.subPass(0U);
	pm.rasterizationSamples(vku::DefaultSampleCount);

	auto& cache = _fw.pipelineCache();
	rtData.pipeline = pm.create(_device, cache, *_rtSharedDescSet[eVoxelDescSharedLayoutSet::VOXEL_CLEAR].pipelineLayout, _window->zPass());
}

void cVulkan::CreateResources()
{
	CreateIndirectActiveCountBuffer(); // 1st //

	// create mousebuffer for mouse picking
	using buf = vk::BufferUsageFlagBits;
	using pfb = vk::MemoryPropertyFlagBits;

	point2D_t const framebufferSz(MinCity::getFramebufferSize());

	// mouse gpu readback buffers
	_mouseBuffer[0] = vku::GenericBuffer(buf::eTransferDst, framebufferSz.x * framebufferSz.y * sizeof(uint32_t),
		pfb::eHostVisible, VMA_MEMORY_USAGE_GPU_TO_CPU, true, true); 
	_mouseBuffer[1] = vku::GenericBuffer(buf::eTransferDst, framebufferSz.x * framebufferSz.y * sizeof(uint32_t),
		pfb::eHostVisible, VMA_MEMORY_USAGE_GPU_TO_CPU, true, true); 

	// offscreen gpu readback buffer
	_offscreenBuffer = vku::GenericBuffer(buf::eTransferDst, framebufferSz.x * framebufferSz.y * sizeof(uint32_t),
		pfb::eHostVisible, VMA_MEMORY_USAGE_GPU_TO_CPU, true, true);

	CreateComputeResources();

	CreateNuklearResources();
	CreateVoxelResources();
	CreateVolumetricResources(); // also _rtSharedData's ubo being created already
	CreateUpsampleResources();
	CreateDepthResolveResources();
	CreatePostAAResources();
}

constexpr uint32_t const cVulkan::getIndirectActiveCountOffset(uint32_t const vertexBuffer /*eVoxelVertexBuffer*/) const  // Use this in the command buffer draw routines to resolve correct indirect buffer offset, leading to the active count for the corresponding voxel vertex buffer
{
	// vertexBuffer is eVoxelVertexBuffer type
	// which has the correct index for the offset this function returns
	/*
		vertexCount
		instanceCount
		firstVertex
		firstInstance
	*/
	return(vertexBuffer * 4u * sizeof(uint32_t));
}

void cVulkan::CreateIndirectActiveCountBuffer() // required critical buffer needed before static command buffer generation.
{
	using buf = vk::BufferUsageFlagBits;
	using pfb = vk::MemoryPropertyFlagBits;

	_activeCountBuffer[0] = vku::GenericBuffer(buf::eTransferSrc, eVoxelVertexBuffer::_size() * sizeof(vk::DrawIndirectCommand), pfb::eHostVisible, VMA_MEMORY_USAGE_CPU_TO_GPU, true, true); // persistantly mapped
	_activeCountBuffer[1] = vku::GenericBuffer(buf::eTransferSrc, eVoxelVertexBuffer::_size() * sizeof(vk::DrawIndirectCommand), pfb::eHostVisible, VMA_MEMORY_USAGE_CPU_TO_GPU, true, true); // persistantly mapped
	_indirectActiveCount[0] = new vku::IndirectBuffer(eVoxelVertexBuffer::_size() * sizeof(vk::DrawIndirectCommand), true); // this is the gpu buffer, use staging buffer to update.
	_indirectActiveCount[1] = new vku::IndirectBuffer(eVoxelVertexBuffer::_size() * sizeof(vk::DrawIndirectCommand), true); // this is the gpu buffer, use staging buffer to update.
}

void cVulkan::UpdateIndirectActiveCountBuffer(vk::CommandBuffer& cb, uint32_t const resource_index)
{
	using buf = vk::BufferUsageFlagBits;
	using pfb = vk::MemoryPropertyFlagBits;

	vk::DrawIndirectCommand const drawCommand[eVoxelVertexBuffer::_size()]{

		/*
		vertexCount
		instanceCount
		firstVertex
		firstInstance
		*/

		// VOXEL_TERRAIN
		{ (*_rtData[eVoxelPipeline::VOXEL_TERRAIN]._vbo[resource_index])->ActiveVertexCount<VertexDecl::VoxelNormal>(), 1, 0, 0 },

		// VOXEL_ROAD
		{ (*_rtData[eVoxelPipeline::VOXEL_ROAD]._vbo[resource_index])->ActiveVertexCount<VertexDecl::VoxelNormal>(), 1, 0, 0 },

		// VOXEL_STATIC
		{ (*_rtData[eVoxelPipeline::VOXEL_STATIC]._vbo[resource_index])->ActiveVertexCount<VertexDecl::VoxelNormal>(), 1, 0, 0 },
		
		// VOXEL_DYNAMIC
		{ (*_rtData[eVoxelPipeline::VOXEL_DYNAMIC]._vbo[resource_index])->ActiveVertexCount<VertexDecl::VoxelDynamic>(), 1, 0, 0 },

	};
	
	// this function is always called once per frame by the compute transfer uniform operation.
	// so this copy actually happens on the cpu->staging buffer here
	// the staging buffer->gpu copy is deferred until command buffer execution by the transfer queue.
	// this is in the beginning of the frame, coherent before any usage of the indirect buffer.
	// always updates the counts (active voxels)
	vk::DrawIndirectCommand const* const updateDrawCommand(drawCommand);
	_activeCountBuffer[resource_index].updateLocal(updateDrawCommand, sizeof(drawCommand)); // template cannot use an array of drawCommands, so give it a pointer to the array's first element in memory. All elements are copied as the size copied is equal to the total number of elements.

	_indirectActiveCount[resource_index]->uploadDeferred(cb, _activeCountBuffer[resource_index]); // fast single copy to gpu only indirect draw buffer - only once per frame.
}

// macros for sampler sets
#define SAMPLER_SET_STANDARD getLinearSampler(), getLinearSampler<eSamplerAddressing::REPEAT>(), getLinearSampler<eSamplerAddressing::MIRRORED_REPEAT>()
#define SAMPLER_SET_STANDARD_POINT getLinearSampler(), getLinearSampler<eSamplerAddressing::REPEAT>(), getLinearSampler<eSamplerAddressing::MIRRORED_REPEAT>(), getNearestSampler(), getNearestSampler<eSamplerAddressing::REPEAT>()
#define SAMPLER_SET_STANDARD_POINT_ANISO getLinearSampler(), getLinearSampler<eSamplerAddressing::REPEAT>(), getLinearSampler<eSamplerAddressing::MIRRORED_REPEAT>(), getNearestSampler(), getNearestSampler<eSamplerAddressing::REPEAT>(), getAnisotropicSampler(), getAnisotropicSampler<eSamplerAddressing::REPEAT>()

void cVulkan::UpdateDescriptorSetsAndStaticCommandBuffer()
{
	////////////////////////////////////////
//
// Update the descriptor sets for the shader uniforms.
	{ // ###### Compute
		_dsu.beginDescriptorSet(_comData.light.sets[0]);
		MinCity::VoxelWorld->UpdateDescriptorSet_ComputeLight(_dsu, getLinearSampler<eSamplerAddressing::BORDER>());

		// update descriptor set (still called)
		_dsu.update(_device);

		// [[deprecated]] ###### Texture Shaders (Compute)
		/*for (uint32_t shader = 0; shader < eTextureShader::_size(); ++shader)
		{
			_dsu.beginDescriptorSet(_comData.texture.sets[shader][0]);
			MinCity::VoxelWorld->UpdateDescriptorSet_TextureShader(_dsu, shader, SAMPLER_SET_STANDARD_POINT);

			// update descriptor set (still called)
			_dsu.update(_device);
		}*/
	}
	
	{ // ###### Nuklear
		_dsu.beginDescriptorSet(_nkData.sets[0]);

		// Set initial uniform buffer value
		_dsu.beginBuffers(0, 0, vk::DescriptorType::eUniformBuffer);
		_dsu.buffer(_nkData._ubo.buffer(), 0, sizeof(UniformDecl::nk_uniform));

		// Set initial sampler value
		MinCity::Nuklear->UpdateDescriptorSet(_dsu, getLinearSampler());

		// update descriptor set
		_dsu.update(_device);
	}
	{ // Shared Voxel "One descriptor: set for all voxels"
		for (uint32_t resource_index = 0; resource_index < vku::double_buffer<uint32_t>::count; ++resource_index) {
			_dsu.beginDescriptorSet(_rtSharedDescSet[eVoxelDescSharedLayoutSet::VOXEL_COMMON].sets[resource_index]);

			// Set initial uniform buffer value
			_dsu.beginBuffers(0, 0, vk::DescriptorType::eUniformBuffer);
			_dsu.buffer(_rtSharedData._ubo[resource_index].buffer(), 0, sizeof(UniformDecl::VoxelSharedUniform));

			// Set initial sampler value
			MinCity::VoxelWorld->UpdateDescriptorSet_VoxelCommon(resource_index, _dsu, _window->colorReflectionImageView(), _window->lastColorImageView(), SAMPLER_SET_STANDARD_POINT_ANISO);
			
			// update descriptor set
			_dsu.update(_device);
		}
	}
	{ // Shared Voxel Masking Clear
		for (uint32_t resource_index = 0; resource_index < vku::double_buffer<uint32_t>::count; ++resource_index) {
			_dsu.beginDescriptorSet(_rtSharedDescSet[eVoxelDescSharedLayoutSet::VOXEL_CLEAR].sets[resource_index]);

			// Set initial uniform buffer value
			_dsu.beginBuffers(0, 0, vk::DescriptorType::eUniformBuffer);
			_dsu.buffer(_rtSharedData._ubo[resource_index].buffer(), 0, sizeof(UniformDecl::VoxelSharedUniform));

			MinCity::VoxelWorld->UpdateDescriptorSet_Voxel_ClearMask(resource_index, _dsu);

			// update descriptor set
			_dsu.update(_device);
		}
	}

	{ // ###### Volumetric Light
		for (uint32_t resource_index = 0; resource_index < vku::double_buffer<uint32_t>::count; ++resource_index) {
			_dsu.beginDescriptorSet(_volData.sets[resource_index]);

			// Set initial uniform buffer value
			_dsu.beginBuffers(0, 0, vk::DescriptorType::eUniformBuffer);
			_dsu.buffer(_volData._ubo[resource_index].buffer(), 0, sizeof(UniformDecl::VoxelSharedUniform));

			// Set initial sampler value
			MinCity::VoxelWorld->UpdateDescriptorSet_VolumetricLight(_dsu, _window->depthResolvedImageView(1), _window->colorVolumetricDownResCheckeredImageView(), _window->colorReflectionDownResCheckeredImageView(), SAMPLER_SET_STANDARD_POINT);

			// update descriptor set
			_dsu.update(_device);
		}
	}
	{ // ###### Bilateral Upsample for Volumetrics

		// resolve part
		for (uint32_t resource_index = 0; resource_index < vku::double_buffer<uint32_t>::count; ++resource_index) {
			_dsu.beginDescriptorSet(_upData[eUpsamplePipeline::RESOLVE].sets[resource_index]);

			// Set initial uniform buffer value
			_dsu.beginBuffers(0, 0, vk::DescriptorType::eUniformBuffer);
			_dsu.buffer(_volData._ubo[resource_index].buffer(), 0, sizeof(UniformDecl::VoxelSharedUniform));

			MinCity::VoxelWorld->UpdateDescriptorSet_VolumetricLightResolve(_dsu, _window->colorVolumetricDownResCheckeredImageView(), _window->colorReflectionDownResCheckeredImageView(), _window->colorVolumetricImageView(), _window->colorReflectionImageView(), SAMPLER_SET_STANDARD_POINT);

			// update descriptor set
			_dsu.update(_device);
		}

		// upsample part
		for (uint32_t resource_index = 0; resource_index < vku::double_buffer<uint32_t>::count; ++resource_index) {
			_dsu.beginDescriptorSet(_upData[eUpsamplePipeline::UPSAMPLE].sets[resource_index]);

			// Set initial uniform buffer value
			_dsu.beginBuffers(0, 0, vk::DescriptorType::eUniformBuffer);
			_dsu.buffer(_volData._ubo[resource_index].buffer(), 0, sizeof(UniformDecl::VoxelSharedUniform));

			// Set initial sampler value
			MinCity::VoxelWorld->UpdateDescriptorSet_VolumetricLightUpsample(resource_index, _dsu, _window->depthResolvedImageView(0), _window->depthResolvedImageView(1), _window->colorVolumetricDownResImageView(), _window->colorReflectionDownResImageView(), SAMPLER_SET_STANDARD_POINT);
			
			// update descriptor set
			_dsu.update(_device);
		}

		// blend part
		_dsu.beginDescriptorSet(_upData[eUpsamplePipeline::BLEND].sets[0]);

		// Set initial sampler value
		_dsu.beginImages(0U, 0, vk::DescriptorType::eInputAttachment);
		_dsu.image(nullptr, _window->colorVolumetricImageView(), vk::ImageLayout::eShaderReadOnlyOptimal);

		// update descriptor set
		_dsu.update(_device);
	}
	{ // ###### Depth resolve
		_dsu.beginDescriptorSet(_depthData.sets[0]);

		// Set input attachment
		_dsu.beginImages(0U, 0, vk::DescriptorType::eInputAttachment);
		_dsu.image(nullptr, _window->depthImageView(), vk::ImageLayout::eDepthStencilReadOnlyOptimal);
		_dsu.beginImages(1U, 0, vk::DescriptorType::eStorageImage);
		_dsu.image(nullptr, _window->depthResolvedImageView(1), vk::ImageLayout::eGeneral);

		// update descriptor set
		_dsu.update(_device);
	}
	{ // ###### Post Anti-aliasing
		for (uint32_t resource_index = 0; resource_index < vku::double_buffer<uint32_t>::count; ++resource_index) {
			_dsu.beginDescriptorSet(_aaData.sets[resource_index]);

			// Set initial uniform buffer value
			_dsu.beginBuffers(0, 0, vk::DescriptorType::eUniformBuffer);
			_dsu.buffer(_rtSharedData._ubo[resource_index].buffer(), 0, sizeof(UniformDecl::VoxelSharedUniform));

			// Set initial sampler value
			MinCity::VoxelWorld->UpdateDescriptorSet_PostAA(_dsu, _window->lastColorImageView(), _window->guiImageView(), SAMPLER_SET_STANDARD_POINT);

			// update descriptor set
			_dsu.update(_device);
		}
	}
	// set static pre-recorded command buffers here //
	_window->setStaticCommands(cVulkan::renderStaticCommandBuffer);
	_window->setGpuReadbackCommands(cVulkan::gpuReadback);
	_window->setStaticPresentCommands(cVulkan::renderPresentCommandBuffer);
	_window->setStaticClearCommands(cVulkan::renderClearCommandBuffer);
	
	WaitDeviceIdle(); // bugfix, wait until device is ready before proceeding with execution
}

namespace vku
{
	__forceinline void resource_control::stage_resources(uint32_t const resource_index)
	{
		cMinCity::StageResources(resource_index);
	}
} // end ns

bool const cVulkan::renderCompute(vku::compute_pass&& __restrict c)
{
	return(MinCity::Vulkan->_renderCompute(std::forward<vku::compute_pass&& __restrict>(c)));
}
void cVulkan::renderStaticCommandBuffer(vku::static_renderpass&& __restrict s)
{
	MinCity::Vulkan->_renderStaticCommandBuffer(std::forward<vku::static_renderpass&&>(s));
}
void cVulkan::renderDynamicCommandBuffer(vku::dynamic_renderpass&& __restrict d)
{
	MinCity::Vulkan->_renderDynamicCommandBuffer(std::forward<vku::dynamic_renderpass&&>(d));
}
void cVulkan::renderOverlayCommandBuffer(vku::overlay_renderpass&& __restrict o)
{
	MinCity::Vulkan->_renderOverlayCommandBuffer(std::forward<vku::overlay_renderpass&&>(o));
}
void cVulkan::renderPresentCommandBuffer(vku::present_renderpass&& __restrict pp)
{
	MinCity::Vulkan->_renderPresentCommandBuffer(std::forward<vku::present_renderpass&&>(pp));
}
void cVulkan::renderClearCommandBuffer(vku::clear_renderpass&& __restrict c)
{
	MinCity::Vulkan->_renderClearCommandBuffer(std::forward<vku::clear_renderpass&&>(c));
}
void cVulkan::gpuReadback(vk::CommandBuffer& cb, uint32_t const resource_index)
{
	MinCity::Vulkan->_gpuReadback(cb, resource_index);
}

inline bool const cVulkan::_renderCompute(vku::compute_pass&& __restrict c)
{
	if (c.cb_transfer) { // **** very first gpu transfer on every new frame ***

		vk::CommandBufferBeginInfo bi(vk::CommandBufferUsageFlagBits::eOneTimeSubmit); // updated every frame
		c.cb_transfer.begin(bi); VKU_SET_CMD_BUFFER_LABEL(c.cb_transfer, vkNames::CommandBuffer::TRANSFER);

		MinCity::VoxelWorld->Transfer(c.cb_transfer, _rtSharedData._ubo[c.resource_index]);

		c.cb_transfer.end();
	}
	
	return(MinCity::VoxelWorld->renderCompute(std::forward<vku::compute_pass&& __restrict>(c), _comData)); // only care about return value from transfer light (if light needs to be uploaded) otherwise return value is not used.
}

void cVulkan::renderClearMasks(vku::static_renderpass&& __restrict s, sRTDATA_CHILD const* (& __restrict deferredChildMasks)[NUM_CHILD_MASKS], uint32_t const ActiveMaskCount)
{
	uint32_t const resource_index(s.resource_index);
	// ***** descriptor set must be set outside of this function ***** //

	{ // clearmask transparent roads
		constexpr uint32_t const voxelPipeline = eVoxelPipeline::VOXEL_ROAD_CLEARMASK;

		uint32_t const ActiveVertexCount = (*_rtData[voxelPipeline]._vbo[resource_index])->ActiveVertexCount<VertexDecl::VoxelNormal>();
		if (0 != ActiveVertexCount) {

			s.cb.bindVertexBuffers(0, (*_rtData[voxelPipeline]._vbo[resource_index])->buffer(), vk::DeviceSize(0));

			// trans partition
			{
				auto const& partition_info((*_rtData[voxelPipeline]._vbo[resource_index])->partitions()[eVoxelRoadVertexBufferPartition::PARENT_TRANS]);
				uint32_t const partition_vertex_count = partition_info.active_vertex_count;
				if (0 != partition_vertex_count) {
					s.cb.bindPipeline(vk::PipelineBindPoint::eGraphics, _rtData[voxelPipeline].pipeline);
					uint32_t const partition_start_vertex = partition_info.vertex_start_offset;
					s.cb.draw(partition_vertex_count, 1, partition_start_vertex, 0);
				}
			}
		}
	}

	// ## deferred mask dynamic children "Transparent Mask" voxels
	//    has to be last as the depth buffer must be complete by this point for correct masking
	if (ActiveMaskCount) {

		// leveraging dynamic vb 
		// all children that use a clear mask use common descriptor set and pipeline
		s.cb.bindVertexBuffers(0, (*_rtData[eVoxelPipeline::VOXEL_DYNAMIC]._vbo[resource_index])->buffer(), vk::DeviceSize(0));

		if (0 != resource_index) {
			s.cb.bindPipeline(vk::PipelineBindPoint::eGraphics, _rtSharedPipeline[eVoxelSharedPipeline::VOXEL_CLEAR]);
		}
		else {
			s.cb.bindPipeline(vk::PipelineBindPoint::eGraphics, _rtSharedPipeline[eVoxelSharedPipeline::VOXEL_CLEAR_MOUSE]);
		}

		// batching of all clears (*** requires all clears to be grouped together in vb layout)
		vku::VertexBufferPartition clear_partitions{};

		for (uint32_t child = 0; child < ActiveMaskCount; ++child) {

			sRTDATA_CHILD const& __restrict rtDataChild(*deferredChildMasks[child]);
			vku::VertexBufferPartition const* const __restrict child_partition = rtDataChild.vbo_partition_info[resource_index];

			if (0 != child_partition->active_vertex_count) {
				if (0 == clear_partitions.active_vertex_count) {
					clear_partitions.vertex_start_offset = child_partition->vertex_start_offset;
				}
				clear_partitions.active_vertex_count += child_partition->active_vertex_count;
			}
		}

		// draw batch of clear masks
		if (0 != clear_partitions.active_vertex_count) {
			s.cb.draw(clear_partitions.active_vertex_count, 1, clear_partitions.vertex_start_offset, 0);
		}
	}
}

void cVulkan::clearAllVoxels(vku::clear_renderpass&& __restrict c)  // for clearing opacity map (only done in finalPass / Present //
{
	uint32_t const resource_index(c.resource_index);
	// ***** descriptor set must be set outside of this function ***** //

	{ // dynamic voxels

		constexpr uint32_t const voxelPipeline = eVoxelPipeline::VOXEL_DYNAMIC_BASIC_CLEAR;

		c.cb.bindPipeline(vk::PipelineBindPoint::eGraphics, _rtData[voxelPipeline].pipeline);

		c.cb.bindVertexBuffers(0, (*_rtData[voxelPipeline]._vbo[resource_index])->buffer(), vk::DeviceSize(0));
		c.cb.drawIndirect(_indirectActiveCount[resource_index]->buffer(), getIndirectActiveCountOffset(eVoxelVertexBuffer::VOXEL_DYNAMIC), 1, 0);
	}

	{ // static voxels
		constexpr uint32_t const voxelPipeline = eVoxelPipeline::VOXEL_STATIC_BASIC_CLEAR;

		c.cb.bindPipeline(vk::PipelineBindPoint::eGraphics, _rtData[voxelPipeline].pipeline);

		c.cb.bindVertexBuffers(0, (*_rtData[voxelPipeline]._vbo[resource_index])->buffer(), vk::DeviceSize(0));
		c.cb.drawIndirect(_indirectActiveCount[resource_index]->buffer(), getIndirectActiveCountOffset(eVoxelVertexBuffer::VOXEL_STATIC), 1, 0);
	}

	{ // terrain
		constexpr uint32_t const voxelPipeline = eVoxelPipeline::VOXEL_TERRAIN_BASIC_CLEAR;

		c.cb.bindPipeline(vk::PipelineBindPoint::eGraphics, _rtData[voxelPipeline].pipeline);

		c.cb.bindVertexBuffers(0, (*_rtData[voxelPipeline]._vbo[resource_index])->buffer(), vk::DeviceSize(0));
		c.cb.drawIndirect(_indirectActiveCount[resource_index]->buffer(), getIndirectActiveCountOffset(eVoxelVertexBuffer::VOXEL_TERRAIN), 1, 0);
	}
}

inline void cVulkan::_gpuReadback(vk::CommandBuffer& cb, uint32_t const resource_index)
{
	vk::CommandBufferBeginInfo bi{}; // static present cb only set once at init start-up
	cb.begin(bi); VKU_SET_CMD_BUFFER_LABEL(cb, vkNames::CommandBuffer::GPU_READBACK);

	copyMouseBuffer(cb, resource_index); // mouse picking image copy to buffer ** needs to be done here as this command buffer is dynamically generated / frame
	barrierMouseBuffer(cb, resource_index); // ensure gpu read back will be visible to cpu

	// *** Command buffer ends
	cb.end();
}

void cVulkan::copyMouseBuffer(vk::CommandBuffer& cb, uint32_t const resource_index) const
{
	vk::BufferImageCopy region{};
	region.bufferOffset = 0;

	vk::Extent3D extent;

	point2D_t const framebufferSz = MinCity::getFramebufferSize();
	extent.width = framebufferSz.x;
	extent.height = framebufferSz.y;
	extent.depth = 1;

	region.imageSubresource = { vk::ImageAspectFlagBits::eColor, 0, 0, 1 };
	region.imageExtent = extent;

	cb.copyImageToBuffer(_window->mouseImage().image(), vk::ImageLayout::eTransferSrcOptimal, _mouseBuffer[resource_index].buffer(), region);
}

void cVulkan::barrierMouseBuffer(vk::CommandBuffer& cb, uint32_t const resource_index) const
{
	_mouseBuffer[resource_index].barrier(	// ## ACQUIRE ## //
		cb, vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eHost,
		vk::DependencyFlagBits::eByRegion,
		vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eHostRead, MinCity::Vulkan->getTransferQueueIndex(), MinCity::Vulkan->getTransferQueueIndex()
	);
}

NO_INLINE point2D_t const __vectorcall cVulkan::queryMouseBuffer(XMVECTOR const xmMouse, uint32_t const resource_index)
{
	if (resource_index == _current_free_resource_index) {
		
		point2D_t const mouse_coord = v2_to_p2D_rounded(xmMouse);

		point2D_t const framebufferSz(MinCity::getFramebufferSize());

		point2D_t const sample_coord = p2D_clamp(mouse_coord, point2D_t(0), p2D_subs(framebufferSz, 1));

		size_t const offset(size_t(sample_coord.y) * size_t(framebufferSz.x) + size_t(sample_coord.x));

		uint32_t aR16G16(0);
		{
			uint32_t const* const __restrict gpu_read_back = reinterpret_cast<uint32_t const* const __restrict>(_mouseBuffer[resource_index].map());

			// capture and return mouse hovered voxel index (no need to copy out the entire mouseBuffer image buffer - isolated to current pixel instead that mouse pointer hovers)
			aR16G16 = *(gpu_read_back + offset);		// correct 2D to 1D coordinates

			_mouseBuffer[resource_index].unmap();
		}

		// converted: return mouse hovered voxel index
		_mouse_query_cache[resource_index].v = point2D_t((aR16G16 & 0x0000FFFF), ((aR16G16 & 0xFFFF0000) >> 16)).v;
	}
	
	return(_mouse_query_cache[resource_index]); // correct order
}


void cVulkan::copyOffscreenBuffer(vk::CommandBuffer& cb) const
{
	// currently shaderreadonlyoptimal as set by renderpass on completion
	_window->offscreenImage().setCurrentLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
	// setup for copy
	_window->offscreenImage().setLayout(cb, vk::ImageLayout::eTransferSrcOptimal);

	vk::BufferImageCopy region{};
	region.bufferOffset = 0;

	vk::Extent3D extent;

	point2D_t const framebufferSz = MinCity::getFramebufferSize();
	extent.width = framebufferSz.x;
	extent.height = framebufferSz.y;
	extent.depth = 1;

	region.imageSubresource = { vk::ImageAspectFlagBits::eColor, 0, 0, 1 };
	region.imageExtent = extent;

	cb.copyImageToBuffer(_window->offscreenImage().image(), vk::ImageLayout::eTransferSrcOptimal, _offscreenBuffer.buffer(), region);

	// don't need to transition back, as there is no further rendering usage of this image and next frame the renderpass expects undefined layout and clears image
}

void cVulkan::barrierOffscreenBuffer(vk::CommandBuffer& cb) const
{
	_offscreenBuffer.barrier(	// ## ACQUIRE ## //
		cb, vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eHost,
		vk::DependencyFlagBits::eByRegion,
		vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eHostRead, MinCity::Vulkan->getGraphicsQueueIndex(), MinCity::Vulkan->getGraphicsQueueIndex()
	);
}

// offscreen image capture
NO_INLINE void cVulkan::queryOffscreenBuffer(uint32_t* const __restrict mem_out) const	// *** only safe to call after the atomic flag returned from enableOffscreenCopy is cleared ***
{
	point2D_t const framebufferSz(MinCity::getFramebufferSize());
	size_t const size(size_t(framebufferSz.x) * size_t(framebufferSz.y) * sizeof(uint32_t));
	{
		uint32_t const* const __restrict gpu_read_back = reinterpret_cast<uint32_t const* const __restrict>(_offscreenBuffer.map());

		// copy entire framebuffer to output
		// 200 to 400 us faster than standard memcpy - hopefully this works for all frameBuffer sizes
		// frameBufferSizes are guaranteed to have a granularity of 8 (bugfix in GLFW)
		___memcpy_threaded<32>((uint8_t* const __restrict)mem_out, (uint8_t const* const __restrict)gpu_read_back, size, size / MinCity::hardware_concurrency());

		_offscreenBuffer.unmap();
	}
}

void cVulkan::renderOffscreenVoxels(vku::static_renderpass const& s)
{
	uint32_t const resource_index(s.resource_index);

	// front to back

	{ // dynamic voxels

		constexpr uint32_t const voxelPipeline = eVoxelPipeline::VOXEL_DYNAMIC_OFFSCREEN;

		uint32_t const ActiveVertexCount = (*_rtData[voxelPipeline]._vbo[resource_index])->ActiveVertexCount<VertexDecl::VoxelDynamic>();
		if (0 != ActiveVertexCount) {

			s.cb.bindVertexBuffers(0, (*_rtData[voxelPipeline]._vbo[resource_index])->buffer(), vk::DeviceSize(0));

			// main partition (always starts at vertex positiob 0)
			{
				uint32_t const partition_vertex_count = (*_rtData[voxelPipeline]._vbo[resource_index])->partitions()[eVoxelDynamicVertexBufferPartition::PARENT_MAIN].active_vertex_count;
				if (0 != partition_vertex_count) {
					s.cb.bindPipeline(vk::PipelineBindPoint::eGraphics, _rtData[voxelPipeline].pipeline);
					s.cb.draw(partition_vertex_count, 1, 0, 0);
				}
			}

			// draw children dynamic shader voxels (customized) (opaque voxel shaders only)
			// leveraging existing vb already bound

			for (uint32_t child = 0; child < eVoxelPipelineCustomized::_size(); ++child) {

				sRTDATA_CHILD const& __restrict rtDataChild(_rtDataChild[child]);
				vku::VertexBufferPartition const* const __restrict child_partition = rtDataChild.vbo_partition_info[resource_index];
				if (nullptr != child_partition && !rtDataChild.transparency) {

					if (!rtDataChild.mask) {
						uint32_t const partition_vertex_count = child_partition->active_vertex_count;
						if (0 != partition_vertex_count) {

							s.cb.bindPipeline(vk::PipelineBindPoint::eGraphics, rtDataChild.pipeline);

							uint32_t const partition_start_vertex = child_partition->vertex_start_offset;
							s.cb.draw(partition_vertex_count, 1, partition_start_vertex, 0);
						}
					}
				}
			}
		}
	}

	{ // static voxels
		constexpr uint32_t const voxelPipeline = eVoxelPipeline::VOXEL_STATIC_OFFSCREEN;

		uint32_t const ActiveVertexCount = (*_rtData[voxelPipeline]._vbo[resource_index])->ActiveVertexCount<VertexDecl::VoxelNormal>();
		if (0 != ActiveVertexCount) {
			s.cb.bindPipeline(vk::PipelineBindPoint::eGraphics, _rtData[voxelPipeline].pipeline);

			s.cb.bindVertexBuffers(0, (*_rtData[voxelPipeline]._vbo[resource_index])->buffer(), vk::DeviceSize(0));
			s.cb.draw(ActiveVertexCount, 1, 0, 0);
		}
	}

	{ // roads
		constexpr uint32_t const voxelPipeline = eVoxelPipeline::VOXEL_ROAD_OFFSCREEN;

		uint32_t const ActiveVertexCount = (*_rtData[voxelPipeline]._vbo[resource_index])->ActiveVertexCount<VertexDecl::VoxelNormal>();
		if (0 != ActiveVertexCount) {

			s.cb.bindVertexBuffers(0, (*_rtData[voxelPipeline]._vbo[resource_index])->buffer(), vk::DeviceSize(0));

			// main partition (always starts at vertex position 0)
			{
				uint32_t const partition_vertex_count = (*_rtData[voxelPipeline]._vbo[resource_index])->partitions()[eVoxelRoadVertexBufferPartition::PARENT_MAIN].active_vertex_count;
				if (0 != partition_vertex_count) {
					s.cb.bindPipeline(vk::PipelineBindPoint::eGraphics, _rtData[voxelPipeline].pipeline);
					s.cb.draw(partition_vertex_count, 1, 0, 0);
				}
			}
		}
	}
}

inline void cVulkan::_renderStaticCommandBuffer(vku::static_renderpass&& __restrict s)
{
	uint32_t const resource_index(s.resource_index);

	vk::CommandBufferBeginInfo bi{}; // static cb may persist across multiple frames if no changes in vbo active sizes
	s.cb.begin(bi); VKU_SET_CMD_BUFFER_LABEL(s.cb, vkNames::CommandBuffer::STATIC);
	
	// setup opacity map, internal image layout state only needs update here as it's image layout is carried forward from the last present.
	MinCity::VoxelWorld->getVolumetricOpacity().getVolumeSet().OpacityMap->setCurrentLayout(vk::ImageLayout::eGeneral);

	// [[deprecated]] transition alll textureshader outputs to shaderreadonlyoptimal
	// MinCity::VoxelWorld->makeTextureShaderOutputsReadOnly(s.cb);

	// prepare halfres target for writes in depth resolve fragment shader
	_window->depthResolvedImage(1).setLayout(s.cb, vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits::eFragmentShader, vku::ACCESS_READONLY, vk::PipelineStageFlagBits::eFragmentShader, vku::ACCESS_WRITEONLY);

	// transfer queue ownership of vertex buffers *required* see voxelworld.cpp Transfer() function
	{
		static constexpr size_t const buffer_count(4ULL);
		std::array<vku::GenericBuffer const* const, buffer_count> const buffers{ (*_rtData[eVoxelPipeline::VOXEL_TERRAIN]._vbo[resource_index]), (*_rtData[eVoxelPipeline::VOXEL_ROAD]._vbo[resource_index]), (*_rtData[eVoxelPipeline::VOXEL_STATIC]._vbo[resource_index]), (*_rtData[eVoxelPipeline::VOXEL_DYNAMIC]._vbo[resource_index]) };
		vku::GenericBuffer::barrier(buffers, // ## ACQUIRE ## // batched 
			s.cb, vk::PipelineStageFlagBits::eHost, vk::PipelineStageFlagBits::eVertexInput,
			vk::DependencyFlagBits::eByRegion,
			vk::AccessFlagBits::eHostWrite, vk::AccessFlagBits::eVertexAttributeRead, MinCity::Vulkan->getTransferQueueIndex(), MinCity::Vulkan->getGraphicsQueueIndex()
		);
	}
	// transfer queue ownership of main uniform buffer *required* see voxelworld.cpp Transfer() function
	_rtSharedData._ubo[resource_index].barrier(	// ## ACQUIRE ## //
		s.cb, vk::PipelineStageFlagBits::eHost, vk::PipelineStageFlagBits::eVertexShader,
		vk::DependencyFlagBits::eByRegion,
		vk::AccessFlagBits::eHostWrite, vk::AccessFlagBits::eUniformRead, MinCity::Vulkan->getTransferQueueIndex(), MinCity::Vulkan->getGraphicsQueueIndex()
	);
	MinCity::VoxelWorld->AcquireTransferQueueOwnership(resource_index, s.cb);

	// #### Z RENDER PASS BEGIN #### //
	s.cb.beginRenderPass(s.rpbiZ, vk::SubpassContents::eInline);	// SUBPASS - regular rendering //
	// all voxels share the same descriptor set
	bindVoxelDescriptorSet<eVoxelDescSharedLayoutSet::VOXEL_CLEAR>(resource_index, s.cb);

	{
		sRTDATA_CHILD const* deferredChildMasks[NUM_CHILD_MASKS];			  
		uint32_t const ActiveMaskCount = renderAllVoxels_ZPass<NUM_CHILD_MASKS>(s, deferredChildMasks);
		renderClearMasks(std::forward<vku::static_renderpass&&>(s), deferredChildMasks, ActiveMaskCount);
	}
	s.cb.nextSubpass(vk::SubpassContents::eInline);

	// SUBPASS - depth resolve //
	s.cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *_depthData.pipelineLayout, 0, _depthData.sets[0], nullptr);
	s.cb.bindPipeline(vk::PipelineBindPoint::eGraphics, _depthData.pipeline);
	// Post-process quad simple generation - fullscreen triangle optimized!
	// https://www.saschawillems.de/blog/2016/08/13/vulkan-tutorial-on-rendering-a-fullscreen-quad-without-buffers/
	s.cb.draw(3, 1, 0, 0);

	s.cb.endRenderPass();

	// prepare for usage in fragment shaders (raymarching & voxel frag) remains read-only until end of frame where it is cleared and setup for next frame (finalPass / Present)
	auto& volume_set(MinCity::VoxelWorld->getVolumetricOpacity().getVolumeSet());

	volume_set.OpacityMap->setLayout(s.cb, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits::eVertexShader, vku::ACCESS_WRITEONLY,
		vk::PipelineStageFlagBits::eFragmentShader, vku::ACCESS_READONLY);

	{ // required at this point, light:
		volume_set.LightMap->DistanceDirection->setCurrentLayout(vk::ImageLayout::eGeneral); // *bugfix for tricky validation error
		volume_set.LightMap->Color->setCurrentLayout(vk::ImageLayout::eGeneral);
		volume_set.LightMap->Reflection->setCurrentLayout(vk::ImageLayout::eGeneral);

		static constexpr size_t const image_count(3ULL); // batched
		std::array<vku::GenericImage* const, image_count> const images{ volume_set.LightMap->DistanceDirection, volume_set.LightMap->Color, volume_set.LightMap->Reflection };

		vku::GenericImage::setLayout<image_count>(images, s.cb, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits::eComputeShader, vku::ACCESS_WRITEONLY, vk::PipelineStageFlagBits::eFragmentShader, vku::ACCESS_READONLY);

		volume_set.LightMap->DistanceDirection->setCurrentLayout(vk::ImageLayout::eShaderReadOnlyOptimal); // *bugfix for tricky validation error
		volume_set.LightMap->Color->setCurrentLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
		volume_set.LightMap->Reflection->setCurrentLayout(vk::ImageLayout::eShaderReadOnlyOptimal);
	}

	UpdateIndirectActiveCountBuffer(s.cb, resource_index); // indirect draw active counts are transferred here, they are then only used by the clear opacity map operation in the final pass / present.

	// #### HALF RESOLUTION RENDER PASS BEGIN #### //
	s.cb.beginRenderPass(s.rpbiHalf, vk::SubpassContents::eInline);	// SUBPASS - regular rendering //

	// SUBPASS - raymarch rendering //

	// Volume Rendering using raymarch of 3d/volume texture:
	{
#ifndef VOLUMETRIC_RAYMARCHING_DISABLED

		s.cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *_volData.pipelineLayout, 0,
			_volData.sets[resource_index], nullptr);

		s.cb.bindPipeline(vk::PipelineBindPoint::eGraphics, _volData.pipeline);

		s.cb.bindVertexBuffers(0, _volData._vbo.buffer(), vk::DeviceSize(0));
		s.cb.bindIndexBuffer(_volData._ibo.buffer(), vk::DeviceSize(0), vk::IndexType::eUint16);

		s.cb.drawIndexed(_volData.index_count, 1, 0, 0, 0);
#endif
		// renderpass changes layout - update state of image to match
		_window->depthResolvedImage(1).setCurrentLayout(vk::ImageLayout::eShaderReadOnlyOptimal);

	}

	// SUBPASS - resolve //
	s.cb.nextSubpass(vk::SubpassContents::eInline);

	s.cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *_upData[eUpsamplePipeline::RESOLVE].pipelineLayout, 0, _upData[eUpsamplePipeline::RESOLVE].sets[resource_index], nullptr);
	s.cb.bindPipeline(vk::PipelineBindPoint::eGraphics, _upData[eUpsamplePipeline::RESOLVE].pipeline);
	// Post-process quad simple generation - fullscreen triangle optimized!
	// https://www.saschawillems.de/blog/2016/08/13/vulkan-tutorial-on-rendering-a-fullscreen-quad-without-buffers/
	s.cb.draw(3, 1, 0, 0);

	s.cb.endRenderPass();

	// #### FULL RESOLUTION RENDER PASS BEGIN #### //
	s.cb.beginRenderPass(s.rpbiFull, vk::SubpassContents::eInline);	// SUBPASS - regular rendering //
	
	// SUBPASS - bilateral upsampling //
	s.cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *_upData[eUpsamplePipeline::UPSAMPLE].pipelineLayout, 0, _upData[eUpsamplePipeline::UPSAMPLE].sets[resource_index], nullptr);
	s.cb.bindPipeline(vk::PipelineBindPoint::eGraphics, _upData[eUpsamplePipeline::UPSAMPLE].pipeline);
	// Post-process quad simple generation - fullscreen triangle optimized!
	// https://www.saschawillems.de/blog/2016/08/13/vulkan-tutorial-on-rendering-a-fullscreen-quad-without-buffers/
	s.cb.draw(3, 1, 0, 0);

	s.cb.endRenderPass();

	// layout is changed in previous renderpass, sync its state
	_window->colorReflectionImage().setCurrentLayout(vk::ImageLayout::eShaderReadOnlyOptimal); // fixes bug with validation error on first frame


	// #### MID / INTERMEDIATTE RENDER PASS BEGIN #### //
	s.cb.beginRenderPass(s.rpbiMid, vk::SubpassContents::eInline);	// SUBPASS - regular rendering //

	// SUBPASS - Regular voxel rendering
	bindVoxelDescriptorSet<eVoxelDescSharedLayoutSet::VOXEL_COMMON>(resource_index, s.cb); // all voxels share the same descriptor set
	(void)renderAllVoxels<1>(s);

	// SUBPASS - bilateral blend //
	s.cb.nextSubpass(vk::SubpassContents::eInline);

	s.cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *_upData[eUpsamplePipeline::BLEND].pipelineLayout, 0, _upData[eUpsamplePipeline::BLEND].sets[0], nullptr);
	s.cb.bindPipeline(vk::PipelineBindPoint::eGraphics, _upData[eUpsamplePipeline::BLEND].pipeline);
	// Post-process quad simple generation - fullscreen triangle optimized!
	// https://www.saschawillems.de/blog/2016/08/13/vulkan-tutorial-on-rendering-a-fullscreen-quad-without-buffers/
	s.cb.draw(3, 1, 0, 0);

	// last color render copy ** now resolved from the above subpass for transparency in overlay pass//

	s.cb.endRenderPass();

	// End of frame main renderpass //

	// #### optional OFFSCREEN RENDERPASS BEGIN #### //
	if (_bOffscreenRender) {
		s.cb.beginRenderPass(s.rpbiOff, vk::SubpassContents::eInline);	// SUBPASS - regular rendering //

		// SUBPASS - Regular voxel rendering
		bindVoxelDescriptorSet<eVoxelDescSharedLayoutSet::VOXEL_COMMON>(resource_index, s.cb); // all voxels share the same descriptor set

		// specific voxel rendering for offscreen pass //
		renderOffscreenVoxels(s); // currently draws all voxels except terrain for GUI presentation

		s.cb.endRenderPass();
	}
	else {
		// just allow transition of image layout 
		s.cb.beginRenderPass(s.rpbiOff, vk::SubpassContents::eInline);	// SUBPASS - regular rendering //
		s.cb.endRenderPass();
	}

	s.cb.end();
}

inline void cVulkan::_renderDynamicCommandBuffer(vku::dynamic_renderpass&& __restrict d)
{
	uint32_t const resource_index(d.resource_index);

	vku::DynamicVertexBuffer* const __restrict vbos[] = {
		(*_rtData[eVoxelPipeline::VOXEL_TERRAIN]._vbo[resource_index]),
		(*_rtData[eVoxelPipeline::VOXEL_ROAD]._vbo[resource_index]),
		(*_rtData[eVoxelPipeline::VOXEL_STATIC]._vbo[resource_index]),
		(*_rtData[eVoxelPipeline::VOXEL_DYNAMIC]._vbo[resource_index]),
		
	};

	MinCity::VoxelWorld->Transfer(resource_index, d.cb, vbos);
}

inline void cVulkan::_renderOverlayCommandBuffer(vku::overlay_renderpass&& __restrict o) // fully dynamic command buffer (every frame)
{
	uint32_t const resource_index(o.resource_index);

	if (nullptr != o.cb_transfer) {
		MinCity::Nuklear->Upload(resource_index, *o.cb_transfer, _nkData._vbo[resource_index], _nkData._ibo[resource_index], _nkData._ubo);
	}
	else if (nullptr != o.cb_render ) // rendering
	{
		// ############# TRANSPARENCY BEGIN PASS ################################################ //
		vk::CommandBufferBeginInfo bi2(vk::CommandBufferUsageFlagBits::eOneTimeSubmit); // updated every frame
		o.cb_render->begin(bi2); VKU_SET_CMD_BUFFER_LABEL(*o.cb_render, vkNames::CommandBuffer::OVERLAY_RENDER);

		// leverage free (waiting) time to ...
		
		// prepare for usage in fragment shader, transparent voxels do not modify the opacity map so its layout is left unmodified
		_window->colorReflectionImage().setLayout(*o.cb_render, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits::eFragmentShader, vku::ACCESS_WRITEONLY, vk::PipelineStageFlagBits::eFragmentShader, vku::ACCESS_READONLY);

		MinCity::Nuklear->AcquireTransferQueueOwnership(*o.cb_render, _nkData._vbo[resource_index], _nkData._ibo[resource_index], _nkData._ubo);

		o.cb_render->beginRenderPass(o.rpbi, vk::SubpassContents::eInline);

		// all voxels share the same descriptor set
		bindVoxelDescriptorSet<eVoxelDescSharedLayoutSet::VOXEL_COMMON>(resource_index, *o.cb_render); // all voxels share the same descriptor set

		// SUBPASS - voxels w/Transparency //

		{ // transparent roads
			constexpr uint32_t const voxelPipeline = eVoxelPipeline::VOXEL_ROAD_TRANS;

			uint32_t const ActiveVertexCount = (*_rtData[voxelPipeline]._vbo[resource_index])->ActiveVertexCount<VertexDecl::VoxelNormal>();
			if (0 != ActiveVertexCount) {

				o.cb_render->bindVertexBuffers(0, (*_rtData[voxelPipeline]._vbo[resource_index])->buffer(), vk::DeviceSize(0));

				// trans partition
				{
					auto const& partition_info((*_rtData[voxelPipeline]._vbo[resource_index])->partitions()[eVoxelRoadVertexBufferPartition::PARENT_TRANS]);
					uint32_t const partition_vertex_count = partition_info.active_vertex_count;
					if (0 != partition_vertex_count) {
						o.cb_render->bindPipeline(vk::PipelineBindPoint::eGraphics, _rtData[voxelPipeline].pipeline);
						uint32_t const partition_start_vertex = partition_info.vertex_start_offset;
						o.cb_render->draw(partition_vertex_count, 1, partition_start_vertex, 0);
					}
				}
			}
		}
		{ // dynamic voxels
			uint32_t const ActiveVertexCount = (*_rtData[eVoxelPipeline::VOXEL_DYNAMIC]._vbo[resource_index])->ActiveVertexCount<VertexDecl::VoxelDynamic>();
			if (0 != ActiveVertexCount) {

				// draw children dynamic shader voxels (customized) (transparent voxel shaders only)

				// leveraging dynamic vb 
				// all child shaders share this descriptor set, but have unique pipelines
				o.cb_render->bindVertexBuffers(0, (*_rtData[eVoxelPipeline::VOXEL_DYNAMIC]._vbo[resource_index])->buffer(), vk::DeviceSize(0));

				for (uint32_t child = 0; child < eVoxelPipelineCustomized::_size(); ++child) {

					vku::VertexBufferPartition const* const __restrict child_partition = _rtDataChild[child].vbo_partition_info[resource_index];
					if (nullptr != child_partition && _rtDataChild[child].transparency) {

						uint32_t const partition_vertex_count = child_partition->active_vertex_count;
						if (0 != partition_vertex_count) {

							o.cb_render->bindPipeline(vk::PipelineBindPoint::eGraphics, _rtDataChild[child].pipeline);

							uint32_t const partition_start_vertex = child_partition->vertex_start_offset;
							o.cb_render->draw(partition_vertex_count, 1, partition_start_vertex, 0);
						}
					}
				}
			}
		}

		o.cb_render->nextSubpass(vk::SubpassContents::eInline); // SUBPASS - Isolated Resolve of Color buffer to fix validation error (required, see vku_framework)
		

		o.cb_render->nextSubpass(vk::SubpassContents::eInline); // SUBPASS - Nuklear 2D GUI Overlay *bugfix, must be in seperate dedicated pass or else performance suffers greatly! //
		{
			RenderingInfo const nk_renderInfo(o.rpbi, _nkData.pipelineLayout, _nkData.pipeline, _nkData.descLayout, _nkData.sets);
			MinCity::Nuklear->Render(*o.cb_render, _nkData._vbo[resource_index], _nkData._ibo[resource_index], _nkData._ubo, nk_renderInfo);
		}

		// ############# TRANSPARENCY END PASS ################################################ //
		o.cb_render->endRenderPass();

		// prepare for usage in vertex shader (general layout) - clearing opacity map 
		MinCity::VoxelWorld->getVolumetricOpacity().getVolumeSet().OpacityMap->setCurrentLayout(vk::ImageLayout::eShaderReadOnlyOptimal); // required to update internal image state - present command buffer is reused and only recorded once at init.
		MinCity::VoxelWorld->getVolumetricOpacity().getVolumeSet().OpacityMap->setLayout(*o.cb_render, vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits::eFragmentShader, vku::ACCESS_READONLY, vk::PipelineStageFlagBits::eVertexShader, vku::ACCESS_WRITEONLY);
		
		// The offscreen copy if enabled has to happen here, where there is no further usage by rendering of it in an imageView (like in the Nuklear GUI above)
		[[unlikely]] if ( _bOffscreenCopy ) { // single frame capture is a rare operation
			copyOffscreenBuffer(*o.cb_render);		// copy & barrier done here to simplify - overlay cb is dynamic (built every frame) unlike the static or present cb's
			barrierOffscreenBuffer(*o.cb_render);	// don't want to reset both cb's for single frame capture
		}

		o.cb_render->end();
	}
}

// global for vku
void setPresentationBlendWeight(uint32_t const imageIndex) // workaround to get current blend weight for current imageIndex
{
	MinCity::Vulkan->setPresentationBlendWeight(imageIndex);
}
void cVulkan::setPresentationBlendWeight(uint32_t const imageIndex)
{
	MinCity::PostProcess->setPresentationBlendWeight(imageIndex);
}

inline void cVulkan::_renderPresentCommandBuffer(vku::present_renderpass&& __restrict pp) // fully static set once command buffer at app startup
{
	vk::CommandBufferBeginInfo bi{}; // static present cb only set once at init start-up
	pp.cb.begin(bi); VKU_SET_CMD_BUFFER_LABEL(pp.cb, vkNames::CommandBuffer::PRESENT);
	
	// begin "actual render pass"
	pp.cb.beginRenderPass(pp.rpbi, vk::SubpassContents::eInline);	// SUBPASS - present post aa rendering //

	MinCity::PostProcess->Render(std::forward<vku::present_renderpass&& __restrict>(pp), _aaData);
	// must be back to back //
	MinCity::PostProcess->Present(std::forward<vku::present_renderpass&& __restrict>(pp), _aaData);

	// *** Command buffer ends
	pp.cb.endRenderPass();
	pp.cb.end();
}
inline void cVulkan::_renderClearCommandBuffer(vku::clear_renderpass&& __restrict c)
{
	// Clear pass
	vk::CommandBufferBeginInfo bi{}; // static present cb only set once at init start-up
	c.cb.begin(bi); VKU_SET_CMD_BUFFER_LABEL(c.cb, vkNames::CommandBuffer::CLEAR);


	_indirectActiveCount[c.resource_index]->barrier( // required b4 usage //
		c.cb, vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eDrawIndirect,
		vk::DependencyFlagBits::eByRegion,
		vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eIndirectCommandRead, MinCity::Vulkan->getGraphicsQueueIndex(), MinCity::Vulkan->getGraphicsQueueIndex()
	);

	// begin "actual render pass"
	c.cb.beginRenderPass(c.rpbi, vk::SubpassContents::eInline);	// SUBPASS - present post aa rendering //

	{
		// all voxels share the same descriptor set
		bindVoxelDescriptorSet<eVoxelDescSharedLayoutSet::VOXEL_CLEAR>(c.resource_index, c.cb);
		//################ specialized FAST clear 3d volume/texture "opacity" for usage next frame ##########################//
		clearAllVoxels(std::forward<vku::clear_renderpass&&>(c));
		// *** no reads or writes occur until next frame after this point from the opacity map *** //
	}

	// *** Command buffer ends
	c.cb.endRenderPass();
	c.cb.end();
}

void cVulkan::renderComplete(uint32_t const resource_index) // triggered internally on Render Completion (after final queue submission / present by vku framework
{
	MinCity::OnRenderComplete(resource_index);

	[[unlikely]] if ( _bOffscreenCopy ) { // single frame capture is a rare operation

		_OffscreenCopied.clear(); // signal copied / copy finished
		_bOffscreenCopy = false; // only a single frame capture, reset always
	}
}

static microseconds const frameTiming(tTime const& tNow, bool const real_frame) // real-time domain
{
	static constexpr milliseconds const PRINT_FRAME_INTERVAL = milliseconds(5500); // ms
	static auto start = high_resolution_clock::now();
	static auto lastPrint = high_resolution_clock::now();
	static microseconds last{}, sum{}, peak{}, aboveaveragelevel{}, lastaverage{};
	static size_t framecount(0), aboveaveragecount(0), belowaveragecount(0);

	auto const deltaNano = tNow - start;
	start = tNow;
	if (!real_frame) {
		return(last);
	}

	microseconds const deltaMicro = duration_cast<microseconds>(deltaNano);
	peak = microseconds( std::max(peak, deltaMicro) );
	sum += deltaMicro;
	if (deltaMicro > aboveaveragelevel)
		++aboveaveragecount;
	else if (deltaMicro < lastaverage)
		++belowaveragecount;

	++framecount;

	if (tNow - lastPrint > PRINT_FRAME_INTERVAL) {
		lastPrint = tNow;
		// uncomment to get frame time.
		microseconds const avgdelta = sum / framecount;
		
		fmt::print(fg(fmt::color::magenta), "\n" "[ {:d} us, {:d} us peak ]\n", avgdelta.count(), peak.count());
		if (aboveaveragecount && belowaveragecount) {

			size_t const belowaverage = (belowaveragecount * 100) / framecount;
			size_t const aboveaverage = (aboveaveragecount * 100) / framecount;

			fmt::print(fg(fmt::color::white), "[ ");
			
			if (belowaverage < 24) {
				fmt::print(fg(fmt::color::red), "-{:d}% ", belowaverage);
			}
			else if (belowaverage < 48) {
				fmt::print(fg(fmt::color::yellow), "-{:d}% ", belowaverage);
			}
			else {
				fmt::print(fg(fmt::color::lime_green), "-{:d}% ", belowaverage);
			}
			if (aboveaverage < 24) {
				fmt::print(fg(fmt::color::lime_green), " +{:d}% ", aboveaverage);
			}
			else if (aboveaverage < 48) {
				fmt::print(fg(fmt::color::yellow), " +{:d}% ", aboveaverage);
			}
			else {
				fmt::print(fg(fmt::color::red), " +{:d}% ", aboveaverage);
			}

			fmt::print(fg(fmt::color::white), " ]\n");
		}
		
		aboveaveragelevel = microseconds((avgdelta + peak).count() >> 1);
		lastaverage = avgdelta;
		peak = microseconds(0); sum = lastaverage; framecount = 0; aboveaveragecount = 0; belowaveragecount = 0;

		return(lastaverage);
	}

	last = sum / framecount;
	return(last);
}

void cVulkan::Render()
{
	[[unlikely]] if (!_bRenderingEnabled) {
		_mm_pause();
		return;
	}

#if (!defined(NDEBUG) & defined(LIVESHADER_MODE) & defined(LIVE_PIPELINE) & defined(LIVE_PIPELINELAYOUT) & defined(LIVE_RENDERPASS) & defined(LIVE_INTERVAL))
	liveshader::recreate_pipeline(LIVE_PIPELINE, _device, _fw, LIVE_PIPELINELAYOUT, LIVE_RENDERPASS, LIVE_INTERVAL);
#endif

	// ####### //
	constinit static uint32_t last_free_resource_index(0);
	
	_current_free_resource_index = _window->draw(
		_device, cVulkan::renderCompute, cVulkan::renderDynamicCommandBuffer, cVulkan::renderOverlayCommandBuffer
	);
	// ####### //

	bool const real_frame(last_free_resource_index != _current_free_resource_index);

	if (real_frame) {
		renderComplete(_current_free_resource_index); // this happens after queue submission / present by the vku framework on real frames only
	}
	_frameTimingAverage = frameTiming(high_resolution_clock::now(), real_frame);

	last_free_resource_index = _current_free_resource_index;
}

void cVulkan::setStaticCommandsDirty()
{
	_window->setStaticCommandsDirty(cVulkan::renderStaticCommandBuffer);
}

void cVulkan::checkStaticCommandsDirty(uint32_t const resource_index)
{
	// check static command buffers
	int32_t index = (int32_t)eVoxelPipeline::_size() - 1;
	do {
		switch (index) {

		case eVoxelPipeline::VOXEL_TERRAIN:
		case eVoxelPipeline::VOXEL_ROAD:
		case eVoxelPipeline::VOXEL_STATIC:
		case eVoxelPipeline::VOXEL_DYNAMIC:
			if ((*_rtData[index]._vbo[resource_index])->isBufferActiveSizeDelta()) {
				_window->setStaticCommandsDirty(cVulkan::renderStaticCommandBuffer);
				return;
			}
			break;
		default:
			break;
		}
	} while (--index >= 0);
}
void cVulkan::enableOffscreenRendering(bool const bEnable)
{ 
	if (bEnable != _bOffscreenRender) {
		_bOffscreenRender = bEnable;
		_window->setStaticCommandsDirty(cVulkan::renderStaticCommandBuffer); // trigger update as static rendering has changed
	}
}
void cVulkan::enableOffscreenCopy()
{
	if (!_bOffscreenCopy) {
		enableOffscreenRendering(true); // enable offscreen rendering if not already enabled

		_OffscreenCopied.test_and_set(); // reset copied flag 
		_bOffscreenCopy = true;
	}

	// caller to this function should poll by test_and_set waiting for false to return indicating flag has been cleared
	// when flag is cleared the copy is complete 
	// use method: getOffscreenCopyStatus()
}

static void setHdrMetadata(vk::HdrMetadataEXT& metadata)
{
	// Mastered on LG DisplayHDR 400 (Vesa certified 400nit maximum luminance) ST2084 BT.2020
	// these numbers are ITU-R BT.2020 color primaries.
	// https://www.itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.2020-2-201510-I!!PDF-E.pdf
	metadata.displayPrimaryRed.x = 0.708f;
	metadata.displayPrimaryRed.y = 0.292f;
	metadata.displayPrimaryGreen.x = 0.170f;
	metadata.displayPrimaryGreen.y = 0.797f;
	metadata.displayPrimaryBlue.x = 0.131f;
	metadata.displayPrimaryBlue.y = 0.046f;

	// most vibrant dark to hi contrast ratio when metadata.maxFrameAverageLightLevel = metadata.maxContentLightLevel = metadata.maxLuminance (nits) from the original content creators own monitor. I got a weak 400 nit HDR monitor (it's bright enough, any friggen brighter and I would be concerned with my eyes and very low frequency red colors borderlining the infrared spectrum. thats laser beam wavelengths - is that possible?
	metadata.minLuminance = 0.0f;   // --------- notes on HDR10 and standards research done - might forget them. https://www.itu.int/dms_pubrec/itu-r/rec/bt/R-REC-BT.2020-2-201510-I!!PDF-E.pdf (easy to follow document solved in post processing final HDR steps (conversion to extended color primary bt.2020 then using the steps as outlined in the standards document)).
	metadata.maxLuminance = 400.0f; // This will cause tonemapping to happen on display end as long as it's greater than display's actual queried max luminance. The look will change and it will be display dependent! 
									// --------- (adapts to varying brighness [nits] that exist in HDR Vesa Certified displays. range [100 .... 4000+] nits) -------- imagine the limit, 10000+ nits. how far back should you be. Then there
									// --------- is the proprietary standard DolbyHDR which has a limit of 40,000 nits. In a dark room that thing is illuminating everything extremely powerful. Where do these screens actually exist?
									// --------- So instead of paying $$$ for license an open standard exists HDR10, which is backed by Vesa - the original standards organization for the majority of monitors.
									// --------- HDR10 is far more successful in terms of usage currently vs DolbyHDR being limited to select few actual instalations in the world. No consumer displays exist that use DolbyHDR, it's all HDR10.
	metadata.maxContentLightLevel = 400.0f;
	metadata.maxFrameAverageLightLevel = 400.0f; // max and average content light level data will be used to do tonemapping on display
	metadata.whitePoint.x = 0.3127f;
	metadata.whitePoint.y = 0.3290f;
}

void cVulkan::OnRestored(struct GLFWwindow* const glfwwindow)
{
#if defined(FULLSCREEN_EXCLUSIVE)
	if (isFullScreenExclusive()) { // extension is enabled & supported, device queried for actual support, enabled on swap chain

		if (!_bFullScreenExclusiveAcquired) {

			if ((VkResult)vk::Result::eSuccess == vkAcquireFullScreenExclusiveModeEXT(_device, _window->swapchain()))
			{
				_bFullScreenExclusiveAcquired = true;
				FMT_LOG_OK(GPU_LOG, "Fullscreen Exclusive Mode Acquired");

				if (isHDR()) {
#if defined(VK_EXT_hdr_metadata) // *** must only be set after fullscreen exclusive has been acquired
					// mastering hdr metadata "hint" for HDR10 
					vk::HdrMetadataEXT metadata{};
					setHdrMetadata(metadata);
					_device.setHdrMetadataEXT(1u, &_window->swapchain(), &metadata);
#endif
				}
			}
		}
	}
#endif
	if (!_bRenderingEnabled) {
		if (!_bRestoreAsPaused) {
			MinCity::Pause(false);
		}
		FMT_LOG_OK(GPU_LOG, "Rendering Enabled");
		_bRenderingEnabled = true;
	}
}

void cVulkan::OnLost(struct GLFWwindow* const glfwwindow)
{
#if defined(FULLSCREEN_EXCLUSIVE)
	if (isFullScreenExclusive()) { // extension is enabled & supported, device queried for actual support, enabled on swap chain

		if (_bFullScreenExclusiveAcquired) {
			vkReleaseFullScreenExclusiveModeEXT(_device, _window->swapchain());
			FMT_LOG_OK(GPU_LOG, "Fullscreen Exclusive Mode Released");
		}
		_bFullScreenExclusiveAcquired = false;
	}
#endif
	if (_bRenderingEnabled) {
		_bRestoreAsPaused = MinCity::isPaused();
#ifndef DEBUG_DISALLOW_PAUSE_FOCUS_LOST
		MinCity::Pause(true);
#endif
#ifndef DEBUG_DISALLOW_RENDER_DISABLING
		FMT_LOG_OK(GPU_LOG, "Rendering Disabled");
		_bRenderingEnabled = false;
#endif
	}
	glfwRequestWindowAttention(glfwwindow);
}

void cVulkan::WaitDeviceIdle()
{
	_device.waitIdle();
}

void cVulkan::Cleanup(GLFWwindow* const glfwwindow)
{
	// Wait until all drawing is done and then kill the window.
	WaitDeviceIdle();

	using pool_map = tbb::concurrent_unordered_map<VkCommandPool, vku::CommandBufferContainer<1>>;
	for (pool_map::iterator iter = vku::pool_.begin(); iter != vku::pool_.end(); ++iter) {

		iter->second.release(_device);
	}
	
	_activeCountBuffer[0].release(); _activeCountBuffer[1].release();
	SAFE_RELEASE_DELETE(_indirectActiveCount[0]); SAFE_RELEASE_DELETE(_indirectActiveCount[1]);

	_mouseBuffer[0].release(); _mouseBuffer[1].release();
	_offscreenBuffer.release();

	// clean up main vertex buffers
	// remove references / aliases
	for (uint32_t resource_index = 0; resource_index < vku::double_buffer<uint32_t>::count; ++resource_index) {
		(*_rtData[eVoxelPipeline::VOXEL_TERRAIN_BASIC_ZONLY]._vbo[resource_index]) = nullptr;
		(*_rtData[eVoxelPipeline::VOXEL_TERRAIN_BASIC]._vbo[resource_index]) = nullptr;
		(*_rtData[eVoxelPipeline::VOXEL_TERRAIN_BASIC_CLEAR]._vbo[resource_index]) = nullptr;
		(*_rtData[eVoxelPipeline::VOXEL_ROAD_BASIC_ZONLY]._vbo[resource_index]) = nullptr;
		(*_rtData[eVoxelPipeline::VOXEL_ROAD_BASIC]._vbo[resource_index]) = nullptr;
		(*_rtData[eVoxelPipeline::VOXEL_ROAD_TRANS]._vbo[resource_index]) = nullptr;
		(*_rtData[eVoxelPipeline::VOXEL_ROAD_CLEARMASK]._vbo[resource_index]) = nullptr;
		(*_rtData[eVoxelPipeline::VOXEL_ROAD_OFFSCREEN]._vbo[resource_index]) = nullptr;
		(*_rtData[eVoxelPipeline::VOXEL_STATIC_BASIC_ZONLY]._vbo[resource_index]) = nullptr;
		(*_rtData[eVoxelPipeline::VOXEL_STATIC_BASIC]._vbo[resource_index]) = nullptr;
		(*_rtData[eVoxelPipeline::VOXEL_STATIC_BASIC_CLEAR]._vbo[resource_index]) = nullptr;
		(*_rtData[eVoxelPipeline::VOXEL_STATIC_OFFSCREEN]._vbo[resource_index]) = nullptr;
		(*_rtData[eVoxelPipeline::VOXEL_DYNAMIC_BASIC_ZONLY]._vbo[resource_index]) = nullptr;
		(*_rtData[eVoxelPipeline::VOXEL_DYNAMIC_BASIC]._vbo[resource_index]) = nullptr;
		(*_rtData[eVoxelPipeline::VOXEL_DYNAMIC_BASIC_CLEAR]._vbo[resource_index]) = nullptr;
		(*_rtData[eVoxelPipeline::VOXEL_DYNAMIC_OFFSCREEN]._vbo[resource_index]) = nullptr;
	}

	for (auto vbo : _vbos) {
		SAFE_RELEASE_DELETE(vbo);
	}
	_vbos.clear();
	_vbos.shrink_to_fit();

	_aaData.~sPOSTAADATA();
	_depthData.~sDEPTHRESOLVEDATA();

#ifdef DEBUG_LIGHT_PROPAGATION
	_comData.debugLightData.~sCOMPUTEDEBUGLIGHTDATA();
#endif

	_comData.~sCOMPUTEDATA();
	_volData.~sVOLUMETRICLIGHTDATA();

	for (uint32_t iDx = 0; iDx < eUpsamplePipeline::_size(); ++iDx) {
		_upData[iDx].~sUPSAMPLEDATA();
	}

	// ### STATIC structs must be manually released her #### //
	_rtSharedData.~sRTSHARED_DATA();
	for (uint32_t iDx = 0; iDx < eVoxelDescSharedLayoutSet::_size(); ++iDx) {
		_rtSharedDescSet[iDx].~sRTSHARED_DESCSET();
	}

	for (uint32_t iDx = 0; iDx < eVoxelPipelineCustomized::_size(); ++iDx) {
		_rtDataChild[iDx].~sRTDATA_CHILD();
	}
	for (uint32_t iDx = 0; iDx < eVoxelPipeline::_size(); ++iDx) {
		_rtData[iDx].~sRTDATA();
	}
	_nkData.~sNUKLEARDATA();

	glfwDestroyWindow(glfwwindow);
	
	SAFE_DELETE(_window);

	// MUST BE LAST !! //
	glfwTerminate();  // MUST BE LAST !! //
}

cVulkan::~cVulkan()
{
	_device.waitIdle();
}