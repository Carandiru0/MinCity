#pragma once
/* Copyright (C) 20xx Jason Tully - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License
 * http://www.supersinfulsilicon.com/
 *
This work is licensed under the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
To view a copy of this license, visit http://creativecommons.org/licenses/by-nc-sa/4.0/
or send a letter to Creative Commons, PO Box 1866, Mountain View, CA 94042, USA.
 */
#include <Math/point2D_t.h>
#include <Math/superfastmath.h>
#include <vector>

namespace Volumetric
{
	namespace voxB
	{

		typedef struct voxelSequence
		{
		private:
			vector<uint32_t>         offsets;
			vector<uint32_t>         sizes;	// number of voxels for each frame
			vector<std::string>		 channel_names;
		public:
			uint32_t const getOffset(uint32_t const frame) const { return(offsets[frame]); }
			uint32_t const numVoxels(uint32_t const frame) const { return(sizes[frame]); } // number of voxels for a given frame (frame index is not checked and must be valid - no bounds checking)
			uint32_t const numFrames() const { return((uint32_t)sizes.size()); } // number of frames in the sequence
			
			std::string_view const  getChannelName(uint32_t const channel) const { return(channel_names[channel]); }
			uint32_t const			numChannels() const { return((uint32_t)channel_names.size()); }
			
			void addFrame(uint32_t const offset, uint32_t const size) { offsets.push_back(offset); sizes.push_back(size); }

			void addChannel(uint32_t const channel, std::string const channel_name) { channel_names.emplace_back(); channel_names[channel] = channel_name; }
			
			voxelSequence() = default;

			voxelSequence(voxelSequence const& src) noexcept
				: offsets(src.offsets), sizes(src.sizes), channel_names( src.channel_names )
			{}

			voxelSequence& operator=(voxelSequence const& src) noexcept
			{
				offsets = src.offsets;
				sizes = src.sizes;
				channel_names = src.channel_names;				
				return(*this);
			}

		} voxelSequence;


	} // end ns
} // end ns
