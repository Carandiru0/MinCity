#pragma once
#include <Math/point2D_t.h>
#include <Math/superfastmath.h>
#include <vector>

namespace Volumetric
{
	namespace voxB
	{

		typedef struct voxelSequence
		{
		public:
			static constexpr uint32_t const MAX_CHANNELS = 3;
			
		private:
			vector<uint32_t> offsets;
			vector<uint32_t> sizes;	// number of voxels for each frame
			std::string		 channel_names[MAX_CHANNELS];
		public:
			uint32_t const getOffset(uint32_t const frame) const { return(offsets[frame]); }
			uint32_t const numVoxels(uint32_t const frame) const { return(sizes[frame]); } // number of voxels for a given frame (frame index is not checked and must be valid - no bounds checking)
			uint32_t const numFrames() const { return((uint32_t)sizes.size()); } // number of frames in the sequence
			
			std::string_view const  getChannelName(uint32_t const channel) const { return(channel_names[channel]); }
			uint32_t const			numChannels() const { return(uint32_t(!channel_names[0].empty()) + uint32_t(!channel_names[1].empty()) + uint32_t(!channel_names[2].empty())); }
			
			void addFrame(uint32_t const offset, uint32_t const size) { offsets.push_back(offset); sizes.push_back(size); }

			void addChannel(uint32_t const channel, std::string_view const channel_name) { channel_names[channel] = channel_name; }
			
			voxelSequence() = default;

			voxelSequence(voxelSequence const& src) noexcept
				: offsets(src.offsets), sizes(src.sizes), channel_names{ src.channel_names[0], src.channel_names[1], src.channel_names[2] }
			{}

			voxelSequence& operator=(voxelSequence const& src) noexcept
			{
				offsets = src.offsets;
				sizes = src.sizes;
				
				for (uint32_t channel = 0; channel < MAX_CHANNELS; ++channel) {
					channel_names[channel] = src.channel_names[channel];
				}
				
				return(*this);
			}

		} voxelSequence;


	} // end ns
} // end ns
