#pragma once
// this is the only area of program that should include these files *****
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vku/volk/volk.h>

#define VKU_SURFACE "VK_KHR_win32_surface"

#include <vku/vku_framework.hpp>	// this is the one and only place vku_framework.hpp can be included, use cVulkan->h (prefer)

// ^^^^^^^ this is the only area of program that should include these files *****
#include <Utility/class_helper.h>
#include <vku/vku_doublebuffer.h>
#include "Declarations.h"
#include "RenderInfo.h"
#include "tTime.h"
#include <Math/superfastmath.h>
#include <Math/point2D_t.h>
#include "betterenums.h"
#include <optional>

#define BACK_TO_FRONT 0  // Set to 1 for "special mouse behind models" feature at the cost of large amounts of overdraw. Only affects the first z-only//mouse renderpass.
                         // 0 (default) - all rendering in the renderpass is drawn (roughly) front to back, which has the benefit of using the z-buffer as geometry is rendered. Only affects the first z-only/mouse renderpass.

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
		CLAMP = 0,  // also known as: clamp to edge
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

BETTER_ENUM(eVoxelDescSharedLayout, uint32_t const,

	VOXEL_COMMON = 0,
	VOXEL_CLEAR,
	VOXEL_ZONLY
);
BETTER_ENUM(eVoxelDescSharedLayoutSet, uint32_t const,

	VOXEL_COMMON = 0,
	VOXEL_CLEAR,
	VOXEL_ZONLY
);
BETTER_ENUM(eVoxelSharedPipeline, uint32_t const,

	VOXEL_CLEAR = 0,
	VOXEL_CLEAR_MOUSE = 1
);
BETTER_ENUM(eVoxelVertexBuffer, uint32_t const,

	VOXEL_TERRAIN = 0,
	VOXEL_STATIC,
	VOXEL_DYNAMIC
);
BETTER_ENUM(eVoxelPipeline, int32_t const,

	VOXEL_TERRAIN_BASIC_ZONLY = 0,
	VOXEL_TERRAIN_BASIC,
	VOXEL_TERRAIN,
	VOXEL_TERRAIN_BASIC_CLEAR,

	VOXEL_STATIC_BASIC_ZONLY,
	VOXEL_STATIC_BASIC,
	VOXEL_STATIC,
	VOXEL_STATIC_BASIC_CLEAR,
	VOXEL_STATIC_OFFSCREEN,

	VOXEL_DYNAMIC_BASIC_ZONLY,
	VOXEL_DYNAMIC_BASIC,
	VOXEL_DYNAMIC,
	VOXEL_DYNAMIC_BASIC_CLEAR,
	VOXEL_DYNAMIC_OFFSCREEN
);
BETTER_ENUM(eVoxelPipelineCustomized, uint32_t const,  // ***** shaders first, then clears (clears must be last)

	// ## shaders //
	VOXEL_SHADER_RAIN = 0,

	// main transparent only (must be last shader) //
	VOXEL_SHADER_TRANS, // #### MUST BE LAST ####
	
	//-----------------------------------------------------------------------------------------------------------//

	// ## clears (mask clearing, all grouped together)  //
	VOXEL_CLEAR_RAIN,

	// main transparent only clear (must be last clear)
	VOXEL_CLEAR_TRANS
);
BETTER_ENUM(eVoxelDynamicVertexBufferPartition, uint32_t const,	// ***** this has to always be same as eVoxelPipelineCustomized but + 1 excluding clears

	// main //
	PARENT_MAIN = 0,

	// ## shaders //
	VOXEL_SHADER_RAIN,

	// main transparent only //
	PARENT_TRANS // #### MUST BE LAST ####
);
BETTER_ENUM(eComputeLightPipeline, uint32_t const,

	SEED = 0,
	JFA = 1,
	FILTER = 2
);
BETTER_ENUM(eTextureShader, uint32_t const,	// compute shaders that work on a 2D texture, like shadertoy fullscreen quad pixel shaders - except with compute.

	WIND_FBM = 0,
	WIND_DIRECTION
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
	vk::Device const&	__restrict								getDevice() const { return(_device); }
	vk::Queue const& __restrict									graphicsQueue() const { return(_window->graphicsQueue()); }
	vk::Queue const& __restrict									computeQueue() const { return(_window->computeQueue()); }
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
	bool const													isFullScreenExclusive() const;
	bool const													isHDR() const;
	uint32_t const												getMaximumNits() const;
	bool const													isRenderingEnabled() const { return(_bRenderingEnabled); }
	

	void setPresentationBlendWeight(uint32_t const imageIndex);
	
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
	__inline vku::VertexBufferPartition* const __restrict& __restrict    getDynamicPartitionInfo(uint32_t const resource_index) const;

	// Main Methods //
	void setFullScreenExclusiveEnabled(bool const bEnabled);
	void setHDREnabled(bool const bEnabled, uint32_t const max_nits = 0);
	void ForceVsync();

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
	void setStaticCommandsDirty();
	void checkStaticCommandsDirty(uint32_t const resource_index);
	void enableOffscreenRendering(bool const bEnable);	// *only in main thread* is this a safe operation
	void enableOffscreenCopy(); // *only in main thread* is this a safe operation
	std::atomic_flag& getOffscreenCopyStatus() { return(_OffscreenCopied); } // when return value (flag) is cleared offscreen copy is available - to be used asynchronously in a seperate thread only!
																			 // enableOffscreenCopy needs to be called first, otherwise return value (flag) is undefined
	// offscreen image capture - should only be called at most once/frame
	NO_INLINE void queryOffscreenBuffer(uint32_t* const __restrict mem_out) const;	// *** only safe to call after the atomic flag returned from enableOffscreenCopy is cleared ***

	// pixel perfect mouse picking - should only be called at most once/frame
	NO_INLINE point2D_t const __vectorcall queryMouseBuffer(XMVECTOR const xmMouse, uint32_t const resource_index);
	
	void Render();

	template<typename T>
	void uploadBuffer(vku::GenericBuffer& __restrict baseBufferObject, const std::vector<T> & __restrict value) const {
		baseBufferObject.upload(_device, transientPool(), _window->graphicsQueue(), value);
	}
	template<typename T>
	void uploadBufferDeferred(vku::GenericBuffer& __restrict baseBufferObject, vk::CommandBuffer& __restrict cb, vku::GenericBuffer& __restrict stagingBuffer, T const& __restrict value, vk::DeviceSize const sizeInBytes, vk::DeviceSize const maxsizeInBytes = 0) const {
		baseBufferObject.uploadDeferred(cb, stagingBuffer, value, sizeInBytes, (0 == maxsizeInBytes ? sizeInBytes : maxsizeInBytes) );
	}
	template<typename T>
	void uploadBufferDeferred(vku::GenericBuffer& __restrict baseBufferObject, vk::CommandBuffer& __restrict cb, vku::GenericBuffer& __restrict stagingBuffer, const std::vector<T, tbb::scalable_allocator<T> > & __restrict value, size_t const maxreservecount = 0) const {
		baseBufferObject.uploadDeferred(cb, stagingBuffer, value, maxreservecount);
	}

private:
	static constexpr uint32_t const getIndirectCountOffset(uint32_t const drawCommandIndex)
	{
		// drawCommandIndex is pulled from "drawCommandIndices" member
		
		// which has the correct index for the offset this function returns
		/*
			vertexCount  <---- *returning*
			instanceCount
			firstVertex
			firstInstance
		*/
		return(drawCommandIndex * 4u * sizeof(uint32_t));
	}

	void CreateIndirectActiveCountBuffer();
	void UpdateIndirectActiveCountBuffer(vk::CommandBuffer& cb, uint32_t const resource_index);

	template<bool const isDynamic = false, bool const isBasic = false, bool const isClear = false, bool const isTransparent = false, bool const isSampleShading = false>
	void CreateVoxelResource(	cVulkan::sRTDATA& rtData, vk::RenderPass const& renderPass, uint32_t const width, uint32_t const height,
								vku::ShaderModule const& __restrict vert,
								vku::ShaderModule const& __restrict geom,
								vku::ShaderModule const& __restrict frag,
								uint32_t const subPassIndex = 0U);
	// always dynamic
	template<uint32_t const childIndex>
	void CreateVoxelChildResource(vk::RenderPass const& renderPass, uint32_t const width, uint32_t const height,
		vku::ShaderModule const& __restrict vert,
		vku::ShaderModule const& __restrict geom,
		vku::ShaderModule const& __restrict frag,
		uint32_t const subPassIndex = 0U);
	template<uint32_t const childIndex>
	void CreateVoxelChildResource(vk::Pipeline& pipeline, vku::double_buffer<vku::VertexBufferPartition const*>&& partition);

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
		vku::ShaderModule const& __restrict vert,
		vku::ShaderModule const& __restrict geom);

	void CreateVoxelResources();

	
public:
	static bool const renderCompute(vku::compute_pass&& __restrict c);
	static void renderStaticCommandBuffer(vku::static_renderpass&& __restrict s);
	static void renderDynamicCommandBuffer(vku::dynamic_renderpass&& __restrict d);
	static void renderOverlayCommandBuffer(vku::overlay_renderpass&& __restrict o);
	static void renderPresentCommandBuffer(vku::present_renderpass&& __restrict pp);
	static void renderClearCommandBuffer(vku::clear_renderpass&& __restrict pp);
	static void gpuReadback(vk::CommandBuffer& cb, uint32_t const resource_index);
private:
    inline bool const _renderCompute(vku::compute_pass&& __restrict c);
	inline void _renderStaticCommandBuffer(vku::static_renderpass&& __restrict s);
	inline void _renderDynamicCommandBuffer(vku::dynamic_renderpass&& __restrict d);
	inline void _renderOverlayCommandBuffer(vku::overlay_renderpass&& __restrict o);
	inline void _renderPresentCommandBuffer(vku::present_renderpass&& __restrict pp);
	inline void _renderClearCommandBuffer(vku::clear_renderpass&& __restrict c);
	inline void _gpuReadback(vk::CommandBuffer& cb, uint32_t resource_index);

	void renderComplete(uint32_t const resource_index); // triggered internally on Render Completion (after final queue submission / present by vku framework

	void renderClearMasks(vku::static_renderpass&& __restrict s);
	void clearAllVoxels(vku::clear_renderpass&& __restrict c);  // <-- this one clears the opacitymap

	void copyMouseBuffer(vk::CommandBuffer& cb, uint32_t resource_index) const;
	void barrierMouseBuffer(vk::CommandBuffer& cb, uint32_t resource_index) const;

	void copyOffscreenBuffer(vk::CommandBuffer& cb) const;
	void barrierOffscreenBuffer(vk::CommandBuffer& cb) const;
private:
	vk::Device						_device;
	vku::Framework					_fw;
	vku::Window* 					_window;

	constinit static inline vku::double_buffer<vku::IndirectBuffer* __restrict>   _indirectActiveCount{};
	constinit static inline vku::double_buffer<vku::GenericBuffer>				  _activeCountBuffer{};

	vku::GenericBuffer				_mouseBuffer[2];

	vk::UniqueImageView				_offscreenImageView2DArray;
	vku::GenericBuffer				_offscreenBuffer;

	vku::DescriptorSetUpdater		_dsu;
	vk::UniqueSampler				_sampNearest[eSamplerAddressing::size()],			// commonly used "repeat" samplers
									_sampLinear[eSamplerAddressing::size()],
									_sampAnisotropic[eSamplerAddressing::size()];

	microseconds					_frameTimingAverage;

	bool                            _mouse_query_invalidated[2] = { false, false };
	uint32_t						_current_free_resource_index = 0;
	point2D_t						_mouse_query_cache[2];
	
	bool							_bFullScreenExclusiveAcquired = false,
									_bRenderingEnabled = false,
									_bRestoreAsPaused = true, // initial value is true
									_bOffscreenRender = false,
									_bOffscreenCopy = false;

	std::atomic_flag				_OffscreenCopied;
//-------------------------------------------------------------------------------------------------------------------------------//

	constinit static inline struct {

		int32_t
			voxel_terrain,
			voxel_static,
			voxel_dynamic;

		struct {
			int32_t
				voxel_dynamic_main,
				voxel_dynamic_clear_mask,
				voxel_dynamic_custom_pipelines[eVoxelPipelineCustomized::_size()],
				voxel_dynamic_transparency[eVoxelPipelineCustomized::_size()];
		} partition;

		void reset()
		{
			voxel_terrain = -1; voxel_static = -1; voxel_dynamic = -1;
			partition.voxel_dynamic_main = -1; partition.voxel_dynamic_clear_mask = -1;
			for (uint32_t i = 0; i < eVoxelPipelineCustomized::_size(); ++i) {
				partition.voxel_dynamic_custom_pipelines[i] = -1;
				partition.voxel_dynamic_transparency[i] = -1;
			}
		}

	} _drawCommandIndices{};

//-------------------------------------------------------------------------------------------------------------------------------//

public:	// ### public skeleton
	typedef struct sPOSTAADATA
	{
		enum eStage
		{
			Post = 0,
			Final = 1,
			Count
		};
		
		vk::UniquePipelineLayout		pipelineLayout[eStage::Count];
		vk::Pipeline					pipeline[5];
		vk::UniqueDescriptorSetLayout	descLayout[eStage::Count];
		std::vector<vk::DescriptorSet>	sets[eStage::Count];

		sPOSTAADATA()
		{}

		~sPOSTAADATA()
		{
			for (uint32_t i = 0; i < eStage::Count; ++i) {
				pipelineLayout[i].release();
				sets[i].clear(); sets[i].shrink_to_fit();
				descLayout[i].release();
			}
		}
	} sPOSTAADATA;
private: // ### private instance
	static sPOSTAADATA _aaData;

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
		struct compute_light {
			vk::UniquePipelineLayout		pipelineLayout;
			vk::UniquePipeline				pipeline[eComputeLightPipeline::_size()];
			vk::UniqueDescriptorSetLayout	descLayout;
			std::vector<vk::DescriptorSet>	sets;

			~compute_light() {
				for (uint32_t i = 0; i < eComputeLightPipeline::_size(); ++i) {
					pipeline[i].release();
				}
				sets.clear(); sets.shrink_to_fit();
				pipelineLayout.release();
				descLayout.release();
			}
		} light;

		/*
		[[deprecated]] struct compute_texture {
			vk::UniquePipelineLayout		pipelineLayout;
			vk::UniquePipeline				pipeline[eTextureShader::_size()];
			vk::UniqueDescriptorSetLayout	descLayout;
			std::vector<vk::DescriptorSet>	sets[eTextureShader::_size()];

			~compute_texture() {
				for (uint32_t i = 0; i < eTextureShader::_size(); ++i) {
					pipeline[i].release();
					sets[i].clear(); sets[i].shrink_to_fit();
				}
				pipelineLayout.release();
				descLayout.release();
			}
		} texture;
		*/

	} sCOMPUTEDATA;
private: // ### private instance
	static sCOMPUTEDATA _comData;

//-------------------------------------------------------------------------------------------------------------------------------//
private: // ### private skeleton, private instance
	static struct sNUKLEARDATA
	{
		vku::UniformBuffer									_ubo;
		vku::double_buffer<vku::DynamicVertexBuffer>		_vbo;
		vku::double_buffer<vku::DynamicIndexBuffer>			_ibo;

		vk::UniquePipelineLayout		pipelineLayout;
		vk::Pipeline					pipeline;
		vk::UniqueDescriptorSetLayout	descLayout;
		std::vector<vk::DescriptorSet>	sets;

		~sNUKLEARDATA()
		{
			_ubo.release();
			for (uint32_t i = 0; i < vku::double_buffer<uint32_t>::count; ++i) {
				_vbo[i].release();
				_ibo[i].release();
			}

			pipelineLayout.release();
			sets.clear(); sets.shrink_to_fit();
			descLayout.release();
		}
	} _nkData;
	//-------------------------------------------------------------------------------------------------------------------------------//
	static struct sRTSHARED_DATA
	{
		vku::double_buffer<vku::UniformBuffer>	_ubo;
		vk::UniqueDescriptorSetLayout			descLayout[eVoxelDescSharedLayout::_size()];

		~sRTSHARED_DATA()
		{
			_ubo[0].release(); _ubo[1].release();
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
		vku::double_buffer<VertexBufferPool::iterator const>		_vbo;
		vk::Pipeline												pipeline;

		sRTDATA()
			: _vbo{ _vbos.emplace_back(nullptr), _vbos.emplace_back(nullptr) } // pointer to available position in vector
		{}
		~sRTDATA() = default;
	} _rtData[eVoxelPipeline::_size()];
	// ****** //
	static struct sRTDATA_CHILD
	{
		sRTDATA const&												parent;
		vk::Pipeline												pipeline;
		vku::double_buffer<vku::VertexBufferPartition const*>		vbo_partition_info;
		bool const													transparency,
																	mask;

		sRTDATA_CHILD(sRTDATA const& parent_, bool const transparency_, bool const mask_)
			: parent(parent_), vbo_partition_info{ nullptr, nullptr }, transparency(transparency_), mask(mask_)
		{}
		~sRTDATA_CHILD() = default;
	} _rtDataChild[eVoxelPipelineCustomized::_size()];
	//-------------------------------------------------------------------------------------------------------------------------------//
	static struct sVOLUMETRICLIGHTDATA
	{
		vku::double_buffer<vku::UniformBuffer> const&	_ubo;
		vku::VertexBuffer								_vbo;
		vku::IndexBuffer								_ibo;
		uint32_t										index_count;

		vk::UniquePipelineLayout		pipelineLayout;
		vk::Pipeline					pipeline;
		vk::UniqueDescriptorSetLayout	descLayout;
		std::vector<vk::DescriptorSet>	sets;

		sVOLUMETRICLIGHTDATA(vku::double_buffer<vku::UniformBuffer> const& common_shared_ubo)
			: _ubo(common_shared_ubo), index_count(0)
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
	STATIC_INLINE void bindVoxelDescriptorSet(uint32_t const resource_index, vk::CommandBuffer& __restrict cb);

	template<int32_t const voxel_pipeline_index>
	STATIC_INLINE void renderDynamicVoxels(vku::static_renderpass const& s);

	template<int32_t const voxel_pipeline_index>
	STATIC_INLINE void renderStaticVoxels(vku::static_renderpass const& s);

	template<int32_t const voxel_pipeline_index>
	STATIC_INLINE void renderTerrainVoxels(vku::static_renderpass const& s);

	template<int32_t const voxel_pipeline_index>
	STATIC_INLINE void renderAllVoxels(vku::static_renderpass const& s);

	STATIC_INLINE void renderTransparentVoxels(vku::static_renderpass const& s);
	
	static void renderOffscreenVoxels(vku::static_renderpass const& s);
};

template<bool const isDynamic, bool const isBasic, bool const isClear, bool const isTransparent, bool const isSampleShading>
void cVulkan::CreateVoxelResource(
	cVulkan::sRTDATA& rtData, vk::RenderPass const& renderPass, uint32_t const width, uint32_t const height,
	vku::ShaderModule const& __restrict vert,
	vku::ShaderModule const& __restrict geom,
	vku::ShaderModule const& __restrict frag,
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
		pm.blendDstAlphaBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha);
		pm.blendAlphaBlendOp(vk::BlendOp::eAdd);

		typedef vk::ColorComponentFlagBits ccbf;
		pm.blendColorWriteMask(ccbf::eR | ccbf::eG | ccbf::eB | ccbf::eA); // ***** note does not preserve clear mask! intended to be used in overlay pass
	}
	else if constexpr (!isClear) {

		pm.shader(vk::ShaderStageFlagBits::eGeometry, geom);

		if constexpr (!isBasic) {
			pm.shader(vk::ShaderStageFlagBits::eFragment, frag);
			typedef vk::ColorComponentFlagBits ccbf;
			pm.blendBegin(VK_FALSE);
			pm.blendColorWriteMask((vk::ColorComponentFlagBits)ccbf::eR | ccbf::eG | ccbf::eB); // no alpha writes to preserve "clear masks"

		}
		else {  // basic / zpass only
			
			if (!frag.ok()) { // ie frag == null, fragment shader not used
				// just Z no fragment shader required if basic
				// no color attachments
			}
			else {
				typedef vk::ColorComponentFlagBits ccbf;

				// outputs a color to a second color attachment and a color to a third color attachment
				pm.shader(vk::ShaderStageFlagBits::eFragment, frag);

				pm.blendBegin(VK_FALSE);
				pm.blendColorWriteMask((vk::ColorComponentFlagBits)ccbf::eA); // alpha writes only for clearmask "basic" (first color attachment)
				
				pm.blendBegin(VK_FALSE);
				pm.blendColorWriteMask((vk::ColorComponentFlagBits)ccbf::eR | ccbf::eG | ccbf::eB | ccbf::eA); // full writes to second color attachment

				pm.blendBegin(VK_FALSE);
				pm.blendColorWriteMask((vk::ColorComponentFlagBits)ccbf::eR | ccbf::eG | ccbf::eB | ccbf::eA); // full writes to third color attachment
			}

		}
	}

	static_assert(sizeof(VertexDecl::VoxelDynamic) == sizeof(VertexDecl::VoxelNormal)); // hint
	if constexpr (isDynamic) {
		pm.vertexBinding(0, (uint32_t)sizeof(VertexDecl::VoxelDynamic));
	}
	else {
		pm.vertexBinding(0, (uint32_t)sizeof(VertexDecl::VoxelNormal));
	}
	
	pm.vertexAttribute(0, 0, vk::Format::eR32G32B32A32Sfloat, (uint32_t)offsetof(VertexDecl::VoxelDynamic, worldPos));
	pm.vertexAttribute(1, 0, vk::Format::eR32G32B32A32Sfloat, (uint32_t)offsetof(VertexDecl::VoxelDynamic, uv_color));

	pm.depthCompareOp(vk::CompareOp::eLessOrEqual);
	pm.depthClampEnable(VK_FALSE);

	if constexpr (isClear) {
		pm.depthCompareOp(vk::CompareOp::eNever); // during a "voxel clear" it is ojnly required to remove it from the opacity volume texture, this fails the depth test an prevents any unneccessary fragment shading.
		pm.depthTestEnable(VK_FALSE);
		pm.depthWriteEnable(VK_FALSE);
		pm.rasterizerDiscardEnable(VK_TRUE); // fragment shader not used, so discard fragments (clear op occurs in vertex shading stage of pipeline for the opacity volume map)
	}
	else if constexpr (isBasic) {
		pm.depthTestEnable(VK_TRUE);

		if (!frag.ok()) { // ie frag == null, fragment shader not used
			// just Z no fragment shader required if basic
			// no color attachments
			pm.depthWriteEnable(VK_TRUE); // Z Writes only enabled in Z-Only RenderPass
		}
		else {
			pm.depthWriteEnable(VK_FALSE);
		}
	}
	else {
		pm.depthTestEnable(VK_TRUE);
		pm.depthWriteEnable(VK_FALSE);
	}
	pm.cullMode(vk::CullModeFlagBits::eBack);
	pm.frontFace(vk::FrontFace::eClockwise);

	pm.subPass(subPassIndex);
	pm.rasterizationSamples(vku::DefaultSampleCount);
	if constexpr (isSampleShading) {
		pm.sampleShadingEnable(VK_TRUE);
		pm.minSampleShading(0.25f);
	}
	
	// Create a pipeline using a renderPass built for our window.
	auto& cache = _fw.pipelineCache();
	if constexpr (isClear | isBasic) {

		if constexpr (isBasic && !isClear) {

			if (!frag.ok()) { // ie frag == null, fragment shader not used
				rtData.pipeline = pm.create(_device, cache, *cVulkan::_rtSharedDescSet[eVoxelDescSharedLayoutSet::VOXEL_ZONLY].pipelineLayout, renderPass);
				return; // all other permutations create the pipeline using VOXEL_CLEAR
			}
		}
		rtData.pipeline = pm.create(_device, cache, *cVulkan::_rtSharedDescSet[eVoxelDescSharedLayoutSet::VOXEL_CLEAR].pipelineLayout, renderPass);
	}
	else {
		// regular rendering shading pipeline
		rtData.pipeline = pm.create(_device, cache, *cVulkan::_rtSharedDescSet[eVoxelDescSharedLayoutSet::VOXEL_COMMON].pipelineLayout, renderPass);
	}
}

// always dynamic,  - overload for creating unique pipeline
template<uint32_t const childIndex>
void cVulkan::CreateVoxelChildResource(
	vk::RenderPass const& renderPass, uint32_t const width, uint32_t const height,
	vku::ShaderModule const& __restrict vert,
	vku::ShaderModule const& __restrict geom,
	vku::ShaderModule const& __restrict frag,
	uint32_t const subPassIndex)
{
	// only specializes pipeline, everything else is owned by parent sRTDATA
	cVulkan::sRTDATA_CHILD& rtData(_rtDataChild[childIndex]);
	// initialize const pointer to the correct vertex buffer partition info, partition memory is allocated before any calls to this function
	for (uint32_t resource_index = 0; resource_index < vku::double_buffer<uint32_t>::count; ++resource_index) {
		rtData.vbo_partition_info[resource_index] = &(*rtData.parent._vbo[resource_index])->partitions()[childIndex + 1];
	}

	// Make a pipeline to use the vertex format and shaders.
	vku::PipelineMaker pm(width, height);
	pm.topology(vk::PrimitiveTopology::ePointList);
	pm.shader(vk::ShaderStageFlagBits::eVertex, vert);
	pm.shader(vk::ShaderStageFlagBits::eGeometry, geom);
	pm.shader(vk::ShaderStageFlagBits::eFragment, frag);

	// dynamic voxels only
	pm.vertexBinding(0, (uint32_t)sizeof(VertexDecl::VoxelDynamic));

	pm.vertexAttribute(0, 0, vk::Format::eR32G32B32A32Sfloat, (uint32_t)offsetof(VertexDecl::VoxelDynamic, worldPos));
	pm.vertexAttribute(1, 0, vk::Format::eR32G32B32A32Sfloat, (uint32_t)offsetof(VertexDecl::VoxelDynamic, uv_color));

	pm.cullMode(vk::CullModeFlagBits::eBack);
	pm.frontFace(vk::FrontFace::eClockwise);

	pm.depthCompareOp(vk::CompareOp::eLessOrEqual);
	pm.depthClampEnable(VK_FALSE); 
	pm.depthTestEnable(VK_TRUE);
	pm.depthWriteEnable(VK_FALSE); // *** bugfix: required for proper sorting of radial grid whether its opaque or transparent!

	if (rtData.transparency) { // Uses a "voxel clear alpha mask" in first main renderpass
							   // for improved refraction, see https://developer.nvidia.com/gpugems/GPUGems2/gpugems2_chapter19.html

		pm.blendBegin(VK_TRUE);
		pm.blendSrcColorBlendFactor(vk::BlendFactor::eOne); // this is pre-multiplied alpha
		pm.blendDstColorBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha);
		pm.blendColorBlendOp(vk::BlendOp::eAdd);
		pm.blendSrcAlphaBlendFactor(vk::BlendFactor::eOne);
		pm.blendDstAlphaBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha);
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
void cVulkan::CreateVoxelChildResource(vk::Pipeline& pipeline, vku::double_buffer<vku::VertexBufferPartition const*>&& partition)
{
	// only specializes pipeline, everything else is owned by parent sRTDATA
	cVulkan::sRTDATA_CHILD& rtData(_rtDataChild[childIndex]);

	rtData.pipeline = pipeline;  // reference
	rtData.vbo_partition_info = std::move(partition);
}

__inline vku::VertexBufferPartition* const __restrict& __restrict cVulkan::getDynamicPartitionInfo(uint32_t const resource_index) const {
	return((*_rtData[eVoxelPipeline::VOXEL_DYNAMIC]._vbo[resource_index])->partitions());
}

template<uint32_t const descriptor_set>
STATIC_INLINE void cVulkan::bindVoxelDescriptorSet(uint32_t const resource_index, vk::CommandBuffer& __restrict cb)
{
	cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *_rtSharedDescSet[descriptor_set].pipelineLayout, 0,
						  _rtSharedDescSet[descriptor_set].sets[resource_index], nullptr);
}

template<int32_t const voxel_pipeline_index>
STATIC_INLINE void cVulkan::renderDynamicVoxels(vku::static_renderpass const& s)
{
	uint32_t const resource_index(s.resource_index);

	// dynamic voxels

	constexpr int32_t const voxelPipeline = eVoxelPipeline::VOXEL_DYNAMIC_BASIC + voxel_pipeline_index;

	uint32_t const ActiveVertexCount = (*_rtData[voxelPipeline]._vbo[resource_index])->ActiveVertexCount<VertexDecl::VoxelDynamic>();
	if (0 != ActiveVertexCount) {

		s.cb.bindVertexBuffers(0, (*_rtData[voxelPipeline]._vbo[resource_index])->buffer(), vk::DeviceSize(0));

		// main partition (always starts at vertex position 0)
		{
			if (_drawCommandIndices.partition.voxel_dynamic_main >= 0) {
				s.cb.bindPipeline(vk::PipelineBindPoint::eGraphics, _rtData[voxelPipeline].pipeline);
				s.cb.drawIndirect(_indirectActiveCount[resource_index]->buffer(), getIndirectCountOffset(_drawCommandIndices.partition.voxel_dynamic_main), 1, 0);
			}
		}

		// draw children dynamic shader voxels (customized) (opaque voxel shaders only)
		// leveraging existing vb already bound

		for (uint32_t child = 0; child < eVoxelPipelineCustomized::_size(); ++child) {

			if (_drawCommandIndices.partition.voxel_dynamic_custom_pipelines[child] >= 0) {

				s.cb.bindPipeline(vk::PipelineBindPoint::eGraphics, _rtDataChild[child].pipeline);
				s.cb.drawIndirect(_indirectActiveCount[resource_index]->buffer(), getIndirectCountOffset(_drawCommandIndices.partition.voxel_dynamic_custom_pipelines[child]), 1, 0);
			}
		}
	}
}

template<int32_t const voxel_pipeline_index>
STATIC_INLINE void cVulkan::renderStaticVoxels(vku::static_renderpass const& s)
{
	uint32_t const resource_index(s.resource_index);

	// static voxels
	if (_drawCommandIndices.voxel_static >= 0) {
		constexpr int32_t const voxelPipeline = eVoxelPipeline::VOXEL_STATIC_BASIC + voxel_pipeline_index;

		s.cb.bindVertexBuffers(0, (*_rtData[voxelPipeline]._vbo[resource_index])->buffer(), vk::DeviceSize(0));
		s.cb.bindPipeline(vk::PipelineBindPoint::eGraphics, _rtData[voxelPipeline].pipeline);
		s.cb.drawIndirect(_indirectActiveCount[resource_index]->buffer(), getIndirectCountOffset(_drawCommandIndices.voxel_static), 1, 0);
	}
}

template<int32_t const voxel_pipeline_index>
STATIC_INLINE void cVulkan::renderTerrainVoxels(vku::static_renderpass const& s)
{
	uint32_t const resource_index(s.resource_index);

	// terrain
	if (_drawCommandIndices.voxel_terrain >= 0) {
		constexpr int32_t const voxelPipeline = eVoxelPipeline::VOXEL_TERRAIN_BASIC + voxel_pipeline_index;

		s.cb.bindVertexBuffers(0, (*_rtData[voxelPipeline]._vbo[resource_index])->buffer(), vk::DeviceSize(0));
		s.cb.bindPipeline(vk::PipelineBindPoint::eGraphics, _rtData[voxelPipeline].pipeline);
		s.cb.drawIndirect(_indirectActiveCount[resource_index]->buffer(), getIndirectCountOffset(_drawCommandIndices.voxel_terrain), 1, 0);
	}
}

template<int32_t const voxel_pipeline_index>
STATIC_INLINE void cVulkan::renderAllVoxels(vku::static_renderpass const& s)
{
	// ***** descriptor set must be set outside of this function ***** //

	// always front to back (optimal order for z culling) //
	
	// dynamic voxels //
	renderDynamicVoxels<voxel_pipeline_index>(s);

	// static voxels //
	renderStaticVoxels<voxel_pipeline_index>(s);

	// terrain voxels //
	renderTerrainVoxels<voxel_pipeline_index>(s);
}

// old but left as reference for the "mouse behind building models feature", major overdraw though.
/*template<uint32_t const numChildMasks>
STATIC_INLINE uint32_t const cVulkan::renderAllVoxels_ZPass(vku::static_renderpass const& s, [[maybe_unused]] sRTDATA_CHILD const* __restrict* const __restrict& __restrict deferredChildMasks)
{
	// ***** common shared descriptor set must be set outside of this function ***** //

	uint32_t ActiveMaskCount(0);

#if BACK_TO_FRONT // *note there is a major bug where the normals are not output on ZONLY, causing no models to be rendered (only terrain) to the mouse color attachment. It also causes no models to be rendered on the normals color attachment, This does affect front to back rendering, as it always uses the MOUSE pipeline and does not alternate between MOUSE and ZONLY.
	// [back_to_front] is needed for the mouse color atttachment to be rendered in correct order (only on initial z pass)
	// for seamless selection / dragging of background roads (behind buildings etc)
	// for transparent voxel draw order clears aswell
	
	// terrain voxels //
	renderTerrainVoxels<MOUSE>(s);	// always output to mouse buffer

	if (0 != s.resource_index) //// alternate from ZONLY to MOUSE frame by frame Is required for mouse input! (part of trick that enables mousing behind buildings / models)
	{
		// static voxels //
		renderStaticVoxels<ZONLY>(s);

		// dynamic voxels //
		ActiveMaskCount = renderDynamicVoxels<ZONLY, numChildMasks>(s, deferredChildMasks);
	}
	else {

		// static voxels //
		renderStaticVoxels<MOUSE>(s);
		
		// dynamic voxels //
		ActiveMaskCount = renderDynamicVoxels<MOUSE, numChildMasks>(s, deferredChildMasks);
	}
#else // (default) FRONT TO BACK

	// dynamic voxels //
	ActiveMaskCount = renderDynamicVoxels<MOUSE, numChildMasks>(s, deferredChildMasks);

	// static voxels //
	renderStaticVoxels<MOUSE>(s);

	// terrain voxels //
	renderTerrainVoxels<MOUSE>(s);
#endif

	return(ActiveMaskCount);
}
*/

__inline void cVulkan::renderTransparentVoxels(vku::static_renderpass const& s)
{
	uint32_t const resource_index(s.resource_index);
	
	// SUBPASS - voxels w/Transparency //

	{ // dynamic voxels
		uint32_t const ActiveVertexCount = (*_rtData[eVoxelPipeline::VOXEL_DYNAMIC]._vbo[resource_index])->ActiveVertexCount<VertexDecl::VoxelDynamic>();
		if (0 != ActiveVertexCount) {

			// draw children dynamic shader voxels (customized) (transparent voxel shaders only)

			// leveraging dynamic vb 
			// all child shaders share this descriptor set, but have unique pipelines
			s.cb.bindVertexBuffers(0, (*_rtData[eVoxelPipeline::VOXEL_DYNAMIC]._vbo[resource_index])->buffer(), vk::DeviceSize(0));

			for (uint32_t child = 0; child < eVoxelPipelineCustomized::_size(); ++child) {

				if (_drawCommandIndices.partition.voxel_dynamic_transparency[child] >= 0) {

					s.cb.bindPipeline(vk::PipelineBindPoint::eGraphics, _rtDataChild[child].pipeline);
					s.cb.drawIndirect(_indirectActiveCount[resource_index]->buffer(), getIndirectCountOffset(_drawCommandIndices.partition.voxel_dynamic_transparency[child]), 1, 0);
				}
			}
		}
	}
}







