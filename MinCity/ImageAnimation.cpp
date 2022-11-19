#include "pch.h"
#include "MinCity.h"
#include "cVoxelWorld.h"
#include <Random/superrandom.hpp>
#include <filesystem>
#include <Utility/async_long_task.h>
#include <Utility/stringconv.h>
#include "ImageAnimation.h"

static constexpr uint32_t const MICRO_PIXEL_HEIGHT_MAX = 16;

namespace fs = std::filesystem;

ImageAnimation::ImageAnimation(Volumetric::voxB::voxelScreen const& voxelscreen_, uint32_t const unique_hash_seed_)
	: _background_task_id(0), sequence(nullptr), next_sequence(nullptr), tFrameStarted(zero_time_point), frame(0), loops(0), status(UNLOADED),
	voxelscreen(voxelscreen_), unique_hash_seed(unique_hash_seed_), obtain_allowed(false), forced_sequence(-1)

{
}

uint32_t const __vectorcall ImageAnimation::getPixelColor(__m128i const xmPosition) const
{
	if (UNLOADED != status && sequence) {

		uvec4_t voxel;
		uvec4_v(xmPosition).xyzw(voxel);

		point2D_t screenspace;
		if ( voxelscreen.major_axis) { // MAJOR_AXIS_Z
			screenspace = p2D_sub(point2D_t(voxel.z, voxel.y), voxelscreen.screen_rect.left_top());
		}
		else { // MAJOR_AXIS_X
			screenspace = p2D_sub(point2D_t(voxel.x, voxel.y), voxelscreen.screen_rect.left_top());
		}

		point2D_t const screen_max_indices(p2D_sub(voxelscreen.screen_rect.width_height(), point2D_t(1)));

		// clamp
		screenspace = p2D_min(screen_max_indices, screenspace);
		screenspace = p2D_max(point2D_t{}, screenspace);

		// flip x & y
		screenspace = p2D_sub(screen_max_indices, screenspace);

		// assume image is greater or equal in dimension size with respect to rect of videoscreen

		return(reinterpret_cast<uint32_t const* const __restrict>(
			sequence->images[frame].block)[screenspace.y * sequence->xsize + screenspace.x]); // return the color as in image (may have alpha)
	}

	if (!PsuedoRandom5050()) {
		return(0);
	}

	return(Volumetric::Konstants::PALETTE_WINDOW_INDEX);
}

void ImageAnimation::OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta)
{
	static constexpr milliseconds const
		MINIMUM_FRAME_DURATION = milliseconds(32),
		MAXIMUM_FRAME_DURATION = milliseconds(250);

	static constexpr seconds const
		DURATION_MIN = seconds(30),
		DURATION_MAX = seconds(45);

	if (obtain_allowed && LOADED != status) {
		if (UNLOADED == status.compare_and_swap(PENDING, UNLOADED)) {

			// load next image
			async_loadNextImage(voxelscreen.screen_rect.width(), voxelscreen.screen_rect.height(), unique_hash_seed);
		}
	}

	if (PENDING == status) {

		if (nullptr != next_sequence) {

			ImagingSequence* sequence_delete = (ImagingSequence*)_InterlockedExchangePointer((PVOID*)&sequence, next_sequence);
			next_sequence = nullptr;

			if (sequence_delete) {
				ImagingDelete(sequence_delete); sequence_delete = nullptr;
			}

			tFrameStarted = zero_time_point;
			frame = 0;

			// get average frame duration 
			milliseconds tFrameDuration;
			for (uint32_t i = 0; i < sequence->count; ++i) { 
				tFrameDuration += milliseconds(sequence->images[i].delay);
			}
			tFrameDuration = milliseconds(SFM::round_to_u32(time_to_float(fp_seconds(tFrameDuration) / double(SFM::max(1U, sequence->count)))));
			// limits
			tFrameDuration = std::max(MINIMUM_FRAME_DURATION, tFrameDuration);
			tFrameDuration = std::min(MAXIMUM_FRAME_DURATION, tFrameDuration);

			// animation loop duration = average frame duration * frame count
			// loops = random animation duration / animation loop duration
			// if animation loop duration > random animation duration
			// loops = 1

			// duration for of a single loop
			milliseconds const tLoopDuration(milliseconds(tFrameDuration.count() * sequence->count));

			// random animation duration
			milliseconds const tAnimDuration(milliseconds(seconds(PsuedoRandomNumber((int32_t)DURATION_MIN.count(), (int32_t)DURATION_MAX.count()))));

			if (tLoopDuration > tAnimDuration) {
				loops = 1;
			}
			else {
				loops = SFM::max(1U, SFM::round_to_u32(time_to_float(fp_seconds(tAnimDuration) / fp_seconds(tLoopDuration))));
			}

			status = LOADED;
		}
	}


	if (UNLOADED != status) {

		if (zero_time_point == tFrameStarted) {
			tFrameStarted = tNow;
		}

		if (sequence) {
			ImagingSequenceInstance const& image(sequence->images[frame]);

			fp_seconds const tFrame = fp_seconds(tNow - tFrameStarted);
			if (tFrame >= fp_seconds(milliseconds(image.delay))) {

				if (++frame == sequence->count) {
					frame = 0;
				}
				tFrameStarted = tNow;

				if (0 == frame) { // rolled over back to beginning ?

					if (--loops < 0) { // continue playing current sequence if pending image sequence is loading *or* if not allowed to load a new sequence

						if (obtain_allowed && PENDING != status) {
							// load next image
							async_loadNextImage(voxelscreen.screen_rect.width(), voxelscreen.screen_rect.height(), unique_hash_seed);
							// if async unsuccessul, state does not change to pending, no need to revert to original status
						}
					}
				}
			}
		}
	}

	obtain_allowed = false; // always reset every frame
}

void ImageAnimation::buildUniquePlaylist(fs::path const path_to_gifs)
{
	// guarded
	if (unique_playlist.empty()) {

		vector<std::wstring> tmpList;
		
		for (auto const& entry : fs::directory_iterator(path_to_gifs)) {

			if (entry.exists() && !entry.is_directory()) {
				if (stringconv::case_insensitive_compare(GIF_FILE_EXT, entry.path().extension().wstring())) // only vdb files 
				{
					tmpList.emplace_back(entry.path().wstring());
				}
			}
		}
		
		random_shuffle(tmpList.begin(), tmpList.end());
		
		while (!tmpList.empty()) {

			unique_playlist.emplace(std::move(tmpList.back()));   // put into concurrent queue
			tmpList.pop_back();
		}
	}
}

void ImageAnimation::loadNextImage(uint32_t desired_width, uint32_t const desired_height, uint32_t const unique_hash_seed)
{	
	if (!unique_playlist.empty()) // *bugfix - loading gif consistent now.
	{
		std::wstring filepath(L"");
		
		uint32_t max_tries(10);
		bool bGet(false);
		
		do {
			
			bGet = unique_playlist.try_pop(filepath);
			
			if (bGet) {
				next_sequence = ImagingLoadGIFSequence(filepath, desired_width, desired_height);
				return;
			}
			
			_mm_pause();
			
		} while (!bGet && --max_tries);
	}

	next_sequence = nullptr;
}
void ImageAnimation::async_loadNextImage(uint32_t const desired_width, uint32_t const desired_height, uint32_t const unique_hash_seed)
{
	// this gif is ready to load
	status = PENDING;

	bool const micro(desired_height <= MICRO_PIXEL_HEIGHT_MAX);

	if (unique_playlist.empty()) {

		fs::path gifDirectory(GIF_DIR);
		if (micro) {
			gifDirectory += L"micro/";
		}
		buildUniquePlaylist(gifDirectory);
	}
	
	_background_task_id = async_long_task::enqueue<background>([=] { return(loadNextImage(desired_width, desired_height, unique_hash_seed)); }); // this maybe bugged now @TODO
}

ImageAnimation::~ImageAnimation()
{
	//async_long_task::wait<background>(_background_task_id, "gif sequence"); // bug-fix prevent deletion/destruction while potentially pending

	ImagingSequence* del_image = next_sequence.compare_and_swap(nullptr, next_sequence);		// threadsafe deletion!
	if (nullptr != del_image) {
		ImagingDelete(del_image);
		del_image = nullptr;
	}

	if (sequence) {
		ImagingDelete(sequence); sequence = nullptr;
	}
}














