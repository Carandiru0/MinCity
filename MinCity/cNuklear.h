#pragma once
#include <Utility/class_helper.h>
#include "cVulkan.h"
#include "tTime.h"
#include "Declarations.h"
#include <Math/point2D_t.h>
#include "betterenums.h"
#include <tbb/tbb.h>
#include <Imaging/Imaging/Imaging.h>
#include <vector>
#include "nk_custom.h" // safe to include here in header, only exposes public structs and methods
#include <unordered_map>

BETTER_ENUM(eNK_FONTS, uint32_t const,

	DEFAULT = 0,		// minimum larger size
	SMALL,				// minimum smaller size
	//BOLD				// visitor larger size
	ENCRYPT,			// axiss smaller size
	ENCRYPT_SMALL		// axiss smaller size
);

BETTER_ENUM(eWindowName, uint32_t const,

	WINDOW_TOP_DEBUG,
	WINDOW_QUIT,
	WINDOW_SAVE,
	WINDOW_LOAD,
	WINDOW_PAUSED,
	WINDOW_MINCITY,
	WINDOW_BOTTOM_MENU,
	WINDOW_LUT

);

BETTER_ENUM(eWindowType, uint32_t const,

	DISABLED = 0,
	QUIT = 1,
	SAVE = 2,
	LOAD = 3

);
BETTER_ENUM(eWindowQuit, int32_t const,

	PENDING = -1,		// pending - encompasses all states except idle //
	IDLE = 0,
	JUST_QUIT = 2,
	SAVE_AND_QUIT = 3

);
BETTER_ENUM(eWindowSave, int32_t const,

	PENDING = -1,		// pending - encompasses all states except idle //
	IDLE = 0,
	SAVE = 2

);
BETTER_ENUM(eWindowLoad, int32_t const,

	PENDING = -1,		// pending - encompasses all states except idle //
	IDLE = 0,
	LOAD = 2

);
struct GLFWwindow; // forward decl //



class no_vtable cNuklear : no_copy
{
	static constexpr float const NK_FONT_HEIGHT = 14.0f;
	static constexpr size_t const
		NK_MAX_VERTEX_COUNT = (Globals::NK_MAX_VERTEX_BUFFER_SZ / sizeof(VertexDecl::nk_vertex)),
		NK_MAX_INDEX_COUNT = (Globals::NK_MAX_INDEX_BUFFER_SZ / sizeof(uint16_t));

public:
	// Accessors //
	struct GLFWwindow* const&			getGLFWWindow() const;

	point2D_t const __vectorcall		getFramebufferSize() const { return(_frameBufferSize); }
	float const							getFramebufferAspect() const { return(_frameBufferAspect); }
	bool const						    isFramebufferDPIAware() const {	return(_frameBufferDPIAware); }
	bool const							isMinimapShown() const { return(_bMinimapRenderingEnabled); }
	bool const							isEditControlFocused() const { return(_bEditControlFocused); }
	uint32_t const						getWindowEnabled() const { return(_uiWindowEnabled); }
	size_t const						getImageCount() const { return(_imageVector.size()); }

	// Mutators //
	void __vectorcall setFrameBufferSize(point2D_t const framebufferSize) { _frameBufferSize.v = framebufferSize.v; _frameBufferAspect = float(framebufferSize.x) / float(framebufferSize.y); }
	void setFrameBufferSize(int32_t const Width, int32_t const Height) { setFrameBufferSize(point2D_t(Width, Height)); }
	void setFrameBufferDPIAware(bool const bDPIAware) {	_frameBufferDPIAware = bDPIAware; }
	void setPauseProgress(uint32_t const progress) { _uiPauseProgress = progress; }
	
	// Main Methods //
	void Initialize(struct GLFWwindow* const& __restrict win);
	void SetSpecializationConstants(std::vector<vku::SpecializationConstant>& __restrict constants);
	void UpdateDescriptorSet(vku::DescriptorSetUpdater& __restrict dsu, vk::Sampler const& sampler);
	
	template<uint32_t const windowType>
	int32_t const getLastSelectionForWindow() const { 
		if constexpr (eWindowType::QUIT == windowType) {
			return(_iWindowQuitSelection);
		}
		else if constexpr (eWindowType::SAVE == windowType) {
			return(_iWindowSaveSelection);
		}
		else if constexpr (eWindowType::LOAD == windowType) {
			return(_iWindowLoadSelection);
		}

		return(0); // Common IDLE state is always zero for any window selection
	}

	// returns previous value
	template<uint32_t const windowType>
	bool const enableWindow(bool const bEnable) { 

		uint32_t const windowEnabled(bEnable ? windowType : eWindowType::DISABLED);

		// return prior state
		uint32_t const prior = _InterlockedCompareExchange(&_uiWindowEnabled, windowEnabled, ((eWindowType::DISABLED == windowEnabled) ? windowType : eWindowType::DISABLED));

		// if current state was changed and is equal to desired (enabled/disabled) for the state were interested in
		if (prior != _uiWindowEnabled && windowEnabled == _uiWindowEnabled ) {

			// for enabling only reset these states if its the state were interested in
			if (bEnable) {
				if constexpr (eWindowType::QUIT == windowType) {
					_iWindowQuitSelection = eWindowQuit::IDLE;
					_bHintReset = true;
				}
				else if constexpr (eWindowType::SAVE == windowType) {
					_iWindowSaveSelection = eWindowSave::IDLE;
					_saveWindow.bReset = true;
				}
				else if constexpr (eWindowType::LOAD == windowType) {
					_iWindowLoadSelection = eWindowLoad::IDLE;
					_loadWindow.bReset = true;
				}
			}
			else { // disabling
				if constexpr (eWindowType::QUIT != windowType) {
					_iWindowQuitSelection = eWindowQuit::IDLE;	// must reset the root "quit" menu on exit of save or load dialogs
				}
			}
		}
		return(prior);
	}
	
	bool const enableMinimapRendering(bool const bEnable) { return(_InterlockedCompareExchange(reinterpret_cast<uint32_t* const __restrict>(&_bMinimapRenderingEnabled), bEnable, !bEnable)); }

	void Upload(uint32_t const resource_index, vk::CommandBuffer& __restrict cb_transfer,
				vku::DynamicVertexBuffer& __restrict vbo, vku::DynamicIndexBuffer& __restrict ibo, vku::UniformBuffer& __restrict ubo);
	void AcquireTransferQueueOwnership(vk::CommandBuffer& __restrict cb, vku::DynamicVertexBuffer& __restrict vbo, vku::DynamicIndexBuffer& __restrict ibo, vku::UniformBuffer& __restrict ubo);
	void Render(vk::CommandBuffer& __restrict cb_render,
				vku::DynamicVertexBuffer& __restrict vbo, vku::DynamicIndexBuffer& __restrict ibo, vku::UniformBuffer& __restrict ubo,
				RenderingInfo const& __restrict renderInfo) const;

	bool const UpdateInput();
	void UpdateGUI();

	// public callbacks //
	static void nk_focus_callback(GLFWwindow* const win, int const focused);
	static void nk_iconify_callback(GLFWwindow* const win, int const minimized);
	
	void CleanUp();
	
	// Specific //
	__inline void AddActiveWindowRect(rect2D_t const rectWindow) { _activeWindowRects.emplace_back(rectWindow); }

private:
	void LoadGUITextures();
	void SetGUIDirty();

#ifdef DEBUG_LUT_WINDOW
	void draw_lut_window(tTime const& __restrict tNow, uint32_t const height_offset);
#endif
private:
	vku::double_buffer<vku::GenericBuffer>	_stagingBuffer[2];
	vku::TextureImage2DArray*	_fontTexture;
	struct GLFWwindow*		    _win;
	struct nk_context*			_ctx;
	struct nk_buffer*			_cmds;
	struct nk_font_atlas*		_atlas;
	struct nk_font*			    _fonts[eNK_FONTS::_size()];
	struct nk_draw_null_texture* _nulltex; 
	struct nk_canvas*			_offscreen_canvas;
#ifdef DEBUG_LIGHT_PROPAGATION
	struct nk_image const*		_debugLightImage;
#endif
	std::vector<rect2D_t>		_activeWindowRects;

	point2D_t					_frameBufferSize;
	float						_frameBufferAspect;
	bool						_frameBufferDPIAware;
	bool						_bUniformSet, _bUniformAcquireRequired,
								_bAcquireRequired;
	vku::double_buffer<bool>	_bGUIDirty;
	int32_t						_iWindowQuitSelection,
								_iWindowSaveSelection,
								_iWindowLoadSelection;
	uint32_t					_uiWindowEnabled;
	uint32_t					_uiPauseProgress;
	bool						_bHintReset,
								_bEditControlFocused;
	bool                        _bMinimapRenderingEnabled;

	struct {

		nk_text_animated_state			thumbnail_text_state;
		bool							bReset;

		void reset() {
			thumbnail_text_state.reset();
			bReset = false;
		}

	} _saveWindow;

	struct {

		nk_text_animated_state			thumbnail_text_state;
		bool							bReset;

		void reset() {
			thumbnail_text_state.reset();
			bReset = false;
		}

	} _loadWindow;

	alignas(16) VertexDecl::nk_vertex*		_vertices;
	alignas(16) uint16_t*					_indices;
private:
	typedef struct guiSequence
	{
		ImagingSequence
			* sequence = nullptr;
		fp_seconds
			tAccumulateFrame = zero_time_duration;
		uint32_t
			frame = 0;

		~guiSequence() {
			if (sequence) {
				ImagingDelete(sequence); sequence = nullptr;
			}
		}

	} guiSequence;

	struct guiTextures
	{
		vku::TextureImage2DArray   

			*create = nullptr,
			*open = nullptr,
			*save = nullptr,
			*road = nullptr,
			*zoning = nullptr;

		struct thumbnail {
			vku::TextureImage2DArray
				*texture = nullptr;
			Imaging 
				image = nullptr;
			guiSequence*
				sequence = nullptr;
			uint32_t
				select = 0;

			~thumbnail() {
				if (image) {
					ImagingDelete(image); image = nullptr;
				}
				SAFE_DELETE_ARRAY(sequence);
			}
		} load_thumbnail;

		// don't forget to init to nullptr here, and delete in cleanup() //
	} _guiTextures;

	struct guiImages
	{
		struct nk_image const
			*create = nullptr,
			*open = nullptr,
			*save = nullptr,
			*road = nullptr,
			*zoning = nullptr,
			*load_thumbnail = nullptr,
			*offscreen = nullptr;

		// don't forget to init to nullptr here //
	} _guiImages;

	std::vector<struct nk_image_extended*> _imageVector;

	struct sequenceImages
	{
		static constexpr uint32_t const	STATIC_IMAGE_COUNT = 3;

		ImagingSequence
			* static_screen[STATIC_IMAGE_COUNT] = { nullptr };

	} _sequenceImages;

public:
	cNuklear();

#ifndef NDEBUG
	void debug_out_nuklear(std::string const& message); // thread safe 
	std::string _szDebugMessage;
	tbb::spin_mutex lock_debug_message; // thread safe 
#endif
};

#ifndef NDEBUG	
extern void debug_out_nuklear(std::string const message);
#endif
