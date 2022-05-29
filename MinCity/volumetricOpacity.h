#pragma once
#include "cVulkan.h"

#include "lightBuffer3D.h"

#include <vku/vku_addon.hpp>
#include "IsoCamera.h"
#include <Math/v2_rotation_t.h>
#include <Utility/scalable_aligned_allocator.h>
#include "voxelAlloc.h"

namespace Volumetric {
	extern bool const isGraduallyStartingUp();
} // end ns

namespace Volumetric
{
	BETTER_ENUM(ePingPongMap, uint32_t const,
		PING = 0U,
		PONG = 1U
	);

	struct voxelTexture3D {

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

		vku::TextureImageStorage3D
			* __restrict Reflection;

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

	template< uint32_t const Size > // "uniform world volume size"
	class alignas(16) volumetricOpacity
	{
	public:
		static constexpr uint32_t const TEMPORAL_VOLUMES = 2;	// last n frames
	public:
		static constexpr uint32_t const // "non-uniform light volume size"
			LightWidth = (Size >> ComputeLightConstants::LIGHT_MOD_WIDTH_BITS),
			LightHeight = (Size >> ComputeLightConstants::LIGHT_MOD_HEIGHT_BITS),
			LightDepth = (Size >> ComputeLightConstants::LIGHT_MOD_DEPTH_BITS);

	private:
		using lightVolume = lightBuffer3D<ComputeLightConstants::memLayoutV, LightWidth, LightHeight, LightDepth, Size>;

		static constexpr uint32_t const PING = ePingPongMap::PING, PONG = ePingPongMap::PONG;
		static constexpr uint32_t const getStepMax() {
			uint32_t step(1);
			while (((step << 1) < LightWidth) | ((step << 1) < LightHeight) | ((step << 1) < LightDepth)) {
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
			//return((ePingPongMap::PING == uPingPong ? ePingPongMap::PONG : ePingPongMap::PING));
			return((ePingPongMap::PING == uPingPong ? ePingPongMap::PING : ePingPongMap::PONG)); // ** extra step "filter" pipeline
		}

		static constexpr uint32_t const
			MAX_STEP_PINGPONG = getStepMax(),
			STEP_COUNT = getStepCount(),
			PING_PONG_CHAIN_LAST_INDEX = getPingPongChainLastIndex();

	public:
		static inline constexpr uint32_t const		getSize() { return(Size); }
		static inline constexpr float const			getInvSize() { return(1.0f/((float)Size)); }
		static inline float const					getVolumeLength() { return(VolumeLength); }
		static inline float const					getInvVolumeLength() { return(InvVolumeLength); }

		static inline constexpr uint32_t const getLightWidth() { return(LightWidth); }
		static inline constexpr uint32_t const getLightHeight() { return(LightHeight); }
		static inline constexpr uint32_t const getLightDepth() { return(LightDepth); }
		static inline constexpr size_t const   getLightProbeMapSizeInBytes() { return(LightWidth * LightHeight * LightDepth * ComputeLightConstants::NUM_BYTES_PER_VOXEL_LIGHT); }

	public:
		// Accessor ///
		voxelVolumeSet const& __restrict													getVolumeSet() const { return(VolumeSet); }
		__inline lightVolume const& __restrict												getMappedVoxelLights() const { return(MappedVoxelLights); }

		// Mutators //
		__inline void __vectorcall pushViewMatrix(FXMMATRIX xmView) {
			// only need mat3 for purposes of compute shader transforming light direction vector
			XMStoreFloat4x4(&PushConstants.view, xmView);
		}

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
		void commit(uint32_t const resource_index, size_t const hw_concurrency) const { // this method will quickly [map, copy, unmap] to gpu write-combined stagingBuffer, no reads of this buffer of any kind
														   // private write-combined memory copy - no *reading* *warning* *severe* *performance degradation if one reads from a write-combined gpu buffer*
			const_cast<volumetricOpacity<Size>* __restrict>(this)->MappedVoxelLights.commit(LightProbeMap.stagingBuffer[resource_index], hw_concurrency);
		}

		void clear() const { // happens before a comitt, does not require stagingbuffer to clear, all safe memory

			const_cast<volumetricOpacity<Size>* __restrict>(this)->MappedVoxelLights.clear();
		}

		void release() {

			for (uint32_t i = 0; i < vku::double_buffer<uint32_t>::count; ++i) {
				LightProbeMap.stagingBuffer[i].release();
				SAFE_RELEASE_DELETE(LightProbeMap.imageGPUIn[i]);
			}

			SAFE_RELEASE_DELETE(ComputeLightDispatchBuffer);

			for (uint32_t i = 0; i < 2; ++i) {
				SAFE_RELEASE_DELETE(PingPongMap[i]);
			}

			VolumeSet.LightMap = nullptr;
			for (uint32_t i = 0; i < TEMPORAL_VOLUMES; ++i) {
				SAFE_RELEASE_DELETE(LightMapHistory[i].DistanceDirection);
				SAFE_RELEASE_DELETE(LightMapHistory[i].Color);
				SAFE_RELEASE_DELETE(LightMapHistory[i].Reflection);
			}
			SAFE_RELEASE_DELETE(LightMap.DistanceDirection);
			SAFE_RELEASE_DELETE(LightMap.Color);
			SAFE_RELEASE_DELETE(LightMap.Reflection);

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
			{ // light compute dispatch

				vk::DispatchIndirectCommand const dispatchCommand{

					(LightWidth >> ComputeLightConstants::SHADER_LOCAL_SIZE_BITS) + (0U == (LightWidth % ComputeLightConstants::SHADER_LOCAL_SIZE) ? 0U : 1U), // local size x = 8
					(LightDepth >> ComputeLightConstants::SHADER_LOCAL_SIZE_BITS) + (0U == (LightDepth % ComputeLightConstants::SHADER_LOCAL_SIZE) ? 0U : 1U), // local size y = 8
					(LightHeight >> ComputeLightConstants::SHADER_LOCAL_SIZE_BITS_Z) + (0U == (LightHeight % ComputeLightConstants::SHADER_LOCAL_SIZE_Z) ? 0U : 1U) // local size z = 1 to 4 - this is currently manually set based on the above 2 dimensions
				};

				ComputeLightDispatchBuffer = new vku::IndirectBuffer(sizeof(dispatchCommand), true);

				ComputeLightDispatchBuffer->upload(device, commandPool, queue, dispatchCommand);
			}
		}

	public:
		void create(vk::Device const& __restrict device, vk::CommandPool const& __restrict commandPool, vk::Queue const& __restrict queue, point2D_t const frameBufferSize) {

			createIndirectDispatch(device, commandPool, queue);

			for (uint32_t resource_index = 0; resource_index < vku::double_buffer<uint32_t>::count; ++resource_index) {

				LightProbeMap.stagingBuffer[resource_index].createAsCPUToGPUBuffer(getLightProbeMapSizeInBytes(), true, true);

				LightProbeMap.imageGPUIn[resource_index] = new vku::TextureImage3D(vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst, device,
					LightWidth, LightDepth, LightHeight, 1U, vk::Format::eR32G32B32A32Sfloat, false, true);

				VKU_SET_OBJECT_NAME(vk::ObjectType::eImage, (VkImage)LightProbeMap.imageGPUIn[resource_index]->image(), vkNames::Image::LightProbeMap);
			}
			
			for (uint32_t i = 0; i < 2; ++i) {
				PingPongMap[i] = new vku::TextureImageStorage3D(vk::ImageUsageFlagBits::eSampled, device,
					LightWidth, LightDepth, LightHeight, 1U, vk::Format::eR32G32B32A32Sfloat, false, true);

				VKU_SET_OBJECT_NAME(vk::ObjectType::eImage, (VkImage)PingPongMap[i]->image(), vkNames::Image::PingPongMap);
			}

			LightMap.DistanceDirection = new vku::TextureImageStorage3D(vk::ImageUsageFlagBits::eSampled, device,
				LightWidth, LightDepth, LightHeight, 1U, vk::Format::eR16G16B16A16Snorm, false, true); // only signed normalized values
			LightMap.Color = new vku::TextureImageStorage3D(vk::ImageUsageFlagBits::eSampled, device,
				LightWidth, LightDepth, LightHeight, 1U, vk::Format::eR16G16B16A16Sfloat, false, true);
			LightMap.Reflection = new vku::TextureImageStorage3D(vk::ImageUsageFlagBits::eSampled, device,
				LightWidth, LightDepth, LightHeight, 1U, vk::Format::eR8G8B8A8Unorm, false, true);
			VolumeSet.LightMap = &LightMap;

			VKU_SET_OBJECT_NAME(vk::ObjectType::eImage, (VkImage)LightMap.DistanceDirection->image(), vkNames::Image::LightMap_DistanceDirection);
			VKU_SET_OBJECT_NAME(vk::ObjectType::eImage, (VkImage)LightMap.Color->image(), vkNames::Image::LightMap_Color);
			VKU_SET_OBJECT_NAME(vk::ObjectType::eImage, (VkImage)LightMap.Reflection->image(), vkNames::Image::LightMap_Reflection);

			for (uint32_t i = 0; i < TEMPORAL_VOLUMES; ++i) {
				LightMapHistory[i].DistanceDirection = new vku::TextureImageStorage3D(vk::ImageUsageFlagBits::eSampled, device,
					LightWidth, LightDepth, LightHeight, 1U, vk::Format::eR16G16B16A16Snorm, false, false); // only signed normalized values
				LightMapHistory[i].Color = new vku::TextureImageStorage3D(vk::ImageUsageFlagBits::eSampled, device,
					LightWidth, LightDepth, LightHeight, 1U, vk::Format::eR8G8B8A8Unorm, false, false);
				LightMapHistory[i].Reflection = new vku::TextureImageStorage3D(vk::ImageUsageFlagBits::eSampled, device,
					LightWidth, LightDepth, LightHeight, 1U, vk::Format::eR8G8B8A8Unorm, false, false);

				VKU_SET_OBJECT_NAME(vk::ObjectType::eImage, (VkImage)LightMapHistory[i].DistanceDirection->image(), vkNames::Image::LightMapHistory_DistanceDirection);
				VKU_SET_OBJECT_NAME(vk::ObjectType::eImage, (VkImage)LightMapHistory[i].Color->image(), vkNames::Image::LightMapHistory_Color);
				VKU_SET_OBJECT_NAME(vk::ObjectType::eImage, (VkImage)LightMapHistory[i].Reflection->image(), vkNames::Image::LightMapHistory_Reflection);
			}

			OpacityMap = new vku::TextureImageStorage3D(vk::ImageUsageFlagBits::eSampled, device,
				Size, Size, Size, 1U, vk::Format::eR8Unorm, false, true);
			VolumeSet.OpacityMap = OpacityMap;

			VKU_SET_OBJECT_NAME(vk::ObjectType::eImage, (VkImage)OpacityMap->image(), vkNames::Image::OpacityMap);

			FMT_LOG(TEX_LOG, "LightProbe Volumetric data: {:n} bytes", LightProbeMap.imageGPUIn[0]->size() + LightProbeMap.imageGPUIn[1]->size());
			FMT_LOG(TEX_LOG, "Lightmap [GPU Resident Only] Volumetric data: {:n} bytes", PingPongMap[0]->size() + PingPongMap[1]->size() + LightMap.DistanceDirection->size() + LightMap.Color->size() + LightMap.Reflection->size() + (LightMapHistory[0].DistanceDirection->size() + LightMapHistory[0].Color->size() + LightMapHistory[0].Reflection->size()) * TEMPORAL_VOLUMES);
			FMT_LOG(TEX_LOG, "Opacitymap [GPU Resident Only] Volumetric data: {:n} bytes", OpacityMap->size());

#ifdef DEBUG_LIGHT_PROPAGATION
			DebugDevice = &device;
			DebugMinMaxBuffer = new vku::HostStorageBuffer(DebugMinMax);
			DebugTexture = new vku::TextureImageStorage2D(vk::ImageUsageFlagBits::eSampled, device, LightWidth, LightDepth, 1, vk::Format::eR8G8B8A8Unorm);
			resetMinMax();
#endif

			vku::executeImmediately(device, commandPool, queue, [&](vk::CommandBuffer cb) {
				LightProbeMap.imageGPUIn[0]->setLayout(cb, vk::ImageLayout::eTransferDstOptimal);  // required initial state
				LightProbeMap.imageGPUIn[1]->setLayout(cb, vk::ImageLayout::eTransferDstOptimal);  // required initial state
				VolumeSet.OpacityMap->setLayout(cb, vk::ImageLayout::eGeneral);					   //    ""      ""      ""

				LightMap.DistanceDirection->setLayoutCompute(cb, vku::ACCESS_WRITEONLY);		// the final oututs are never "read" in comute shaders
				LightMap.Color->setLayoutCompute(cb, vku::ACCESS_WRITEONLY);					// *only* read by fragment shaders
				LightMap.Reflection->setLayoutCompute(cb, vku::ACCESS_WRITEONLY);				// *only* read by fragment shaders

				PingPongMap[0]->setLayoutCompute(cb, vku::ACCESS_READWRITE);		// never changes
				PingPongMap[1]->setLayoutCompute(cb, vku::ACCESS_READWRITE);		// never changes

				for (uint32_t i = 0; i < TEMPORAL_VOLUMES; ++i) {
					LightMapHistory[i].DistanceDirection->setLayoutCompute(cb, vku::ACCESS_READWRITE);			// never changes
					LightMapHistory[i].Color->setLayoutCompute(cb, vku::ACCESS_READWRITE);						// never changes
					LightMapHistory[i].Reflection->setLayoutCompute(cb, vku::ACCESS_READWRITE);					// never changes
				}

				});
		}

		void SetSpecializationConstants_ComputeLight(std::vector<vku::SpecializationConstant>& __restrict constants)
		{
			// full world volume dimensions //
			constants.emplace_back(vku::SpecializationConstant(0, (float)Size)); // should be world volume uniform size (width=height=depth)
			constants.emplace_back(vku::SpecializationConstant(1, (float)VolumeLength)); // should be world volume length (diagonal from min to max of volume extents)
			constants.emplace_back(vku::SpecializationConstant(2, (float)InvVolumeLength)); // should be inverse world volume length

			// light volume dimensions //
			constants.emplace_back(vku::SpecializationConstant(3, (float)LightWidth)); // should be width
			constants.emplace_back(vku::SpecializationConstant(4, (float)LightDepth)); // should be depth
			constants.emplace_back(vku::SpecializationConstant(5, (float)LightHeight)); // should be height
			constants.emplace_back(vku::SpecializationConstant(6, 1.0f / (float)LightWidth)); // should be inv width
			constants.emplace_back(vku::SpecializationConstant(7, 1.0f / (float)LightDepth)); // should be inv depth
			constants.emplace_back(vku::SpecializationConstant(8, 1.0f / (float)LightHeight)); // should be inv height

			constants.emplace_back(vku::SpecializationConstant(9, (float)Iso::MINI_VOX_SIZE*0.5f)); // should be minivoxsize * 0.5f (0.5f for only compute shader provides better smoother light propogation)
		}

		void UpdateDescriptorSet_ComputeLight(vku::DescriptorSetUpdater& __restrict dsu, vk::Sampler const& __restrict samplerLinearBorder) const
		{
			dsu.beginImages(0U, 0, vk::DescriptorType::eCombinedImageSampler);
			dsu.image(samplerLinearBorder, LightProbeMap.imageGPUIn[0]->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal);
			dsu.beginImages(0U, 1, vk::DescriptorType::eCombinedImageSampler);
			dsu.image(samplerLinearBorder, LightProbeMap.imageGPUIn[1]->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal);

			dsu.beginImages(1U, 0, vk::DescriptorType::eCombinedImageSampler);
			dsu.image(samplerLinearBorder, PingPongMap[0]->imageView(), vk::ImageLayout::eGeneral);
			dsu.beginImages(1U, 1, vk::DescriptorType::eCombinedImageSampler);
			dsu.image(samplerLinearBorder, PingPongMap[1]->imageView(), vk::ImageLayout::eGeneral);

			dsu.beginImages(2U, 0, vk::DescriptorType::eStorageImage);
			dsu.image(nullptr, PingPongMap[0]->imageView(), vk::ImageLayout::eGeneral);
			dsu.beginImages(2U, 1, vk::DescriptorType::eStorageImage);
			dsu.image(nullptr, PingPongMap[1]->imageView(), vk::ImageLayout::eGeneral);

			/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

			dsu.beginImages(3U, 0, vk::DescriptorType::eCombinedImageSampler);
			dsu.image(samplerLinearBorder, LightMapHistory[0].DistanceDirection->imageView(), vk::ImageLayout::eGeneral);
			dsu.beginImages(3U, 1, vk::DescriptorType::eCombinedImageSampler);
			dsu.image(samplerLinearBorder, LightMapHistory[1].DistanceDirection->imageView(), vk::ImageLayout::eGeneral);

			dsu.beginImages(4U, 0, vk::DescriptorType::eStorageImage);
			dsu.image(nullptr, LightMapHistory[0].DistanceDirection->imageView(), vk::ImageLayout::eGeneral);
			dsu.beginImages(4U, 1, vk::DescriptorType::eStorageImage);
			dsu.image(nullptr, LightMapHistory[1].DistanceDirection->imageView(), vk::ImageLayout::eGeneral);
			dsu.beginImages(4U, 2, vk::DescriptorType::eStorageImage);
			dsu.image(nullptr, LightMap.DistanceDirection->imageView(), vk::ImageLayout::eGeneral);

			dsu.beginImages(5U, 0, vk::DescriptorType::eCombinedImageSampler);
			dsu.image(samplerLinearBorder, LightMapHistory[0].Color->imageView(), vk::ImageLayout::eGeneral);
			dsu.beginImages(5U, 1, vk::DescriptorType::eCombinedImageSampler);
			dsu.image(samplerLinearBorder, LightMapHistory[1].Color->imageView(), vk::ImageLayout::eGeneral);

			dsu.beginImages(6U, 0, vk::DescriptorType::eStorageImage);
			dsu.image(nullptr, LightMapHistory[0].Color->imageView(), vk::ImageLayout::eGeneral);
			dsu.beginImages(6U, 1, vk::DescriptorType::eStorageImage);
			dsu.image(nullptr, LightMapHistory[1].Color->imageView(), vk::ImageLayout::eGeneral);
			
			dsu.beginImages(7U, 0, vk::DescriptorType::eStorageImage);
			dsu.image(nullptr, LightMap.Color->imageView(), vk::ImageLayout::eGeneral);

			dsu.beginImages(8U, 0, vk::DescriptorType::eCombinedImageSampler);
			dsu.image(samplerLinearBorder, LightMapHistory[0].Reflection->imageView(), vk::ImageLayout::eGeneral);
			dsu.beginImages(8U, 1, vk::DescriptorType::eCombinedImageSampler);
			dsu.image(samplerLinearBorder, LightMapHistory[1].Reflection->imageView(), vk::ImageLayout::eGeneral);

			dsu.beginImages(9U, 0, vk::DescriptorType::eStorageImage);
			dsu.image(nullptr, LightMapHistory[0].Reflection->imageView(), vk::ImageLayout::eGeneral);
			dsu.beginImages(9U, 1, vk::DescriptorType::eStorageImage);
			dsu.image(nullptr, LightMapHistory[1].Reflection->imageView(), vk::ImageLayout::eGeneral);
			dsu.beginImages(9U, 2, vk::DescriptorType::eStorageImage);
			dsu.image(nullptr, LightMap.Reflection->imageView(), vk::ImageLayout::eGeneral);
		}

		__inline bool const renderCompute(vku::compute_pass&& __restrict  c, struct cVulkan::sCOMPUTEDATA const& __restrict render_data);

	private:
		__inline bool const upload_light(uint32_t const resource_index, vk::CommandBuffer& __restrict cb);  // returns true when "dirty" status should be set, so that compute shader knows it needs to run

		__inline void renderSeed(vku::compute_pass const& __restrict  c, struct cVulkan::sCOMPUTEDATA const& __restrict render_data, uint32_t const index_input);

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
		lightVolume											MappedVoxelLights;
		voxelTexture3D										LightProbeMap;

		vku::IndirectBuffer* __restrict						ComputeLightDispatchBuffer;
		vku::TextureImageStorage3D*							PingPongMap[2];
		voxelLightmapSet									LightMap; // final output
		voxelLightmapSet									LightMapHistory[TEMPORAL_VOLUMES];
		vku::TextureImageStorage3D*							OpacityMap;
		voxelVolumeSet										VolumeSet;

		static inline float const							VolumeLength = (std::hypot(float(Size), float(Size), float(Size))),
															InvVolumeLength = (1.0f / VolumeLength);

		int32_t												ClearStage;

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
			ComputeLightDispatchBuffer{ nullptr }, LightMap{ nullptr }, LightMapHistory{ nullptr }, OpacityMap{ nullptr },
			VolumeSet{},
			ClearStage(0)

#ifdef DEBUG_LIGHT_PROPAGATION
			, DebugDevice(nullptr), DebugMinMaxBuffer(nullptr), DebugMinMax{ { 99999.0f, 99999.0f, 99999.0f }, { -99999.0f, -99999.0f, -99999.0f } }, DebugTexture(nullptr), DebugSlice(0)
#endif
		{
			// REQUIRED initial state - *do not change or remove*
			PushConstants.step = 0;			 // zero
			PushConstants.index_input = 0;	 // zero
			PushConstants.index_output = 1;  // index_output != index_input  **important
			PushConstants.index_filter = 0;  // independent
			
			XMStoreFloat4x4(&PushConstants.view, XMMatrixIdentity());
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
	__inline void volumetricOpacity<Size>::renderSeed(vku::compute_pass const& __restrict  c, struct cVulkan::sCOMPUTEDATA const& __restrict render_data, uint32_t const index_input)
	{
		PushConstants.step = 0; 
		PushConstants.index_output = !index_input;
		PushConstants.index_input = index_input; // seed input alternates

#ifndef NDEBUG
#ifdef DEBUG_ASSERT_JFA_SEED_INDICES_OK // good validation, state is setup at runtime so this is a good dynamic test.
		assert_print(PushConstants.index_output != PushConstants.index_input, "[fail] jfa seed indices equal! [fail]")
#endif
#endif

		c.cb_render_light.pushConstants(*render_data.light.pipelineLayout, vk::ShaderStageFlagBits::eCompute,
			(uint32_t)0U, (uint32_t)sizeof(UniformDecl::ComputeLightPushConstantsJFA), reinterpret_cast<void const* const>(&PushConstants));

		c.cb_render_light.dispatchIndirect(ComputeLightDispatchBuffer->buffer(), 0);
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
			(uint32_t)0U, (uint32_t)sizeof(UniformDecl::ComputeLightPushConstantsJFA), reinterpret_cast<void const* const>(&PushConstants));

		c.cb_render_light.dispatchIndirect(ComputeLightDispatchBuffer->buffer(), 0);
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
		PushConstants.index_output = index_input; // this is not used in the shader @ this stage. however, it is important that this is still updated to maintain the current state of PushConstants.
		PushConstants.index_input = index_input;  // this is the input to filter stage that is the output of the jfa stage.                                                                          
		
		constexpr size_t const begin(offsetof(UniformDecl::ComputeLightPushConstants, index_input));

		c.cb_render_light.pushConstants(*render_data.light.pipelineLayout, vk::ShaderStageFlagBits::eCompute,
			(uint32_t)0U,
			(uint32_t)sizeof(UniformDecl::ComputeLightPushConstantsOverlap) + (uint32_t)begin, reinterpret_cast<void const* const>(&PushConstants));

		c.cb_render_light.dispatchIndirect(ComputeLightDispatchBuffer->buffer(), 0);
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
		if (c.cb_transfer_light)
		{
			return(upload_light(c.resource_index, c.cb_transfer_light));  // returns current dirty state
		}
		else if (c.cb_render_light) { 

			constinit static bool bRecorded[2]{ false, false }; // these are synchronized 

			uint32_t const resource_index(c.resource_index);

			// Update Memory for Push Constants
			PushConstants.index_filter = !PushConstants.index_filter;			// alternate the index for the very last stage

			// Record Compute Command buffer if not flagged as already recorded. 
			if (!bRecorded[resource_index]) {

				vk::CommandBufferBeginInfo bi{};

				c.cb_render_light.begin(bi); VKU_SET_CMD_BUFFER_LABEL(c.cb_render_light, vkNames::CommandBuffer::COMPUTE_LIGHT);

				// grouping barriers as best as possible (down sample depth sets 2 aswell)
				// set pipeline barriers only once before ping pong begins

				{
					static constexpr size_t const image_count(3ULL); // batched
					std::array<vku::GenericImage* const, image_count> const images{ LightMap.DistanceDirection, LightMap.Color, LightMap.Reflection };

					vku::GenericImage::setLayoutFromUndefined<image_count>(images, c.cb_render_light, vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits::eComputeShader, vku::ACCESS_WRITEONLY);
				}

				{ // bugfix: Must be set for both buffers, validation error.
					static constexpr size_t const image_count(2ULL); // batched
					std::array<vku::GenericImage* const, image_count> const images{ LightProbeMap.imageGPUIn[0], LightProbeMap.imageGPUIn[1] };

					vku::GenericImage::setLayout<image_count>(images, c.cb_render_light, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits::eTransfer, vku::ACCESS_WRITEONLY, vk::PipelineStageFlagBits::eComputeShader, vku::ACCESS_READONLY);
				}

				// common descriptor set and pipline layout to SEED and JFA, seperate pipelines
				c.cb_render_light.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *render_data.light.pipelineLayout, 0, render_data.light.sets[0], nullptr);

				// Jump flooding runs in a specfic series from seed (1 + JFA) to jumpflooding (JFA) to finally (JFA + 1) being the last step.
				
				// (1 + JFA)
				c.cb_render_light.bindPipeline(vk::PipelineBindPoint::eCompute, *render_data.light.pipeline[eComputeLightPipeline::SEED]);
				renderSeed(c, render_data, resource_index); // input flips with resource_index

				// ( JFA ) //
				uint32_t uPing(!resource_index), uPong(resource_index); // this is always true constants for output of seed to JFA's first ping-pong input. 

				c.cb_render_light.bindPipeline(vk::PipelineBindPoint::eCompute, *render_data.light.pipeline[eComputeLightPipeline::JFA]);

				uint32_t step(MAX_STEP_PINGPONG >> 1);
				do
				{
					renderJFA(c, render_data, uPing, uPong, step);

					std::swap(uPing, uPong);

					step >>= 1;
				} while (0 != step);

				// (JFA + 1)
				renderJFA(c, render_data, uPing, uPong, 1);

				// Last step, filtering - temporal super-sampling, blending & antialiasing
				c.cb_render_light.bindPipeline(vk::PipelineBindPoint::eCompute, *render_data.light.pipeline[eComputeLightPipeline::FILTER]);

				renderFilter(c, render_data, uPong);

				{ // bugfix: Must be set for both buffers, validation error.
					static constexpr size_t const image_count(2ULL); // batched
					std::array<vku::GenericImage* const, image_count> const images{ LightProbeMap.imageGPUIn[0], LightProbeMap.imageGPUIn[1] };

					vku::GenericImage::setLayout<image_count>(images, c.cb_render_light, vk::ImageLayout::eTransferDstOptimal, vk::PipelineStageFlagBits::eComputeShader, vku::ACCESS_READONLY, vk::PipelineStageFlagBits::eTransfer, vku::ACCESS_WRITEONLY);
				}
				c.cb_render_light.end();

				bRecorded[resource_index] = true;
			}
			
			// Just update layouts that will be current after this compute cb is done.
			LightProbeMap.imageGPUIn[0]->setCurrentLayout(vk::ImageLayout::eTransferDstOptimal);
			LightProbeMap.imageGPUIn[1]->setCurrentLayout(vk::ImageLayout::eTransferDstOptimal);
			LightMap.DistanceDirection->setCurrentLayout(vk::ImageLayout::eGeneral);
			LightMap.Color->setCurrentLayout(vk::ImageLayout::eGeneral);
			LightMap.Reflection->setCurrentLayout(vk::ImageLayout::eGeneral);
			
		}

		return(false); // returns no longer dirty by default, resetting dirty state after compute cb is "scheduled"
	}

	// returns true when "dirty" status should be set, so that compute shader knows it needs to run
	template< uint32_t Size >
	__inline bool const volumetricOpacity<Size>::upload_light(uint32_t const resource_index, vk::CommandBuffer& __restrict cb) {

		constinit static bool bRecorded[2]{ false, false }; // these are synchronized 

		// cheap loads reused, must be static - *bugfix: per resource state required
		constinit static uvec4_t new_max_extents[2]{}, new_min_extents[2]{};		// these extents are before rounding to gpu memory granularity (pre-check)

		[[maybe_unused]] uvec4_v clear_max_max, clear_min_min;

		// at this point, these are the new extents only if they have changed
		uvec4_v current_max(MappedVoxelLights.getCurrentVolumeExtentsMax()), current_min(MappedVoxelLights.getCurrentVolumeExtentsMin());
		
		__m128i const xmZero(_mm_setzero_si128()), xmLimit(MappedVoxelLights.getVolumeExtentsLimit().v);

		{ // pad by one         // no modifications to .w component
 			__m128i const xmOne(uvec4_v(1, 0).v);

			current_max.v = _mm_add_epi32(current_max.v, xmOne);
			current_max.v = SFM::clamp(current_max.v, xmZero, xmLimit);
			current_min.v = _mm_sub_epi32(current_min.v, xmOne);
			current_min.v = SFM::clamp(current_min.v, xmZero, xmLimit);
		}

		// at this point, these compare the currently used extents, and the new extents that may be pending
		{
			uvec4_v const last_max(new_max_extents[resource_index]), last_min(new_min_extents[resource_index]);
			if (uvec4_v::any<3>(current_max != last_max) ||
				uvec4_v::any<3>(current_min != last_min)) {

				if (_mm_testz_si128(current_max.v, current_max.v)) {
					return(false); // maximum is zero, skip upload or changes to any state
				}

				// ClearStage 0 
				// for the change in dimensions of the light volume extents.
				// a "union" which is the largest (max) volume that comprises the area's of the current and new volume extents.

				// *bugfix:
				
				// when "growing", need full volume clear
				if (uvec4_v::any<3>(current_max > last_max) ||
					uvec4_v::any<3>(current_min < last_min)) {

					clear_max_max.v = xmLimit;
					clear_min_min.v = xmZero;
				}
				else { // when "shrinking", optimized union of regions for volume clearing!

					// a maximum of both extents
					clear_max_max.v = SFM::max(current_max.v, last_max.v);
					
					// a minimum of both extents
					clear_min_min.v = SFM::min(current_min.v, last_min.v);
				}

				// new extents cached, updates comparison & extents that will be used after ClearStage 0
				current_max.xyzw(new_max_extents[resource_index]);
				current_min.xyzw(new_min_extents[resource_index]);

				ClearStage = 1;
			}
		}
		// Clearing is a two-frame process. The first frame clears and overwrites (current volume extents + new volume extents).
		// The second frame is then just the new volume extents. This is reduced to updating what the current code uses for this
		// iterations bounding extents. De-coupled from the actual command buffer, so that two paths are not required in it's re-record
		// current_max & current_min now contain the frames bounding extents.
		if (0 == --ClearStage) {

			// Use the Clearing Volume that has enough area to actually clear the old extents with the new extents pending.
			// Visually, hard to describe, a picture of the union of the two volumes merged would be better.
			// There will be areas that clear the volume, and areas that overwrite the volume. Important that the volume
			// does not shrink, it must be the maximum and minimum of both volumes to cover all of the space that may need to be cleared.
			current_max.v = SFM::max(current_max.v, clear_max_max.v);
			current_min.v = SFM::min(current_min.v, clear_min_min.v);
			bRecorded[resource_index] = false; // both buffers must be reset so that they are in sync from frame to frame
		}
		else { // this is less than zero due to post-decrement above

			// *reduces to new region this frame forward
			
			// ClearStage < 0
			// for all numbers less than zero that this condition will be true for
			// the volume xtents are finally updated to the new volume extents.
			if (-1 == ClearStage) { // only required to invalidate the command buffer on first iteration (second frame this condition will be true (above))
				bRecorded[resource_index] = false; // both buffers must be reset so that they are in sync from frame to frame
			}
		}

		// Prevent re-recording of command buffer if last output extents used matches in resolution //
		// important to prevent de-optimization // *bugfix: per resource state required
		constinit static uvec4_t last_max_extents[2]{}, last_min_extents[2]{};	// these extents are *after* rounding to gpu memory granularity (final check)

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

			// back to vector form
			current_max.v = uvec4_v(extents_max).v;
			current_max.v = SFM::clamp(current_max.v, xmZero, xmLimit);
			current_min.v = uvec4_v(extents_min).v;
			current_min.v = SFM::clamp(current_min.v, xmZero, xmLimit);

			if (uvec4_v::any<3>(current_max != uvec4_v(last_max_extents[resource_index])) ||
				uvec4_v::any<3>(current_min != uvec4_v(last_min_extents[resource_index]))) {

				// update last used extents, at this point all 3 (extents_xxx, last_xxx_extents, current_xxx) will be equal
				current_max.xyzw(last_max_extents[resource_index]);
				current_min.xyzw(last_min_extents[resource_index]);

				vk::CommandBufferBeginInfo bi{}; // recorded once only
				cb.begin(bi); VKU_SET_CMD_BUFFER_LABEL(cb, vkNames::CommandBuffer::TRANSFER_LIGHT);

				vk::BufferImageCopy region{};

				// 
				// slices ordered by Z 
				// (z * xMax * yMax) + (y * xMax) + x;

				// slices ordered by Y: <---- USING Y
				// (y * xMax * zMax) + (z * xMax) + x;
				region.bufferOffset = ((extents_min.y * LightWidth * LightDepth) + (extents_min.z * LightWidth) + extents_min.x) * MappedVoxelLights.element_size();
				region.bufferOffset = SFM::roundToMultipleOf<false>((int32_t)region.bufferOffset, 8); // rounding down (effectively min)
				region.bufferRowLength = LightWidth;
				region.bufferImageHeight = LightDepth;

				// swizzle to xzy
				region.imageOffset.x = extents_min.x;
				region.imageOffset.z = extents_min.y;
				region.imageOffset.y = extents_min.z;
				//  ""  ""  "  ""
				region.imageExtent.width = extents_max.x - extents_min.x;
				region.imageExtent.depth = extents_max.y - extents_min.y;
				region.imageExtent.height = extents_max.z - extents_min.z;   // Y Axis Major (Slices ordered by Y)

				region.imageSubresource = { vk::ImageAspectFlagBits::eColor, 0, 0, 1 };

				cb.copyBufferToImage(LightProbeMap.stagingBuffer[resource_index].buffer(), LightProbeMap.imageGPUIn[resource_index]->image(), vk::ImageLayout::eTransferDstOptimal, region);

				cb.end();
			}
			bRecorded[resource_index] = true; // always set to true so that command buffer recording is not necessary next frame, it can be re-used
		}

		return(true); // always upload whether the command buffer was updated or not. *Fractional* changes in position need to be accounted for (which reuses the command buffer for upload, but this updates the internal light position data).
	}				  // then compute will run as it should on the updated light position data.


} // end ns