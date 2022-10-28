#include "pch.h"
#include "globals.h"
#include "MinCity.h"
#include "cTextureBoy.h"
#include "cVulkan.h"
#include "cProcedural.h"
#include "MinCity.h"
#include <Noise/supernoise.hpp>

#include <filesystem>
namespace fs = std::filesystem;

#include <Utility/stringconv.h>

// for the texture indices used in large texture array
#include "../Data/Shaders/texturearray.glsl"


cTextureBoy::cTextureBoy()
	: _device(MinCity::Priv_Vulkan().getDevice()), // privaleged access required for private const reference on construction //
	_noiseTexture(nullptr)
{
	_tex.array.reserve(TEXTURE_ARRAY_LENGTH + 1);
	_tex.array.emplace_back(nullptr); // 0 index is always reserved for special noise texture

}
void cTextureBoy::Initialize()
{
	supernoise::NewNoisePermutation();

	// Generate Image
	ImagingMemoryInstance* imageNoise = MinCity::Procedural->GenerateNoiseImageMixed(NOISE_TEXTURE_SIZE, supernoise::interpolator::SmoothStep());

	MinCity::TextureBoy->ImagingToTexture<false>(imageNoise, _noiseTexture); // generated texture is in linear colorspace

#ifndef NDEBUG
#ifdef DEBUG_EXPORT_NOISEMIX_KTX
	ImagingSaveToKTX(imageNoise, DEBUG_DIR "noisemix_test.ktx");
#endif
#endif

	ImagingDelete(imageNoise);

	AddTextureToTextureArray(_noiseTexture);  // 0 index is always reserved for special noise texture
}

vk::CommandPool const& __restrict								cTextureBoy::transientPool() const
{
	return(MinCity::Vulkan->transientPool());
}
vk::CommandPool const& __restrict							    cTextureBoy::dmaTransferPool() const
{
	return(MinCity::Vulkan->dmaTransferPool(vku::eCommandPools::DMA_TRANSFER_POOL_PRIMARY));
	//return(transientPool()); // bug: currently images some how are in state imagesrcoptimal if the dma queue is used for there upload
}							 // this is a workaround for now
vk::Queue const& __restrict										cTextureBoy::graphicsQueue() const
{
	return(MinCity::Vulkan->graphicsQueue());
}
vk::Queue const& __restrict										cTextureBoy::transferQueue() const
{
	return(MinCity::Vulkan->transferQueue());
	//return(graphicsQueue());  // bug: currently images some how are in state imagesrcoptimal if the dma queue is used for there upload
}							  // this is a workaround for now

void cTextureBoy::ImagingSequenceToTexture(ImagingSequence const* const image, vku::TextureImage2DArray*& __restrict texture)
{
	// flatten all images into single array
	uint32_t const count(image->count);
	size_t const image_size(size_t(image->xsize) * size_t(image->ysize) * size_t(image->pixelsize));

	// large allocation for all frames
	uint8_t* const __restrict flat = (uint8_t* const __restrict)scalable_aligned_malloc(image_size * size_t(count), 32);
	size_t image_offset(0);

	ImagingSequenceInstance const* const images(image->images);
	for (uint32_t frame = 0 ; frame < count ; ++frame) {

		memcpy(flat + image_offset, images[frame].block, image_size);
		image_offset += image_size;
			
	}

	// update/create texture
	vk::CommandPool const* __restrict commandPool(&transientPool());
	vk::Queue const* __restrict queue(&graphicsQueue());

	if ((0 == image->xsize % 8) && (0 == image->ysize % 8)) {

		if (nullptr != texture) { // for an update of a texture - only before a dma transfer, texture must be transitioned on the graphics queue to transferdstoptimal // DMA
			vku::executeImmediately(_device, *commandPool, *queue, [&](vk::CommandBuffer cb) {

				texture->setLayout(cb, vk::ImageLayout::eTransferDstOptimal);
			});
		}
		// switch to dma transfer queue
		commandPool = &dmaTransferPool();
		queue = &transferQueue();
	}

	if (nullptr == texture) {
		texture = new vku::TextureImage2DArray(_device, image->xsize, image->ysize, count,
			vk::Format::eR8G8B8A8Srgb, false, false); // gifs are always non-linear, load as srgb
	}

	// image_offset is now equal to the image size total bytes
	texture->upload<false>(_device, flat, image_offset, *commandPool, *queue); // image_offset is now equal to the image size total bytes // DMA
	texture->finalizeUpload(_device, transientPool(), graphicsQueue()); // must be graphics queue for shaderreadonly to be set, this also reverts texture to graphics queue from transferdstoptimal.

	scalable_aligned_free(flat);
}

size_t const cTextureBoy::AddTextureToTextureArray(vku::TextureImage2DArray const* const __restrict texture, size_t const desired_index)
{
	size_t const current_index(_tex.array.size() - 1ULL);  // size is always greater or equal to 1, never zero guaranteed by the textureboy ctor

	if (_tex.locked) {
		FMT_LOG_FAIL(TEX_LOG, "Texture array is locked down, cannot add texture to index: {:d} !!", desired_index);
		return(0ULL);
	}
	if (desired_index > current_index) {

		size_t const new_size(current_index + (desired_index - current_index) + 1ULL);
		_tex.array.reserve(new_size);
		for (size_t index = new_size - 1ULL; index > current_index; --index) {
			_tex.array.emplace_back(nullptr);
		}
	}

#ifndef NDEBUG
	else if ( 0ULL != desired_index && nullptr != _tex.array[desired_index]) {
		FMT_LOG_FAIL(TEX_LOG, "Existing texture replaced in texture array at index: {:d} !!", desired_index);
	}

	if (_tex.array.size() > TEXTURE_ARRAY_LENGTH) {
		FMT_LOG_FAIL(TEX_LOG, "Texture array reserve size should be increased to: {:d} !!", _tex.array.size());
	}
#endif

	if (0ULL == desired_index) {
		_tex.array[current_index] = texture;
		return(current_index);
	}
	else {
		_tex.array[desired_index] = texture;
	}

	return(desired_index);  // if zero is somehow returned its an error
}
size_t const cTextureBoy::AddTextureToTextureArray(vku::TextureImage2D* const __restrict texture, size_t const desired_index)
{
	texture->changeImageView(_device, vk::ImageViewType::e2DArray);  // automatic change to texture 2d array image view
	return( AddTextureToTextureArray(reinterpret_cast<vku::TextureImage2DArray const* const __restrict>(texture), desired_index) );
}


tbb::concurrent_vector<vku::TextureImage2DArray const*> const& cTextureBoy::lockTextureArray() { 

	_tex.locked = true;
	_tex.array.shrink_to_fit(); 

#ifndef NDEBUG
	tbb::concurrent_vector<vku::TextureImage2DArray const*>::const_iterator iter(_tex.array.cbegin());
	for (; iter != _tex.array.cend(); ++iter) {

		if (nullptr == *iter) {
			FMT_LOG_FAIL(TEX_LOG, "Texture not set at index: {:d} !!", ptrdiff_t(iter - _tex.array.cbegin()));
		}
	}
#endif

	return(_tex.array); 
}

bool const cTextureBoy::KTXFileExists(std::wstring_view const path) const
{
	return(fs::exists(path));
}
bool const cTextureBoy::LoadKTXTexture(vku::TextureImage2D*& __restrict texture, std::wstring_view const path)
{
	return(LoadKTXTexture<vku::TextureImage2D>(texture, path));
}
bool const cTextureBoy::LoadKTXTexture(vku::TextureImage2DArray*& __restrict texture, std::wstring_view const path)
{
	return(LoadKTXTexture<vku::TextureImage2DArray>(texture, path));
}
bool const cTextureBoy::LoadKTXTexture(vku::TextureImage3D*& __restrict texture, std::wstring_view const path)
{
	return(LoadKTXTexture<vku::TextureImage3D>(texture, path));
}
bool const cTextureBoy::LoadKTXTexture(vku::TextureImageCube*& __restrict texture, std::wstring_view const path)
{
	return(LoadKTXTexture<vku::TextureImageCube>(texture, path));
}
bool const cTextureBoy::LoadKTX2Texture(vku::TextureImage2D*& __restrict texture, std::wstring_view const path)
{
	return(LoadKTXTexture<vku::TextureImage2D, 2>(texture, path));
}
bool const cTextureBoy::LoadKTX2Texture(vku::TextureImage2DArray*& __restrict texture, std::wstring_view const path)
{
	return(LoadKTXTexture<vku::TextureImage2DArray, 2>(texture, path));
}
bool const cTextureBoy::LoadKTX2Texture(vku::TextureImage3D*& __restrict texture, std::wstring_view const path)
{
	return(LoadKTXTexture<vku::TextureImage3D, 2>(texture, path));
}
bool const cTextureBoy::LoadKTX2Texture(vku::TextureImageCube*& __restrict texture, std::wstring_view const path)
{
	return(LoadKTXTexture<vku::TextureImageCube, 2>(texture, path));
}
void cTextureBoy::CleanUp()
{
	SAFE_RELEASE_DELETE(_noiseTexture);
}