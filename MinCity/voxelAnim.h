#pragma once
#include "tTime.h"
#include "voxelModelInstance.h"

namespace Volumetric
{
	template< bool const Dynamic >
	struct voxelAnim
	{
	private:
		static constexpr uint32_t const DEFAULT_FRAMERATE = 60;

		float			 accumulator;

		float			 frame_interval;
		uint32_t		 frame, repeat, frame_count;
		bool             reverse;

	public:
		uint32_t const getFrameCount() const { return(frame_count); }

		float const getElapsed() const // returns t in the range [0.0f .... 1.0f]
		{
			float elapsed = SFM::linearstep((float)repeat, (float)frame_count, (float)frame);

			if (reverse) {
				elapsed = 1.0f - elapsed;
			}

			return(elapsed);
		}

		void setFrame(uint32_t const frame_) {
			frame = frame_;
		}
		void reset() {
			if (!reverse) {
				frame = repeat;
			}
			else {
				frame = frame_count - 1;
			}
		}
		void setRepeatFrameIndex(uint32_t const repeat_) { // can set repeat index at any time (start index / offset from start), setting the same value will work while in reverse too, don't have to worry about a reverse repeat frame index either, just use the same index as with the forward repeat mode.
			if (!reverse) {
				repeat = repeat_;
			}
			else {
				repeat = frame_count - 1 - repeat_;
			}
		}  
		void setReverse(bool const reverse_) { // can reverse at any time, does reset repeat, repeat must be set again if required
			reverse = reverse_; 
			if (reverse) {
				repeat = frame_count - 1;
			}
			else {
				repeat = 0;
			}
		} 

		bool const update(voxelModelInstance<Dynamic>* const __restrict instance, fp_seconds const& __restrict tDelta) // returns true when a single full animation loop has finished
		{
			if (instance)
			{
				voxB::voxelModel<Dynamic> const& __restrict model(instance->getModel());

				if (nullptr != model._Features.sequence) {

					int32_t const num_frames(frame_count);

					accumulator += time_to_float(tDelta);

					if (accumulator >= frame_interval)
					{
						int32_t frame_next(frame);

						if (!reverse) {
							++frame_next;

							if (frame_next > (num_frames - 1)) {
								frame_next = repeat; // loop
							}
						}
						else {
							--frame_next;

							if (frame_next < repeat || frame_next < 0) {
								frame_next = num_frames - 1;
							}
						}

						frame = frame_next;
						instance->setVoxelOffsetCount(model._Features.sequence->getOffset(frame), model._Features.sequence->numVoxels(frame)); // update the instance voxel offset and voxel count, which defines the frame used for rendering of this instance.

						accumulator -= frame_interval;
					}

					return((reverse ? (repeat == frame) : (num_frames - 1 - repeat) == frame)); // returning if last frame - indicating that animation is finished
				}
			}
			
			return(true); // returning true as in last frame is current so that the behaviour when the sequence does not exist is the animation is finished. eg.) gameobject destroys itself when animation is finished.
		}
		
		voxelAnim(voxelModelInstance<Dynamic>*const __restrict instance, uint32_t const framerate_ = DEFAULT_FRAMERATE)
			: accumulator(0.0f), frame_interval(1.0f / (float)framerate_), frame(0), repeat(0), frame_count(0), reverse(false)
		{
			if (instance)
			{
				voxB::voxelModel<Dynamic> const& __restrict model(instance->getModel());

				if (nullptr != model._Features.sequence) {
					instance->setVoxelOffsetCount(model._Features.sequence->getOffset(0), model._Features.sequence->numVoxels(0)); // update the instance voxel offset and voxel count, which defines the frame used for rendering of this instance.
					frame_count = model._Features.sequence->numFrames();
				}
			}
		}
		
	};
} // end ns;

