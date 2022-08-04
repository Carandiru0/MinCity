#include "pch.h"
#include "cPostProcess.h"
#include "cTextureBoy.h"
#include "MinCity.h"
#include <Imaging/Imaging/Imaging.h>
#include <Utility/stringconv.h>
#include <Utility/async_long_task.h>
#include "tTime.h"

#define COLOR_LUT_FILE "color.cube"

cPostProcess::cPostProcess()
	: _blurStep{ nullptr }, _temporalColorImage(nullptr), _anamorphicFlare{ nullptr },
	_lutTex{ nullptr }
#ifdef DEBUG_LUT_WINDOW
	,_lut(nullptr), _task_id_mix_luts(0)
#endif
{

}

void cPostProcess::create(vk::Device const& __restrict device, vk::CommandPool const& __restrict commandPool, vk::Queue const& __restrict queue, point2D_t const frameBufferSize) {

	// LUT's
	if (!LoadLUT(TEXTURE_DIR COLOR_LUT_FILE)) {
		FMT_LOG_FAIL(TEX_LOG, "Unable to load 3D lut {:s}", COLOR_LUT_FILE);
	}

	_temporalColorImage = new vku::TextureImageStorage2D(vk::ImageUsageFlagBits::eSampled, device,
		frameBufferSize.x, frameBufferSize.y, 1U, vk::SampleCountFlagBits::e1, vk::Format::eR8G8B8A8Unorm, false, true);
	
	_anamorphicFlare[0] = new vku::TextureImageStorage2D(vk::ImageUsageFlagBits::eSampled, device,
		frameBufferSize.x, frameBufferSize.y, 1U, vk::SampleCountFlagBits::e1, vk::Format::eR8Unorm);
	_anamorphicFlare[1] = new vku::TextureImageStorage2D(vk::ImageUsageFlagBits::eSampled, device,
		frameBufferSize.x, frameBufferSize.y, 1U, vk::SampleCountFlagBits::e1, vk::Format::eR8Unorm);

	_blurStep[0] = new vku::TextureImageStorage2D(vk::ImageUsageFlagBits::eSampled, device,
		frameBufferSize.x, frameBufferSize.y, 1U, vk::SampleCountFlagBits::e1, vk::Format::eR8G8B8A8Unorm, false, true);
	_blurStep[1] = new vku::TextureImageStorage2D(vk::ImageUsageFlagBits::eSampled, device,
		frameBufferSize.x, frameBufferSize.y, 1U, vk::SampleCountFlagBits::e1, vk::Format::eR8G8B8A8Unorm, false, true);

	vku::executeImmediately(device, commandPool, queue, [&](vk::CommandBuffer cb) {
		// image layout initial startup requirement
		_temporalColorImage->setLayout(cb, vk::ImageLayout::eGeneral);		// ""    ""     //
		_anamorphicFlare[0]->setLayout(cb, vk::ImageLayout::eGeneral);
		_anamorphicFlare[1]->setLayout(cb, vk::ImageLayout::eGeneral);
		_blurStep[0]->setLayout(cb, vk::ImageLayout::eGeneral);		// never changes //
		_blurStep[1]->setLayout(cb, vk::ImageLayout::eGeneral);		// ""    ""     //
	});
}

bool const cPostProcess::LoadLUT(std::wstring_view filenamepath)
{
	bool bReturn(false);
	ImagingLUT* lut(nullptr);

	lut = ImagingLoadLUT(filenamepath);
	if (nullptr != lut) {
		
		MinCity::TextureBoy->ImagingToTexture(lut, _lutTex);
		bReturn = true;

		ImagingDelete(lut); lut = nullptr;
	}

	return(bReturn);
}

#ifdef DEBUG_LUT_WINDOW
template<bool const bSaveResult>
static bool const MixLUT(ImagingLUT*& __restrict lut, std::string_view const szlutA, std::string_view const szlutB, float const tT)
{
	bool bReturn(true);

	ImagingLUT* lut_A(nullptr);
	ImagingLUT* lut_B(nullptr);

	std::wstring const relative_directory(TEXTURE_DIR);

	lut_A = ImagingLoadLUT(relative_directory + stringconv::s2ws(szlutA));
	if (nullptr != lut_A) {

		lut_B = ImagingLoadLUT(relative_directory + stringconv::s2ws(szlutB));
		if (nullptr != lut_B) {

			ImagingLUTLerp(lut_A, lut_B, tT);
		}
	}

	if constexpr (bSaveResult) {

		if (nullptr != lut_A) {
			std::string szMixedLut("");
			szMixedLut += szlutA.substr(0, szlutA.find_last_of('.'));
			szMixedLut += szlutB.substr(0, szlutB.find_last_of('.'));
			szMixedLut += ".cube";

			if (ImagingSaveLUT(lut_A, szMixedLut.c_str(), relative_directory + stringconv::s2ws(szMixedLut))) {
				FMT_NUKLEAR_DEBUG(true, "{:s}  saved lut.", szMixedLut);
			}
			else {
				FMT_NUKLEAR_DEBUG_OFF();
				FMT_LOG_FAIL(TEX_LOG, "Mixed LUT not saved");
				bReturn = false;
			}
		}
		else {
			FMT_NUKLEAR_DEBUG_OFF();
			FMT_LOG_FAIL(TEX_LOG, "Invalid LUT not saved");
			bReturn = false;
		}
	}

	if (nullptr != lut_A) {
		lut = std::move(lut_A);
		lut_A = nullptr;
	}
	else {
		FMT_LOG_FAIL(TEX_LOG, "Could not mix luts");
		bReturn = false;
	}

	if (nullptr != lut_A) {
		ImagingDelete(lut_A); lut_A = nullptr;
	}

	if (nullptr != lut_B) {
		ImagingDelete(lut_B); lut_B = nullptr;
	}

	return(bReturn);
}

template<bool const bSaveResult>
static bool const MixLUTTask(int64_t& __restrict task_id, ImagingLUT*& __restrict lut, std::string_view const szlutA, std::string_view const szlutB, float const tT)
{
	// saving always wait so that saves can always succeed. otherwise it's just a test //
	if (!async_long_task::wait<background, !bSaveResult>(task_id, "lutmix")) { // only if no pending/active task

		task_id = async_long_task::enqueue<background>([szlutA, szlutB, tT, &lut] {

			// inside thread - return result not used
			MixLUT<bSaveResult>(lut, szlutA, szlutB, tT);
			
			});

		return(true); // operation sent
	}

	return(false); // operation is active or pending
}

// asynchronous
bool const cPostProcess::MixLUT(std::string_view const szlutA, std::string_view const szlutB, float const tT)
{
	return(MixLUTTask<false>(_task_id_mix_luts, _lut, szlutA, szlutB, tT));
}

// synchronous
bool const cPostProcess::SaveMixedLUT(std::string& szlutMixed, std::string_view const szlutA, std::string_view const szlutB, float const tT)
{
	if (::MixLUT<true>(_lut, szlutA, szlutB, tT)) {

		async_long_task::wait<background>(_task_id_mix_luts, "lutmix");

		std::string szMixedLut("");
		szMixedLut += szlutA.substr(0, szlutA.find_last_of('.'));
		szMixedLut += szlutB.substr(0, szlutB.find_last_of('.'));
		szMixedLut += ".cube";

		szlutMixed = szMixedLut;
		return(true);
	}
	return(false);
}

bool const cPostProcess::UploadLUT()
{
	if (!async_long_task::wait<background, true>(_task_id_mix_luts, "lutmix")) {
		if (nullptr != _lut) {
			if (nullptr == _lutTex) {
				MinCity::TextureBoy->ImagingToTexture(_lut, _lutTex);
			}
			else {
				MinCity::TextureBoy->UpdateTexture(_lut->block, _lutTex);
			}

			ImagingDelete(_lut); _lut = nullptr;

			return(true);
		}
	}

	return(false);
}
#endif

void cPostProcess::UpdateDescriptorSet_PostAA_Post(vku::DescriptorSetUpdater& __restrict dsu, vk::ImageView const& __restrict lastFrameView, vk::Sampler const& __restrict samplerLinearClamp)
{
	// 1 - colorview (backbuffer) (prior function set)
	// 2 - bluenoise (prior function set)

	// 3 - temporal out image
	dsu.beginImages(3U, 0, vk::DescriptorType::eStorageImage);
	dsu.image(nullptr, _temporalColorImage->imageView(), vk::ImageLayout::eGeneral);
	
	// 4 - temporal sampler general
	dsu.beginImages(4U, 0, vk::DescriptorType::eCombinedImageSampler);
	dsu.image(samplerLinearClamp, _temporalColorImage->imageView(), vk::ImageLayout::eGeneral);

	// 5 - last color input attachment 
	dsu.beginImages(5U, 0, vk::DescriptorType::eInputAttachment);
	dsu.image(nullptr, lastFrameView, vk::ImageLayout::eShaderReadOnlyOptimal);

	// 6 - blurStep[] sampler general
	dsu.beginImages(6U, 0, vk::DescriptorType::eCombinedImageSampler);
	dsu.image(samplerLinearClamp, _blurStep[0]->imageView(), vk::ImageLayout::eGeneral);
	dsu.beginImages(6U, 1, vk::DescriptorType::eCombinedImageSampler);
	dsu.image(samplerLinearClamp, _blurStep[1]->imageView(), vk::ImageLayout::eGeneral);

	// 7 - blurStep[] out image
	dsu.beginImages(7U, 0, vk::DescriptorType::eStorageImage);
	dsu.image(nullptr, _blurStep[0]->imageView(), vk::ImageLayout::eGeneral);
	dsu.beginImages(7U, 1, vk::DescriptorType::eStorageImage);
	dsu.image(nullptr, _blurStep[1]->imageView(), vk::ImageLayout::eGeneral);

	// 8 - anamorphic flare sampler array
	dsu.beginImages(8U, 0, vk::DescriptorType::eCombinedImageSampler);
	dsu.image(samplerLinearClamp, _anamorphicFlare[0]->imageView(), vk::ImageLayout::eGeneral);
	dsu.beginImages(8U, 1, vk::DescriptorType::eCombinedImageSampler);
	dsu.image(samplerLinearClamp, _anamorphicFlare[1]->imageView(), vk::ImageLayout::eGeneral);

	// 9 - anamorphic flare image array
	dsu.beginImages(9U, 0, vk::DescriptorType::eStorageImage);
	dsu.image(nullptr, _anamorphicFlare[0]->imageView(), vk::ImageLayout::eGeneral);
	dsu.beginImages(9U, 1, vk::DescriptorType::eStorageImage);
	dsu.image(nullptr, _anamorphicFlare[1]->imageView(), vk::ImageLayout::eGeneral);
}

void cPostProcess::UpdateDescriptorSet_PostAA_Final(vku::DescriptorSetUpdater& __restrict dsu, vk::ImageView const& __restrict guiImageView, vk::Sampler const& __restrict samplerLinearClamp)
{
	// 1 - colorview (backbuffer) (prior function set)
	// 2 - bluenoise (prior function set)

	// 3 - temporal sampler general
	dsu.beginImages(3U, 0, vk::DescriptorType::eCombinedImageSampler);
	dsu.image(samplerLinearClamp, _temporalColorImage->imageView(), vk::ImageLayout::eGeneral);

	// 4 - blurStep[] sampler general
	dsu.beginImages(4U, 0, vk::DescriptorType::eCombinedImageSampler);
	dsu.image(samplerLinearClamp, _blurStep[0]->imageView(), vk::ImageLayout::eGeneral);
	dsu.beginImages(4U, 1, vk::DescriptorType::eCombinedImageSampler);
	dsu.image(samplerLinearClamp, _blurStep[1]->imageView(), vk::ImageLayout::eGeneral);

	// 5 - anamorphic flare sampler array
	dsu.beginImages(5U, 0, vk::DescriptorType::eCombinedImageSampler);
	dsu.image(samplerLinearClamp, _anamorphicFlare[0]->imageView(), vk::ImageLayout::eGeneral);
	dsu.beginImages(5U, 1, vk::DescriptorType::eCombinedImageSampler);
	dsu.image(samplerLinearClamp, _anamorphicFlare[1]->imageView(), vk::ImageLayout::eGeneral);

	// 6 - 3d lut
	dsu.beginImages(6U, 0, vk::DescriptorType::eCombinedImageSampler);
	dsu.image(samplerLinearClamp, _lutTex->imageView(), vk::ImageLayout::eShaderReadOnlyOptimal);

	// 7 - gui
	dsu.beginImages(7U, 0, vk::DescriptorType::eInputAttachment);
	dsu.image(nullptr, guiImageView, vk::ImageLayout::eShaderReadOnlyOptimal);
}

void cPostProcess::CleanUp()
{
#ifdef DEBUG_LUT_WINDOW
	if (nullptr != _lut) {
		ImagingDelete(_lut); _lut = nullptr;
	}
#endif

	SAFE_RELEASE_DELETE(_blurStep[0]);
	SAFE_RELEASE_DELETE(_blurStep[1]);
	SAFE_RELEASE_DELETE(_temporalColorImage)
	SAFE_RELEASE_DELETE(_anamorphicFlare[0]);
	SAFE_RELEASE_DELETE(_anamorphicFlare[1]);
	SAFE_RELEASE_DELETE(_lutTex);
}



