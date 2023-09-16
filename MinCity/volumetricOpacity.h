/* Copyright (C) 20xx Jason Tully - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License
 * http://www.supersinfulsilicon.com/
 *
This work is licensed under the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
To view a copy of this license, visit http://creativecommons.org/licenses/by-nc-sa/4.0/
or send a letter to Creative Commons, PO Box 1866, Mountain View, CA 94042, USA.
 */

#pragma once
#include "cVulkan.h"

#include "lightBuffer3D.h"

#include <vku/vku_addon.hpp>
#include "IsoCamera.h"
#include <Math/v2_rotation_t.h>
#include <Utility/scalable_aligned_allocator.h>
#include "voxelAlloc.h"
#include <Utility/async_long_task.h>

namespace Volumetric {
	extern bool const isGraduallyStartingUp();
} // end ns

namespace Volumetric
{
	BETTER_ENUM(ePingPongMap, uint32_t const,
		PING = 0U,
		PONG = 1U
	);

	struct voxelTexture3D { // *bugfix - fully double buffered on cpu and gpu now to support faster frame submission performance, fixes lighting missing or not appearing at all

		vku::double_buffer<vku::GenericBuffer>		stagingBuffer;

		vku::double_buffer<vku::TextureImage3D*>	imageGPUIn;

		voxelTexture3D()
			: imageGPUIn{ nullptr, nullptr }
		{}
	};

	typedef struct voxelLightmapSet {

		vku::TextureImageStorage3D
			* __restrict DistanceDirection;

		vku::TextureImageStorage3D
			* __restrict Color;

	} voxelLightmapSet;

	typedef struct voxelVolumeSet {

		voxelLightmapSet
			* __restrict LightMap;		// alias of output (the lightmap)

		vku::TextureImageStorage3D
			* __restrict OpacityMap;	// alias   ""     ""    ""    ""

	} voxelVolumeSet;

#ifdef DEBUG_LIGHT_PROPAGATION
	struct alignas(16) debugLightMinMax {
		XMVECTOR min, max;
	};
#endif

	// opacity map initialization (internal methods)
	namespace internal {
		void InitializeOpacityMap(uint32_t const world_volume_size, vku::TextureImage2D*& srcGround);
		void RenderInitializeOpacityMap(uint32_t const world_volume_size, vk::CommandBuffer& cb, vku::TextureImage2D const* const srcGround, vku::TextureImageStorage3D const* const dstVolume);
	}

	template< uint32_t const Size > // "uniform world volume size"
	class alignas(64) volumetricOpacity
	{
	public:
		static constexpr uint32_t const // "uniform light volume size"
			LightSize = ComputeLightConstants::LIGHT_RESOLUTION,
			DispatchHeights = LightSize >> 3u; // dispatch granularity of 8 voxels, this is the count or number of unique dispatch height maximums

	private:
		using lightVolume = lightBuffer3D<ComputeLightConstants::memLayoutV, LightSize, Size>;

		static constexpr uint32_t const PING = ePingPongMap::PING, PONG = ePingPongMap::PONG;
		static constexpr uint32_t const getStepMax() {
			uint32_t step(1);
			while (((step << 1) < LightSize)) {
				step <<= 1;
			}
			return(step);
		}
		static constexpr uint32_t const getStepCount() {
			uint32_t count(0);
			uint32_t step(MAX_STEP_PINGPONG);

			do
			{
				// what the output index currently
				++count;

				step >>= 1;

			} while (0 != step);

			return(count);
		}
		static constexpr uint32_t const getPingPongChainLastIndex() {

			uint32_t uPingPong(ePingPongMap::PING);
			uint32_t step(MAX_STEP_PINGPONG);
			do
			{
				// what the output index currently
				uPingPong = (ePingPongMap::PING == uPingPong ? ePingPongMap::PONG : ePingPongMap::PING);

				step >>= 1;

			} while (0 != step);
			// input.output
			// [pi.po], po.pi, pi.po, po.pi, pi.po, po.pi, pi.po, po.pi, pi.po [po.pi]
			//  1       2      4      8      16     32     64     128    256    512
			return(uPingPong);
		}

		static constexpr uint32_t const
			MAX_STEP_PINGPONG = getStepMax(),
			STEP_COUNT = getStepCount(),
			PING_PONG_CHAIN_LAST_INDEX = getPingPongChainLastIndex();

	public:
		// world volume metrics
		static inline constexpr uint32_t const		getSize() { return(Size); }
		static inline float const					getVolumeLength() { return(VolumeLength); }
		static inline float const					getInvVolumeLength() { return(InvVolumeLength); }

		// light volume metrics
		static inline constexpr uint32_t const		getLightSize() { return(LightSize); }
		static inline constexpr size_t const		getLightProbeMapSizeInBytes() { return(LightSize * LightSize * LightSize * ComputeLightConstants::NUM_BYTES_PER_VOXEL_LIGHT); }

	public:
		// Accessor ///
		voxelVolumeSet const& __restrict			 getVolumeSet() const { return(VolumeSet); }
		__inline lightVolume const& __restrict		 getMappedVoxelLights() const { return(MappedVoxelLights); }
		
		// Mutators //
		
#ifdef DEBUG_LIGHT_PROPAGATION
		int32_t const getDebugSliceIndex() const {
			return(DebugSlice);
		}
		int32_t& getDebugSliceIndex() {
			return(DebugSlice);
		}
		XMVECTOR const XM_CALLCONV getDebugMin() {
			return(DebugMinMax.min);
		}
		XMVECTOR const XM_CALLCONV getDebugMax() {
			return(DebugMinMax.max);
		}
		vku::TextureImageStorage2D const* const getDebugSliceImage() const
		{
			return(DebugTexture);
		}
#endif
		// Main Methods //
		void map() const {
			const_cast<volumetricOpacity<Size>* const __restrict>(this)->MappedVoxelLights.map();
		}
		void commit() const { 
			const_cast<volumetricOpacity<Size>* const __restrict>(this)->MappedVoxelLights.commit();
		}

		void clear(uint32_t const resource_index) const { // clears cache & staging buffer
			const_cast<volumetricOpacity<Size>* const __restrict>(this)->MappedVoxelLights.clear(resource_index);
		}
		
		void clear(vk::CommandBuffer& cb, uint32_t const resource_index) { // for clearing the light probe map (gpu local)
			LightProbeMap.imageGPUIn[resource_index]->clear<false>(cb);
		}

		void release() {

			for (uint32_t i = 0; i < vku::double_buffer<uint32_t>::count; ++i) {
				LightProbeMap.stagingBuffer[i].release();
				SAFE_RELEASE_DELETE(LightProbeMap.imageGPUIn[i]);
			}
			
			for (uint32_t i = 0; i < DispatchHeights; ++i) {
				SAFE_RELEASE_DELETE(ComputeLightDispatchBuffer[i]);
			}

			for (uint32_t i = 0; i < 2; ++i) {
				SAFE_RELEASE_DELETE(PingPongMap[i]);
			}

			VolumeSet.LightMap = nullptr;

			SAFE_RELEASE_DELETE(LightMap.DistanceDirection);
			SAFE_RELEASE_DELETE(LightMap.Color);

			VolumeSet.OpacityMap = nullptr;
			SAFE_RELEASE_DELETE(OpacityMap);

#ifdef DEBUG_LIGHT_PROPAGATION
			if (DebugMinMaxBuffer) {
				DebugMinMaxBuffer->release();
				SAFE_DELETE(DebugMinMaxBuffer);
			}

			if (DebugTexture) {
				DebugTexture->release();
				SAFE_DELETE(DebugTexture);
			}
#endif
		}

	private:
		void createIndirectDispatch(vk::Device const& __restrict device, vk::CommandPool const& __restrict commandPool, vk::Queue const& __restrict queue)
		{
			uint32_t dispatch_height(LightSize);

			for ( uint32_t i = 0 ; i < DispatchHeights ; ++i )
			{ // light compute dispatch
				uint32_t const local_size((LightSize >> ComputeLightConstants::SHADER_LOCAL_SIZE_BITS) + (0U == (LightSize % ComputeLightConstants::SHADER_LOCAL_SIZE) ? 0U : 1U));
				uint32_t const local_size_z((dispatch_height >> ComputeLightConstants::SHADER_LOCAL_SIZE_BITS) + (0U == (dispatch_height % ComputeLightConstants::SHADER_LOCAL_SIZE) ? 0U : 1U));

				if (0 == local_size_z) {
					return; // don't create the last DispatchHeight if its dispatch size is zero.
				}

				vk::DispatchIndirectCommand const dispatchCommand{

					local_size, local_size, local_size_z
				};

				ComputeLightDispatchBuffer[i] = new vku::IndirectBuffer(sizeof(dispatchCommand), true);

				ComputeLightDispatchBuffer[i]->upload(device, commandPool, queue, dispatchCommand);

				dispatch_height -= 8; // next dispatch height
			}
		}

	public:
		void create(vk::Device const& __restrict device, 
			        vk::CommandPool const& __restrict commandPool, vk::Queue const& __restrict queue, 
			        vk::CommandPool const& __restrict commandPoolGraphics, vk::Queue const& __restrict queueGraphics, 
			        point2D_t const frameBufferSize, size_t const hardware_concurrency) {

			createIndirectDispatch(device, commandPool, queue);

			for (uint32_t resource_index = 0; resource_index < vku::double_buffer<uint32_t>::count; ++resource_index) {

				LightProbeMap.stagingBuffer[resource_index].createAsStagingBuffer(getLightProbeMapSizeInBytes(), vku::eMappedAccess::Sequential, false, true); // *bugfix - this needs to be staging buffer type (excessive extra pcie bandwidth with the wrong type before)

				LightProbeMap.imageGPUIn[resource_index] = new vku::TextureImage3D(vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst, device,
					LightSize, LightSize, LightSize, 1U, vk::Format::eR16G16B16A16Unorm, false, true);

				VKU_SET_OBJECT_NAME(vk::ObjectType::eImage, (VkImage)LightProbeMap.imageGPUIn[resource_index]->image(), vkNames::Image::LightProbeMap);
			}
			
			MappedVoxelLights.create(hardware_concurrency); // prepares/clears buffers/memory

			for (uint32_t i = 0; i < 2; ++i) {
				PingPongMap[i] = new vku::TextureImageStorage3D(vk::ImageUsageFlagBits::eSampled, device,
					LightSize, LightSize, LightSize, 1U, vk::Format::eR16G16B16A16Unorm, false, true);

				VKU_SET_OBJECT_NAME(vk::ObjectType::eImage, (VkImage)PingPongMap[i]->image(), vkNames::Image::PingPongMap);
			}

			LightMap.DistanceDirection = new vku::TextureImageStorage3D(vk::ImageUsageFlagBits::eSampled, device,
				LightSize, LightSize, LightSize, 1U, vk::Format::eR16G16B16A16Unorm, false, true); 
			LightMap.Color = new vku::TextureImageStorage3D(vk::ImageUsageFlagBits::eSampled, device,
				LightSize, LightSize, LightSize, 1U, vk::Format::eR16G16B16A16Sfloat, false, true);
			VolumeSet.LightMap = &LightMap;

			VKU_SET_OBJECT_NAME(vk::ObjectType::eImage, (VkImage)LightMap.DistanceDirection->image(), vkNames::Image::LightMap_DistanceDirection);
			VKU_SET_OBJECT_NAME(vk::ObjectType::eImage, (VkImage)LightMap.Color->image(), vkNames::Image::LightMap_Color);

			OpacityMap = new vku::TextureImageStorage3D(vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst, device,
				Size, Size, Size, 1U, vk::Format::eR8Unorm, false, true);
			VolumeSet.OpacityMap = OpacityMap;

			VKU_SET_OBJECT_NAME(vk::ObjectType::eImage, (VkImage)OpacityMap->image(), vkNames::Image::OpacityMap);

			FMT_LOG(TEX_LOG, "LightProbe Volumetric data: {:n} bytes", LightProbeMap.imageGPUIn[0]->size() + LightProbeMap.imageGPUIn[1]->size());
			FMT_LOG(TEX_LOG, "Lightmap [GPU Resident Only] Volumetric data: {:n} bytes", PingPongMap[0]->size() + PingPongMap[1]->size() + LightMap.DistanceDirection->size() + LightMap.Color->size());
			FMT_LOG(TEX_LOG, "Opacitymap [GPU Resident Only] Volumetric data: {:n} bytes", OpacityMap->size());

#ifdef DEBUG_LIGHT_PROPAGATION
			DebugDevice = &device;
			DebugMinMaxBuffer = new vku::HostStorageBuffer(DebugMinMax);
			DebugTexture = new vku::TextureImageStorage2D(vk::ImageUsageFlagBits::eSampled, device, LightWidth, LightDepth, 1, vk::Format::eR8G8B8A8Unorm);
			resetMinMax();
#endif
			vku::executeImmediately<false>(device, commandPool, queue, [&](vk::CommandBuffer cb) {

				LightProbeMap.imageGPUIn[0]->setLayout(cb, vk::ImageLayout::eTransferDstOptimal);  // required initial state
				LightProbeMap.imageGPUIn[1]->setLayout(cb, vk::ImageLayout::eTransferDstOptimal);

				LightMap.DistanceDirection->setLayoutCompute(cb, vku::ACCESS_WRITEONLY);		// the final oututs are never "read" in compute shaders
				LightMap.Color->setLayoutCompute(cb, vku::ACCESS_WRITEONLY);					// *only* read by fragment shaders

				PingPongMap[0]->setLayoutCompute(cb, vku::ACCESS_READWRITE);		// never changes
				PingPongMap[1]->setLayoutCompute(cb, vku::ACCESS_READWRITE);		// never changes

				// initialize the first slice of opacitymap so there is always "ground" for all voxels that extend to the dimensions of the opacitymap plane/slice/widthxheight
				// this slice is then always present, never overwritten, and solves a couple problems
				//  - reflections are not present for bounces off offscreen voxels
				//  - raymarch performance, ground "misses" extend the raymarch distance if no ground is present
				//  - graphical glitch of no ground outside the view frustum
				// 
				// *bugfixes
				//

				VolumeSet.OpacityMap->setLayout(cb, vk::ImageLayout::eTransferDstOptimal);        // prepare for initialization
			});

			// temporary src texture for ground plane blit to opacity map.
			vku::TextureImage2D* texture_ground_plane(nullptr);
			internal::InitializeOpacityMap(Size, texture_ground_plane);

			vku::executeImmediately<false>(device, commandPoolGraphics, queueGraphics, [&](vk::CommandBuffer cb) {

				texture_ground_plane->setLayout(cb, vk::ImageLayout::eTransferSrcOptimal); // setup for copy/blit

				internal::RenderInitializeOpacityMap(Size, cb, texture_ground_plane, VolumeSet.OpacityMap);

			    VolumeSet.OpacityMap->setLayout(cb, vk::ImageLayout::eGeneral); // initial run-time rendering condition requirement
		    });

			// release temporary ground plane texture
			SAFE_RELEASE_DELETE(texture_ground_plane);
		}

		void SetSpecializationConstants_ComputeLight(std::vector<vku::SpecializationConstant>& __restrict constants)
		{
			// full world volume dimensions //
			constants.emplace_back(vku::SpecializationConstant(0, (float)Size)); // should be world volume uniform size (width=height=depth)
			constants.emplace_back(vku::SpecializationConstant(1, (float)VolumeLength)); // should be world volume length

			// light volume dimensions //
			constants.emplace_back(vku::SpecializationConstant(2, (float)LightSize)); // should be light volume uniform size
			constants.emplace_back(vku::SpecializationConstant(3, 1.0f / (float)LightSize)); // should be inverse light volume size

			constants.emplace_back(vku::SpecializationConstant(4, (float)Iso::MINI_VOX_SIZE)); // should be mini vox size
		}

		void UpdateDescriptorSet_ComputeLight(uint32_t const resource_index, vku::DescriptorSetUpdater& __restrict dsu, vk::ImageView const& __restrict imgBluenoise, vk::Sampler const& __restrict samplerLinearClamp, vk::Sampler const& __restrict samplerNearestRepeat) const
		{
			dsu.beginImages(0U, 0, vk::DescriptorType::eCombinedImageSampler);
			dsu.image(samplerLinearClamp, LightProbeMap.imageGPUIn[resource_index]->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal);

			dsu.beginImages(1U, 0, vk::DescriptorType::eCombinedImageSampler);
			dsu.image(samplerNearestRepeat, imgBluenoise, vk::ImageLayout::eShaderReadOnlyOptimal);

			dsu.beginImages(2U, 0, vk::DescriptorType::eCombinedImageSampler);
			dsu.image(samplerLinearClamp, PingPongMap[0]->imageView(), vk::ImageLayout::eGeneral);
			dsu.beginImages(2U, 1, vk::DescriptorType::eCombinedImageSampler);
			dsu.image(samplerLinearClamp, PingPongMap[1]->imageView(), vk::ImageLayout::eGeneral);

			dsu.beginImages(3U, 0, vk::DescriptorType::eStorageImage);
			dsu.image(nullptr, PingPongMap[0]->imageView(), vk::ImageLayout::eGeneral);
			dsu.beginImages(3U, 1, vk::DescriptorType::eStorageImage);
			dsu.image(nullptr, PingPongMap[1]->imageView(), vk::ImageLayout::eGeneral);

			/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

			dsu.beginImages(4U, 0, vk::DescriptorType::eStorageImage);
			dsu.image(nullptr, LightMap.DistanceDirection->imageView(), vk::ImageLayout::eGeneral);

			dsu.beginImages(5U, 0, vk::DescriptorType::eStorageImage);
			dsu.image(nullptr, LightMap.Color->imageView(), vk::ImageLayout::eGeneral);
		}

		__inline bool const renderCompute(vku::compute_pass&& __restrict  c, struct cVulkan::sCOMPUTEDATA const& __restrict render_data);

	private:
		__inline bool const upload_light(uint32_t const resource_index, vk::CommandBuffer& __restrict cb, uint32_t const transferQueueFamilyIndex, uint32_t const computeQueueFamilyIndex, uint32_t& __restrict dispatch_volume_height);

		__inline void renderSeed(vku::compute_pass const& __restrict c, struct cVulkan::sCOMPUTEDATA const& __restrict render_data, uint32_t const index_output);

		__inline void renderJFA(vku::compute_pass const& __restrict  c, struct cVulkan::sCOMPUTEDATA const& __restrict render_data,
			uint32_t const index_input, uint32_t const index_output, uint32_t const step);

		__inline void renderFilter(
			vku::compute_pass const& __restrict  c, struct cVulkan::sCOMPUTEDATA const& __restrict render_data,
			uint32_t const index_input);
#ifdef DEBUG_LIGHT_PROPAGATION
		__inline void resetMinMax() const;
		__inline void updateMinMax();
		__inline void renderDebugLight(vku::compute_pass const& __restrict  c, struct cVulkan::sCOMPUTEDEBUGLIGHTDATA const& __restrict render_data) const;
#endif

	private:
		voxelTexture3D										LightProbeMap;
		lightVolume											MappedVoxelLights;
		
		vku::IndirectBuffer* __restrict						ComputeLightDispatchBuffer[DispatchHeights];  // High (index 0) to Low (index n)
		uint32_t                                            SelectedDispatchHeightIndex;

		vku::TextureImageStorage3D*							PingPongMap[2];
		voxelLightmapSet									LightMap; // final output
		vku::TextureImageStorage3D*							OpacityMap;
		voxelVolumeSet										VolumeSet;

		static inline float const							VolumeSize = Size, 
															VolumeLength = SFM::__sqrt(VolumeSize*VolumeSize + VolumeSize*VolumeSize + VolumeSize*VolumeSize),
															InvVolumeLength = (1.0f / VolumeLength);

		UniformDecl::ComputeLightPushConstants				PushConstants;

#ifdef DEBUG_LIGHT_PROPAGATION
		vk::Device const* DebugDevice;
		vku::HostStorageBuffer* DebugMinMaxBuffer;
		debugLightMinMax									DebugMinMax;
		vku::TextureImageStorage2D* DebugTexture;
		int32_t												DebugSlice;
#endif
	public:
		volumetricOpacity()
			:
			MappedVoxelLights(LightProbeMap.stagingBuffer),
			ComputeLightDispatchBuffer{}, SelectedDispatchHeightIndex(0), LightMap{ nullptr }, OpacityMap{ nullptr },
			VolumeSet{}

#ifdef DEBUG_LIGHT_PROPAGATION
			, DebugDevice(nullptr), DebugMinMaxBuffer(nullptr), DebugMinMax{ { 99999.0f, 99999.0f, 99999.0f }, { -99999.0f, -99999.0f, -99999.0f } }, DebugTexture(nullptr), DebugSlice(0)
#endif
		{
			// REQUIRED initial state - *do not change or remove*
			PushConstants.step = 0;			 // zero
			PushConstants.index_input = 0;	 // zero
			PushConstants.index_output = 1;  // index_output != index_input  **important
		}
		~volumetricOpacity()
		{
			release();
		}
	};

#ifdef DEBUG_LIGHT_PROPAGATION
	template< uint32_t Size >
	__inline void volumetricOpacity<Size>::resetMinMax() const
	{
		inline XMVECTORF32 const xmResetMin{ 99999.0f, 99999.0f, 99999.0f };
		inline XMVECTORF32 const xmResetMax{ -99999.0f, -99999.0f, -99999.0f };

		debugLightMinMax const resetMinMaxBufferValue{ xmResetMin, xmResetMax };

		if (DebugDevice) {
			debugLightMinMax* const __restrict out = (debugLightMinMax* const __restrict)DebugMinMaxBuffer->map(*DebugDevice);
			*out = resetMinMaxBufferValue;
			DebugMinMaxBuffer->unmap(*DebugDevice);
			//DebugMinMaxBuffer->flush(*DebugDevice);
		}
	}
	template< uint32_t Size >
	__inline void volumetricOpacity<Size>::updateMinMax()
	{
		if (DebugDevice) {
			//DebugMinMaxBuffer->invalidate(*DebugDevice);
			debugLightMinMax const* const __restrict in = (debugLightMinMax const* const __restrict)DebugMinMaxBuffer->map(*DebugDevice);
			DebugMinMax = *in;
			DebugMinMaxBuffer->unmap(*DebugDevice);
		}
	}
	template< uint32_t Size >
	__inline void volumetricOpacity<Size>::renderDebugLight(vku::compute_pass const& __restrict  c, struct cVulkan::sCOMPUTEDEBUGLIGHTDATA const& __restrict render_data) const
	{
		// common descriptor set and pipline layout to MINMAX and BLIT, seperate pipelines
		c.cb_render.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *render_data.pipelineLayout, 0, render_data.sets[0], nullptr);

		// minmax stage //
		c.cb_render.bindPipeline(vk::PipelineBindPoint::eCompute, *render_data.pipeline[eComputeDebugLightPipeline::MINMAX]);

		UniformDecl::ComputeDebugLightPushConstantsJFA pc{};
		pc.slice_index = DebugSlice;

		c.cb_render.pushConstants(*render_data.pipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(UniformDecl::ComputeDebugLightPushConstantsJFA), &pc);

		c.cb_render.dispatch(
			(LightWidth >> ComputeLightConstants::SHADER_LOCAL_SIZE_BITS) + (0U == (LightWidth % ComputeLightConstants::SHADER_LOCAL_SIZE) ? 0U : 1U), // local size x = 8
			(LightDepth >> ComputeLightConstants::SHADER_LOCAL_SIZE_BITS) + (0U == (LightDepth % ComputeLightConstants::SHADER_LOCAL_SIZE) ? 0U : 1U), // local size y = 8
			1U);

		// blit stage //
		c.cb_render.bindPipeline(vk::PipelineBindPoint::eCompute, *render_data.pipeline[eComputeDebugLightPipeline::BLIT]);

		c.cb_render.pushConstants(*render_data.pipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(UniformDecl::ComputeDebugLightPushConstantsJFA), &pc);

		c.cb_render.dispatch(
			(LightWidth >> ComputeLightConstants::SHADER_LOCAL_SIZE_BITS) + (0U == (LightWidth % ComputeLightConstants::SHADER_LOCAL_SIZE) ? 0U : 1U), // local size x = 8
			(LightDepth >> ComputeLightConstants::SHADER_LOCAL_SIZE_BITS) + (0U == (LightDepth % ComputeLightConstants::SHADER_LOCAL_SIZE) ? 0U : 1U), // local size y = 8
			1U);
	}
#endif

	template< uint32_t Size >
	__inline void volumetricOpacity<Size>::renderSeed(vku::compute_pass const& __restrict  c, struct cVulkan::sCOMPUTEDATA const& __restrict render_data, uint32_t const index_output)
	{
		PushConstants.step = 1; // 1+JFA (error correction) note: 1+JFA is more accurate than JFA+1 by effectively resulting in a jump flood with far less errors.
		PushConstants.index_output = index_output; // ping pong output index
		PushConstants.index_input = !index_output; // last frames ping pong output index (can be used as an input)

		c.cb_render_light.pushConstants(*render_data.light.pipelineLayout, vk::ShaderStageFlagBits::eCompute,
			(uint32_t)0U, (uint32_t)sizeof(UniformDecl::ComputeLightPushConstants), reinterpret_cast<void const* const>(&PushConstants));

		c.cb_render_light.dispatchIndirect(ComputeLightDispatchBuffer[0]->buffer(), 0); // always use the highest dispatch height for seed & filter *only*, required for clean clear volume at start of every frame
		// dispatchIndirect() is faster than dispatch(), if you can meet the requirements of being constant and pre-loaded dispatch local size information
		// otherwise each dispatch() must upload to GPU that information
	}
	template< uint32_t Size >
	__inline void volumetricOpacity<Size>::renderJFA(vku::compute_pass const& __restrict  c, struct cVulkan::sCOMPUTEDATA const& __restrict render_data,
		uint32_t const index_input, uint32_t const index_output, uint32_t const step)
	{
		PushConstants.step = step;
		PushConstants.index_output = index_output;
		PushConstants.index_input = index_input;

		c.cb_render_light.pushConstants(*render_data.light.pipelineLayout, vk::ShaderStageFlagBits::eCompute,
			(uint32_t)0U, (uint32_t)sizeof(UniformDecl::ComputeLightPushConstants), reinterpret_cast<void const* const>(&PushConstants));

		c.cb_render_light.dispatchIndirect(ComputeLightDispatchBuffer[SelectedDispatchHeightIndex]->buffer(), 0);
		// dispatchIndirect() is faster than dispatch(), if you can meet the requirements of being constant and pre-loaded dispatch local size information
		// otherwise each dispatch() must upload to GPU that information
	}

	template< uint32_t Size >
	__inline void volumetricOpacity<Size>::renderFilter(
		vku::compute_pass const& __restrict  c, struct cVulkan::sCOMPUTEDATA const& __restrict render_data,
		uint32_t const index_input)
	{
		// trick uses consistent place in meory (this->PushConstants) so that command buffer does not need
		// to be updated every frame for the view matrix. 
		// The value of the view matrix is set in this->PushConstants
		// the command buffer references this->PushConstants

		// note: view matrix is manually updated outside of this function
		// note: pushconstant filter index is manually updated outside of this function
		//       it alternates independently so that a frame history can be made in less memory.
		PushConstants.index_output = !index_input; // this is not used in the shader @ this stage. however, it is important that this is still updated to maintain the current state of PushConstants.
		PushConstants.index_input = index_input;  // this is the input to filter stage that is the output of the jfa stage.                                                                          
		
		c.cb_render_light.pushConstants(*render_data.light.pipelineLayout, vk::ShaderStageFlagBits::eCompute,
			(uint32_t)0U, (uint32_t)sizeof(UniformDecl::ComputeLightPushConstants), reinterpret_cast<void const* const>(&PushConstants));

		c.cb_render_light.dispatchIndirect(ComputeLightDispatchBuffer[0]->buffer(), 0); // always use the highest dispatch height for seed & filter *only*, required for clean clear volume at start of every frame
		// dispatchIndirect() is faster than dispatch(), if you can meet the requirements of being constant and pre-loaded dispatch local size information
		// otherwise each dispatch() must upload to GPU that information
	}
	/* native vulkan command clearcolorimage is 2x faster than this
	template< uint32_t Size >
	__inline void volumetricOpacity<Size>::clearOpacityVolume(vku::compute_gpu_function const& __restrict  c, struct cVulkan::sCOMPUTECLEARVOLUMEDATA const& __restrict render_data) const
	{
		static constexpr uint32_t const	// clearing shader can have higher total local size as it is simpler
			SHADER_LOCAL_SIZE_BITS = 3U, // 2^3 = 8
			SHADER_LOCAL_SIZE = (1U << SHADER_LOCAL_SIZE_BITS);			//  **** 8 x 8 x 8 ****

		c.cb_render_light.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *render_data.pipelineLayout, 0, render_data.sets[0], nullptr);

		// minmax stage //
		c.cb_render_light.bindPipeline(vk::PipelineBindPoint::eCompute, *render_data.pipeline);

		c.cb_render_light.dispatch(
			(Width >> SHADER_LOCAL_SIZE_BITS) + (0U == (Width % SHADER_LOCAL_SIZE) ? 0U : 1U),    // local size x = 8
			(Depth >> SHADER_LOCAL_SIZE_BITS) + (0U == (Depth % SHADER_LOCAL_SIZE) ? 0U : 1U),    // local size y = 8
			(Height >> SHADER_LOCAL_SIZE_BITS) + (0U == (Height % SHADER_LOCAL_SIZE) ? 0U : 1U)); // local size z = 8

	}*/

	template< uint32_t Size >
	__inline bool const volumetricOpacity<Size>::renderCompute(vku::compute_pass&& __restrict c, struct cVulkan::sCOMPUTEDATA const& __restrict render_data)
	{
		constinit static bool bRecorded[2]{ false, false }; // these are synchronized 
		constinit static uint32_t dispatch_volume_height{ LightSize }, last_dispatch_volume_height{ LightSize };

		uint32_t const resource_index(c.resource_index);

		if (c.cb_transfer_light)
		{
			bRecorded[resource_index] = upload_light(resource_index, c.cb_transfer_light, c.transferQueueFamilyIndex, c.computeQueueFamilyIndex, dispatch_volume_height); // compute may need to re-record command buffer
		}
		
		if (c.cb_render_light) { 

			// Record Compute Command buffer if not flagged as already recorded. 
			if (!bRecorded[resource_index] || c.bypass) {
			
				uint32_t const current_height(dispatch_volume_height); // *bugfix: light was not being cleared on shrink eg.) when the ship is descending.

				if (dispatch_volume_height < last_dispatch_volume_height) { // Required to "erase" lights that are no longer in the current bound (height shrinking)
					dispatch_volume_height = last_dispatch_volume_height;   // set to last dispatch height
				}
				last_dispatch_volume_height = current_height; // always updated to latest

				// Select the closest dispatch height
				SelectedDispatchHeightIndex = SFM::max(0, ((int32_t)(DispatchHeights - (dispatch_volume_height >> 3u))) - 1);  // need *index* ie.) 128 >> 3 = 16, 120 >> 3 = 15, 112 >> 3 = 14 ... etc *note: order needs to be reversed - index 0 equals highest dispatch height, index n equals lowest dispatch height ... *note: also needs to select the closest maximum dispatch height

				vk::CommandBufferBeginInfo bi{};

				c.cb_render_light.begin(bi); VKU_SET_CMD_BUFFER_LABEL(c.cb_render_light, vkNames::CommandBuffer::COMPUTE_LIGHT);

				// grouping barriers as best as possible (down sample depth sets 2 aswell)
				// set pipeline barriers only once before ping pong begins
				{
					static constexpr size_t const image_count(2ULL); // batched
					std::array<vku::GenericImage* const, image_count> const images{ LightMap.DistanceDirection, LightMap.Color };

					vku::GenericImage::setLayoutFromUndefined<image_count>(images, c.cb_render_light, vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits::eComputeShader, vku::ACCESS_WRITEONLY);
				}

									/*{
						static constexpr size_t const image_count(1ULL); // batched
						std::array<vku::GenericImage* const, image_count> const images{ LightProbeMap.imageGPUIn[resource_index] };

						// [acquire image barrier]
						using afb = vk::AccessFlagBits;

						std::array<vk::ImageMemoryBarrier, image_count> imbs{};

						for (uint32_t i = 0; i < image_count; ++i) {

							imbs[i].srcQueueFamilyIndex = c.transferQueueFamilyIndex;  // (default) VK_QUEUE_FAMILY_IGNORED;
							imbs[i].dstQueueFamilyIndex = c.computeQueueFamilyIndex;  // (default) VK_QUEUE_FAMILY_IGNORED;
							imbs[i].oldLayout = vk::ImageLayout::eTransferDstOptimal;
							imbs[i].newLayout = vk::ImageLayout::eTransferDstOptimal; // layout change cannot be done here, this is queue ownership transfer only
							imbs[i].image = images[i]->image();
							imbs[i].subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };

							imbs[i].srcAccessMask = afb::eTransferWrite;
							imbs[i].dstAccessMask = afb::eNone;
						}

						using psfb = vk::PipelineStageFlagBits;

						vk::PipelineStageFlags const srcStageMask(psfb::eTransfer),
							dstStageMask(psfb::eTopOfPipe);

						c.cb_render_light.pipelineBarrier(srcStageMask, dstStageMask, vk::DependencyFlagBits::eByRegion, 0, nullptr, 0, nullptr, image_count, imbs.data());
						LightProbeMap.imageGPUIn[resource_index]->setCurrentLayout(vk::ImageLayout::eTransferDstOptimal);
					}*/

				{
					using psfb = vk::PipelineStageFlagBits;

					// Image layout transition barrier must be separate from the queue ownership transfer barrier above
					LightProbeMap.imageGPUIn[resource_index]->setLayout<false>(c.cb_render_light, vk::ImageLayout::eShaderReadOnlyOptimal, psfb::eTransfer, vku::ACCESS_WRITEONLY, psfb::eComputeShader, vku::ACCESS_READONLY);
				}

				[[likely]] if (!c.bypass)
				{
					// common descriptor set and pipline layout to SEED and JFA, seperate pipelines
					c.cb_render_light.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *render_data.light.pipelineLayout, 0, render_data.light.sets[resource_index], nullptr);

					// Seed and 1+JFA (error correction) note: 1+JFA is more accurate than JFA+1 by effectively resulting in a jump flood with far less errors.
					c.cb_render_light.bindPipeline(vk::PipelineBindPoint::eCompute, *render_data.light.pipeline[eComputeLightPipeline::SEED]);
					renderSeed(c, render_data, resource_index); // output alternates every frame

					//*bugfix - sync validation, solved by automation
					vku::memory_barrier(c.cb_render_light,
						vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
						vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead);

					// Jump flooding runs in a specfic series from seed (includes 1+JFA) to jumpflooding (JFA) to finally a Filter being the last step.
					uint32_t step(MAX_STEP_PINGPONG >> 1);

					// ( JFA ) //
					uint32_t uPing(resource_index), uPong(!resource_index); // this is always true constants for output of seed to JFA's first ping-pong input. 

					c.cb_render_light.bindPipeline(vk::PipelineBindPoint::eCompute, *render_data.light.pipeline[eComputeLightPipeline::JFA]);

					do
					{
						renderJFA(c, render_data, uPing, uPong, step);

						//*bugfix - sync validation
						vku::memory_barrier(c.cb_render_light,
							vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eComputeShader,
							vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead);

						std::swap(uPing, uPong);

						step >>= 1; // correct, halving rather than doubling is required - otherwise major JFA error count results
					} while (0 != step);

					// Last step, filtering - temporal super-sampling, blending & antialiasing
					c.cb_render_light.bindPipeline(vk::PipelineBindPoint::eCompute, *render_data.light.pipeline[eComputeLightPipeline::FILTER]);

					renderFilter(c, render_data, uPing); // pong has last ping (input), ping has last pong (output)

				} // !bypass

				{ 
					static constexpr size_t const image_count(1ULL); // batched
					std::array<vku::GenericImage* const, image_count> const images{ LightProbeMap.imageGPUIn[resource_index] };

					vku::GenericImage::setLayoutFromUndefined<image_count>(images, c.cb_render_light, vk::ImageLayout::eTransferDstOptimal, vk::PipelineStageFlagBits::eTransfer, vku::ACCESS_WRITEONLY);
					clear(c.cb_render_light, resource_index);
				}

				// [release image barrier]
				using afb = vk::AccessFlagBits;

				static constexpr size_t const image_count(2ULL); // batched
				std::array<vku::GenericImage* const, image_count> const images{ LightMap.DistanceDirection, LightMap.Color };
				std::array<vk::ImageMemoryBarrier, image_count> imbs{};

				for (uint32_t i = 0; i < image_count; ++i) {

					imbs[i].srcQueueFamilyIndex = c.computeQueueFamilyIndex;  // (default) VK_QUEUE_FAMILY_IGNORED;
					imbs[i].dstQueueFamilyIndex = c.graphicsQueueFamilyIndex;  // (default) VK_QUEUE_FAMILY_IGNORED;
					imbs[i].oldLayout = vk::ImageLayout::eGeneral;
					imbs[i].newLayout = vk::ImageLayout::eGeneral; // layout change cannot be done here, this is queue ownership transfer only
					imbs[i].image = images[i]->image();
					imbs[i].subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };

					imbs[i].srcAccessMask = afb::eShaderWrite;
					imbs[i].dstAccessMask = (afb)0;
				}

				using psfb = vk::PipelineStageFlagBits;

				vk::PipelineStageFlags const srcStageMask(psfb::eComputeShader),
											 dstStageMask(psfb::eTopOfPipe);

				c.cb_render_light.pipelineBarrier(srcStageMask, dstStageMask, vk::DependencyFlagBits::eByRegion, 0, nullptr, 0, nullptr, image_count, imbs.data());

				c.cb_render_light.end();

				bRecorded[resource_index] = !c.bypass; // these compute buffers need only be recorded once, then re-used for every frame
			}
			
			// Just update layouts that will be current after this compute cb is done.
			LightProbeMap.imageGPUIn[resource_index]->setCurrentLayout(vk::ImageLayout::eTransferDstOptimal);
			LightMap.DistanceDirection->setCurrentLayout(vk::ImageLayout::eGeneral);
			LightMap.Color->setCurrentLayout(vk::ImageLayout::eGeneral);
		}

		return(true); // always
	}

	// returns true when "dirty" status should be set, so that compute shader knows it needs to re-record command buffers to adjust dispatch buffer selection change which adapts to the volume height (work reduction).
	template< uint32_t Size >
	__inline bool const volumetricOpacity<Size>::upload_light(uint32_t const resource_index, vk::CommandBuffer& __restrict cb, uint32_t const transferQueueFamilyIndex, uint32_t const computeQueueFamilyIndex, uint32_t& __restrict dispatch_volume_height) {

		// optimizations removed due to unsynchronized behaviour of needing 2 frames to process the clear, then an upload of the volumetric area currently occupied by lights.
		// this de-synchronizes the light positions by lagging behind. then the lights do not occur at the position they should be at for this frame messing with the fractional offset differences between the world and the light.
		
		// instead - brute force update of volume occurs every frame (currently 128x128x128 ~33MB to upload every frame (<2ms), **note if 256x256x256 instead the upload size is ~268MB per frame which is dog slow (>21ms))
		constinit static bool bRecorded[2]{ false, false }; // these are synchronized 
		constinit static uvec4_v last_max, last_min;

		uvec4_v const current_max(MappedVoxelLights.getCurrentVolumeExtentsMax()), 
			          current_min(MappedVoxelLights.getCurrentVolumeExtentsMin()); // adapt to current volume extents containing lights [optimization]

		if (uvec4_v::any<3>(current_max != last_max)) {

			last_max = current_max; // always updated to latest
			bRecorded[resource_index] = false; // require cb to be updated
		} // if equal re-run the last recorded cb. 


		if (uvec4_v::any<3>(current_min != last_min)) {   

			last_min = current_min; // always updated to latest
			bRecorded[resource_index] = false; // require cb to be updated
		} // if equal re-run the last recorded cb

		// UPLOAD image already transitioned to transfer dest layout at this point
		// ################################## //
		// Light Probe Map 3D Texture Upload  //
		// ################################## //

		if (!bRecorded[resource_index]) {

			// to scalars for rounding to multiple of 8
			uvec4_t extents_max, extents_min;
			current_max.xyzw(extents_max);
			current_min.xyzw(extents_min);

			// rounding down (min)
			extents_min.x = SFM::roundToMultipleOf<false>(extents_min.x, 8);
			extents_min.y = SFM::roundToMultipleOf<false>(extents_min.y, 8);
			extents_min.z = SFM::roundToMultipleOf<false>(extents_min.z, 8);

			// rounding up (max)
			extents_max.x = SFM::roundToMultipleOf<true>(extents_max.x, 8);
			extents_max.y = SFM::roundToMultipleOf<true>(extents_max.y, 8);
			extents_max.z = SFM::roundToMultipleOf<true>(extents_max.z, 8);

			// only change the dispatch height when recording new upload light command buffer, otherwise it remains unchanged
			dispatch_volume_height = extents_max.y;

			vk::CommandBufferBeginInfo bi{}; // recorded once only
			cb.begin(bi); VKU_SET_CMD_BUFFER_LABEL(cb, vkNames::CommandBuffer::TRANSFER_LIGHT);

			vk::BufferImageCopy buffer_region{};

			// 
			// slices ordered by Z 
			// (z * xMax * yMax) + (y * xMax) + x;

			// slices ordered by Y: <---- USING Y
			// (y * xMax * zMax) + (z * xMax) + x;
			buffer_region.bufferOffset = ((extents_min.y * (LightSize) * (LightSize)) + (extents_min.z * (LightSize)) + extents_min.x) * MappedVoxelLights.element_size();
			buffer_region.bufferOffset = SFM::roundToMultipleOf<false>((int32_t)buffer_region.bufferOffset, 8); // rounding down (effectively min)
			buffer_region.bufferRowLength = LightSize;
			buffer_region.bufferImageHeight = LightSize;

			// swizzle to xzy
			buffer_region.imageOffset.x = extents_min.x;
			buffer_region.imageOffset.z = extents_min.y; // Y Axis Major (Slices ordered by Y)
			buffer_region.imageOffset.y = extents_min.z;
			//  ""  ""  "  ""
			buffer_region.imageExtent.width = extents_max.x - extents_min.x;
			buffer_region.imageExtent.depth = extents_max.y - extents_min.y; // Y Axis Major (Slices ordered by Y)
			buffer_region.imageExtent.height = extents_max.z - extents_min.z;

			buffer_region.imageSubresource = { vk::ImageAspectFlagBits::eColor, 0, 0, 1 }; // for buffer copy

			// image should already be transitioned to eTransferDstOptimal 
			cb.copyBufferToImage(LightProbeMap.stagingBuffer[resource_index].buffer(), LightProbeMap.imageGPUIn[resource_index]->image(), vk::ImageLayout::eTransferDstOptimal, buffer_region); // copy the light probe map (cpu->gpu)
			/*
			{
				static constexpr size_t const image_count(1ULL); // batched
				std::array<vku::GenericImage* const, image_count> const images{ LightProbeMap.imageGPUIn[resource_index] };

				// [release image barrier]
				using afb = vk::AccessFlagBits;

				std::array<vk::ImageMemoryBarrier, image_count> imbs{};

				for (uint32_t i = 0; i < image_count; ++i) {

					imbs[i].srcQueueFamilyIndex = transferQueueFamilyIndex;  // (default) VK_QUEUE_FAMILY_IGNORED;
					imbs[i].dstQueueFamilyIndex = computeQueueFamilyIndex;  // (default) VK_QUEUE_FAMILY_IGNORED;
					imbs[i].oldLayout = vk::ImageLayout::eTransferDstOptimal;
					imbs[i].newLayout = vk::ImageLayout::eTransferDstOptimal; // layout change cannot be done here, this is queue ownership transfer only
					imbs[i].image = images[i]->image();
					imbs[i].subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };

					imbs[i].srcAccessMask = afb::eTransferWrite;
					imbs[i].dstAccessMask = afb::eNone;
				}

				using psfb = vk::PipelineStageFlagBits;

				vk::PipelineStageFlags const srcStageMask(psfb::eTransfer),
					                         dstStageMask(psfb::eTopOfPipe);

				cb.pipelineBarrier(srcStageMask, dstStageMask, vk::DependencyFlagBits::eByRegion, 0, nullptr, 0, nullptr, image_count, imbs.data());
			}
			*/
			cb.end();
			bRecorded[resource_index] = true; // always set to true so that command buffer recording is not necessary next frame, it can be re-used

			return(false); // indicate compute needs to run and re-record command buffer
		}
		
		return(true); // indicate compute needs to run, but does not need to re-record
	}


} // end ns