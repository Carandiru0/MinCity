#include "pch.h"
#include "MinCity.h"
#include "cVoxelWorld.h"
#include <Random/superrandom.hpp>
#include <filesystem>
#include <Utility/async_long_task.h>

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

static uint32_t const count_gifs_in_directory( fs::path const path_to_textures )
{
	uint32_t gif_count(0);

	fs::path const gif_extension(L".gif");

	for (auto const& entry : fs::directory_iterator(path_to_textures)) {

		if (entry.path().extension() == gif_extension) {
			++gif_count;
		}

	}

	return(gif_count);
}

void ImageAnimation::loadNextImage(uint32_t desired_width, uint32_t const desired_height, uint32_t const unique_hash_seed)
{
	constinit static uint32_t gif_count[2]{};

	uint32_t const micro((uint32_t const)(desired_height <= MICRO_PIXEL_HEIGHT_MAX));

	if (0 == gif_count[micro]) { // acquire gif count on first request of either regular gif dir or alternatively micro gif sub dir

		if (micro) {
			gif_count[micro] = count_gifs_in_directory(GIF_DIR L"micro/");
		}
		else {
			gif_count[micro] = count_gifs_in_directory(GIF_DIR);
		}
	}

	int32_t const random_index((forced_sequence >= 0 ? forced_sequence : PsuedoRandomNumber(0, gif_count[micro])));
	
	std::string const szFile(fmt::format(FMT_STRING("anim_{:03d}.gif"), random_index));

	std::wstring wszFile(szFile.begin(), szFile.end());

	if (micro) {
		wszFile = (GIF_DIR L"micro/") + wszFile;
		desired_width = 0; // for micro default to native gif width (which imaging does on zeros paased in for either width or height
	}
	else {
		wszFile = GIF_DIR + wszFile;
	}

	if (fs::exists(wszFile)) {

		next_sequence = ImagingLoadGIFSequence(wszFile, desired_width, desired_height);
		return;
	}

	next_sequence = nullptr;
}
void ImageAnimation::async_loadNextImage(uint32_t const desired_width, uint32_t const desired_height, uint32_t const unique_hash_seed)
{
	// this gif is ready to load
	status = PENDING;

	_background_task_id = async_long_task::enqueue<background>([=] { return(loadNextImage(desired_width, desired_height, unique_hash_seed)); });
}

ImageAnimation::~ImageAnimation()
{
	async_long_task::wait<background>(_background_task_id, "gif sequence"); // bug-fix prevent deletion/destruction while potentially pending

	ImagingSequence* del_image = next_sequence.compare_and_swap(nullptr, next_sequence);		// threadsafe deletion!
	if (nullptr != del_image) {
		ImagingDelete(del_image);
		del_image = nullptr;
	}

	if (sequence) {
		ImagingDelete(sequence); sequence = nullptr;
	}
}














