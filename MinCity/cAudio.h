#pragma once
#include <Utility/class_helper.h>
#include <Utility/mio/mmap.hpp>
#include <list>


// soundbank\ includes (subsound indices) //
#include "soundbank/music.h"

#define STARTING_MUSIC_SUBSOUND_INDEX (_06CYBERPUNK_INTHECLUB_)

static constexpr float const DEFAULT_MUSIC_VOLUME = 0.2f;

class no_vtable cAudio : no_copy
{

public:
	bool const Initialize();
	void Update();

	bool const PlayMusicSubsound(int const subsound_index);

private:
	bool const LoadSoundbanks();
	char const* const MemoryMapSoundbankFile(std::wstring_view const path, uint32_t& size);
	
private:
	std::list<mio::mmap_source> _lmmapPersistant;

	struct FMOD_SOUND*		_music_soundbank;
	struct FMOD_SOUND*		_music_subsound;
	struct FMOD_CHANNEL*	_music_channel;
	int						_music_subsound_index;

	int64_t					_task_id_fmod;
	tbb::atomic<int32_t>	_last_fmod_result;
public:
	cAudio();
	~cAudio() = default;  // uses CleanUp instead
	void CleanUp();
};
