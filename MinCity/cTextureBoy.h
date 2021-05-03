#pragma once
#include <Utility/class_helper.h>
#include <Utility/stringconv.h>
#include "betterenums.h"
#include <Imaging/Imaging/Imaging.h>
#include "cVulkan.h"
#include <Utility/mio/mmap.hpp>

BETTER_ENUM(eTex, uint32_t,
	KTX_CIRCUIT
);

//forward decl
namespace vku
{
	class TextureImage2D;
}

class no_vtable cTextureBoy : no_copy
{
	static constexpr uint32_t const NOISE_TEXTURE_SIZE = 128; // should be a power of 2
private:
	template<typename T, bool const WorkaroundLayerSizeDoubledInFileBug = false>
	bool const LoadKTXTexture(T*& __restrict texture, std::wstring_view const path);
public:
	// ######################################################################################
	// Adding textures to array used in common ara accessible by shaders
	// program order (order of the AddTextureToTextureArray being called) does not matter and can be at any moment
	// the order for textures is strictly defined in one header file, that is also shared with shaders
	// texturearray.glsl

	// (optional) desired index, if specified, can than not equal zero
	size_t const AddTextureToTextureArray(vku::TextureImage2DArray const* const __restrict texture, size_t const desired_index = 0ULL); // index used is returned(=desired_index, or next free index if desired_index is not specified) //
	size_t const AddTextureToTextureArray(vku::TextureImage2D* const __restrict texture, size_t const desired_index = 0ULL); // index used is returned(=desired_index, or next free index if desired_index is not specified) //

	// called intenally when descriptor set is set for the texture array, all textures that must be in the array should already be added
	tbb::concurrent_vector<vku::TextureImage2DArray const*> const& lockTextureArray();

	// ### LOADING ### //
	bool const KTXFileExists(std::wstring_view const path) const;
	bool const LoadKTXTexture(vku::TextureImage2D*& __restrict texture, std::wstring_view const path);
	template<bool const WorkaroundLayerSizeDoubledInFileBug = false>
	bool const LoadKTXTexture(vku::TextureImage2DArray*& __restrict texture, std::wstring_view const path);

	// simplest way to create a blank placeholder texture is to leverage the ImagingToTexture functions, with image initialized to blank memory

	// all of the ImagingToTexture functions are 
	// capable of creating a new texture if texture does not already exist
	// *or*
	// capable of updating an existing texture *infrequently* (if fast frame by frame updates of a texture are required then this should not be used. rather a custom update routine should be developed)
	// determined by texture argument being nullptr
	// **note bDedicatedMemory template parameter is ignored / not used in the updating context**

	template<bool const bDedicatedMemory = false>
	void ImagingToTexture(ImagingMemoryInstance const* const image, vku::TextureImage1DArray*& __restrict texture);  // default rgba
	template<bool const bDedicatedMemory = false>
	void ImagingToTexture(ImagingMemoryInstance const* const image, vku::TextureImage1D*& __restrict texture);  // default rgba
	template<bool const bDedicatedMemory = false>
	void ImagingToTexture(ImagingMemoryInstance const* const image, vku::TextureImage2DArray*& __restrict texture);	// default rgba
	template<bool const bDedicatedMemory = false>
	void ImagingToTexture(ImagingMemoryInstance const* const image, vku::TextureImage2D*& __restrict texture);	// default rgba
	template<bool const bDedicatedMemory = false>
	void ImagingToTexture_RG(ImagingMemoryInstance const* const image, vku::TextureImage2D*& __restrict texture);
	template<bool const bDedicatedMemory = false>
	void ImagingToTexture_R(ImagingMemoryInstance const* const image, vku::TextureImage2D*&__restrict texture);
	template<bool const bDedicatedMemory = false>
	void ImagingToTexture_BC7(ImagingMemoryInstance const* const image, vku::TextureImage2D*& __restrict texture); // compressed rgba
	// 3D lut always in dedicated memory 
	__inline void ImagingToTexture(ImagingLUT const* const lut, vku::TextureImage3D*& __restrict texture);	// default rgba
	// Sequences
	void ImagingSequenceToTexture(ImagingSequence const* const image, vku::TextureImage2DArray*& __restrict texture);

private:
	vk::CommandPool const& __restrict								transientPool() const;
	vk::CommandPool const& __restrict							    dmaTransferPool() const;
	vk::Queue const& __restrict										graphicsQueue() const;
	vk::Queue const& __restrict										transferQueue() const;
private:
	struct { // pool of textures, and they're layers, that is indexable and is set for the voxel common descriptor set specifically.
		tbb::concurrent_vector<vku::TextureImage2DArray const*>			array;
		bool															locked;
	} _tex;
	vku::TextureImage2D*											_noiseTexture;
	vk::Device const& __restrict									_device;
	
public:
	void Initialize();
	void CleanUp();

	cTextureBoy();
	~cTextureBoy() = default;
};

template<bool const bDedicatedMemory>
void  cTextureBoy::ImagingToTexture(ImagingMemoryInstance const* const image, vku::TextureImage1DArray*& __restrict texture)
{
	vk::CommandPool const* __restrict commandPool(&transientPool());
	vk::Queue const* __restrict queue(&graphicsQueue());
	if ((0 == image->xsize % 8)) {

		if (nullptr != texture) { // for an update of a texture - only before a dma transfer, texture must be transitioned on the graphics queue to transferdstoptimal //
			vku::executeImmediately(_device, *commandPool, *queue, [&](vk::CommandBuffer cb) {

				texture->setLayout(cb, vk::ImageLayout::eTransferDstOptimal);
			});
		}
		// switch to dma transfer queue
		commandPool = &dmaTransferPool();
		queue = &transferQueue();
	}

	if (nullptr == texture) {
		texture = new vku::TextureImage1DArray(_device, image->xsize, image->ysize,
											   vk::Format::eR8G8B8A8Unorm, false, bDedicatedMemory);
	}
	texture->upload<false>(_device, image->block, *commandPool, *queue);
	texture->finalizeUpload(_device, transientPool(), graphicsQueue()); // must be graphics queue for shaderreadonly to be set
}
template<bool const bDedicatedMemory>
void  cTextureBoy::ImagingToTexture(ImagingMemoryInstance const* const image, vku::TextureImage1D*& __restrict texture)
{
	vk::CommandPool const* __restrict commandPool(&transientPool());
	vk::Queue const* __restrict queue(&graphicsQueue());
	if ((0 == image->xsize % 8)) {

		if (nullptr != texture) { // for an update of a texture - only before a dma transfer, texture must be transitioned on the graphics queue to transferdstoptimal //
			vku::executeImmediately(_device, *commandPool, *queue, [&](vk::CommandBuffer cb) {

				texture->setLayout(cb, vk::ImageLayout::eTransferDstOptimal);
			});
		}
		// switch to dma transfer queue
		commandPool = &dmaTransferPool();
		queue = &transferQueue();
	}

	if (nullptr == texture) {
		texture = new vku::TextureImage1D(_device, image->xsize,
			vk::Format::eR8G8B8A8Unorm, false, bDedicatedMemory);
	}
	texture->upload<false>(_device, image->block, *commandPool, *queue);
	texture->finalizeUpload(_device, transientPool(), graphicsQueue()); // must be graphics queue for shaderreadonly to be set
}
template<bool const bDedicatedMemory>
void  cTextureBoy::ImagingToTexture(ImagingMemoryInstance const* const image, vku::TextureImage2DArray*& __restrict texture)	// always updates array layer 0, or creates a texture array with 1 layer
{
	vk::CommandPool const* __restrict commandPool(&transientPool());
	vk::Queue const* __restrict queue(&graphicsQueue());

	if ((0 == image->xsize % 8) && (0 == image->ysize % 8)) {

		if (nullptr != texture) { // for an update of a texture - only before a dma transfer, texture must be transitioned on the graphics queue to transferdstoptimal //
			vku::executeImmediately(_device, *commandPool, *queue, [&](vk::CommandBuffer cb) {

				texture->setLayout(cb, vk::ImageLayout::eTransferDstOptimal);
				});
		}
		// switch to dma transfer queue
		commandPool = &dmaTransferPool();
		queue = &transferQueue();
	}

	if (nullptr == texture) {
		texture = new vku::TextureImage2DArray(_device, image->xsize, image->ysize, 1, // this is for a single 2D image / layer,
			vk::Format::eR8G8B8A8Unorm, false, bDedicatedMemory);								  // for array with n layers content, use ImagingSequenceToTexture
	}

	texture->upload<false>(_device, image->block, image->ysize * image->linesize, 0, *commandPool, *queue);
	texture->finalizeUpload(_device, transientPool(), graphicsQueue()); // must be graphics queue for shaderreadonly to be set
}
template<bool const bDedicatedMemory>
void  cTextureBoy::ImagingToTexture(ImagingMemoryInstance const* const image, vku::TextureImage2D*& __restrict texture)
{
	vk::CommandPool const* __restrict commandPool(&transientPool());
	vk::Queue const* __restrict queue(&graphicsQueue());

	if ((0 == image->xsize % 8) && (0 == image->ysize % 8)) {

		if (nullptr != texture) { // for an update of a texture - only before a dma transfer, texture must be transitioned on the graphics queue to transferdstoptimal //
			vku::executeImmediately(_device, *commandPool, *queue, [&](vk::CommandBuffer cb) {

				texture->setLayout(cb, vk::ImageLayout::eTransferDstOptimal);
			});
		}
		// switch to dma transfer queue
		commandPool = &dmaTransferPool();
		queue = &transferQueue();
	}

	if (nullptr == texture) {
		texture = new vku::TextureImage2D(_device, image->xsize, image->ysize,
			vk::Format::eR8G8B8A8Unorm, false, bDedicatedMemory);
	}

	texture->upload<false>(_device, image->block, *commandPool, *queue);
	texture->finalizeUpload(_device, transientPool(), graphicsQueue()); // must be graphics queue for shaderreadonly to be set
}
template<bool const bDedicatedMemory>
void  cTextureBoy::ImagingToTexture_RG(ImagingMemoryInstance const* const image, vku::TextureImage2D*& __restrict texture)
{
	vk::CommandPool const* __restrict commandPool(&transientPool());
	vk::Queue const* __restrict queue(&graphicsQueue());
	if ((0 == image->xsize % 8) && (0 == image->ysize % 8)) {

		if (nullptr != texture) { // for an update of a texture - only before a dma transfer, texture must be transitioned on the graphics queue to transferdstoptimal //
			vku::executeImmediately(_device, *commandPool, *queue, [&](vk::CommandBuffer cb) {

				texture->setLayout(cb, vk::ImageLayout::eTransferDstOptimal);
			});
		}
		// switch to dma transfer queue
		commandPool = &dmaTransferPool();
		queue = &transferQueue();
	}

	if (nullptr == texture) {
		texture = new vku::TextureImage2D(_device, image->xsize, image->ysize,
			vk::Format::eR8G8Unorm, false, bDedicatedMemory);
	}
	texture->upload<false>(_device, image->block, *commandPool, *queue);
	texture->finalizeUpload(_device, transientPool(), graphicsQueue()); // must be graphics queue for shaderreadonly to be set
}
template<bool const bDedicatedMemory>
void  cTextureBoy::ImagingToTexture_R(ImagingMemoryInstance const* const image, vku::TextureImage2D*& __restrict texture)
{
	vk::CommandPool const* __restrict commandPool(&transientPool());
	vk::Queue const* __restrict queue(&graphicsQueue());
	if ((0 == image->xsize % 8) && (0 == image->ysize % 8)) {

		if (nullptr != texture) { // for an update of a texture - only before a dma transfer, texture must be transitioned on the graphics queue to transferdstoptimal //
			vku::executeImmediately(_device, *commandPool, *queue, [&](vk::CommandBuffer cb) {

				texture->setLayout(cb, vk::ImageLayout::eTransferDstOptimal);
			});
		}
		// switch to dma transfer queue
		commandPool = &dmaTransferPool();
		queue = &transferQueue();
	}

	if (nullptr == texture) {
		texture = new vku::TextureImage2D(_device, image->xsize, image->ysize,
			vk::Format::eR8Unorm, false, bDedicatedMemory);
	}
	texture->upload<false>(_device, image->block, *commandPool, *queue);
	texture->finalizeUpload(_device, transientPool(), graphicsQueue()); // must be graphics queue for shaderreadonly to be set
}
template<bool const bDedicatedMemory>
void  cTextureBoy::ImagingToTexture_BC7(ImagingMemoryInstance const* const image, vku::TextureImage2D*& __restrict texture)
{
	vk::CommandPool const* __restrict commandPool(&transientPool());
	vk::Queue const* __restrict queue(&graphicsQueue());
	if ((0 == image->xsize % 8) && (0 == image->ysize % 8)) {

		if (nullptr != texture) { // for an update of a texture - only before a dma transfer, texture must be transitioned on the graphics queue to transferdstoptimal //
			vku::executeImmediately(_device, *commandPool, *queue, [&](vk::CommandBuffer cb) {

				texture->setLayout(cb, vk::ImageLayout::eTransferDstOptimal);
			});
		}
		// switch to dma transfer queue
		commandPool = &dmaTransferPool();
		queue = &transferQueue();
	}

	if (nullptr == texture) {
		texture = new vku::TextureImage2D(_device, image->xsize, image->ysize,
			vk::Format::eBc7UnormBlock, false, bDedicatedMemory);
	}
	texture->upload<false>(_device, image->block, *commandPool, *queue);
	texture->finalizeUpload(_device, transientPool(), graphicsQueue()); // must be graphics queue for shaderreadonly to be set
}

__inline void  cTextureBoy::ImagingToTexture(ImagingLUT const* const image, vku::TextureImage3D*& __restrict texture)
{
	vk::CommandPool const* __restrict commandPool(&transientPool());
	vk::Queue const* __restrict queue(&graphicsQueue());

	if ((0 == image->size % 8)) {

		if (nullptr != texture) { // for an update of a texture - only before a dma transfer, texture must be transitioned on the graphics queue to transferdstoptimal //
			vku::executeImmediately(_device, *commandPool, *queue, [&](vk::CommandBuffer cb) {

				texture->setLayout(cb, vk::ImageLayout::eTransferDstOptimal);
			});
		}
		// switch to dma transfer queue
		commandPool = &dmaTransferPool();
		queue = &transferQueue();
	}

	if (nullptr == texture) {
		texture = new vku::TextureImage3D(_device, image->size, image->size, image->size,
			vk::Format::eR16G16B16A16Unorm, false, true);
	}
	texture->upload<false>(_device, image->block, image->slicesize * image->size, *commandPool, *queue);
	texture->finalizeUpload(_device, transientPool(), graphicsQueue()); // must be graphics queue for shaderreadonly to be set
}

template<typename T, bool const WorkaroundLayerSizeDoubledInFileBug>
bool const cTextureBoy::LoadKTXTexture(T*& __restrict texture, std::wstring_view const path)
{
	std::error_code error{};

	mio::mmap_source mmap = mio::make_mmap_source(path, error);
	if (!error) {

		if (mmap.is_open() && mmap.is_mapped()) {
			__prefetch_vmem(mmap.data(), mmap.size());

			uint8_t const* const pReadPointer((uint8_t*)mmap.data());

			vku::KTXFileLayout<WorkaroundLayerSizeDoubledInFileBug> const ktxFile(pReadPointer, pReadPointer + mmap.length());

			if (ktxFile.ok()) {

				uint32_t const width(ktxFile.width(0)), height(ktxFile.height(0));
				vk::CommandPool const* __restrict commandPool(&transientPool());
				vk::Queue const* __restrict queue(&graphicsQueue());
				if ((0 == width % 8) && (0 == height % 8)) {
					commandPool = &dmaTransferPool();
					queue = &transferQueue();
				}

				if constexpr (std::is_same<T, vku::TextureImage2DArray>::value) {
					texture = new T(_device, width, height, ktxFile.arrayLayers(), ktxFile.format());
				}
				else {
					texture = new T(_device, width, height, 1, ktxFile.format());
				}
				// must use the upload in KTXFile
				ktxFile.upload(_device, *texture, pReadPointer, *commandPool, *queue);
				ktxFile.finalizeUpload(_device, *texture, transientPool(), graphicsQueue()); // must be graphics queue for shaderreadonly to be set

				FMT_LOG_OK(TEX_LOG, "loaded KTX texture [{:s}] {:s}", vk::to_string(ktxFile.format()), stringconv::ws2s(path));
				return(true);
			}
			else
				FMT_LOG_FAIL(TEX_LOG, "unable to parse KTX file: {:s}", stringconv::ws2s(path));
		}
		else
			FMT_LOG_FAIL(TEX_LOG, "unable to open or mmap KTX file: {:s}", stringconv::ws2s(path));
	}
	else
		FMT_LOG_FAIL(TEX_LOG, "unable to open KTX file: {:s}", stringconv::ws2s(path));

	return(false);
}

template<bool const WorkaroundLayerSizeDoubledInFileBug> // placed in header file to support Workaround
bool const cTextureBoy::LoadKTXTexture(vku::TextureImage2DArray*& __restrict texture, std::wstring_view const path)
{
	return(LoadKTXTexture<vku::TextureImage2DArray, WorkaroundLayerSizeDoubledInFileBug>(texture, path));
}


