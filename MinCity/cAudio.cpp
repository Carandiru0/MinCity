#include "pch.h"
#include "cAudio.h"
#include <fmod.h>
#include <Utility/mio/mmap.hpp>
#include <filesystem>
#include <Utility/async_long_task.h>

namespace // private to this file (anonymous)
{
	static inline struct no_vtable : no_copy {

		FMOD_SYSTEM* system;

	} audio{};

} // end ns

cAudio::cAudio()
	: _music_soundbank(nullptr), _music_subsound(nullptr), _music_channel(nullptr), _music_subsound_index(STARTING_MUSIC_SUBSOUND_INDEX),
	_task_id_fmod(0), _last_fmod_result(FMOD_OK)
{

}

bool const cAudio::Initialize()
{
#ifndef DEBUG_DISABLE_MUSIC

	if (FMOD_OK != FMOD_System_Create(&audio.system))
		return(false);

	FMOD_System_SetOutput(audio.system, FMOD_OUTPUTTYPE_WINSONIC);

	if (FMOD_OK != FMOD_System_Init(audio.system, 32, FMOD_INIT_STREAM_FROM_UPDATE | FMOD_INIT_MIX_FROM_UPDATE | FMOD_INIT_NORMAL, nullptr)) {

		FMOD_System_SetOutput(audio.system, FMOD_OUTPUTTYPE_WASAPI);

		if (FMOD_OK != FMOD_System_Init(audio.system, 32, FMOD_INIT_STREAM_FROM_UPDATE | FMOD_INIT_MIX_FROM_UPDATE | FMOD_INIT_NORMAL, nullptr))
			return(false);
		
	}

	FMOD_OUTPUTTYPE used_output;
	if (FMOD_OK != FMOD_System_GetOutput(audio.system, &used_output))
		return(false);

	FMT_LOG_OK(AUDIO_LOG, "FMOD Initialized - {:s}{:s}", ((FMOD_OUTPUTTYPE_WINSONIC == used_output) ? "Windows Sonic Output" : ""),
														 ((FMOD_OUTPUTTYPE_WASAPI == used_output) ? "WASAPI Output" : ""));

	if (!LoadSoundbanks())
		return(false);

	FMT_LOG_OK(AUDIO_LOG, "All soundbanks loaded");

#endif

	return(true);
}

char const* const cAudio::MemoryMapSoundbankFile(std::wstring_view const path, uint32_t& size) {	// persistantly open until program termination, mmap is then released

	std::error_code error{};

	mio::mmap_source& mmap = _lmmapPersistant.emplace_back(mio::make_mmap_source(path, FILE_FLAG_SEQUENTIAL_SCAN | FILE_ATTRIBUTE_NORMAL, error));
	if (!error) {
		if (mmap.is_open() && mmap.is_mapped()) {

			size = (uint32_t const)mmap.size();
			return(mmap.data());
		}
	}

	size = 0;
	return(nullptr);
}


bool const cAudio::LoadSoundbanks()
{
	uint32_t soundbank_size(0);

	char const* const music_data = MemoryMapSoundbankFile(AUDIO_DIR "music.fsb", soundbank_size);
	if (nullptr == music_data || 0 == soundbank_size) {
		FMT_LOG_FAIL(AUDIO_LOG, "music soundbank file could not be loaded");
		return(false);
	}

	FMOD_CREATESOUNDEXINFO info{};
	info.cbsize = sizeof(FMOD_CREATESOUNDEXINFO);
	info.length = soundbank_size;
	info.suggestedsoundtype = FMOD_SOUND_TYPE_FSB;/* | FMOD_SOUND_TYPE_FADPCM*/;
	info.filebuffersize = -1;
	info.ignoresetfilesystem = true;
	info.initialsubsound = _music_subsound_index;

	if (FMOD_OK != FMOD_System_CreateSound(audio.system, music_data, FMOD_CREATESTREAM | FMOD_OPENMEMORY_POINT | FMOD_IGNORETAGS | FMOD_LOWMEM | FMOD_2D | FMOD_LOOP_NORMAL | FMOD_UNIQUE | FMOD_VIRTUAL_PLAYFROMSTART, 
		                                   &info, &_music_soundbank)) 
	{
		FMT_LOG_FAIL(AUDIO_LOG, "music soundbank could not be created");
		return(false);
	}

	return(true);
}

bool const cAudio::PlayMusicSubsound(int const subsound_index)
{
#ifndef DEBUG_DISABLE_MUSIC
	if (_music_channel) {
		FMOD_Channel_Stop(_music_channel);
		_music_channel = nullptr;
	}

	if (_music_subsound) {
		FMOD_Sound_Release(_music_subsound);
		_music_subsound = nullptr;
	}


	if (FMOD_OK != FMOD_Sound_GetSubSound(_music_soundbank, subsound_index, &_music_subsound))
	{
		FMT_LOG_FAIL(AUDIO_LOG, "music soundbank could not select subsound");
		return(false);
	}
	if (FMOD_OK != FMOD_System_PlaySound(audio.system, _music_subsound, nullptr, true, &_music_channel))
	{
		FMT_LOG_FAIL(AUDIO_LOG, "music soundbank could not be played");
		return(false);
	}
	FMOD_Channel_SetPriority(_music_channel, 1);
	FMOD_Channel_SetLoopCount(_music_channel, 0);
	FMOD_Channel_SetVolume(_music_channel, DEFAULT_MUSIC_VOLUME);
	FMOD_Channel_SetPaused(_music_channel, false);

#endif
	return(true);
}

void cAudio::Update()
{
	if (audio.system) {

		// Bug: crash here when window is out of focus for a long period of time and FMOD_System_Update doesn't like it
		// suggest pausing playback when window is out of focus and not updating FMOD while out of focus as fix
		async_long_task::wait<background_critical>(_task_id_fmod, "audio");
		_task_id_fmod = async_long_task::enqueue<background_critical>( [&] {

			FMOD_RESULT const result(FMOD_System_Update(audio.system));
			_last_fmod_result.store<tbb::relaxed>(result);
		});
		
		FMOD_RESULT const result( (FMOD_RESULT const)_last_fmod_result.load<tbb::relaxed>() );
		if (FMOD_OK != result) {
			FMT_LOG_WARN(AUDIO_LOG, "FMOD update reporting error   [ {:0X} ]", result);
		}

		if (_music_channel) {
			FMOD_BOOL playing(false);
			FMOD_Channel_IsPlaying(_music_channel, &playing);
			if (!playing) {
				_music_channel = nullptr;
			}
		}
		else if (nullptr == _music_subsound){ // start music //
			PlayMusicSubsound(_music_subsound_index);
		}
		else { // next track/subsound
			int num_subsounds;
			if (FMOD_OK == FMOD_Sound_GetNumSubSounds(_music_soundbank, &num_subsounds)) {
				if (++_music_subsound_index >= num_subsounds) {
					_music_subsound_index = 0;
				}
				PlayMusicSubsound(_music_subsound_index);
			}
		}
	}
}


void cAudio::CleanUp()
{
	if (_music_channel) {
		FMOD_Channel_Stop(_music_channel);
	}

	async_long_task::wait<background_critical>(_task_id_fmod, "audio");

	if (_music_subsound) {
		FMOD_Sound_Release(_music_subsound);
		_music_subsound = nullptr;
	}
	if (_music_soundbank) {
		FMOD_Sound_Release(_music_soundbank); 
		_music_soundbank = nullptr;
	}

	while (!_lmmapPersistant.empty()) {
		_lmmapPersistant.pop_back();
	}

	if (audio.system) {
		FMOD_System_Release(audio.system);
		audio.system = nullptr;
	}
}

