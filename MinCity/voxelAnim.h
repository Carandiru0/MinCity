#pragma once
#include "tTime.h"
#include "voxelModelInstance.h"

namespace Volumetric
{
	template< bool const Dynamic >
	struct voxelAnim
	{
		static constexpr uint32_t const DEFAULT_FRAMERATE = 30;

		float			 accumulator;

		float			 frame_interval;
		uint32_t		 frame;

		bool const update(voxelModelInstance<Dynamic>* const __restrict instance, fp_seconds const& __restrict tDelta)
		{
			if (instance)
			{
				voxB::voxelModel<Dynamic> const& __restrict model(instance->getModel());

				if (nullptr != model._Features.sequence) {

					uint32_t const num_frames(model._Features.sequence->numFrames());

					accumulator += time_to_float(tDelta);

					if (accumulator >= frame_interval)
					{
						++frame;

						if (frame >= num_frames) {
							frame = 0; // loop
						}

						instance->setVoxelOffsetCount(model._Features.sequence->getOffset(frame), model._Features.sequence->numVoxels(frame)); // update the instance voxel offset and voxel count, which defines the frame used for rendering of this instance.

						accumulator -= frame_interval;
					}

					return((num_frames - 1) == frame); // returning if last frame - indicating that animation is finished
				}
			}
			
			return(true); // returning true as in last frame is current so that the behaviour when the sequence does not exist is the animation is finished. eg.) gameobject destroys itself when animation is finished.
		}
		
		voxelAnim(voxelModelInstance<Dynamic>*const __restrict instance, uint32_t const framerate_ = DEFAULT_FRAMERATE)
			: accumulator(0.0f), frame_interval(1.0f / (float)framerate_), frame(0)
		{
			if (instance)
			{
				voxB::voxelModel<Dynamic> const& __restrict model(instance->getModel());

				if (nullptr != model._Features.sequence) {
					instance->setVoxelOffsetCount(model._Features.sequence->getOffset(0), model._Features.sequence->numVoxels(0)); // update the instance voxel offset and voxel count, which defines the frame used for rendering of this instance.
				}
			}
		}
		
	};
} // end ns;

