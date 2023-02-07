/* Copyright (C) 20xx Jason Tully - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License
 * http://www.supersinfulsilicon.com/
 *
This work is licensed under the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
To view a copy of this license, visit http://creativecommons.org/licenses/by-nc-sa/4.0/
or send a letter to Creative Commons, PO Box 1866, Mountain View, CA 94042, USA.
 */

#include "pch.h"
#include "volumetricOpacity.h"
#include "MinCity.h"
#include "cTextureBoy.h"

namespace Volumetric
{
	namespace internal
	{
		void InitializeOpacityMap(uint32_t const world_volume_size, vku::TextureImage2D*& srcGround)
		{
			Imaging imgGround = ImagingNew(MODE_L, world_volume_size, world_volume_size); // Single Channel temporary image
			memset(imgGround->block, 0x7f, imgGround->xsize * imgGround->ysize * imgGround->pixelsize); // set all pixels to the opacity of a non emissive ground voxel.

			MinCity::TextureBoy->ImagingToTexture_R<false>(imgGround, srcGround, vk::ImageUsageFlagBits::eTransferSrc); // create gpu resource

			ImagingDelete(imgGround); // release cpu resource

			// step 1 complete
		}

		void RenderInitializeOpacityMap(uint32_t const world_volume_size, vk::CommandBuffer& cb, vku::TextureImage2D const* const srcGround, vku::TextureImageStorage3D const* const dstVolume)
		{
			vk::ImageSubresourceLayers const srcLayer(vk::ImageAspectFlagBits::eColor, 0, 0, 1);
			vk::ImageSubresourceLayers const dstLayer(vk::ImageAspectFlagBits::eColor, 0, 0, 1);

			std::array<vk::Offset3D, 2> const srcOffsets = { vk::Offset3D(), vk::Offset3D(world_volume_size, world_volume_size, 1) };
			std::array<vk::Offset3D, 2> const dstOffsets = { vk::Offset3D(), vk::Offset3D(world_volume_size, world_volume_size, 1) };

			vk::ImageBlit const region(srcLayer, srcOffsets, dstLayer, dstOffsets);

			cb.blitImage(srcGround->image(), vk::ImageLayout::eTransferSrcOptimal, dstVolume->image(), vk::ImageLayout::eTransferDstOptimal, 1, &region, vk::Filter::eNearest);

			// srcGround texture released outside this function
		}

	} // end ns
} // end ns


