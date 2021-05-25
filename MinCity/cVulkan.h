#pragma once
// this is the only area of program that should include these files *****
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vku/volk/volk.h>

#define VKU_SURFACE "VK_KHR_win32_surface"

#include <vku/vku_framework.hpp>	// this is the one and only place vku_framework.hpp can be included, use cVulkan.h (prefer)

// ^^^^^^^ this is the only area of program that should include these files *****
#include <Utility/class_helper.h>
#include "Declarations.h"
#include "RenderInfo.h"
#include "tTime.h"
#include <Math/superfastmath.h>
#include <Math/point2D_t.h>
#include "betterenums.h"
#include <optional>

namespace eSamplerSampling {
	enum : uint32_t const {
		NEAREST = 0,
		LINEAR,
		ANISOTROPIC
		//------------------------------//
		, COUNT
	};
	static constexpr uint32_t size() {
		return(COUNT);
	}
}
namespace eSamplerAddressing {
	enum {
		CLAMP = 0,
		REPEAT,
		MIRRORED_REPEAT,
		BORDER
		//------------------------------//
		, COUNT
	};
	static constexpr uint32_t size() {
		return(COUNT);
	}
}

BETTER_ENUM(eSourceData, uint32_t,

	NUKLEAR_DATA = 0,
	VOXEL_DATA
);
BETTER_ENUM(eVoxelDescSharedLayout, uint32_t const,

	VOXEL_COMMON = 0,
	VOXEL_CLEAR
);
BETTER_ENUM(eVoxelDescSharedLayoutSet, uint32_t const,

	VOXEL_COMMON = 0,
	VOXEL_CLEAR
);
BETTER_ENUM(eVoxelSharedPipeline, uint32_t const,

	VOXEL_CLEAR = 0
);
BETTER_ENUM(eVoxelVertexBuffer, uint32_t const,

	VOXEL_TERRAIN = 0,
	VOXEL_ROAD,
	VOXEL_STATIC,
	VOXEL_DYNAMIC
);
BETTER_ENUM(eVoxelPipeline, uint32_t const,

	VOXEL_TERRAIN_BASIC = 0,
	VOXEL_TERRAIN,
	VOXEL_TERRAIN_BASIC_CLEAR,

	VOXEL_ROAD_BASIC,
	VOXEL_ROAD,
	VOXEL_ROAD_TRANS,
	VOXEL_ROAD_CLEARMASK, // ROAD is not written to opacitymap, ground underneath is close enough for raymarch and it uses a zbuffer with the road actually in it
	VOXEL_ROAD_OFFSCREEN,

	VOXEL_STATIC_BASIC,
	VOXEL_STATIC,
	VOXEL_STATIC_BASIC_CLEAR,
	VOXEL_STATIC_OFFSCREEN,

	VOXEL_DYNAMIC_BASIC,
	VOXEL_DYNAMIC,
	VOXEL_DYNAMIC_BASIC_CLEAR,
	VOXEL_DYNAMIC_OFFSCREEN
);
BETTER_ENUM(eVoxelPipelineCustomized, uint32_t const,  // ***** shaders first, then clears (clears must be last)

	// ## shaders //
	VOXEL_SHADER_EXPLOSION = 0,
	VOXEL_SHADER_TORNADO,
	VOXEL_SHADER_SHOCKWAVE,
	VOXEL_SHADER_RAIN,

	// main transparent only (must be last shader) //
	VOXEL_SHADER_TRANS, // #### MUST BE LAST ####
	
	//-----------------------------------------------------------------------------------------------------------//

	// ## clears (mask clearing, all grouped together)  //
	VOXEL_CLEAR_EXPLOSION,
	VOXEL_CLEAR_TORNADO,
	VOXEL_CLEAR_SHOCKWAVE, // shared for any transparent voxels
	VOXEL_CLEAR_RAIN,

	// main transparent only clear (must be last clear)
	VOXEL_CLEAR_TRANS
);
BETTER_ENUM(eVoxelDynamicVertexBufferPartition, uint32_t const,	// ***** this has to always be same as eVoxelPipelineCustomized but + 1 excluding clears

	// main //
	PARENT_MAIN = 0,

	// ## shaders //
	VOXEL_SHADER_EXPLOSION,
	VOXEL_SHADER_TORNADO,
	VOXEL_SHADER_SHOCKWAVE,
	VOXEL_SHADER_RAIN,

	// main transparent only //
	PARENT_TRANS // #### MUST BE LAST ####
);
BETTER_ENUM(eVoxelRoadVertexBufferPartition, uint32_t const,	// ***** this has to always be same as eVoxelPipelineCustomized but + 1 excluding clears

	// main //
	PARENT_MAIN = 0,

	// main transparent only //
	PARENT_TRANS // #### MUST BE LAST ####
);
BETTER_ENUM(eComputeLightPipeline, uint32_t const,

	SEED = 0,
	JFA = 1,
	FILTER = 2
);

BETTER_ENUM(eUpsamplePipeline, uint32_t const,

	RESOLVE = 0,
	UPSAMPLE,
	BLEND
);

#ifdef DEBUG_LIGHT_PROPAGATION
BETTER_ENUM(eComputeDebugLightPipeline, uint32_t const,

	MINMAX = 0,
	BLIT = 1
);
#endif

class no_vtable cVulkan : no_copy
{
private:
	//private constants
	static constexpr uint32_t const NUM_CHILD_MASKS = uint32_t(eVoxelPipelineCustomized::_size() >> 1);
	
	//forward declarations
	struct sRTDATA; struct sRTDATA_CHILD;

public:
	// Common Accessors //
	vk::Device&	__restrict										getDevice() { return(_device); }
	vk::Queue const& __restrict									graphicsQueue() const { return(_window->graphicsQueue()); }
	vk::Queue const& __restrict									computeQueue(uint32_t const index = 0) const { return(_window->computeQueue(index)); }
	vk::Queue const& __restrict									transferQueue(uint32_t const index = 0) const { return(_window->transferQueue(index)); }

	vk::CommandPool const& __restrict							defaultPool() const { return(_window->commandPool(vku::eCommandPools::DEFAULT_POOL)); }
	vk::CommandPool const& __restrict							transientPool() const { return(_window->commandPool(vku::eCommandPools::TRANSIENT_POOL)); }
	vk::CommandPool const& __restrict							computePool() const { return(_window->commandPool(vku::eCommandPools::COMPUTE_POOL)); }
	vk::CommandPool const& __restrict							dmaTransferPool(vku::eCommandPools const dma_transfer_pool_id) const { return(_window->commandPool(dma_transfer_pool_id)); }

	vk::ImageView const&										offscreenImageView2D() const { return(_window->offscreenImageView()); }
	vk::ImageView const&										offscreenImageView2DArray() const { return(*_offscreenImageView2DArray); } // for compatbility with Nuklear GUI shader pipeline

	microseconds const&                                         frameTimeAverage() const { return(_frameTimingAverage); }
	vku::DescriptorSetUpdater& __restrict						getDescriptorSetUpdater() { return(_dsu); }
	vk::DescriptorPool const									getDescriptorPool() const { return(_fw.descriptorPool()); }
	uint32_t const												getGraphicsQueueIndex() const { return(_fw.graphicsQueueFamilyIndex()); }
	uint32_t const												getComputeQueueIndex() const { return(_fw.computeQueueFamilyIndex()); }
	uint32_t const												getTransferQueueIndex() const { return(_fw.transferQueueFamilyIndex()); }
	
	bool const													isFullScreenExclusiveExtensionSupported() const;
	bool const													isRenderingEnabled() const { return(_bRenderingEnabled); }
	
	// Common Samplers //
	
	template <uint32_t const samplerType = eSamplerAddressing::CLAMP>
	constexpr vk::Sampler const& __restrict getNearestSampler() const { return(*_sampNearest[samplerType]); }
	template <uint32_t const samplerType = eSamplerAddressing::CLAMP>
	constexpr vk::Sampler const& __restrict getLinearSampler() const { return(*_sampLinear[samplerType]); }
	template <uint32_t const samplerType = eSamplerAddressing::CLAMP>
	constexpr vk::Sampler const& __restrict getAnisotropicSampler() const { return(*_sampAnisotropic[samplerType]); }

	template<uint32_t const addressing>
	constexpr vk::Sampler const& __restrict getSampler(uint32_t const sampling) const {
		constexpr uint32_t const
			NEAREST = eSamplerSampling::NEAREST,
			LINEAR = eSamplerSampling::LINEAR,
			ANISOTROPIC = eSamplerSampling::ANISOTROPIC;

		if (ANISOTROPIC == sampling) {
			return(*_sampAnisotropic[addressing]);
		}
		else if (NEAREST == sampling) {
			return(*_sampNearest[addressing]);
		}
		//else if constexpr (LINEAR == sampling) {		// default to linear
		return(*_sampLinear[addressing]);
		//}
	}

	template<uint32_t const Addressing, typename... ArgsSampling>
	constexpr vk::Sampler const* const getSamplerArray(ArgsSampling &&... samplings)
	{
		static vk::Sampler const returned[]{ (getSampler<Addressing>(samplings)) ... };
		return(returned);
	}

	// Dynamic Voxels Partition Info Array //
	__inline vku::VertexBufferPartition* const __restrict& __restrict    getDynamicPartitionInfo() const;
	// Road        ""   ""   "" //
	__inline vku::VertexBufferPartition* const __restrict& __restrict    getRoadPartitionInfo() const;

	// Main Methods //
	void setVsyncDisabled(bool const bDisabled);
	void setFullScreenExclusiveEnabled(bool const bEnabled);
	bool const LoadVulkanFramework();
	bool const LoadVulkanWindow(struct GLFWwindow* const glfwwindow);
	void CreateResources();
	void UpdateDescriptorSetsAndStaticCommandBuffer();

	void WaitDeviceIdle();
	void Cleanup(struct GLFWwindow* const glfwwindow);

	// Callbacks //
	void OnRestored(struct GLFWwindow* const glfwwindow);
	void OnLost(struct GLFWwindow* const glfwwindow);

	// Specific Rendering //
	void checkStaticCommandsDirty();
	void enableOffscreenRendering(bool const bEnable);	// *only in main thread* is this a safe operation
	void enableOffscreenCopy(); // *only in main thread* is this a safe operation
	std::atomic_flag& getOffscreenCopyStatus() { return(_OffscreenCopied); } // when return value (flag) is cleared offscreen copy is available - to be used asynchronously in a seperate thread only!
																			 // enableOffscreenCopy needs to be called first, otherwise return value (flag) is undefined
	// offscreen image capture - should only be called at most once/frame
	void queryOffscreenBuffer(uint32_t* const __restrict mem_out) const;	// *** only safe to call after the atomic flag returned from enableOffscreenCopy is cleared ***

	// pixel perfect mouse picking - should only be called at most once/frame
	point2D_t const __vectorcall queryMouseBuffer(XMVECTOR const xmMouse) const;
	
	void Render();

	template<uint32_t const sourceData>
	__inline std::optional<DescriptorSetInfo> GetDescriptorSetInfo(uint32_t const layoutIndex = 0);

	template<typename T>
	void uploadBuffer(vku::GenericBuffer& __restrict baseBufferObject, const std::vector<T> & __restrict value) const {
		baseBufferObject.upload(_device, transientPool(), _window->graphicsQueue(), value);
	}
	template<typename T>
	void upload_BufferDeferred(vku::GenericBuffer& __restrict baseBufferObject, vk::CommandBuffer& __restrict cb, vku::GenericBuffer& __restrict stagingBuffer, T const& __restrict value, vk::DeviceSize const sizeInBytes, vk::DeviceSize const maxsizeInBytes = 0) const {
		baseBufferObject.uploadDeferred(cb, stagingBuffer, value, sizeInBytes, (0 == maxsizeInBytes ? sizeInBytes : maxsizeInBytes) );
	}
	template<typename T>
	void upload_BufferDeferred(vku::GenericBuffer& __restrict baseBufferObject, vk::CommandBuffer& __restrict cb, vku::GenericBuffer& __restrict stagingBuffer, const std::vector<T, tbb::scalable_allocator<T> > & __restrict value, size_t const maxreservecount = 0) const {
		baseBufferObject.uploadDeferred(cb, stagingBuffer, value, maxreservecount);
	}

private:
	template<bool const isDynamic = false, bool const isBasic = false, bool const isClear = false, bool const isTransparent = false>
	void CreateVoxelResource(	cVulkan::sRTDATA& rtData, vk::RenderPass const& renderPass, uint32_t const width, uint32_t const height,
								vku::ShaderModule& __restrict vert,
								vku::ShaderModule& __restrict geom,
								vku::ShaderModule& __restrict frag,
								uint32_t const subPassIndex = 0U);
	// always dynamic
	template<uint32_t const childIndex>
	void CreateVoxelChildResource(vk::RenderPass const& renderPass, uint32_t const width, uint32_t const height,
		vku::ShaderModule& __restrict vert,
		vku::ShaderModule& __restrict geom,
		vku::ShaderModule& __restrict frag,
		uint32_t const subPassIndex = 0U);
	template<uint32_t const childIndex>
	void CreateVoxelChildResource(vk::Pipeline& pipeline, vku::VertexBufferPartition const* const& partition);

	void CreateNuklearResources();

	void CreateComputeResources();
	void CreateDepthResolveResources();
	void CreateVolumetricResources();
	void CreateUpsampleResources();
	void CreatePostAAResources();

	void CreateSharedVoxelResources();
	void CreateSharedPipeline_VoxelClear();
	void CreatePipeline_VoxelClear_Static( // specialization for roads as they are static, there are no static transparents
		cVulkan::sRTDATA& rtData,
		vku::ShaderModule& __restrict vert,
		vku::ShaderModule& __restrict geom);

	void CreateVoxelResources();

	
public:
	static bool const renderCompute(vku::compute_pass&& __restrict c);
	static void renderStaticCommandBuffer(vku::static_renderpass&& __restrict s);
	static void renderDynamicCommandBuffer(vku::dynamic_renderpass&& __restrict d);
	static void renderOverlayCommandBuffer(vku::overlay_renderpass&& __restrict o);
	static void renderPresentCommandBuffer(vku::present_renderpass&& __restrict pp);
private:
    inline bool const _renderCompute(vku::compute_pass&& __restrict c);
	inline void _renderStaticCommandBuffer(vku::static_renderpass&& __restrict s);
	inline void _renderDynamicCommandBuffer(vku::dynamic_renderpass&& __restrict d);
	inline void _renderOverlayCommandBuffer(vku::overlay_renderpass&& __restrict o);
	inline void _renderPresentCommandBuffer(vku::present_renderpass&& __restrict pp);
	void renderComplete(); // triggered internally on Render Completion (after final queue submission / present by vku framework

	void renderClearMasks(vku::static_renderpass&& __restrict s, sRTDATA_CHILD const* (&__restrict deferredChildMasks)[NUM_CHILD_MASKS], uint32_t const ActiveMaskCount);
	void clearAllVoxels(vku::static_renderpass&& __restrict s);

	void copyMouseBuffer(vk::CommandBuffer& cb) const;
	void barrierMouseBuffer(vk::CommandBuffer& cb) const;

	void copyOffscreenBuffer(vk::CommandBuffer& cb) const;
	void barrierOffscreenBuffer(vk::CommandBuffer& cb) const;
private:
	vk::Device						_device;
	vku::Framework					_fw;
	vku::Window* 					_window;

	vku::GenericBuffer				_mouseBuffer;

	vk::UniqueImageView				_offscreenImageView2DArray;
	vku::GenericBuffer				_offscreenBuffer;

	vku::DescriptorSetUpdater		_dsu;
	vk::UniqueSampler				_sampNearest[eSamplerAddressing::size()],			// commonly used "repeat" samplers
									_sampLinear[eSamplerAddressing::size()],
									_sampAnisotropic[eSamplerAddressing::size()];
	microseconds					_frameTimingAverage;
	bool							_bFullScreenExclusiveAcquired = false,
									_bRenderingEnabled = false,
									_bRestoreAsPaused = false,
									_bVsyncDisabled = false,
									_bOffscreenRender = false,
									_bOffscreenCopy = false;

	std::atomic_flag				_OffscreenCopied;
//-------------------------------------------------------------------------------------------------------------------------------//
public:	// ### public skeleton
	typedef struct sPOSTAADATA
	{
		vk::UniquePipelineLayout		pipelineLayout;
		vk::Pipeline					pipeline[5];
		vk::UniqueDescriptorSetLayout	descLayout;
		std::vector<vk::DescriptorSet>	sets;

		sPOSTAADATA()
		{}

		~sPOSTAADATA()
		{
			pipelineLayout.release();
			sets.clear(); sets.shrink_to_fit();
			descLayout.release();
		}
	} sPOSTAADATA;
private: // ### private instance
	static sPOSTAADATA _aaData;

//-------------------------------------------------------------------------------------------------------------------------------//
//-------------------------------------------------------------------------------------------------------------------------------//
	//-------------------------------------------------------------------------------------------------------------------------------//
private:	// ### private skeleton
	typedef struct sDEPTHRESOLVEDATA
	{
		vk::UniquePipelineLayout		pipelineLayout;
		vk::Pipeline					pipeline;
		vk::UniqueDescriptorSetLayout	descLayout;
		std::vector<vk::DescriptorSet>	sets;

		sDEPTHRESOLVEDATA()
		{}

		~sDEPTHRESOLVEDATA()
		{
			pipelineLayout.release();
			sets.clear(); sets.shrink_to_fit();
			descLayout.release();
		}
	} sDEPTHRESOLVEDATA;
private: // ### private instance
	static sDEPTHRESOLVEDATA _depthData;

	//-------------------------------------------------------------------------------------------------------------------------------//
#ifdef DEBUG_LIGHT_PROPAGATION
public:
	typedef struct sCOMPUTEDEBUGLIGHTDATA
	{
		vk::UniquePipelineLayout		pipelineLayout;
		vk::UniquePipeline				pipeline[eComputeDebugLightPipeline::_size()];
		vk::UniqueDescriptorSetLayout	descLayout;
		std::vector<vk::DescriptorSet>	sets;

		~sCOMPUTEDEBUGLIGHTDATA()
		{
			for (uint32_t i = 0; i < eComputeDebugLightPipeline::_size(); ++i) {
				pipeline[i].release();
			}

			sets.clear(); sets.shrink_to_fit();

			pipelineLayout.release();
			descLayout.release();
		}
	} sCOMPUTEDEBUGLIGHTDATA;
private:
#endif
public: // ### public skeleton
	typedef struct sCOMPUTEDATA
	{
		vk::UniquePipelineLayout		pipelineLayout;
		vk::UniquePipeline				pipeline[eComputeLightPipeline::_size()];
		vk::UniqueDescriptorSetLayout	descLayout;
		std::vector<vk::DescriptorSet>	sets;

#ifdef DEBUG_LIGHT_PROPAGATION
		sCOMPUTEDEBUGLIGHTDATA			debugLightData;
#endif

		sCOMPUTEDATA()
		{}
		~sCOMPUTEDATA()
		{
#ifdef DEBUG_LIGHT_PROPAGATION
			debugLightData.~sCOMPUTEDEBUGLIGHTDATA();
#endif
			for (uint32_t i = 0; i < eComputeLightPipeline::_size(); ++i) {
				pipeline[i].release();
			}
			
			sets.clear(); sets.shrink_to_fit();	

			pipelineLayout.release();
			descLayout.release();
		}
	} sCOMPUTEDATA;
private: // ### private instance
	static sCOMPUTEDATA _comData;

//-------------------------------------------------------------------------------------------------------------------------------//
private: // ### private skeleton, private instance
	static struct sNUKLEARDATA
	{
		vku::UniformBuffer				_ubo;
		vku::DynamicVertexBuffer		_vbo;
		vku::DynamicIndexBuffer			_ibo;

		vk::UniquePipelineLayout		pipelineLayout;
		vk::Pipeline					pipeline;
		vk::UniqueDescriptorSetLayout	descLayout;
		std::vector<vk::DescriptorSet>	sets;

		~sNUKLEARDATA()
		{
			_ubo.release();
			_vbo.release();
			_ibo.release();

			pipelineLayout.release();
			sets.clear(); sets.shrink_to_fit();
			descLayout.release();
		}
	} _nkData;
	//-------------------------------------------------------------------------------------------------------------------------------//
	static struct sRTSHARED_DATA
	{
		vku::UniformBuffer				_ubo;
		vk::UniqueDescriptorSetLayout	descLayout[eVoxelDescSharedLayout::_size()];

		~sRTSHARED_DATA()
		{
			_ubo.release();
			for (uint32_t iDx = 0; iDx < eVoxelDescSharedLayout::_size(); ++iDx) {
				descLayout[iDx].release();
			}
		}
	} _rtSharedData; // all "shared" voxel desc layouts and other shared data contained
	static struct sRTSHARED_DESCSET
	{
		vk::UniqueDescriptorSetLayout const&	layout;
		vk::UniquePipelineLayout				pipelineLayout;
		std::vector<vk::DescriptorSet>			sets;

		sRTSHARED_DESCSET(eVoxelDescSharedLayout const layout)
			: layout(_rtSharedData.descLayout[layout])
		{}
		~sRTSHARED_DESCSET()
		{
			pipelineLayout.release();
			sets.clear(); sets.shrink_to_fit();
		}
	} _rtSharedDescSet[eVoxelDescSharedLayoutSet::_size()]; // array of shared layout and set, also allows struct to be used by vox sRTData

	static vk::Pipeline _rtSharedPipeline[eVoxelSharedPipeline::_size()];  // array of shared pipelines, these are not automatically referenced

	// vector containg all vbo's
	using VertexBufferPool = tbb::concurrent_vector< vku::DynamicVertexBuffer* >;
	static VertexBufferPool _vbos;
	//-------------------------------------------------------------------------------------------------------------------------------//
	static struct sRTDATA
	{
		VertexBufferPool::iterator const		_vbo;
		vk::Pipeline							pipeline;

		sRTDATA()
			: _vbo(_vbos.emplace_back(nullptr)) // pointer to available position in vector
		{}
		~sRTDATA() = default;
	} _rtData[eVoxelPipeline::_size()];
	// ****** //
	static struct sRTDATA_CHILD
	{
		sRTDATA const&							parent;
		vk::Pipeline							pipeline;
		vku::VertexBufferPartition const*		vbo_partition_info;
		bool const								transparency,
												mask;

		sRTDATA_CHILD(sRTDATA const& parent_, bool const transparency_, bool const mask_)
			: parent(parent_), vbo_partition_info(nullptr), transparency(transparency_), mask(mask_)
		{}
		~sRTDATA_CHILD() = default;
	} _rtDataChild[eVoxelPipelineCustomized::_size()];
	//-------------------------------------------------------------------------------------------------------------------------------//
	static struct sVOLUMETRICLIGHTDATA
	{
		vku::UniformBuffer const&		_ubo;
		vku::VertexBuffer				_vbo;
		vku::IndexBuffer				_ibo;
		uint32_t						index_count;

		vk::UniquePipelineLayout		pipelineLayout;
		vk::Pipeline					pipeline;
		vk::UniqueDescriptorSetLayout	descLayout;
		std::vector<vk::DescriptorSet>	sets;

		sVOLUMETRICLIGHTDATA(vku::UniformBuffer const& commonshared_ubo)
			: _ubo(commonshared_ubo), index_count(0)
		{}

		~sVOLUMETRICLIGHTDATA()
		{
			// _ubo is just a reference
			_vbo.release();
			_ibo.release();

			pipelineLayout.release();
			sets.clear(); sets.shrink_to_fit();
			descLayout.release();
		}
	} _volData;
	//-------------------------------------------------------------------------------------------------------------------------------//
	static struct sUPSAMPLEDATA
	{
		vk::UniquePipelineLayout		pipelineLayout;
		vk::Pipeline					pipeline;
		vk::UniqueDescriptorSetLayout	descLayout;
		std::vector<vk::DescriptorSet>	sets;

		sUPSAMPLEDATA()
		{}

		~sUPSAMPLEDATA()
		{
			pipelineLayout.release();
			sets.clear(); sets.shrink_to_fit();
			descLayout.release();
		}
	} _upData[eUpsamplePipeline::_size()];
	//-------------------------------------------------------------------------------------------------------------------------------//
	
	//-------------------------------------------------------------------------------------------------------------------------------//

public:
	cVulkan();
	~cVulkan();

private:
	template<uint32_t const descriptor_set>
	STATIC_INLINE void bindVoxelDescriptorSet(vk::CommandBuffer& __restrict cb);

	template<uint32_t const voxel_pipeline_index, uint32_t const numChildMasks = 0, bool const front_to_back = true>
	STATIC_INLINE uint32_t const renderAllVoxels(vku::static_renderpass const& s, sRTDATA_CHILD const* __restrict* const __restrict& __restrict deferredChildMasks = nullptr);

	static void renderOffscreenVoxels(vku::static_renderpass const& s);
};

template<uint32_t const sourceData>
__inline std::optional<DescriptorSetInfo> cVulkan::GetDescriptorSetInfo(uint32_t const layoutIndex)
{
	if constexpr( eSourceData::NUKLEAR_DATA == sourceData ) {
		return(std::optional<DescriptorSetInfo>(DescriptorSetInfo(_nkData.descLayout, _nkData.sets, &_nkData._ubo)));
	}

#ifndef NDEBUG
	static_assert("Incorrect usuage of cVulkan::GetDescriptorSetInfo, source data does not exist");
#endif
	return(std::nullopt);
}

template<bool const isDynamic, bool const isBasic, bool const isClear, bool const isTransparent>
void cVulkan::CreateVoxelResource(
	cVulkan::sRTDATA& rtData, vk::RenderPass const& renderPass, uint32_t const width, uint32_t const height,
	vku::ShaderModule& __restrict vert,
	vku::ShaderModule& __restrict geom,
	vku::ShaderModule& __restrict frag,
	uint32_t const subPassIndex)
{

	// Make a pipeline to use the vertex format and shaders.
	vku::PipelineMaker pm(width, height);
	pm.topology(vk::PrimitiveTopology::ePointList);
	pm.shader(vk::ShaderStageFlagBits::eVertex, vert);

	if constexpr (isTransparent) { // special transparency (simple) for roads in overlay pass, or basic transparents used in other scenarios
		
		pm.shader(vk::ShaderStageFlagBits::eGeometry, geom);
		pm.shader(vk::ShaderStageFlagBits::eFragment, frag);

		pm.blendBegin(VK_TRUE);
		pm.blendSrcColorBlendFactor(vk::BlendFactor::eOne); // this is pre-multiplied alpha
		pm.blendDstColorBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha);
		pm.blendColorBlendOp(vk::BlendOp::eAdd);
		pm.blendSrcAlphaBlendFactor(vk::BlendFactor::eOne);
		pm.blendDstAlphaBlendFactor(vk::BlendFactor::eZero);
		pm.blendAlphaBlendOp(vk::BlendOp::eAdd);

		typedef vk::ColorComponentFlagBits ccbf;
		pm.blendColorWriteMask(ccbf::eR | ccbf::eG | ccbf::eB | ccbf::eA); // ***** note does not preserve clear mask! intended to be used in overlay pass
	}
	else {
		if constexpr (!isClear) {
			pm.shader(vk::ShaderStageFlagBits::eGeometry, geom);
		}
		if constexpr (!isBasic) {
			pm.shader(vk::ShaderStageFlagBits::eFragment, frag);
			typedef vk::ColorComponentFlagBits ccbf;
			pm.blendBegin(VK_FALSE);
			pm.blendColorWriteMask((vk::ColorComponentFlagBits)ccbf::eR | ccbf::eG | ccbf::eB); // no alpha writes to preserve "clear masks"

		}
		else {  // basic / zpass only
			
			if (!frag.ok()) { // ie frag == null, fragment shader not used
				// just Z no fragment shader required if basic
				pm.blendBegin(VK_FALSE);
				pm.blendColorWriteMask((vk::ColorComponentFlagBits)0); // no color writes for "basic" (first color attachment)
				pm.blendBegin(VK_FALSE);
				pm.blendColorWriteMask((vk::ColorComponentFlagBits)0); // no color writes for "basic" (second color attachment)
			}
			else {
				// outputs Z and a color to a second color attachment
				pm.shader(vk::ShaderStageFlagBits::eFragment, frag);
				pm.blendBegin(VK_FALSE);
				pm.blendColorWriteMask((vk::ColorComponentFlagBits)0); // no color writes for "basic" (first color attachment)
				typedef vk::ColorComponentFlagBits ccbf;
				pm.blendBegin(VK_FALSE);
				pm.blendColorWriteMask((vk::ColorComponentFlagBits)ccbf::eR | ccbf::eG | ccbf::eB | ccbf::eA); // full writes to second color attachment
			}

		}
	}
	if constexpr (isDynamic) {
		pm.vertexBinding(0, (uint32_t)sizeof(VertexDecl::VoxelDynamic));

		pm.vertexAttribute(0, 0, vk::Format::eR32G32B32A32Sfloat, (uint32_t)offsetof(VertexDecl::VoxelDynamic, worldPos));
		pm.vertexAttribute(1, 0, vk::Format::eR32G32B32A32Sfloat, (uint32_t)offsetof(VertexDecl::VoxelDynamic, uv_vr));
		pm.vertexAttribute(2, 0, vk::Format::eR32G32B32A32Sfloat, (uint32_t)offsetof(VertexDecl::VoxelDynamic, orient_reserved));
	}
	else {
		pm.vertexBinding(0, (uint32_t)sizeof(VertexDecl::VoxelNormal));

		pm.vertexAttribute(0, 0, vk::Format::eR32G32B32A32Sfloat, (uint32_t)offsetof(VertexDecl::VoxelNormal, worldPos));
		pm.vertexAttribute(1, 0, vk::Format::eR32G32B32A32Sfloat, (uint32_t)offsetof(VertexDecl::VoxelNormal, uv_vr));
	}
	
	pm.depthCompareOp(vk::CompareOp::eLessOrEqual);
	pm.depthClampEnable(VK_FALSE); // must be false

	if constexpr (isClear) {
		pm.depthCompareOp(vk::CompareOp::eNever);
		pm.depthTestEnable(VK_FALSE);
		pm.depthWriteEnable(VK_FALSE);
		pm.rasterizerDiscardEnable(VK_TRUE);
	}
	else if constexpr (isBasic) {
		pm.depthTestEnable(VK_TRUE);
		pm.depthWriteEnable(VK_TRUE);
	}
	else {
		pm.depthTestEnable(VK_TRUE);
		pm.depthWriteEnable(VK_FALSE);
	}
	pm.cullMode(vk::CullModeFlagBits::eBack);
	pm.frontFace(vk::FrontFace::eClockwise);

	pm.subPass(subPassIndex);
	pm.rasterizationSamples(vku::DefaultSampleCount);

	// Create a pipeline using a renderPass built for our window.
	auto& cache = _fw.pipelineCache();
	if constexpr ((isClear | isBasic)) {
		rtData.pipeline = pm.create(_device, cache, *cVulkan::_rtSharedDescSet[eVoxelDescSharedLayoutSet::VOXEL_CLEAR].pipelineLayout, renderPass);
	}
	else {
		rtData.pipeline = pm.create(_device, cache, *cVulkan::_rtSharedDescSet[eVoxelDescSharedLayoutSet::VOXEL_COMMON].pipelineLayout, renderPass);
	}
}

// always dynamic,  - overload for creating unique pipeline
template<uint32_t const childIndex>
void cVulkan::CreateVoxelChildResource(
	vk::RenderPass const& renderPass, uint32_t const width, uint32_t const height,
	vku::ShaderModule& __restrict vert,
	vku::ShaderModule& __restrict geom,
	vku::ShaderModule& __restrict frag,
	uint32_t const subPassIndex)
{
	// only specializes pipeline, everything else is owned by parent sRTDATA
	cVulkan::sRTDATA_CHILD& rtData(_rtDataChild[childIndex]);
	// initialize const pointer to the correct vertex buffer partition info, partition memory is allocated before any calols to this function
	rtData.vbo_partition_info = &(*rtData.parent._vbo)->partitions()[childIndex + 1];

	// Make a pipeline to use the vertex format and shaders.
	vku::PipelineMaker pm(width, height);
	pm.topology(vk::PrimitiveTopology::ePointList);
	pm.shader(vk::ShaderStageFlagBits::eVertex, vert);
	pm.shader(vk::ShaderStageFlagBits::eGeometry, geom);
	pm.shader(vk::ShaderStageFlagBits::eFragment, frag);

	// dynamic voxels only
	pm.vertexBinding(0, (uint32_t)sizeof(VertexDecl::VoxelDynamic));

	pm.vertexAttribute(0, 0, vk::Format::eR32G32B32A32Sfloat, (uint32_t)offsetof(VertexDecl::VoxelDynamic, worldPos));
	pm.vertexAttribute(1, 0, vk::Format::eR32G32B32A32Sfloat, (uint32_t)offsetof(VertexDecl::VoxelDynamic, uv_vr));
	pm.vertexAttribute(2, 0, vk::Format::eR32G32B32A32Sfloat, (uint32_t)offsetof(VertexDecl::VoxelDynamic, orient_reserved));

	pm.cullMode(vk::CullModeFlagBits::eBack);
	pm.frontFace(vk::FrontFace::eClockwise);

	pm.depthCompareOp(vk::CompareOp::eLessOrEqual);
	pm.depthClampEnable(VK_FALSE); // must be false
	pm.depthTestEnable(VK_TRUE);
	pm.depthWriteEnable(VK_FALSE); // *** bugfix: required for proper sorting of radial grid whether its opaque or transparent!

	if (rtData.transparency) { // Uses a "voxel clear alpha mask" in first main renderpass
							   // for improved refraction, see https://developer.nvidia.com/gpugems/GPUGems2/gpugems2_chapter19.html

		pm.blendBegin(VK_TRUE);
		pm.blendSrcColorBlendFactor(vk::BlendFactor::eOne); // this is pre-multiplied alpha
		pm.blendDstColorBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha);
		pm.blendColorBlendOp(vk::BlendOp::eAdd);
		pm.blendSrcAlphaBlendFactor(vk::BlendFactor::eOne);
		pm.blendDstAlphaBlendFactor(vk::BlendFactor::eZero);
		pm.blendAlphaBlendOp(vk::BlendOp::eAdd);

		typedef vk::ColorComponentFlagBits ccbf;
		pm.blendColorWriteMask(ccbf::eR | ccbf::eG | ccbf::eB | ccbf::eA);
	}

	// Create a pipeline using a renderPass built for our window.
	// transparent dynamic voxels in overlay renderpass, subpass 0
	// transparent "mask" dynamic voxels in main renderpass, subpass 2
	pm.subPass(subPassIndex);
	pm.rasterizationSamples(vku::DefaultSampleCount);

	auto& cache = _fw.pipelineCache();
	rtData.pipeline = pm.create(_device, cache, *cVulkan::_rtSharedDescSet[eVoxelDescSharedLayoutSet::VOXEL_COMMON].pipelineLayout, renderPass);
} 
// always dynamic - overload for referencing a shared pipeline, and a reference to an existing partition
template<uint32_t const childIndex>
void cVulkan::CreateVoxelChildResource(vk::Pipeline& pipeline, vku::VertexBufferPartition const* const& partition)
{
	// only specializes pipeline, everything else is owned by parent sRTDATA
	cVulkan::sRTDATA_CHILD& rtData(_rtDataChild[childIndex]);

	rtData.pipeline = pipeline;  // reference
	rtData.vbo_partition_info = partition;
}

__inline vku::VertexBufferPartition* const __restrict& __restrict cVulkan::getDynamicPartitionInfo() const {
	return((*_rtData[eVoxelPipeline::VOXEL_DYNAMIC]._vbo)->partitions());
}
__inline vku::VertexBufferPartition* const __restrict& __restrict cVulkan::getRoadPartitionInfo() const {
	return((*_rtData[eVoxelPipeline::VOXEL_ROAD]._vbo)->partitions());
}

template<uint32_t const descriptor_set>
STATIC_INLINE void cVulkan::bindVoxelDescriptorSet(vk::CommandBuffer& __restrict cb)
{
	cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *_rtSharedDescSet[descriptor_set].pipelineLayout, 0,
		_rtSharedDescSet[descriptor_set].sets[0], nullptr);
}

template<uint32_t const voxel_pipeline_index, uint32_t const numChildMasks, bool const front_to_back>
STATIC_INLINE uint32_t const cVulkan::renderAllVoxels(vku::static_renderpass const& s, [[maybe_unused]] sRTDATA_CHILD const* __restrict* const __restrict& __restrict deferredChildMasks)
{
	// ***** descriptor set must be set outside of this function ***** //

	[[maybe_unused]]
	uint32_t ActiveMaskCount(0);

	if constexpr (0U != numChildMasks) {
		ActiveMaskCount = 0U;
		for (uint32_t i = 0U; i < numChildMasks; ++i)
			deferredChildMasks[i] = nullptr;
	}

	if constexpr (front_to_back)  // optimal order for z culling (default)
	{
		{ // dynamic voxels

			constexpr uint32_t const voxelPipeline = eVoxelPipeline::VOXEL_DYNAMIC_BASIC + voxel_pipeline_index;

			uint32_t const ActiveVertexCount = (*_rtData[voxelPipeline]._vbo)->ActiveVertexCount<VertexDecl::VoxelDynamic>();
			if (0 != ActiveVertexCount) {

				s.cb.bindVertexBuffers(0, (*_rtData[voxelPipeline]._vbo)->buffer(), vk::DeviceSize(0));

				// main partition (always starts at vertex positiob 0)
				{
					uint32_t const partition_vertex_count = (*_rtData[voxelPipeline]._vbo)->partitions()[eVoxelDynamicVertexBufferPartition::PARENT_MAIN].active_vertex_count;
					if (0 != partition_vertex_count) {
						s.cb.bindPipeline(vk::PipelineBindPoint::eGraphics, _rtData[voxelPipeline].pipeline);
						s.cb.draw(partition_vertex_count, 1, 0, 0);
					}
				}

				// draw children dynamic shader voxels (customized) (opaque voxel shaders only)
				// leveraging existing vb already bound

				for (uint32_t child = 0; child < eVoxelPipelineCustomized::_size(); ++child) {

					sRTDATA_CHILD const& __restrict rtDataChild(_rtDataChild[child]);
					vku::VertexBufferPartition const* const __restrict child_partition = rtDataChild.vbo_partition_info;
					if (nullptr != child_partition && !rtDataChild.transparency) {

						if (!rtDataChild.mask) {
							uint32_t const partition_vertex_count = child_partition->active_vertex_count;
							if (0 != partition_vertex_count) {

								s.cb.bindPipeline(vk::PipelineBindPoint::eGraphics, rtDataChild.pipeline);

								uint32_t const partition_start_vertex = child_partition->vertex_start_offset;
								s.cb.draw(partition_vertex_count, 1, partition_start_vertex, 0);
							}
						}
						else if constexpr (0U != numChildMasks) {
							deferredChildMasks[ActiveMaskCount++] = &rtDataChild;  // clear masks are draw later....
						}
					}
				}
			}
		}

		{ // static voxels
			constexpr uint32_t const voxelPipeline = eVoxelPipeline::VOXEL_STATIC_BASIC + voxel_pipeline_index;

			uint32_t const ActiveVertexCount = (*_rtData[voxelPipeline]._vbo)->ActiveVertexCount<VertexDecl::VoxelNormal>();
			if (0 != ActiveVertexCount) {
				s.cb.bindPipeline(vk::PipelineBindPoint::eGraphics, _rtData[voxelPipeline].pipeline);

				s.cb.bindVertexBuffers(0, (*_rtData[voxelPipeline]._vbo)->buffer(), vk::DeviceSize(0));
				s.cb.draw(ActiveVertexCount, 1, 0, 0);
			}
		}

		{ // roads
			constexpr uint32_t const voxelPipeline = eVoxelPipeline::VOXEL_ROAD_BASIC + voxel_pipeline_index;

			uint32_t const ActiveVertexCount = (*_rtData[voxelPipeline]._vbo)->ActiveVertexCount<VertexDecl::VoxelNormal>();
			if (0 != ActiveVertexCount) {

				s.cb.bindVertexBuffers(0, (*_rtData[voxelPipeline]._vbo)->buffer(), vk::DeviceSize(0));

				// main partition (always starts at vertex positiob 0)
				{
					uint32_t const partition_vertex_count = (*_rtData[voxelPipeline]._vbo)->partitions()[eVoxelRoadVertexBufferPartition::PARENT_MAIN].active_vertex_count;
					if (0 != partition_vertex_count) {
						s.cb.bindPipeline(vk::PipelineBindPoint::eGraphics, _rtData[voxelPipeline].pipeline);
						s.cb.draw(partition_vertex_count, 1, 0, 0);
					}
				}
			}
		}

		{ // terrain
			constexpr uint32_t const voxelPipeline = eVoxelPipeline::VOXEL_TERRAIN_BASIC + voxel_pipeline_index;

			uint32_t const ActiveVertexCount = (*_rtData[voxelPipeline]._vbo)->ActiveVertexCount<VertexDecl::VoxelNormal>();
			if (0 != ActiveVertexCount) {
				s.cb.bindPipeline(vk::PipelineBindPoint::eGraphics, _rtData[voxelPipeline].pipeline);

				s.cb.bindVertexBuffers(0, (*_rtData[voxelPipeline]._vbo)->buffer(), vk::DeviceSize(0));
				s.cb.draw(ActiveVertexCount, 1, 0, 0);
			}
		}

	}
	else { // !front_to_back = back_to_front is needed for the mouse color atttachment to be rendered in correct order (only on initial z pass)

		{ // terrain
			constexpr uint32_t const voxelPipeline = eVoxelPipeline::VOXEL_TERRAIN_BASIC + voxel_pipeline_index;

			uint32_t const ActiveVertexCount = (*_rtData[voxelPipeline]._vbo)->ActiveVertexCount<VertexDecl::VoxelNormal>();
			if (0 != ActiveVertexCount) {
				s.cb.bindPipeline(vk::PipelineBindPoint::eGraphics, _rtData[voxelPipeline].pipeline);

				s.cb.bindVertexBuffers(0, (*_rtData[voxelPipeline]._vbo)->buffer(), vk::DeviceSize(0));
				s.cb.draw(ActiveVertexCount, 1, 0, 0);
			}
		}

		{ // roads
			constexpr uint32_t const voxelPipeline = eVoxelPipeline::VOXEL_ROAD_BASIC + voxel_pipeline_index;

			uint32_t const ActiveVertexCount = (*_rtData[voxelPipeline]._vbo)->ActiveVertexCount<VertexDecl::VoxelNormal>();
			if (0 != ActiveVertexCount) {

				s.cb.bindVertexBuffers(0, (*_rtData[voxelPipeline]._vbo)->buffer(), vk::DeviceSize(0));

				// main partition (always starts at vertex positiob 0)
				{
					uint32_t const partition_vertex_count = (*_rtData[voxelPipeline]._vbo)->partitions()[eVoxelRoadVertexBufferPartition::PARENT_MAIN].active_vertex_count;
					if (0 != partition_vertex_count) {
						s.cb.bindPipeline(vk::PipelineBindPoint::eGraphics, _rtData[voxelPipeline].pipeline);
						s.cb.draw(partition_vertex_count, 1, 0, 0);
					}
				}
			}
		}

		{ // static voxels
			constexpr uint32_t const voxelPipeline = eVoxelPipeline::VOXEL_STATIC_BASIC + voxel_pipeline_index;

			uint32_t const ActiveVertexCount = (*_rtData[voxelPipeline]._vbo)->ActiveVertexCount<VertexDecl::VoxelNormal>();
			if (0 != ActiveVertexCount) {
				s.cb.bindPipeline(vk::PipelineBindPoint::eGraphics, _rtData[voxelPipeline].pipeline);

				s.cb.bindVertexBuffers(0, (*_rtData[voxelPipeline]._vbo)->buffer(), vk::DeviceSize(0));
				s.cb.draw(ActiveVertexCount, 1, 0, 0);
			}
		}

		{ // dynamic voxels

			constexpr uint32_t const voxelPipeline = eVoxelPipeline::VOXEL_DYNAMIC_BASIC + voxel_pipeline_index;

			uint32_t const ActiveVertexCount = (*_rtData[voxelPipeline]._vbo)->ActiveVertexCount<VertexDecl::VoxelDynamic>();
			if (0 != ActiveVertexCount) {

				s.cb.bindVertexBuffers(0, (*_rtData[voxelPipeline]._vbo)->buffer(), vk::DeviceSize(0));

				// main partition (always starts at vertex positiob 0)
				{
					uint32_t const partition_vertex_count = (*_rtData[voxelPipeline]._vbo)->partitions()[eVoxelDynamicVertexBufferPartition::PARENT_MAIN].active_vertex_count;
					if (0 != partition_vertex_count) {
						s.cb.bindPipeline(vk::PipelineBindPoint::eGraphics, _rtData[voxelPipeline].pipeline);
						s.cb.draw(partition_vertex_count, 1, 0, 0);
					}
				}

				// draw children dynamic shader voxels (customized) (opaque voxel shaders only)
				// leveraging existing vb already bound

				for (uint32_t child = 0; child < eVoxelPipelineCustomized::_size(); ++child) {

					sRTDATA_CHILD const& __restrict rtDataChild(_rtDataChild[child]);
					vku::VertexBufferPartition const* const __restrict child_partition = rtDataChild.vbo_partition_info;
					if (nullptr != child_partition && !rtDataChild.transparency) {

						if (!rtDataChild.mask) {
							uint32_t const partition_vertex_count = child_partition->active_vertex_count;
							if (0 != partition_vertex_count) {

								s.cb.bindPipeline(vk::PipelineBindPoint::eGraphics, rtDataChild.pipeline);

								uint32_t const partition_start_vertex = child_partition->vertex_start_offset;
								s.cb.draw(partition_vertex_count, 1, partition_start_vertex, 0);
							}
						}
						else if constexpr (0U != numChildMasks) {
							deferredChildMasks[ActiveMaskCount++] = &rtDataChild;  // clear masks are draw later....
						}
					}
				}
			}
		}
	}

	if constexpr (0U != numChildMasks) {
		return(ActiveMaskCount);
	}
	else {
		return(0U);
	}
}






