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
	template<typename T, uint32_t const version>
	void LoadKTXTexture(T*& __restrict texture, KTXFileLayout<version> const& __restrict ktxFile, uint8_t const* const __restrict pReadPointer);
	template<typename T>
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
	// format inside of KTX file, make sure to save in the correct format! (srgb vs unorm) 
	bool const LoadKTXTexture(vku::TextureImageCube*& __restrict texture, std::wstring_view const path);
	bool const LoadKTXTexture(vku::TextureImage3D*& __restrict texture, std::wstring_view const path);
	bool const LoadKTXTexture(vku::TextureImage2D*& __restrict texture, std::wstring_view const path);
	bool const LoadKTXTexture(vku::TextureImage2DArray*& __restrict texture, std::wstring_view const path);
	
	// simplest way to create a blank placeholder texture is to leverage the ImagingToTexture functions, with image initialized to blank memory

	// all of the ImagingToTexture functions are 
	// capable of creating a new texture if texture does not already exist
	// *or*
	// capable of updating an existing texture *infrequently* (if fast frame by frame updates of a texture are required then this should not be used. rather a custom update routine should be developed)
	// determined by texture argument being nullptr
	// **note bDedicatedMemory template parameter is ignored / not used in the updating context**

	// an optional buffer can be supplied resulting in non-blocking operation. Otherwise these functions block until the gpu texture is updated with the image. Must be allocated and of same size as the image.
	template<bool const bSrgb, bool const bDedicatedMemory = false>
	void ImagingToTexture(ImagingMemoryInstance const* const image, vku::TextureImage1DArray*& __restrict texture, vk::ImageUsageFlags const usage = (vk::ImageUsageFlagBits)0, vku::GenericBuffer* const __restrict buffer = nullptr);  // default rgba 8bpc 1D array
	template<bool const bSrgb, bool const bDedicatedMemory = false>
	void ImagingToTexture(ImagingMemoryInstance const* const image, vku::TextureImage1D*& __restrict texture, vk::ImageUsageFlags const usage = (vk::ImageUsageFlagBits)0, vku::GenericBuffer* const __restrict buffer = nullptr);  // default rgba 8bpc
	template<bool const bSrgb, bool const bDedicatedMemory = false>
	void ImagingToTexture(ImagingMemoryInstance const* const image, vku::TextureImage2DArray*& __restrict texture, vk::ImageUsageFlags const usage = (vk::ImageUsageFlagBits)0, vku::GenericBuffer* const __restrict buffer = nullptr);	// default rgba 8bpc 2D array
	template<bool const bSrgb, bool const bDedicatedMemory = false>
	void ImagingToTexture(ImagingMemoryInstance const* const image, vku::TextureImage2D*& __restrict texture, vk::ImageUsageFlags const usage = (vk::ImageUsageFlagBits)0, vku::GenericBuffer* const __restrict buffer = nullptr);	// default rgba 8bpc
	template<bool const bSrgb, bool const bDedicatedMemory = false>
	void ImagingToTexture_RG(ImagingMemoryInstance const* const image, vku::TextureImage2D*& __restrict texture, vk::ImageUsageFlags const usage = (vk::ImageUsageFlagBits)0, vku::GenericBuffer* const __restrict buffer = nullptr); // rg 8bpc
	template<bool const bSrgb, bool const bDedicatedMemory = false>
	void ImagingToTexture_R(ImagingMemoryInstance const* const image, vku::TextureImage2D*&__restrict texture, vk::ImageUsageFlags const usage = (vk::ImageUsageFlagBits)0, vku::GenericBuffer* const __restrict buffer = nullptr); // r 8bpc
	template<bool const bSrgb, bool const bDedicatedMemory = false>
	void ImagingToTexture_BC7(ImagingMemoryInstance const* const image, vku::TextureImage2D*& __restrict texture); // compressed rgba
	// 3D lut always in dedicated memory and is not srgb
	__inline void ImagingToTexture(ImagingLUT const* const lut, vku::TextureImage3D*& __restrict texture);	// default rgba 16bpc
	// Sequences, always non dedicated memory and is srgb
	void ImagingSequenceToTexture(ImagingSequence const* const image, vku::TextureImage2DArray*& __restrict texture); // default rgba 8bpc

	// ### UPDATING ### //
	template <typename T>
	void UpdateTexture(T const* const __restrict bytes, vku::TextureImage3D*& __restrict texture) const;
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

template<bool const bSrgb, bool const bDedicatedMemory>
void  cTextureBoy::ImagingToTexture(ImagingMemoryInstance const* const image, vku::TextureImage1DArray*& __restrict texture, vk::ImageUsageFlags const usage, vku::GenericBuffer* const __restrict buffer)
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

		vk::Format format;

		if constexpr (bSrgb) {
			format = vk::Format::eR8G8B8A8Srgb;
		}
		else {
			format = vk::Format::eR8G8B8A8Unorm;
		}

		texture = new vku::TextureImage1DArray(usage, _device, image->xsize, image->ysize,
											   format, false, bDedicatedMemory);
	}

	if (buffer) {
		buffer->updateLocal(image->block, image->xsize * image->linesize);
		texture->upload<false>(_device, *buffer, 0, *commandPool, *queue);
	}
	else {
		texture->upload<false>(_device, image->block, 0, *commandPool, *queue);
	}
	texture->finalizeUpload(_device, transientPool(), graphicsQueue()); // must be graphics queue for shaderreadonly to be set
}
template<bool const bSrgb, bool const bDedicatedMemory>
void  cTextureBoy::ImagingToTexture(ImagingMemoryInstance const* const image, vku::TextureImage1D*& __restrict texture, vk::ImageUsageFlags const usage, vku::GenericBuffer* const __restrict buffer)
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

		vk::Format format;

		if constexpr (bSrgb) {
			format = vk::Format::eR8G8B8A8Srgb;
		}
		else {
			format = vk::Format::eR8G8B8A8Unorm;
		}

		texture = new vku::TextureImage1D(usage, _device, image->xsize,
										  1, format, false, bDedicatedMemory);
	}

	if (buffer) {
		buffer->updateLocal(image->block, image->xsize * image->linesize);
		texture->upload<false>(_device, *buffer, *commandPool, *queue);
	}
	else {
		texture->upload<false>(_device, image->block, *commandPool, *queue);
	}
	texture->finalizeUpload(_device, transientPool(), graphicsQueue()); // must be graphics queue for shaderreadonly to be set
}
template<bool const bSrgb, bool const bDedicatedMemory>
void  cTextureBoy::ImagingToTexture(ImagingMemoryInstance const* const image, vku::TextureImage2DArray*& __restrict texture, vk::ImageUsageFlags const usage, vku::GenericBuffer* const __restrict buffer)	// always updates array layer 0, or creates a texture array with 1 layer
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

		vk::Format format;

		if constexpr (bSrgb) {
			format = vk::Format::eR8G8B8A8Srgb;
		}
		else {
			format = vk::Format::eR8G8B8A8Unorm;
		}

		texture = new vku::TextureImage2DArray(usage, _device, image->xsize, image->ysize, 1, 1, // this is for a single 2D image / layer,
											   format, false, bDedicatedMemory);	      // for array with n layers content, use ImagingSequenceToTexture
	}

	if (buffer) {
		buffer->updateLocal(image->block, image->ysize * image->linesize);
		texture->upload<false>(_device, *buffer, 0, *commandPool, *queue);
	}
	else {
		texture->upload<false>(_device, image->block, image->ysize * image->linesize, 0, *commandPool, *queue);
	}
	texture->finalizeUpload(_device, transientPool(), graphicsQueue()); // must be graphics queue for shaderreadonly to be set
}
template<bool const bSrgb, bool const bDedicatedMemory>
void  cTextureBoy::ImagingToTexture(ImagingMemoryInstance const* const image, vku::TextureImage2D*& __restrict texture, vk::ImageUsageFlags const usage, vku::GenericBuffer* const __restrict buffer)
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

		vk::Format format;

		if constexpr (bSrgb) {
			format = vk::Format::eR8G8B8A8Srgb;
		}
		else {
			format = vk::Format::eR8G8B8A8Unorm;
		}

		texture = new vku::TextureImage2D(usage, _device, image->xsize, image->ysize, 1,
										  format, false, bDedicatedMemory);
	}

	if (buffer) {
		buffer->updateLocal(image->block, image->ysize * image->linesize);
		texture->upload<false>(_device, *buffer, *commandPool, *queue);
	}
	else {
		texture->upload<false>(_device, image->block, *commandPool, *queue);
	}
	texture->finalizeUpload(_device, transientPool(), graphicsQueue()); // must be graphics queue for shaderreadonly to be set
}
template<bool const bSrgb, bool const bDedicatedMemory>
void  cTextureBoy::ImagingToTexture_RG(ImagingMemoryInstance const* const image, vku::TextureImage2D*& __restrict texture, vk::ImageUsageFlags const usage, vku::GenericBuffer* const __restrict buffer)
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

		vk::Format format;

		if constexpr (bSrgb) {
			format = vk::Format::eR8G8Srgb;
		}
		else {
			format = (image->mode == MODE_LA16) ? vk::Format::eR16G16Unorm : vk::Format::eR8G8Unorm;
		}

		texture = new vku::TextureImage2D(usage, _device, image->xsize, image->ysize, 1,
										  format, false, bDedicatedMemory);
	}

	if (buffer) {
		buffer->updateLocal(image->block, image->ysize * image->linesize);
		texture->upload<false>(_device, *buffer, *commandPool, *queue);
	}
	else {
		texture->upload<false>(_device, image->block, *commandPool, *queue);
	}
	texture->finalizeUpload(_device, transientPool(), graphicsQueue()); // must be graphics queue for shaderreadonly to be set
}
template<bool const bSrgb, bool const bDedicatedMemory>
void  cTextureBoy::ImagingToTexture_R(ImagingMemoryInstance const* const image, vku::TextureImage2D*& __restrict texture, vk::ImageUsageFlags const usage, vku::GenericBuffer* const __restrict buffer)
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

		vk::Format format;

		if constexpr (bSrgb) {
			format = vk::Format::eR8Srgb;
		}
		else {
			format = (image->mode == MODE_L16) ? vk::Format::eR16Unorm : vk::Format::eR8Unorm;
		}

		texture = new vku::TextureImage2D(usage, _device, image->xsize, image->ysize, 1,
										  format, false, bDedicatedMemory);
	}

	if (buffer) {
		buffer->updateLocal(image->block, image->ysize * image->linesize);
		texture->upload<false>(_device, *buffer, *commandPool, *queue);
	}
	else {
		texture->upload<false>(_device, image->block, *commandPool, *queue);
	}
	texture->finalizeUpload(_device, transientPool(), graphicsQueue()); // must be graphics queue for shaderreadonly to be set
}
template<bool const bSrgb, bool const bDedicatedMemory>
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

		vk::Format format;

		if constexpr (bSrgb) {
			format = vk::Format::eBc7SrgbBlock;
		}
		else {
			format = vk::Format::eBc7UnormBlock;
		}

		texture = new vku::TextureImage2D(_device, image->xsize, image->ysize,
										  format, false, bDedicatedMemory);
	}
	texture->upload<false>(_device, image->block, *commandPool, *queue);
	texture->finalizeUpload(_device, transientPool(), graphicsQueue()); // must be graphics queue for shaderreadonly to be set
}

__inline void cTextureBoy::ImagingToTexture(ImagingLUT const* const image, vku::TextureImage3D*& __restrict texture)
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
		texture = new vku::TextureImage3D(_device, image->size, image->size, image->size, 1,
			vk::Format::eR16G16B16A16Unorm, false, true);
	}
	texture->upload<false>(_device, image->block, image->slicesize * image->size, *commandPool, *queue);
	texture->finalizeUpload(_device, transientPool(), graphicsQueue()); // must be graphics queue for shaderreadonly to be set
}

template<typename T, uint32_t const version>
void cTextureBoy::LoadKTXTexture(T*& __restrict texture, KTXFileLayout<version> const& __restrict ktxFile, uint8_t const* const __restrict pReadPointer)
{
	vk::CommandPool const* __restrict commandPool(&transientPool());
	vk::Queue const* __restrict queue(&graphicsQueue());

	static constexpr uint32_t const promote_rgb8(1), promote_rgb16(2);
	vk::Format format(vk::Format(ktxFile.vkformat())); // this is a stupid cast required to construct
	uint32_t bPromoteRGB(0);

	// *bugfix - handle files with only 3 components instead of 4
	switch (format) // promote format for texture image.
	{
	case vk::Format::eR8G8B8Unorm:
		format = vk::Format::eR8G8B8A8Unorm;
		bPromoteRGB = promote_rgb8;
		break;
	case vk::Format::eR8G8B8Srgb:
		format = vk::Format::eR8G8B8A8Srgb;
		bPromoteRGB = promote_rgb8;
		break;
	case vk::Format::eB8G8R8Unorm:
		format = vk::Format::eB8G8R8A8Unorm;
		bPromoteRGB = promote_rgb8;
		break;
	case vk::Format::eB8G8R8Srgb:
		format = vk::Format::eB8G8R8A8Srgb;
		bPromoteRGB = promote_rgb8;
		break;
	case vk::Format::eR16G16B16Unorm:
		format = vk::Format::eR16G16B16A16Unorm;
		bPromoteRGB = promote_rgb16;
		break;
	case vk::Format::eR16G16B16Snorm:
		format = vk::Format::eR16G16B16A16Snorm;
		bPromoteRGB = promote_rgb16;
		break;
	default:
		break;
	}

	// 3D
	if constexpr (std::is_same<T, vku::TextureImage3D>::value) {
		uint32_t const width(ktxFile.width(0)), height(ktxFile.height(0)), depth(ktxFile.depth(0));

		if ((0 == width % 8) && (0 == height % 8) && (0 == depth % 8)) {
			commandPool = &dmaTransferPool();
			queue = &transferQueue();
		}
		texture = new T(_device, width, height, depth, ktxFile.mipLevels(), format);
	}
	else { // 2D
		uint32_t const width(ktxFile.width(0)), height(ktxFile.height(0));

		if ((0 == width % 8) && (0 == height % 8)) {
			commandPool = &dmaTransferPool();
			queue = &transferQueue();
		}

		if constexpr (std::is_same<T, vku::TextureImage2DArray>::value) {
			texture = new T(_device, width, height, ktxFile.arrayLayers(), ktxFile.mipLevels(), format);
		}
		else { // vku::TextureImage2D or vku::TextureImageCube
			texture = new T(_device, width, height, ktxFile.mipLevels(), format);
		}
	}

	if (texture) {

		// upload to gpu
		uint32_t totalActualSize(0);

		for (auto const& size : ktxFile.sizes()) {
			totalActualSize += size;
		}

		if (0 == totalActualSize)
			return;

		auto const bp = vku::getBlockParams(format); // *bugfix - override required for proper final format in RGB --> BGRA promotion

		// bugfix: sometimes the image size is greater than the actual binary size of the data, due to an "upgrade" in alignment
		// so source buffer must have the same size as the image being copied too. The copy into the source buffer only copies the actual size of data,
		// with the rest being zeroed out.
		vk::DeviceSize const alignedSize((uint64_t)SFM::roundToMultipleOf<true>((int64_t)texture->size(), (int64_t)bp.bytesPerBlock));

		vku::GenericBuffer stagingBuffer((vk::BufferUsageFlags)vk::BufferUsageFlagBits::eTransferSrc, alignedSize, vk::MemoryPropertyFlagBits::eHostCoherent | vk::MemoryPropertyFlagBits::eHostVisible, VMA_MEMORY_USAGE_AUTO_PREFER_HOST, vku::eMappedAccess::Sequential);

		uint32_t const baseOffset(ktxFile.offset(0, 0, 0));

		if (!bPromoteRGB) {
			// now loading file data to stagingbuffer
			stagingBuffer.updateLocal(pReadPointer + baseOffset, totalActualSize);
		}
		else { // promote RGB data (pReadPointer) to BGRA (stagingBuffer)

			// lock, clear, convert, copy, unlock, flush the output staging buffer
			uint8_t* const __restrict ptr(static_cast<uint8_t* const __restrict>(stagingBuffer.map()));

			memset(ptr, 0, (size_t)stagingBuffer.maxsizebytes());

			uint32_t image_output_offset(0);

			for (uint32_t mipLevel = 0; mipLevel != ktxFile.mipLevels(); ++mipLevel) {
				auto const width = ktxFile.width(mipLevel);
				auto const height = ktxFile.height(mipLevel);

				uint32_t layer_output_size(ktxFile.layer_size(mipLevel));
				layer_output_size += layer_output_size / 3 + layer_output_size % 3; // add alpha component (exact) for every pixel in layer

				for (uint32_t layer = 0; layer != ktxFile.arrayLayers(); ++layer) {

					if (promote_rgb16 == bPromoteRGB) {
						ImagingFastRGB16TOBGRX16(reinterpret_cast<uint16_t* const __restrict>(ptr + image_output_offset),
							                     reinterpret_cast<uint16_t const* const __restrict>(pReadPointer + (ktxFile.offset(mipLevel, layer, 0))), width, height);
					}
					else { // promote_rgb8
						ImagingFastRGBTOBGRX(ptr + image_output_offset,
							                 pReadPointer + (ktxFile.offset(mipLevel, layer, 0)), width, height);
					}

					image_output_offset += layer_output_size;

					// reference:
					//texture->copy(cb, buf, mipLevel, layer, width, height, depth, (uint64_t)SFM::roundToMultipleOf<true>((int64_t)(ktxFile.offset(mipLevel, layer, 0) - baseOffset), (int64_t)bp.bytesPerBlock));
				}
			}
			stagingBuffer.unmap();
			stagingBuffer.flush(stagingBuffer.maxsizebytes());
		}
		// now loading stagingbuffer to image

		// Copy the staging buffer to the GPU texture and set the layout.
		vku::executeImmediately<false>(_device, *commandPool, *queue, [&](vk::CommandBuffer cb) {
			vk::Buffer buf = stagingBuffer.buffer();
			for (uint32_t mipLevel = 0; mipLevel != ktxFile.mipLevels(); ++mipLevel) {
				auto const width = ktxFile.width(mipLevel);
				auto const height = ktxFile.height(mipLevel);
				auto const depth = ktxFile.depth(mipLevel);
				for (uint32_t layer = 0; layer != ktxFile.arrayLayers(); ++layer) {
					texture->copy(cb, buf, mipLevel, layer, width, height, depth, (uint64_t)SFM::roundToMultipleOf<true>((int64_t)(ktxFile.offset(mipLevel, layer, 0) - baseOffset), (int64_t)bp.bytesPerBlock));
				}
			}
		});

		// finalize texture
		vku::executeImmediately(_device, transientPool(), graphicsQueue(), [&](vk::CommandBuffer cb) { // must be graphics queue for shaderreadonly to be set

			texture->setLayout(cb, vk::ImageLayout::eShaderReadOnlyOptimal);

		});
	}
}
template<typename T>
bool const cTextureBoy::LoadKTXTexture(T*& __restrict texture, std::wstring_view const path)
{
	static constexpr wchar_t const* const EXTENSION_KTX1 = L".ktx";
	static constexpr wchar_t const* const EXTENSION_KTX2 = L".ktx2";

	std::filesystem::path const filename(path);

	uint32_t version(0);

	if (std::filesystem::exists(filename)) {

		if (filename.extension() == EXTENSION_KTX1) {
			version = KTX_VERSION::KTX1;
		}
		else if (filename.extension() == EXTENSION_KTX2) {
			version = KTX_VERSION::KTX2;
		}
	}
	else {
		FMT_LOG_FAIL(TEX_LOG, "KTX file  {:s}  does not exist", stringconv::ws2s(path));
		return(false);
	}

	if (0 != version) {
		std::error_code error{};

		mio::mmap_source mmap = mio::make_mmap_source(path, FILE_FLAG_SEQUENTIAL_SCAN | FILE_ATTRIBUTE_NORMAL, error);
		if (!error) {

			if (mmap.is_open() && mmap.is_mapped()) {
				___prefetch_vmem(mmap.data(), mmap.size());

				uint8_t const* const pReadPointer((uint8_t*)mmap.data());

				if (KTX_VERSION::KTX1 == version) {
					KTXFileLayout<KTX_VERSION::KTX1> const ktxFile(pReadPointer, pReadPointer + mmap.length());

					if (ktxFile.ok()) {

						LoadKTXTexture<T, KTX_VERSION::KTX1>(texture, ktxFile, pReadPointer);
						FMT_LOG_OK(TEX_LOG, "loaded KTX texture [{:s}] {:s}", vk::to_string(vk::Format(ktxFile.vkformat())), stringconv::ws2s(path));
						return(true);
					}
				}
				else {
					KTXFileLayout<KTX_VERSION::KTX2> const ktx2File(pReadPointer, pReadPointer + mmap.length());

					if (ktx2File.ok()) {

						LoadKTXTexture<T, KTX_VERSION::KTX2>(texture, ktx2File, pReadPointer);
						FMT_LOG_OK(TEX_LOG, "loaded KTX texture [{:s}] {:s}", vk::to_string(vk::Format(ktx2File.vkformat())), stringconv::ws2s(path));
						return(true);
					}
					else
						FMT_LOG_FAIL(TEX_LOG, "unable to parse KTX file: {:s}", stringconv::ws2s(path));
				}
			}
			else
				FMT_LOG_FAIL(TEX_LOG, "unable to open or mmap KTX file: {:s}", stringconv::ws2s(path));
		}
		else
			FMT_LOG_FAIL(TEX_LOG, "unable to open KTX file: {:s}", stringconv::ws2s(path));
	}
	else
		FMT_LOG_FAIL(TEX_LOG, "unable to version KTX file: {:s}", stringconv::ws2s(path));

	return(false);
}

template <typename T>
void cTextureBoy::UpdateTexture(T const* const __restrict bytes, vku::TextureImage3D*& __restrict texture) const // todo currently assuming 4 components rgba in size calculation below
{
	vk::CommandPool const* __restrict commandPool(&transientPool());
	vk::Queue const* __restrict queue(&graphicsQueue());

	vk::Extent3D const extents(texture->extent());

	if (extents.width == extents.height == extents.depth) {
		if ((0 == extents.width % 8)) {
			commandPool = &dmaTransferPool();
			queue = &transferQueue();
		}
	}

	texture->upload(_device, bytes, extents.width * extents.height * extents.depth * sizeof(T) * 4, *commandPool, *queue);
}