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

public:
	void create(vk::Device const& __restrict device, vk::CommandPool const& __restrict commandPool, vk::Queue const& __restrict queue, point2D_t const frameBufferSize);
	void UpdateDescriptorSet_PostAA(vku::DescriptorSetUpdater& __restrict dsu, vk::ImageView const& __restrict guiImageView0, vk::ImageView const& __restrict guiImageView1, vk::Sampler const& __restrict samplerLinearClamp);

	__inline void Render(vku::present_renderpass&& __restrict pp,
						 struct cVulkan::sPOSTAADATA const& __restrict render_data) const;

	bool const LoadLUT(std::wstring_view const filenamepath); // full filename and path wide string
#ifdef DEBUG_LUT_WINDOW
	bool const MixLUT(std::string_view const szlutA, std::string_view const szlutB, float const tT); // filename only, normal strings
	bool const SaveMixedLUT(std::string& szlutMixed, std::string_view const szlutA, std::string_view const szlutB, float const tT); // filename only, normal strings
	bool const UploadLUT(); // returns true if uploaded to GPU, false if not
#endif

private:

private:
	vku::TextureImageStorage2D* _anamorphicFlare[2];

	vku::TextureImageStorage2D* _lastColorImage;			// last post process result with temporal weight in alpha (does not contain GUI)
	vku::TextureImageStorage2D* _temporalColorImage;

	vku::TextureImageStorage2D* _blurStep[2];

	vku::TextureImage3D* _lutTex;

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
	// begin "actual render pass"
	pp.cb.beginRenderPass(pp.rpbi, vk::SubpassContents::eInline);	// SUBPASS - present post aa rendering //

	pp.cb.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *render_data.pipelineLayout, 0, render_data.sets[0], nullptr);
	
	// ----- temporal resolve "psuedo-pass"
	pp.cb.bindPipeline(vk::PipelineBindPoint::eGraphics, render_data.pipeline[0]);
	// Post-process quad simple generation - fullscreen triangle optimized!
	// https://www.saschawillems.de/blog/2016/08/13/vulkan-tutorial-on-rendering-a-fullscreen-quad-without-buffers/
	pp.cb.draw(3, 1, 0, 0);




	// ------ 1st smaa edge detecion "psuedo-pass"
	pp.cb.bindPipeline(vk::PipelineBindPoint::eGraphics, render_data.pipeline[1]);
	// Post-process quad simple generation - fullscreen triangle optimized!
	// https://www.saschawillems.de/blog/2016/08/13/vulkan-tutorial-on-rendering-a-fullscreen-quad-without-buffers/
	pp.cb.draw(3, 1, 0, 0);


	// ------- 2nd smaa blend weight calculation "psuedo-pass"
	pp.cb.bindPipeline(vk::PipelineBindPoint::eGraphics, render_data.pipeline[2]);
	// Post-process quad simple generation - fullscreen triangle optimized!
	// https://www.saschawillems.de/blog/2016/08/13/vulkan-tutorial-on-rendering-a-fullscreen-quad-without-buffers/
	pp.cb.draw(3, 1, 0, 0);


	// -------- 3rd smaa neightborhood blend "psuedo-pass" and additional post processing effects
	pp.cb.bindPipeline(vk::PipelineBindPoint::eGraphics, render_data.pipeline[3]);
	// Post-process quad simple generation - fullscreen triangle optimized!
	// https://www.saschawillems.de/blog/2016/08/13/vulkan-tutorial-on-rendering-a-fullscreen-quad-without-buffers/
	pp.cb.draw(3, 1, 0, 0);



	pp.cb.nextSubpass(vk::SubpassContents::eInline); // actual subpass

	// -------- final overlay gui blend subpass
	pp.cb.bindPipeline(vk::PipelineBindPoint::eGraphics, render_data.pipeline[4]);
	// Post-process quad simple generation - fullscreen triangle optimized!
	// https://www.saschawillems.de/blog/2016/08/13/vulkan-tutorial-on-rendering-a-fullscreen-quad-without-buffers/
	pp.cb.draw(3, 1, 0, 0);

	// end "acual render pass"
	pp.cb.endRenderPass();
}




