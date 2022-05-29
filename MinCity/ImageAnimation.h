#pragma once
#include <tbb/tbb.h>
#include <atomic>

#include <Imaging/Imaging/Imaging.h>
#include "tTime.h"
#include "voxelScreen.h"
#include <Utility/type_colony.h>
#include <vector>

#define GIF_FILE_EXT L".gif"

class ImageAnimation : public type_colony<ImageAnimation>, private no_copy
{
	static inline constexpr int32_t const 
		UNLOADED = -1,
		PENDING = 0,
		LOADED = 1;

public: // accessors and mutators
	bool const									isAllowedObtainNewSequences() const { return(obtain_allowed); }

	void										setAllowedObtainNewSequences(bool const allowed) { obtain_allowed = allowed; }
	void										setForcedSequence(uint32_t const index) { forced_sequence = (int32_t)index; }
public: // main methods
	uint32_t const __vectorcall getPixelColor(__m128i const xmPosition) const;
	void OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta);
private:
	void buildUniquePlaylist(std::filesystem::path const path_to_gifs);
	void loadNextImage(uint32_t desired_width, uint32_t const desired_height, uint32_t const unique_hash_seed);
	void async_loadNextImage(uint32_t const desired_width, uint32_t const desired_height, uint32_t const unique_hash_seed);

private:
	ImagingSequence*						sequence;
	tTime									tFrameStarted;
	uint32_t								frame;
	int32_t									loops;
	int32_t									forced_sequence;
	uint32_t								unique_hash_seed;
	Volumetric::voxB::voxelScreen			voxelscreen;
	int64_t									_background_task_id;
	bool									obtain_allowed;
	tbb::atomic<int32_t>					status;
	tbb::atomic<ImagingSequence*>			next_sequence;
	
	tbb::concurrent_queue<std::wstring>	    unique_playlist;
public:
	bool const operator==(ImageAnimation const& src) const
	{
		return(((uint64_t)&(*this)) & ((uint64_t)&src));
	}
	ImageAnimation(ImageAnimation&& src) noexcept
		: voxelscreen(std::move(src.voxelscreen)), unique_hash_seed(std::move(src.unique_hash_seed))
	{
		src.free_ownership();

		_background_task_id = std::move(_background_task_id); src._background_task_id = 0;
		obtain_allowed = std::move(src.obtain_allowed);
		status = std::move(src.status);
		sequence = std::move(src.sequence); src.sequence = nullptr;
		next_sequence = std::move(src.next_sequence); src.next_sequence = nullptr;
		tFrameStarted = std::move(src.tFrameStarted);
		frame = std::move(src.frame);
		loops = std::move(src.loops);
		forced_sequence = std::move(src.forced_sequence);
		
	}
	ImageAnimation& operator=(ImageAnimation&& src) noexcept
	{
		src.free_ownership();

		unique_hash_seed = std::move(src.unique_hash_seed);
		voxelscreen = std::move(src.voxelscreen);

		_background_task_id = std::move(_background_task_id); src._background_task_id = 0;
		obtain_allowed = std::move(src.obtain_allowed);
		status = std::move(src.status);
		sequence = std::move(src.sequence); src.sequence = nullptr;
		next_sequence = std::move(src.next_sequence); src.next_sequence = nullptr;
		tFrameStarted = std::move(src.tFrameStarted);
		frame = std::move(src.frame);
		loops = std::move(src.loops);
		forced_sequence = std::move(src.forced_sequence);

		return(*this);
	}

public:
	ImageAnimation() = default;
	ImageAnimation(Volumetric::voxB::voxelScreen const& voxelscreen_, uint32_t const unique_hash_seed_);
	~ImageAnimation();
};

STATIC_INLINE_PURE void swap(ImageAnimation& __restrict left, ImageAnimation& __restrict right) noexcept
{
	ImageAnimation tmp{ std::move(left) };
	left = std::move(right);
	right = std::move(tmp);

	left.revert_free_ownership();
	right.revert_free_ownership();
}




