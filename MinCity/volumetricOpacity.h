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

	struct voxelTexture3D {

		vku::double_buffer<vku::GenericBuffer>		stagingBuffer;

		vku::TextureImage3D*						imageGPUIn;

		voxelTexture3D()
			: imageGPUIn(nullptr)
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
		static constexpr uint32_t const // "uniform light volume size"
			LightSize = ComputeLightConstants::LIGHT_RESOLUTION;

	private:
		using lightVolume = lightBuffer3D<ComputeLightConstants::memLayoutV, LightSize, LightSize, LightSize, Size>;

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
			//return((ePingPongMap::PING == uPingPong ? ePingPongMap::PONG : ePingPongMap::PING));
			return((ePingPongMap::PING == uPingPong ? ePingPongMap::PING : ePingPongMap::PONG)); // ** extra step "filter" pipeline
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
		__inline void __vectorcall pushViewMatrixOffset(FXMMATRIX xmView, FXMVECTOR xmOffset) {
			XMStoreFloat4x4(&PushConstants.view, xmView);   // view matrix stored in xyz form
			XMStoreFloat3(&PushConstants.offset, XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(xmOffset)); // offset properly swizzled to xzy form
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
		void map() const {
			const_cast<volumetricOpacity<Size>* __restrict>(this)->MappedVoxelLights.map();
		}
		void commit() const { 
			const_cast<volumetricOpacity<Size>* __restrict>(this)->MappedVoxelLights.commit();
		}

		void clear(uint32_t const resource_index) {
			MappedVoxelLights.clear(resource_index);
		}
		
		void release() {

			for (uint32_t i = 0; i < vku::double_buffer<uint32_t>::count; ++i) {
				LightProbeMap.stagingBuffer[i].release();
			}
			SAFE_RELEASE_DELETE(LightProbeMap.imageGPUIn);

			SAFE_RELEASE_DELETE(ComputeLightDispatchBuffer);

			for (uint32_t i = 0; i < 2; ++i) {
				SAFE_RELEASE_DELETE(PingPongMap[i]);
			}

			VolumeSet.LightMap = nullptr;

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
				uint32_t const local_size((LightSize >> ComputeLightConstants::SHADER_LOCAL_SIZE_BITS) + (0U == (LightSize % ComputeLightConstants::SHADER_LOCAL_SIZE) ? 0U : 1U));
				
				vk::DispatchIndirectCommand const dispatchCommand{

					local_size, local_size, local_size
				};

				ComputeLightDispatchBuffer = new vku::IndirectBuffer(sizeof(dispatchCommand), true);

				ComputeLightDispatchBuffer->upload(device, commandPool, queue, dispatchCommand);
			}
		}

	public:
		void create(vk::Device const& __restrict device, vk::CommandPool const& __restrict commandPool, vk::Queue const& __restrict queue, point2D_t const frameBufferSize, size_t const hardware_concurrency) {

			createIndirectDispatch(device, commandPool, queue);

			for (uint32_t resource_index = 0; resource_index < vku::double_buffer<uint32_t>::count; ++resource_index) {

				LightProbeMap.stagingBuffer[resource_index].createAsCPUToGPUBuffer(getLightProbeMapSizeInBytes(), vku::eMappedAccess::Random, true, true);
			}
			
			LightProbeMap.imageGPUIn = new vku::TextureImage3D(vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst, device,
				LightSize, LightSize, LightSize, 1U, vk::Format::eR32G32B32A32Sfloat, false, true);

			VKU_SET_OBJECT_NAME(vk::ObjectType::eImage, (VkImage)LightProbeMap.imageGPUIn->image(), vkNames::Image::LightProbeMap);
			
			MappedVoxelLights.create(hardware_concurrency); // prepares/clears buffers/memory

			for (uint32_t i = 0; i < 2; ++i) {
				PingPongMap[i] = new vku::TextureImageStorage3D(vk::ImageUsageFlagBits::eSampled, device,
					LightSize, LightSize, LightSize, 1U, vk::Format::eR32G32B32A32Sfloat, false, true);

				VKU_SET_OBJECT_NAME(vk::ObjectType::eImage, (VkImage)PingPongMap[i]->image(), vkNames::Image::PingPongMap);
			}

			LightMap.DistanceDirection = new vku::TextureImageStorage3D(vk::ImageUsageFlagBits::eSampled, device,
				LightSize, LightSize, LightSize, 1U, vk::Format::eR16G16B16A16Snorm, false, true); // only signed normalized values
			LightMap.Color = new vku::TextureImageStorage3D(vk::ImageUsageFlagBits::eSampled, device,
				LightSize, LightSize, LightSize, 1U, vk::Format::eR16G16B16A16Sfloat, false, true);
			LightMap.Reflection = new vku::TextureImageStorage3D(vk::ImageUsageFlagBits::eSampled, device,
				LightSize, LightSize, LightSize, 1U, vk::Format::eR8G8B8A8Unorm, false, true);
			VolumeSet.LightMap = &LightMap;

			VKU_SET_OBJECT_NAME(vk::ObjectType::eImage, (VkImage)LightMap.DistanceDirection->image(), vkNames::Image::LightMap_DistanceDirection);
			VKU_SET_OBJECT_NAME(vk::ObjectType::eImage, (VkImage)LightMap.Color->image(), vkNames::Image::LightMap_Color);
			VKU_SET_OBJECT_NAME(vk::ObjectType::eImage, (VkImage)LightMap.Reflection->image(), vkNames::Image::LightMap_Reflection);

			OpacityMap = new vku::TextureImageStorage3D(vk::ImageUsageFlagBits::eSampled, device,
				Size, Size, Size, 1U, vk::Format::eR8Unorm, false, true);
			VolumeSet.OpacityMap = OpacityMap;

			VKU_SET_OBJECT_NAME(vk::ObjectType::eImage, (VkImage)OpacityMap->image(), vkNames::Image::OpacityMap);

			FMT_LOG(TEX_LOG, "LightProbe Volumetric data: {:n} bytes", LightProbeMap.imageGPUIn->size());
			FMT_LOG(TEX_LOG, "Lightmap [GPU Resident Only] Volumetric data: {:n} bytes", PingPongMap[0]->size() + PingPongMap[1]->size() + LightMap.DistanceDirection->size() + LightMap.Color->size() + LightMap.Reflection->size());
			FMT_LOG(TEX_LOG, "Opacitymap [GPU Resident Only] Volumetric data: {:n} bytes", OpacityMap->size());

#ifdef DEBUG_LIGHT_PROPAGATION
			DebugDevice = &device;
			DebugMinMaxBuffer = new vku::HostStorageBuffer(DebugMinMax);
			DebugTexture = new vku::TextureImageStorage2D(vk::ImageUsageFlagBits::eSampled, device, LightWidth, LightDepth, 1, vk::Format::eR8G8B8A8Unorm);
			resetMinMax();
#endif

			vku::executeImmediately(device, commandPool, queue, [&](vk::CommandBuffer cb) {
				LightProbeMap.imageGPUIn->setLayout(cb, vk::ImageLayout::eTransferDstOptimal);  // required initial state

				VolumeSet.OpacityMap->setLayout(cb, vk::ImageLayout::eGeneral);					   //    ""      ""      ""

				LightMap.DistanceDirection->setLayoutCompute(cb, vku::ACCESS_WRITEONLY);		// the final oututs are never "read" in compute shaders
				LightMap.Color->setLayoutCompute(cb, vku::ACCESS_WRITEONLY);					// *only* read by fragment shaders
				LightMap.Reflection->setLayoutCompute(cb, vku::ACCESS_WRITEONLY);				// *only* read by fragment shaders

				PingPongMap[0]->setLayoutCompute(cb, vku::ACCESS_READWRITE);		// never changes
				PingPongMap[1]->setLayoutCompute(cb, vku::ACCESS_READWRITE);		// never changes

				});
		}

		void SetSpecializationConstants_ComputeLight(std::vector<vku::SpecializationConstant>& __restrict constants)
		{
			// full world volume dimensions //
			constants.emplace_back(vku::SpecializationConstant(0, (float)Size)); // should be world volume uniform size (width=height=depth)
			constants.emplace_back(vku::SpecializationConstant(1, (float)InvVolumeLength)); // should be inverse world volume length

			// light volume dimensions //
			constants.emplace_back(vku::SpecializationConstant(2, (float)LightSize)); // should be light volume uniform size
			constants.emplace_back(vku::SpecializationConstant(3, 1.0f / (float)LightSize)); // should be inverse light volume size
		}

		void UpdateDescriptorSet_ComputeLight(vku::DescriptorSetUpdater& __restrict dsu, vk::Sampler const& __restrict samplerLinearBorder) const
		{
			dsu.beginImages(1U, 0, vk::DescriptorType::eCombinedImageSampler);
			dsu.image(samplerLinearBorder, LightProbeMap.imageGPUIn->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal);

			dsu.beginImages(2U, 0, vk::DescriptorType::eCombinedImageSampler);
			dsu.image(samplerLinearBorder, PingPongMap[0]->imageView(), vk::ImageLayout::eGeneral);
			dsu.beginImages(2U, 1, vk::DescriptorType::eCombinedImageSampler);
			dsu.image(samplerLinearBorder, PingPongMap[1]->imageView(), vk::ImageLayout::eGeneral);

			dsu.beginImages(3U, 0, vk::DescriptorType::eStorageImage);
			dsu.image(nullptr, PingPongMap[0]->imageView(), vk::ImageLayout::eGeneral);
			dsu.beginImages(3U, 1, vk::DescriptorType::eStorageImage);
			dsu.image(nullptr, PingPongMap[1]->imageView(), vk::ImageLayout::eGeneral);

			/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

			dsu.beginImages(4U, 0, vk::DescriptorType::eStorageImage);
			dsu.image(nullptr, LightMap.DistanceDirection->imageView(), vk::ImageLayout::eGeneral);

			dsu.beginImages(5U, 0, vk::DescriptorType::eStorageImage);
			dsu.image(nullptr, LightMap.Color->imageView(), vk::ImageLayout::eGeneral);

			dsu.beginImages(6U, 0, vk::DescriptorType::eStorageImage);
			dsu.image(nullptr, LightMap.Reflection->imageView(), vk::ImageLayout::eGeneral);
		}

		__inline bool const renderCompute(vku::compute_pass&& __restrict  c, struct cVulkan::sCOMPUTEDATA const& __restrict render_data);

	private:
		__inline bool const upload_light(uint32_t const resource_index, vk::CommandBuffer& __restrict cb, uint32_t const transferQueueFamilyIndex, uint32_t const computeQueueFamilyIndex);

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
		voxelTexture3D										LightProbeMap;
		lightVolume											MappedVoxelLights;
		
		vku::IndirectBuffer* __restrict						ComputeLightDispatchBuffer;
		vku::TextureImageStorage3D*							PingPongMap[2];
		voxelLightmapSet									LightMap; // final output
		vku::TextureImageStorage3D*							OpacityMap;
		voxelVolumeSet										VolumeSet;

		static inline float const							VolumeSize = Size,
															VolumeLength = (std::hypot(float(VolumeSize), float(VolumeSize), float(VolumeSize))),
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
			MappedVoxelLights(LightProbeMap.stagingBuffer),
			ComputeLightDispatchBuffer{ nullptr }, LightMap{ nullptr }, OpacityMap{ nullptr },
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
		PushConstants.step = 1; // (1 + JFA) //
		PushConstants.index_output = index_output;
		PushConstants.index_input = !index_output; // last frames output is this frames secondary input

#ifndef NDEBUG
#ifdef DEBUG_ASSERT_JFA_SEED_INDICES_OK // good validation, state is setup at runtime so this is a good dynamic test.
		assert_print(PushConstants.index_output != PushConstants.index_input, "[fail] jfa seed indices equal! [fail]")
#endif
#endif

		c.cb_render_light.pushConstants(*render_data.light.pipelineLayout, vk::ShaderStageFlagBits::eCompute,
			(uint32_t)0U, (uint32_t)sizeof(UniformDecl::ComputeLightPushConstants), reinterpret_cast<void const* const>(&PushConstants));

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
			(uint32_t)0U, (uint32_t)sizeof(UniformDecl::ComputeLightPushConstants), reinterpret_cast<void const* const>(&PushConstants));

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
		PushConstants.index_output = !index_input; // this is not used in the shader @ this stage. however, it is important that this is still updated to maintain the current state of PushConstants.
		PushConstants.index_input = index_input;  // this is the input to filter stage that is the output of the jfa stage.                                                                          
		
		c.cb_render_light.pushConstants(*render_data.light.pipelineLayout, vk::ShaderStageFlagBits::eCompute,
			(uint32_t)0U, (uint32_t)sizeof(UniformDecl::ComputeLightPushConstants), reinterpret_cast<void const* const>(&PushConstants));

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
			if (c.async_compute_enabled) {
				return(upload_light(c.resource_index, c.cb_transfer_light, c.transferQueueFamilyIndex, c.computeQueueFamilyIndex));
			}
		}
		else if (c.cb_render_light) { 

			constinit static bool bRecorded[2]{ false, false }; // these are synchronized 

			uint32_t const resource_index(c.resource_index);

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

				{ // [acquire image barrier]
					using afb = vk::AccessFlagBits;

					static constexpr size_t const image_count(1ULL); // batched
					std::array<vku::GenericImage* const, image_count> const images{ LightProbeMap.imageGPUIn };
					std::array<vk::ImageMemoryBarrier, image_count> imbs{};
					
					for (uint32_t i = 0; i < image_count; ++i) {

						imbs[i].srcQueueFamilyIndex = c.transferQueueFamilyIndex;  // (default) VK_QUEUE_FAMILY_IGNORED;
						imbs[i].dstQueueFamilyIndex = c.computeQueueFamilyIndex;  // (default) VK_QUEUE_FAMILY_IGNORED;
						imbs[i].oldLayout = vk::ImageLayout::eTransferDstOptimal;
						imbs[i].newLayout = vk::ImageLayout::eTransferDstOptimal; // layout change cannot be done here, this is queue ownership transfer only
						imbs[i].image = images[i]->image();
						imbs[i].subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };

						imbs[i].srcAccessMask = (afb)0;
						imbs[i].dstAccessMask = afb::eShaderRead;
					}

					using psfb = vk::PipelineStageFlagBits;

					vk::PipelineStageFlags const srcStageMask(psfb::eTopOfPipe),
												 dstStageMask(psfb::eComputeShader);

					c.cb_render_light.pipelineBarrier(srcStageMask, dstStageMask, vk::DependencyFlagBits::eByRegion, 0, nullptr, 0, nullptr, image_count, imbs.data());
				}
				
				// layout actually transitions below
				{ // bugfix: Must be set for both buffers, validation error.
					static constexpr size_t const image_count(1ULL); // batched
					std::array<vku::GenericImage* const, image_count> const images{ LightProbeMap.imageGPUIn };

					vku::GenericImage::setLayout<image_count>(images, c.cb_render_light, vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits::eTransfer, vku::ACCESS_WRITEONLY, vk::PipelineStageFlagBits::eComputeShader, vku::ACCESS_READONLY);
				}
				
				// common descriptor set and pipline layout to SEED and JFA, seperate pipelines
				c.cb_render_light.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *render_data.light.pipelineLayout, 0, render_data.light.sets[resource_index], nullptr);

				// Jump flooding runs in a specfic series from seed (1 + JFA) to jumpflooding (JFA) to finally (JFA + 1) being the last step.
				
				// (1 + JFA) & Seed
				c.cb_render_light.bindPipeline(vk::PipelineBindPoint::eCompute, *render_data.light.pipeline[eComputeLightPipeline::SEED]);
				renderSeed(c, render_data, resource_index); // output alternates every frame

				//*bugfix - sync validation, solved by automation
				vku::memory_barrier(c.cb_render_light,
					vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eTopOfPipe | vk::PipelineStageFlagBits::eComputeShader,
					vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead);
				
				// ( JFA ) //
				uint32_t uPing(resource_index), uPong(!resource_index); // this is always true constants for output of seed to JFA's first ping-pong input. 

				c.cb_render_light.bindPipeline(vk::PipelineBindPoint::eCompute, *render_data.light.pipeline[eComputeLightPipeline::JFA]);

				uint32_t step(MAX_STEP_PINGPONG >> 1);
				do
				{
					renderJFA(c, render_data, uPing, uPong, step);

					//*bugfix - sync validation, solved by automation
					vku::memory_barrier(c.cb_render_light,
						vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eTopOfPipe | vk::PipelineStageFlagBits::eComputeShader,
						vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead);
					
					std::swap(uPing, uPong);

					step >>= 1;
				} while (0 != step);

				// (JFA + 1)
				renderJFA(c, render_data, uPing, uPong, 1);

				//*bugfix - sync validation, solved by automation
				vku::memory_barrier(c.cb_render_light,
					vk::PipelineStageFlagBits::eComputeShader, vk::PipelineStageFlagBits::eTopOfPipe | vk::PipelineStageFlagBits::eComputeShader,
					vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eShaderRead);
				
				// Last step, filtering - temporal super-sampling, blending & antialiasing
				c.cb_render_light.bindPipeline(vk::PipelineBindPoint::eCompute, *render_data.light.pipeline[eComputeLightPipeline::FILTER]);

				renderFilter(c, render_data, uPong);

				{ // bugfix: Must be set for both buffers, validation error.
					static constexpr size_t const image_count(1ULL); // batched
					std::array<vku::GenericImage* const, image_count> const images{ LightProbeMap.imageGPUIn };

					vku::GenericImage::setLayout<image_count>(images, c.cb_render_light, vk::ImageLayout::eTransferDstOptimal, vk::PipelineStageFlagBits::eComputeShader, vku::ACCESS_READONLY, vk::PipelineStageFlagBits::eTransfer, vku::ACCESS_WRITEONLY);
				}

				// [release image barrier]
				using afb = vk::AccessFlagBits;

				static constexpr size_t const image_count(3ULL); // batched
				std::array<vku::GenericImage* const, image_count> const images{ LightMap.DistanceDirection, LightMap.Color, LightMap.Reflection };
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

				bRecorded[resource_index] = true; // these compute buffers need only be recorded once, then re-used for every frame
			}
			
			// Just update layouts that will be current after this compute cb is done.
			LightProbeMap.imageGPUIn->setCurrentLayout(vk::ImageLayout::eTransferDstOptimal);
			LightMap.DistanceDirection->setCurrentLayout(vk::ImageLayout::eGeneral);
			LightMap.Color->setCurrentLayout(vk::ImageLayout::eGeneral);
			LightMap.Reflection->setCurrentLayout(vk::ImageLayout::eGeneral);

			return(true);
		}

		return(false);
	}

	// returns true when "dirty" status should be set, so that compute shader knows it needs to run
	template< uint32_t Size >
	__inline bool const volumetricOpacity<Size>::upload_light(uint32_t const resource_index, vk::CommandBuffer& __restrict cb, uint32_t const transferQueueFamilyIndex, uint32_t const computeQueueFamilyIndex) {

		// optimizations removed due to unsynchronized behaviour of needing 2 frames to process the clear, then an upload of the volumetric area currently occupied by lights.
		// this de-synchronizes the light positions by lagging behind. then the lights do not occur at the position they should be at for this frame messing with the fractional offset differences between the world and the light.
		
		// instead - brute force update of volume occurs every frame (currently 128x128x128 ~33MB to upload every frame (<2ms), **note if 256x256x256 instead the upload size is ~268MB per frame which is dog slow (>21ms))
		constinit static bool bRecorded[2]{ false, false }; // these are synchronized 

		uvec4_v const current_max(MappedVoxelLights.getVolumeExtentsLimit().v/*MappedVoxelLights.getCurrentVolumeExtentsMax()*/), current_min(_mm_setzero_si128()/*MappedVoxelLights.getCurrentVolumeExtentsMin()*/); // no longer culling upload area to extents discovered by light volume occupancy - not worth the headache effects that are caused by this optimization.

		// UPLOAD image already transitioned to transfer dest layout at this point
		// ################################## //
		// Light Probe Map 3D Texture Upload  //
		// ################################## //

		// brute-force always entire volume extents uploaded (allows record once and reuse every frame)

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

			vk::CommandBufferBeginInfo bi{}; // recorded once only
			cb.begin(bi); VKU_SET_CMD_BUFFER_LABEL(cb, vkNames::CommandBuffer::TRANSFER_LIGHT);

			vk::BufferImageCopy region{};

			// 
			// slices ordered by Z 
			// (z * xMax * yMax) + (y * xMax) + x;

			// slices ordered by Y: <---- USING Y
			// (y * xMax * zMax) + (z * xMax) + x;
			region.bufferOffset = ((extents_min.y * LightSize * LightSize) + (extents_min.z * LightSize) + extents_min.x) * MappedVoxelLights.element_size();
			region.bufferOffset = SFM::roundToMultipleOf<false>((int32_t)region.bufferOffset, 8); // rounding down (effectively min)
			region.bufferRowLength = LightSize;
			region.bufferImageHeight = LightSize;

			// swizzle to xzy
			region.imageOffset.x = extents_min.x;
			region.imageOffset.z = extents_min.y; // Y Axis Major (Slices ordered by Y)
			region.imageOffset.y = extents_min.z;
			//  ""  ""  "  ""
			region.imageExtent.width = extents_max.x - extents_min.x;
			region.imageExtent.depth = extents_max.y - extents_min.y; // Y Axis Major (Slices ordered by Y)
			region.imageExtent.height = extents_max.z - extents_min.z;   

			region.imageSubresource = { vk::ImageAspectFlagBits::eColor, 0, 0, 1 };
				
			cb.copyBufferToImage(LightProbeMap.stagingBuffer[resource_index].buffer(), LightProbeMap.imageGPUIn->image(), vk::ImageLayout::eTransferDstOptimal, region);

			{ // [release image barrier]
				using afb = vk::AccessFlagBits;
					
				static constexpr size_t const image_count(1ULL); // batched
				std::array<vku::GenericImage* const, image_count> const images{ LightProbeMap.imageGPUIn };
				std::array<vk::ImageMemoryBarrier, image_count> imbs{};
					
				for (uint32_t i = 0; i < image_count; ++i) {

					imbs[i].srcQueueFamilyIndex = transferQueueFamilyIndex;  // (default) VK_QUEUE_FAMILY_IGNORED;
					imbs[i].dstQueueFamilyIndex = computeQueueFamilyIndex;  // (default) VK_QUEUE_FAMILY_IGNORED;
					imbs[i].oldLayout = vk::ImageLayout::eTransferDstOptimal;
					imbs[i].newLayout = vk::ImageLayout::eTransferDstOptimal; // layout change cannot be done here, this is queue ownership transfer only
					imbs[i].image = images[i]->image();
					imbs[i].subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };
						
					imbs[i].srcAccessMask = afb::eTransferWrite;
					imbs[i].dstAccessMask = (afb)0;
				}

				using psfb = vk::PipelineStageFlagBits;
					
				vk::PipelineStageFlags const srcStageMask(psfb::eTransfer),
												dstStageMask(psfb::eTopOfPipe);
					
				cb.pipelineBarrier(srcStageMask, dstStageMask, vk::DependencyFlagBits::eByRegion, 0, nullptr, 0, nullptr, image_count, imbs.data());
			}
			cb.end();
			bRecorded[resource_index] = true; // always set to true so that command buffer recording is not necessary next frame, it can be re-used
		}
		
		return(true); // always succeed whether command buffer is recorded or re-used - indicating to the next stage (compute) to commence once transfer here has completed.
	}


} // end ns