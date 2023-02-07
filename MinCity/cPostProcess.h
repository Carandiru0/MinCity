#pragma once
#include <Utility/class_helper.h>
#include "cVulkan.h"
#include <vku/vku_addon.hpp>
#include <Math/point2D_t.h>

//forward decl
struct ImagingLUT;
namespace vku
{
	class TextureImage2D;
}

class no_vtable cPostProcess : no_copy
{
public:
	void create(vk::Device const& __restrict device, vk::CommandPool const& __restrict commandPool, vk::Queue const& __restrict queue, point2D_t const frameBufferSize, bool const bHDROn_);

	void UpdateDescriptorSet_PostAA_Post(vku::DescriptorSetUpdater& __restrict dsu, vk::ImageView const& __restrict lastFrameView, vk::Sampler const& __restrict samplerLinearClamp);
	void UpdateDescriptorSet_PostAA_Final(vku::DescriptorSetUpdater& __restrict dsu, vk::ImageView const& __restrict guiImageView, vk::Sampler const& __restrict samplerLinearClamp);

	__inline void Render(vku::present_renderpass&& __restrict pp,
						 struct cVulkan::sPOSTAADATA const& __restrict render_data) const;

private:
	bool const LoadLUT(std::wstring_view const filenamepath); // full filename and path wide string
#ifdef DEBUG_LUT_WINDOW
public:
	bool const MixLUT(std::string_view const szlutA, std::string_view const szlutB, float const tT); // filename only, normal strings
	bool const SaveMixedLUT(std::string& szlutMixed, std::string_view const szlutA, std::string_view const szlutB, float const tT); // filename only, normal strings
	bool const UploadLUT(); // returns true if uploaded to GPU, false if not
#endif

private:
	vku::TextureImageStorage2D* _anamorphicFlare[2];

	vku::TextureImageStorage2D* _temporalColorImage;

	vku::TextureImageStorage2D* _blurStep[2];

	vku::TextureImage3D*		_lutTex;
	
#ifdef DEBUG_LUT_WINDOW
	ImagingLUT*				_lut;
	int64_t					_task_id_mix_luts;
#endif

public:
	cPostProcess();
	~cPostProcess() = default;  // uses CleanUp instead
	void CleanUp();
};


__inline void cPostProcess::Render(vku::present_renderpass&& __restrict pp,
								   struct cVulkan::sPOSTAADATA const& __restrict render_data) const
{
	static constexpr uint32_t const post_stage(cVulkan::sPOSTAADATA::eStage::Post), final_stage(cVulkan::sPOSTAADATA::eStage::Final);

	uint32_t const resource_index(pp.resource_index);
		
	// common descriptor set to 3 post aa passes
	pp.cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *render_data.pipelineLayout[post_stage], 0, render_data.sets[post_stage][0], nullptr);
		
	// ----- 0
	// begin "actual render pass"
	pp.cb.beginRenderPass(pp.rpbi_postAA0, vk::SubpassContents::eInline);
	pp.cb.bindPipeline(vk::PipelineBindPoint::eGraphics, render_data.pipeline[0]);
	// Post-process quad simple generation - fullscreen triangle optimized!
	// https://www.saschawillems.de/blog/2016/08/13/vulkan-tutorial-on-rendering-a-fullscreen-quad-without-buffers/
	pp.cb.draw(3, 1, 0, 0);
	pp.cb.endRenderPass();
	
	// ------ 1
	// begin "actual render pass"
	pp.cb.beginRenderPass(pp.rpbi_postAA1, vk::SubpassContents::eInline);
	pp.cb.bindPipeline(vk::PipelineBindPoint::eGraphics, render_data.pipeline[1]);
	// Post-process quad simple generation - fullscreen triangle optimized!
	// https://www.saschawillems.de/blog/2016/08/13/vulkan-tutorial-on-rendering-a-fullscreen-quad-without-buffers/
	pp.cb.draw(3, 1, 0, 0);
	pp.cb.endRenderPass();
	
	// ------- 2
	// begin "actual render pass"
	pp.cb.beginRenderPass(pp.rpbi_postAA2, vk::SubpassContents::eInline);
	pp.cb.bindPipeline(vk::PipelineBindPoint::eGraphics, render_data.pipeline[2]);
	// Post-process quad simple generation - fullscreen triangle optimized!
	// https://www.saschawillems.de/blog/2016/08/13/vulkan-tutorial-on-rendering-a-fullscreen-quad-without-buffers/
	pp.cb.draw(3, 1, 0, 0);
	pp.cb.endRenderPass();
	
	
	// common descriptor set to final present pass
	pp.cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *render_data.pipelineLayout[final_stage], 0, render_data.sets[final_stage][0], nullptr);

	// -------- final / present
	// begin "actual render pass"
	pp.cb.beginRenderPass(pp.rpbi_final, vk::SubpassContents::eInline);
	pp.cb.bindPipeline(vk::PipelineBindPoint::eGraphics, render_data.pipeline[3]);
	// Post-process quad simple generation - fullscreen triangle optimized!
	// https://www.saschawillems.de/blog/2016/08/13/vulkan-tutorial-on-rendering-a-fullscreen-quad-without-buffers/
	pp.cb.draw(3, 1, 0, 0);

	// -------- overlay gui blend
	pp.cb.bindPipeline(vk::PipelineBindPoint::eGraphics, render_data.pipeline[4]);
	// Post-process quad simple generation - fullscreen triangle optimized!
	// https://www.saschawillems.de/blog/2016/08/13/vulkan-tutorial-on-rendering-a-fullscreen-quad-without-buffers/
	pp.cb.draw(3, 1, 0, 0);
	pp.cb.endRenderPass();
}




