#include "pch.h"
#include "cNuklear.h"
#include "globals.h"
#include <Math/point2D_t.h>
#include "Declarations.h"
#include "MinCity.h"
#include "cTextureBoy.h"
#include "cVulkan.h"
#include "Declarations.h"
#include "cVoxelWorld.h"
#include "cUserInterface.h"
#include "cPostProcess.h"
#include "cCity.h"
#include "data.h"
#include "gui.h"
#include <Utility/async_long_task.h>
#include <filesystem>
#include <algorithm>
#include "eVoxelModels.h"
#include "cImportGameObject.h"
#include <Imaging/Imaging/Imaging.h>
#include "importproxy.h"


#define NK_IMPLEMENTATION  // the only place this is defined
#include "nk_include.h"
#include "nk_style.h"
#ifdef DEBUG_PERFORMANCE_VOXEL_SUBMISSION
#include <queue>
#endif

#ifndef NDEBUG
#ifdef DEBUG_SHOW_GUI_WINDOW_BORDERS
#define NK_TEMP_WINDOW_BORDER NK_WINDOW_BORDER
#else
#define NK_TEMP_WINDOW_BORDER (0)
#endif
#else
#define NK_TEMP_WINDOW_BORDER (0)
#endif

namespace fs = std::filesystem;

namespace { // local to this file only

static constexpr uint32_t const 
	RGBA_IMAGE = 0,
	ARRAY_IMAGE = 1;

static constexpr fp_seconds const
	CYBERPUNK_BEAT = fp_seconds(milliseconds(200));

static inline nk_color const
	TRANSPARENT = {},
	DEFAULT_TEXT_COLOR = { nk_la(255, 255) },
	HOVER_TEXT_COLOR = { nk_rgb(239, 6, 105) },
	ACTIVE_TEXT_COLOR = { nk_rgb(255, 0, 0) },
	DISABLED_TEXT_COLOR = { nk_la(127, 255) };

} // end ns

typedef struct nk_image_extended : nk_image
{
	uint32_t type;

} nk_image_extended;

typedef struct sMouseState
{
	static constexpr milliseconds const CLICK_DELTA = milliseconds(550);

	uint32_t button_state;

	tTime const tStamp;

	bool    left_released,
		    right_released;

	bool	left_clicked,
			right_clicked;

	bool    handled;

	sMouseState()
		: button_state(eMouseButtonState::INACTIVE), tStamp(critical_now()),
		left_clicked(false), right_clicked(false), left_released(false), right_released(false),
		handled(false)
	{
	}
} MouseState;

NK_API const nk_rune*
nk_font_vxlmono_glyph_ranges(void)
{
	NK_STORAGE const nk_rune ranges[] = {
		0x0017, 0x001F,
		0x0020, 0x007E,
		0 // null terminated!
	};
	return ranges;
}

static void
nk_font_stash_begin(struct nk_font_atlas ** atlas) 
{
	static struct {
		struct nk_font_atlas atlas;
	} nk;

	nk_font_atlas_init_custom(&nk.atlas);
	nk_font_atlas_begin(&nk.atlas);
	*atlas = &nk.atlas;
}
static void
nk_font_stash_end(struct nk_context* const __restrict ctx, struct nk_font_atlas * const __restrict atlas, struct nk_draw_null_texture* __restrict nulltex, vku::TextureImage2DArray*& __restrict texture, vk::Sampler const& __restrict sampler)
{
	int32_t width(0), height(0);

	uint8_t const* const __restrict memToLoad = (uint8_t const* const __restrict)nk_font_atlas_bake(atlas, &width, &height, NK_FONT_ATLAS_RGBA32);

	bool bLoaded(false);
	if (MinCity::TextureBoy->KTXFileExists(FONT_DIR L"FontAtlas.ktx"))
	{
		// replace with cached BC7 version instead
		bLoaded = MinCity::TextureBoy->LoadKTXTexture(texture, FONT_DIR L"FontAtlas.ktx");		// This file is manually edited to provide correct transparency
	}

	if (!bLoaded) { // build new compressed texture //  **** note **** building a new texture does most of the work, however there are manual edits to make it work properly ****
		FMT_LOG(TEX_LOG, "Compressing Texture, please wait....");																//     -transparency mask around mouse cursor, everything else opaque in alpha channel.

		Imaging imgRef = ImagingLoadFromMemoryBGRA(memToLoad, width, height);
		Imaging imgFontBC7 = ImagingCompressBGRAToBC7(imgRef);

		ImagingSaveCompressedBC7ToKTX(imgFontBC7, FONT_DIR L"FontAtlas.ktx");  // note - bc7 rgba texture is half the size of a rg only texture of the same dimensions.

		ImagingDelete(imgFontBC7);
		ImagingDelete(imgRef);

		if (MinCity::TextureBoy->KTXFileExists(FONT_DIR L"FontAtlas.ktx"))
		{
			// replace with cached BC7 version instead
			bLoaded = MinCity::TextureBoy->LoadKTXTexture(texture, FONT_DIR L"FontAtlas.ktx");
		}
	}
	
	nk_font_atlas_end(atlas, nk_handle_ptr(sampler), nulltex);
	nk_font_atlas_cleanup(atlas);

	if (atlas->default_font) {
		nk_style_set_font(ctx, &atlas->default_font->handle);
		atlas->default_font->handle.texture.id = 0; // override id for default texture
	}
}

typedef struct key_event
{
	int32_t key;
	int32_t action;
} key_event;

static inline struct // double clicking is purposely not supported for ease of use (touch tablet or touch screen)
{
	static constexpr uint32_t const MAX_TYPED = 24; // lots of room for multiple key actions / frame

	bool input_focused;
		 
	bool skip_click;

	struct nk_vec2 scroll;
	struct nk_vec2 mouse_pos;
	struct nk_vec2 last_mouse_pos;
	
	MouseState mouse_state;

	uint32_t characters_count;
	uint32_t characters[MAX_TYPED];
	
	int32_t key_mod;
	uint32_t keys_count;
	key_event keys[MAX_TYPED];

	void reset() {
		
		characters_count = 0;
		memset(characters, 0, sizeof(uint32_t) * MAX_TYPED);

		key_mod = 0;
		keys_count = 0;
		memset(keys, 0, sizeof(key_event) * MAX_TYPED);
	}

} nk_input = {};

static void
nk_center_cursor(GLFWwindow* const win) {
	point2D_t const ptCenter(p2D_shiftr(MinCity::getFramebufferSize(), 1U));
	glfwSetCursorPos(win, ptCenter.x, ptCenter.y);
	XMVECTOR const xmCenter(p2D_to_v2(ptCenter));
	XMStoreFloat2((XMFLOAT2 * const)& nk_input.mouse_pos, xmCenter);
}

void
cNuklear::nk_focus_callback(GLFWwindow *const win, int const focused)
{
	if (focused) {
		constinit static bool firstFocus{ true };
		if (firstFocus) { // *bugfix, needs to be set on 1st focus so cursor starts at correct location.
			nk_center_cursor(win);
			firstFocus = false;
		}
	}
	nk_input.input_focused = focused;
}
void
cNuklear::nk_iconify_callback(GLFWwindow * const win, int const minimized)
{
	nk_focus_callback(win, !minimized); // leverage
}

static void
nk_key_callback(GLFWwindow* win, int const key, int const scancode, int const action, int const mods)
{
	(void)win; (void)scancode;

	if (nk_input.input_focused) {

		nk_input.key_mod = mods;

		if (nk_input.keys_count < nk_input.MAX_TYPED) {

			nk_input.keys[nk_input.keys_count++] = key_event{ key, action };
		}
	}
}

static void
nk_char_callback(GLFWwindow *win, unsigned int const codepoint)
{
	(void)win;

	if (nk_input.input_focused) {

		if (nk_input.characters_count < nk_input.MAX_TYPED) {

			nk_input.characters[nk_input.characters_count++] = codepoint;
		}
		
	}
}

static void
nk_scroll_callback(GLFWwindow *win, double xoff, double yoff)
{
	(void)win;
	if (nk_input.input_focused) {
		nk_input.scroll.x += (float)xoff;
		nk_input.scroll.y += (float)yoff;
	}
}

static void
nk_mouse_button_callback(GLFWwindow* const window, int const button, int const action, int const mods)
{
	if (nk_input.input_focused) {

		MouseState state;
		state.button_state = eMouseButtonState::RELEASED;
		switch (button)
		{
		case GLFW_MOUSE_BUTTON_LEFT:
			if (GLFW_PRESS == action) state.button_state = eMouseButtonState::LEFT_PRESSED;
			break;
		case GLFW_MOUSE_BUTTON_RIGHT:
			if (GLFW_PRESS == action) state.button_state = eMouseButtonState::RIGHT_PRESSED;
			break;
		default:
			return;
		}

		switch (state.button_state)
		{
		case eMouseButtonState::RELEASED:
			if (eMouseButtonState::LEFT_PRESSED == nk_input.mouse_state.button_state) {
				state.left_clicked = !nk_input.skip_click && (state.tStamp - nk_input.mouse_state.tStamp < MouseState::CLICK_DELTA);
				state.left_released = true;
			}
			else if (eMouseButtonState::RIGHT_PRESSED == nk_input.mouse_state.button_state) {
				state.right_clicked = !nk_input.skip_click && (state.tStamp - nk_input.mouse_state.tStamp < MouseState::CLICK_DELTA);
				state.right_released = true;
			}
			break;
		default:
			break;
		}
		
		memcpy(&nk_input.mouse_state, &state, sizeof(sMouseState));
	}
}
static void
nk_mouse_position_callback(GLFWwindow* const window, double const xpos, double const ypos)
{
	constinit static XMVECTORF32 const xmMouseCursorGlyphNegSize{ float(-NK_CURSOR_DATA_W), float(-NK_CURSOR_DATA_H), 0.0f, 0.0f};

	if (nk_input.input_focused) {

		XMVECTOR xmMousePos(XMVectorSet((float)xpos, (float)ypos, 0.0f, 0.0f));

		xmMousePos = SFM::clamp(xmMousePos, xmMouseCursorGlyphNegSize, p2D_to_v2(MinCity::getFramebufferSize()));	// *bugfix, best way todo with glfw - keeps mouse restricted to the renderable area +- mouse cursor size.
		
		XMStoreFloat2((XMFLOAT2*)&nk_input.mouse_pos, xmMousePos);
		glfwSetCursorPos(window, nk_input.mouse_pos.x, nk_input.mouse_pos.y); // this updates the actual position with clamped values so mouse is never outside the renderable area. 
	}
}

struct nk_canvas {
	struct nk_command_buffer* painter;
	struct nk_vec2 item_spacing;
	struct nk_vec2 panel_padding;
	struct nk_style_item window_background;
};

static int 
nk_canvas_begin(struct nk_context* const ctx, std::string_view const& title, struct nk_canvas* const canvas, 
	struct nk_rect bounds,
	nk_flags flags,
	struct nk_color background_color)
{
	/* save style properties which will be overwritten */
	canvas->panel_padding = ctx->style.window.padding;
	canvas->item_spacing = ctx->style.window.spacing;
	canvas->window_background = ctx->style.window.fixed_background;

	/* use the complete window space and set background */
	ctx->style.window.spacing = nk_vec2(0, 0);
	ctx->style.window.padding = nk_vec2(0, 0);
	ctx->style.window.fixed_background = nk_style_item_color(background_color);

	/* create/update window and set position + size */
	flags = flags & ~NK_WINDOW_DYNAMIC;
	nk_window_set_bounds(ctx, title.data(), bounds);
	if ( nk_begin(ctx, title.data(), bounds, NK_WINDOW_NO_SCROLLBAR | flags) ) {

		/* allocate the complete window space for drawing */
	
		struct nk_rect total_space;
		total_space = nk_window_get_content_region(ctx);
		nk_layout_row_dynamic(ctx, total_space.h, 1);
		if (nk_widget(&total_space, ctx)) {
			canvas->painter = nk_window_get_canvas(ctx);
			return(nk_true);
		}

	}

	return(nk_false);
}

static void
nk_canvas_end(struct nk_context* const ctx, struct nk_canvas* const canvas)
{
	nk_end(ctx);
	ctx->style.window.spacing = canvas->panel_padding;
	ctx->style.window.padding = canvas->item_spacing;
	ctx->style.window.fixed_background = canvas->window_background;
}

static void SetUniformBuffer(vk::CommandBuffer& __restrict cb, vku::UniformBuffer& __restrict ubo)
{
	XMFLOAT2A vFramebufferSize;
	{
		XMVECTOR const xmFramebufferSize = p2D_to_v2(MinCity::getFramebufferSize());
		XMStoreFloat2A(&vFramebufferSize, xmFramebufferSize);
	}
	XMMATRIX const xmProjection(
		{ 2.0f / vFramebufferSize.x, 0.0f, 0.0f, 0.0f },
		{ 0.0f, 2.0f / vFramebufferSize.y, 0.0f, 0.0f },
		{  0.0f,  0.0f, -1.0f, 0.0f },
		{ -1.0f, -1.0f,  0.0f, 1.0f }
	); // this is adjusted for inverting y axis coordinates

	UniformDecl::nk_uniform const u{ xmProjection };;

	cb.updateBuffer(
		ubo.buffer(), 0, sizeof(UniformDecl::nk_uniform), (const void*)&u
	);
}

cNuklear::cNuklear()
	: _fontTexture(nullptr), _ctx(nullptr), _atlas(nullptr), _cmds(nullptr), _win(nullptr),
	_bUniformSet(false), _bUniformAcquireRequired(false),
	_bAcquireRequired(false),
	_bGUIDirty{ false, false }, _bEditControlFocused(false), _bModalPrompted(false),
	_uiWindowEnabled(0), _iWindowQuitSelection(-1), _iWindowSaveSelection(-1), _iWindowLoadSelection(-1), _iWindowImportSelection(-1),
	_uiPauseProgress(0),
	_bHintReset(false), _loadsaveWindow{},
	_bMinimapRenderingEnabled(false),
	_frameBufferSize(0, 0),		//set dynamically on init
	_frameBufferAspect(0.0f),	//set dynamically on init
	_frameBufferDPIAware(true),
	_vertices(nullptr),
	_indices(nullptr)
#ifdef DEBUG_LIGHT_PROPAGATION
	,_debugLightImage(nullptr)
#endif

{

}

GLFWwindow* const& cNuklear::getGLFWWindow() const
{
	return(_win);
}

void cNuklear::Initialize(GLFWwindow* const& __restrict win)
{
	static struct {
		struct nk_allocator			allocator;
		struct nk_context			ctx;
		struct nk_buffer			cmds;
		struct nk_draw_null_texture null;
	} nk;

	_cmds = &nk.cmds;
	_win = win;
	_ctx = &nk.ctx;
	_nulltex = &nk.null;
	nk.allocator.userdata.ptr = 0;
	nk.allocator.alloc = nk_custom_malloc;
	nk.allocator.free = nk_custom_free;

	/*
	glfwGetWindowSize(win, &glfw.width, &glfw.height);
	glfwGetFramebufferSize(win, &glfw.display_width, &glfw.display_height);
	glfw.fb_scale.x = (float)glfw.display_width / (float)glfw.width;
	glfw.fb_scale.y = (float)glfw.display_height / (float)glfw.height;
	*/
	
	glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
	glfwSetScrollCallback(win, nk_scroll_callback);
	glfwSetKeyCallback(win, nk_key_callback);
	glfwSetCharCallback(win, nk_char_callback);
	glfwSetMouseButtonCallback(win, nk_mouse_button_callback);
	glfwSetCursorPosCallback(win, nk_mouse_position_callback);

	// Center Mouse
	nk_center_cursor(win); // required first !
	nk_init(&nk.ctx, &nk.allocator, 0);
	nk_buffer_init_cmd_buffer(&nk.cmds);
	
	nk_font_stash_begin(&_atlas);
	// add fonts here
	struct nk_font_config config = { nk_font_config(NK_FONT_HEIGHT) };
	config.range = nk_font_vxlmono_glyph_ranges();
	config.oversample_h = 4;
	config.oversample_v = 4;
	//config.pixel_snap = true;
	
	_fonts[eNK_FONTS::DEFAULT]		 = nk_font_atlas_add_from_file(_atlas, FONT_DIR L"vxlmono.ttf", NK_FONT_HEIGHT, &config);
	
	_fonts[eNK_FONTS::SMALL]		 = nk_font_atlas_add_from_file(_atlas, FONT_DIR L"vxlmono.ttf", NK_FONT_HEIGHT * 0.8f, &config);
	
	_atlas->default_font = _fonts[eNK_FONTS::DEFAULT];

	// custom canvas'
	_offscreen_canvas = new nk_canvas();

	_vertices = (VertexDecl::nk_vertex* const __restrict)scalable_aligned_malloc(sizeof(VertexDecl::nk_vertex) * NK_MAX_VERTEX_COUNT, 16ULL);
	_indices = (uint16_t* const __restrict)scalable_aligned_malloc(sizeof(uint16_t) * NK_MAX_INDEX_COUNT, 16ULL);

	LoadGUITextures();
}

void cNuklear::SetSpecializationConstants_FS(std::vector<vku::SpecializationConstant>& __restrict constants)
{
	point2D_t const frameBufferSize(_frameBufferSize);

	constants.emplace_back(vku::SpecializationConstant(0, (float)frameBufferSize.x));// // frame buffer width
	constants.emplace_back(vku::SpecializationConstant(1, (float)frameBufferSize.y));// // frame buffer height
	constants.emplace_back(vku::SpecializationConstant(2, 1.0f / (float)frameBufferSize.x));// // frame buffer width
	constants.emplace_back(vku::SpecializationConstant(3, 1.0f / (float)frameBufferSize.y));// // frame buffer height

	// all textures are loaded prior to calling this function
	// _imageVector contains the count of custom textures including the default texture
	constants.emplace_back(vku::SpecializationConstant(4, (int)(getImageCount())));// image vector size
}

void cNuklear::UpdateDescriptorSet(vku::DescriptorSetUpdater& __restrict dsu, vk::Sampler const& sampler)
{
	// stop font adding, bake evrything
	nk_font_stash_end(_ctx, _atlas, _nulltex, _fontTexture, sampler);

	// load cursors from atlas
	nk_style_load_all_cursors(_ctx, _atlas->cursors);

	nk_set_style(_ctx, THEME_GREYSCALE);

	// -additional common style attributes- //

	// text
	_ctx->style.edit.border = 0.0f;
	_ctx->style.edit.border_color = nk_rgb(0, 0, 0);
	_ctx->style.edit.rounding = 0.0f;
	//_ctx->style.text.padding = nk_vec2(0.0f, 0.0f);

	// window
	_ctx->style.window.group_border = 3.0f;
	_ctx->style.window.group_border_color = nk_la(220, 255);
	_ctx->style.window.group_padding = nk_vec2(0.0f, 0.0f);

	//_ctx->style.window.spacing = nk_vec2(0.0f, 0.0f);
	//_ctx->style.window.padding = nk_vec2(0.0f, 0.0f);

	// group
	
	// button
	_ctx->style.button.text_normal = ACTIVE_TEXT_COLOR;
	_ctx->style.button.text_hover = HOVER_TEXT_COLOR;
	_ctx->style.button.text_active = ACTIVE_TEXT_COLOR;
	_ctx->style.button.text_alignment = NK_TEXT_CENTERED;
	_ctx->style.button.border = 0.0f;
	_ctx->style.button.border_color = nk_rgba(0, 0, 0, 0);
	_ctx->style.button.touch_padding = nk_vec2(1.0f, 1.0f); // required
	_ctx->style.button.padding = nk_vec2(0.0f, 0.0f);
	_ctx->style.button.rounding = 0.0f;
	
	// Set initial uniform buffer value ( already done prior to this function and bound to 0 )

	// update the reserved slot
	_imageVector.front()->handle.ptr = _fontTexture->imageView();

	// set all array images added in LoadGUITextures
	uint32_t array_slot(0); 
	for (auto& image : _imageVector) {

		// one off for default texture is at array slot zero
		dsu.beginImages(1, array_slot, vk::DescriptorType::eCombinedImageSampler);
		dsu.image(sampler, (VkImageView)image->handle.ptr, vk::ImageLayout::eShaderReadOnlyOptimal);
		// change temporary pointer to imageview now that were done with it
		// want texture slot for push constant selection of array index
		image->handle.id = array_slot;
		++array_slot;
	}
}


void cNuklear::LoadGUITextures()
{
	// Load Textures


	// Load Image Sequences
	_sequenceImages.menu_item = ImagingLoadGIFSequence(GUI_DIR L"menu_item.gif");
	_sequenceImages.static_screen = ImagingLoadGIFSequence(GUI_DIR L"static.gif");

	// Assign Images or Image Sequences to Textures

	{ // sequences 
		MinCity::TextureBoy->ImagingSequenceToTexture(_sequenceImages.menu_item, _guiTextures.menu_item);

		MinCity::TextureBoy->ImagingSequenceToTexture(_sequenceImages.static_screen, _guiTextures.static_screen);
	}

	{ // load thumbnail
		
		// pre-allocations of .image & .sequence required
		for (uint32_t i = 0; i < guiTextures::load_thumbnail::count; ++i) {
			_guiTextures.load_thumbnail.image[i] = ImagingNew(MODE_BGRA, offscreen_thumbnail_width, offscreen_thumbnail_height);
			ImagingClear(_guiTextures.load_thumbnail.image[i]);
		}

		// Common staging buffer for thumbnail
		_guiTextures.load_thumbnail.stagingBuffer = new vku::GenericBuffer((vk::BufferUsageFlags)vk::BufferUsageFlagBits::eTransferSrc, 
																		   (vk::DeviceSize)(_guiTextures.load_thumbnail.image[0]->ysize * _guiTextures.load_thumbnail.image[0]->linesize), 
																		   vk::MemoryPropertyFlagBits::eHostCoherent | vk::MemoryPropertyFlagBits::eHostVisible, VMA_MEMORY_USAGE_CPU_ONLY);

		// must allocate texture memory for each thumbnail so an id can be associated with the textures imageView()
		for (uint32_t i = 0; i < guiTextures::load_thumbnail::count; ++i) {
			MinCity::TextureBoy->ImagingToTexture<false>(_guiTextures.load_thumbnail.image[i], _guiTextures.load_thumbnail.texture[i]);
		}
	}

	// assigned *temporarily* the image view pointer
	using nk_image = struct nk_image;

	// first slot in array (0) reserved for default texture
	_imageVector.emplace_back( new nk_image_extended
	{	nullptr,	// this data is replaced when font texture is loaded, however this reserves the first slot for the default texture
		0, 0,
		{ 0, 0, 0, 0 },
		RGBA_IMAGE
	});

	_guiImages.demo = _imageVector.emplace_back(new nk_image_extended
		{ _guiTextures.menu_item->imageView(),
			(uint16_t)_guiTextures.menu_item->extent().width, (uint16_t)_guiTextures.menu_item->extent().height, // the size here can be customized
			{ 0, 0, (uint16_t)_guiTextures.menu_item->extent().width, (uint16_t)_guiTextures.menu_item->extent().height },
			ARRAY_IMAGE
		});

	_guiImages.road = _imageVector.emplace_back(new nk_image_extended
		{ _guiTextures.menu_item->imageView(),
			(uint16_t)_guiTextures.menu_item->extent().width, (uint16_t)_guiTextures.menu_item->extent().height, // the size here can be customized
			{ 0, 0, (uint16_t)_guiTextures.menu_item->extent().width, (uint16_t)_guiTextures.menu_item->extent().height },
			ARRAY_IMAGE
		});

	for (uint32_t i = 0; i < 3; ++i) {
		_guiImages.zoning[i] = _imageVector.emplace_back(new nk_image_extended
		{ _guiTextures.menu_item->imageView(),
			(uint16_t)_guiTextures.menu_item->extent().width, (uint16_t)_guiTextures.menu_item->extent().height, // the size here can be customized
			{ 0, 0, (uint16_t)_guiTextures.menu_item->extent().width, (uint16_t)_guiTextures.menu_item->extent().height },
			ARRAY_IMAGE
		});
	}

	_guiImages.power = _imageVector.emplace_back(new nk_image_extended
		{ _guiTextures.menu_item->imageView(),
			(uint16_t)_guiTextures.menu_item->extent().width, (uint16_t)_guiTextures.menu_item->extent().height, // the size here can be customized
			{ 0, 0, (uint16_t)_guiTextures.menu_item->extent().width, (uint16_t)_guiTextures.menu_item->extent().height },
			ARRAY_IMAGE
		});

	_guiImages.security = _imageVector.emplace_back(new nk_image_extended
		{ _guiTextures.menu_item->imageView(),
			(uint16_t)_guiTextures.menu_item->extent().width, (uint16_t)_guiTextures.menu_item->extent().height, // the size here can be customized
			{ 0, 0, (uint16_t)_guiTextures.menu_item->extent().width, (uint16_t)_guiTextures.menu_item->extent().height },
			ARRAY_IMAGE
		});

	_guiImages.science = _imageVector.emplace_back(new nk_image_extended
		{ _guiTextures.menu_item->imageView(),
			(uint16_t)_guiTextures.menu_item->extent().width, (uint16_t)_guiTextures.menu_item->extent().height, // the size here can be customized
			{ 0, 0, (uint16_t)_guiTextures.menu_item->extent().width, (uint16_t)_guiTextures.menu_item->extent().height },
			ARRAY_IMAGE
		});

	_guiImages.static_screen = _imageVector.emplace_back(new nk_image_extended
		{ _guiTextures.static_screen->imageView(),
			(uint16_t)_guiTextures.static_screen->extent().width, (uint16_t)_guiTextures.static_screen->extent().height, // the size here can be customized
			{ 0, 0, (uint16_t)_guiTextures.static_screen->extent().width, (uint16_t)_guiTextures.static_screen->extent().height },
			ARRAY_IMAGE
		});

	for (uint32_t i = 0; i < guiTextures::load_thumbnail::count; ++i) {
		_guiImages.load_thumbnail[i] = _imageVector.emplace_back(new nk_image_extended
		{   _guiTextures.load_thumbnail.texture[i]->imageView(),
			offscreen_thumbnail_width, offscreen_thumbnail_height, // the size here can be customized
			{ 0, 0, offscreen_thumbnail_width, offscreen_thumbnail_height },
			RGBA_IMAGE // initial layout
		});
	}

	_guiImages.offscreen = _imageVector.emplace_back( new nk_image_extended
	{	MinCity::Vulkan->offscreenImageView2DArray(), 
		(uint16_t)_frameBufferSize.x, (uint16_t)_frameBufferSize.y,
		{ 0, 0, (uint16_t)_frameBufferSize.x, (uint16_t)_frameBufferSize.y },
		RGBA_IMAGE
	});
}

void  cNuklear::Upload(uint32_t resource_index, vk::CommandBuffer& __restrict cb_transfer,
					   vku::DynamicVertexBuffer& __restrict vbo, vku::DynamicIndexBuffer& __restrict ibo, vku::UniformBuffer& __restrict ubo)
{	
	static constexpr size_t const VERTICES_SZ = sizeof(VertexDecl::nk_vertex) * NK_MAX_VERTEX_COUNT;
	static constexpr size_t const INDICES_SZ = sizeof(uint16_t) * NK_MAX_INDEX_COUNT;

	vk::CommandBufferBeginInfo bi(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);	// eOneTimeSubmit = cb updated every frame
	cb_transfer.begin(bi); VKU_SET_CMD_BUFFER_LABEL(cb_transfer, vkNames::CommandBuffer::OVERLAY_TRANSFER);

	if (_bGUIDirty[resource_index])
	{	
		{
			/* fill convert configuration */
			static struct nk_draw_vertex_layout_element const vertex_layout[] = {
				{NK_VERTEX_POSITION, NK_FORMAT_FLOAT, NK_OFFSETOF(VertexDecl::nk_vertex, position_uv.x)},
				{NK_VERTEX_TEXCOORD, NK_FORMAT_FLOAT, NK_OFFSETOF(VertexDecl::nk_vertex, position_uv.z)},
				{NK_VERTEX_COLOR, NK_FORMAT_R8G8B8A8, NK_OFFSETOF(VertexDecl::nk_vertex, color)},
				{NK_VERTEX_LAYOUT_END}
			};

			static struct nk_convert_config const config {
					.global_alpha = 1.0f,
					.line_AA = NK_ANTI_ALIASING_ON,
					.shape_AA = NK_ANTI_ALIASING_ON,

					.circle_segment_count = 22,
					.arc_segment_count = 22,
					.curve_segment_count = 22,
					.null = *_nulltex,

					.vertex_layout = vertex_layout,
					.vertex_size = sizeof(VertexDecl::nk_vertex),
					.vertex_alignment = NK_ALIGNOF(VertexDecl::nk_vertex)
			};
			/* setup buffers to load vertices and elements */
			struct nk_buffer vbuf, ebuf;
			nk_buffer_init_fixed(&vbuf, _vertices, VERTICES_SZ);  // buffers are cleared above
			nk_buffer_init_fixed(&ebuf, _indices, INDICES_SZ);
#ifndef NDEBUG
			if ( NK_CONVERT_SUCCESS != nk_convert(_ctx, _cmds, &vbuf, &ebuf, &config)) {
				FMT_LOG_FAIL(GPU_LOG, "Nuklear nk_convert did not succeed!");
			}
#else
			nk_convert(_ctx, _cmds, &vbuf, &ebuf, &config);
#endif

			MinCity::Vulkan->uploadBufferDeferred(vbo, cb_transfer, _stagingBuffer[0][resource_index], _vertices, vbuf.needed, VERTICES_SZ);
			MinCity::Vulkan->uploadBufferDeferred(ibo, cb_transfer, _stagingBuffer[1][resource_index], _indices, ebuf.needed, INDICES_SZ);

			{ // ## RELEASE ## //
				static constexpr size_t const buffer_count(2ULL);
				std::array<vku::GenericBuffer const* const, buffer_count> const buffers{ &vbo, &ibo };
				vku::GenericBuffer::barrier(buffers, // ## RELEASE ## // batched 
					cb_transfer, vk::PipelineStageFlagBits::eHost, vk::PipelineStageFlagBits::eTransfer,
					vk::DependencyFlagBits::eByRegion,
					vk::AccessFlagBits::eHostWrite, vk::AccessFlagBits::eVertexAttributeRead, MinCity::Vulkan->getTransferQueueIndex(), MinCity::Vulkan->getGraphicsQueueIndex()
				);
			}
			_bAcquireRequired = true;
		}

		if (!_bUniformSet) {

			SetUniformBuffer(cb_transfer, ubo);
			
			ubo.barrier( // ## RELEASE ## //
				cb_transfer, vk::PipelineStageFlagBits::eHost, vk::PipelineStageFlagBits::eTransfer,
				vk::DependencyFlagBits::eByRegion,
				vk::AccessFlagBits::eHostWrite, vk::AccessFlagBits::eUniformRead, MinCity::Vulkan->getTransferQueueIndex(), MinCity::Vulkan->getGraphicsQueueIndex()
			);

			_bUniformSet = true;  // only needs to be uploaded once
			_bUniformAcquireRequired = true; // flag that acquire operation needs to take place to complement previous release operation
		}

		_bGUIDirty[resource_index] = false;
	}
	// else is an empty cb but still submitted so normal flow continues (minimal overhead for ease of logical flow)

	cb_transfer.end();
}

void cNuklear::AcquireTransferQueueOwnership(vk::CommandBuffer& __restrict cb_render, vku::DynamicVertexBuffer& __restrict vbo, vku::DynamicIndexBuffer& __restrict ibo, vku::UniformBuffer& __restrict ubo)
{
	if (_bAcquireRequired) { // transfer queue ownership
		static constexpr size_t const buffer_count(2ULL);
		std::array<vku::GenericBuffer const* const, buffer_count> const buffers{ &vbo, &ibo };
		vku::GenericBuffer::barrier(buffers, // ## ACQUIRE ## // batched 
			cb_render, vk::PipelineStageFlagBits::eHost, vk::PipelineStageFlagBits::eVertexInput,
			vk::DependencyFlagBits::eByRegion,
			vk::AccessFlagBits::eHostWrite, vk::AccessFlagBits::eVertexAttributeRead, MinCity::Vulkan->getTransferQueueIndex(), MinCity::Vulkan->getGraphicsQueueIndex()
		);
		_bAcquireRequired = false; // must reset *here*
	}
	if (_bUniformAcquireRequired) {
		// transfer queue ownership
		ubo.barrier(	// ## ACQUIRE ## //
			cb_render, vk::PipelineStageFlagBits::eHost, vk::PipelineStageFlagBits::eVertexShader,
			vk::DependencyFlagBits::eByRegion,
			vk::AccessFlagBits::eHostWrite, vk::AccessFlagBits::eUniformRead, MinCity::Vulkan->getTransferQueueIndex(), MinCity::Vulkan->getGraphicsQueueIndex()
		);
		_bUniformAcquireRequired = false; // must reset *here*
	}
}

void  cNuklear::Render(vk::CommandBuffer& __restrict cb_render,
					   vku::DynamicVertexBuffer& __restrict vbo, vku::DynamicIndexBuffer& __restrict ibo, vku::UniformBuffer& __restrict ubo,
					   RenderingInfo const& __restrict renderInfo) const
{	
#ifndef GIF_MODE
	UniformDecl::NuklearPushConstants PushConstants;

	cb_render.bindPipeline(vk::PipelineBindPoint::eGraphics, renderInfo.pipeline);

	cb_render.bindVertexBuffers(0, vbo.buffer(), vk::DeviceSize(0));
	cb_render.bindIndexBuffer(ibo.buffer(), vk::DeviceSize(0), vk::IndexType::eUint16);
	
	cb_render.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *renderInfo.pipelineLayout, 0, renderInfo.sets, nullptr);

	const struct nk_draw_command *cmd;
	uint32_t index_offset(0);
	int32_t last_texture_id(-1);

	nk_draw_foreach(cmd, _ctx, _cmds)
	//for ((cmd) = nk__draw_list_begin(_drawList, _cmds); (cmd) != 0; (cmd) = nk__draw_list_next(cmd, _cmds, _drawList))
	{
		if (!cmd->elem_count) continue;

		int32_t const texture_id(cmd->texture.id);
		if (texture_id != last_texture_id) { // update push constants whenever texture changes

			if (texture_id < getImageCount()) {
				PushConstants.array_index = texture_id;
			}
			else {
				PushConstants.array_index = 0; // important to default to "default texture" when an id is out of bounds it was set internally by nuklear to something random. Which happens to be the default font texture atlas.
			}
			
			PushConstants.type = _imageVector[PushConstants.array_index]->type; // resolve type (sdf, rgba) 

			cb_render.pushConstants(*renderInfo.pipelineLayout, vk::ShaderStageFlagBits::eFragment, 
				(uint32_t)0U, (uint32_t)sizeof(UniformDecl::NuklearPushConstants), reinterpret_cast<void const* const>(&PushConstants));

			last_texture_id = texture_id;
		}

		static constexpr float const window_room(3.0f);

		vk::Rect2D const scissor(
				vk::Offset2D(SFM::floor_to_i32(SFM::max(cmd->clip_rect.x - window_room, 0.0f)),
							 SFM::floor_to_i32(SFM::max(cmd->clip_rect.y - window_room, 0.0f))),

				vk::Extent2D(SFM::ceil_to_u32(cmd->clip_rect.w + window_room * 2.0f),
					         SFM::ceil_to_u32(cmd->clip_rect.h + window_room * 2.0f))
		);
		cb_render.setScissor(0, scissor);
		cb_render.drawIndexed(cmd->elem_count, 1, index_offset, 0, 0);

		index_offset += cmd->elem_count;
	}
#endif
}

// *******************************************************************************************
// INPUT has no app time paased in, and is purposely not aware of it.
// if time is used inside this function, it has to be local and the high_resolution clock
// do not use now(), delta() etc as that is the global app time
// timing inside of UpdateInput should be for simple things like repeat rate of key pressed
// this local time should not be passed to any child functions - only local to this function
// to allow precise timing for input related stuff
// the timestamp in mouse state is also derived from its own high_resolution_clock query
// its safe to use tLocal here with tStamp
// ********************************************************************************************
static bool const UpdateInput(struct nk_context* const __restrict ctx, GLFWwindow* const __restrict win, vector<rect2D_t> const& __restrict activeWindowRects, bool& __restrict bModalPrompted)
{
	tTime const tLocal(critical_now());
	static tTime tLocalLast(tLocal);

	/* Input */
	nk_input.reset(); // important to call b4 polling events !
	glfwPollEvents();

	bool bInputGUIDelta(false); // *** signals GUI to update (returned from this function) *** remeber to set when GUI is affected by input !!

	constinit static bool bEscapePressed(false);
	if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) {		// always process escape key
		bEscapePressed = true;
	}
	else if (bEscapePressed && glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_RELEASE) { // only on a key "clicked" process escape
		bEscapePressed = false; // exclusively reset state

		if (!bModalPrompted) {
			MinCity::Quit(); // only if not modal continue with closing the actual current window. important for correct "back behaviour" of modal->main window transistion for example.
		}
		bModalPrompted = false; // cancel any modal window with "no effect" to state

		bInputGUIDelta = true;  // this handles all of escape key possible states (back, open main menu, close dialog) it is not for quitting the game - Shutdown() handles that.
	}

	if (!nk_input.input_focused) { // only continue to process input if window is focused
		MinCity::VoxelWorld->OnMouseInactive();
		return(false);
	}
	
	//############################
	nk_input_begin(ctx);
	//############################

	if (MinCity::Nuklear->isEditControlFocused()) { // nuklear input

		for (uint32_t i = 0; i < nk_input.characters_count; ++i) {

			nk_input_unicode(ctx, nk_input.characters[i]);

		}
		bInputGUIDelta = (0 != nk_input.characters_count);

		// only keys we care about for edit control input
		for (uint32_t i = 0; i < nk_input.keys_count; ++i) {

			bool const down(GLFW_PRESS == nk_input.keys[i].action);

			switch (nk_input.keys[i].key)
			{
			case GLFW_KEY_DELETE:
				nk_input_key(ctx, NK_KEY_DEL, down);
				bInputGUIDelta = true;
				break;
			case GLFW_KEY_BACKSPACE:
				nk_input_key(ctx, NK_KEY_BACKSPACE, down);
				bInputGUIDelta = true;
				break;
			case GLFW_KEY_ENTER:
				nk_input_key(ctx, NK_KEY_ENTER, down);
				bInputGUIDelta = true;
				break;
			case GLFW_KEY_LEFT:
				nk_input_key(ctx, NK_KEY_LEFT, down);
				bInputGUIDelta = true;
				break;
			case GLFW_KEY_RIGHT:
				nk_input_key(ctx, NK_KEY_RIGHT, down);
				bInputGUIDelta = true;
				break;
			default:
				break;
			}
		}
	}
	else { // key handling normal (exclusively gui or exclusively game focused)
		bool const ctrl((glfwGetKey(win, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS) || (glfwGetKey(win, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS));

		for (uint32_t i = 0; i < nk_input.keys_count; ++i) {

			bool const down(((GLFW_PRESS & nk_input.keys[i].action) | (GLFW_REPEAT & nk_input.keys[i].action)));
			
			// ** only gui related actions here ** //
			switch (nk_input.keys[i].key)
			{
			/* ADD AS NEEDED
			case GLFW_KEY_DELETE:
				nk_input_key(ctx, NK_KEY_DEL, down);
				bInputGUIDelta = true;
				break;
			case GLFW_KEY_BACKSPACE:
				nk_input_key(ctx, NK_KEY_BACKSPACE, down);
				bInputGUIDelta = true;
				break;
			*/
			default:
				// (exclusively game focused) key events are all forwarded to VoxelWorld for handling //
				MinCity::VoxelWorld->OnKey(nk_input.keys[i].key, down, ctrl);
				break;
			}
		}
	}

	// handling mouse input

	bool const isWindowEnabled(MinCity::Nuklear->getWindowEnabled());
	bool isHoveringWindow(false); 
	
	for (auto const& rectWindow : activeWindowRects) {
		alignas(16) struct nk_rect rectActiveWindow;
		XMStoreFloat4A(reinterpret_cast<XMFLOAT4A*>(&rectActiveWindow), r2D_to_nk_rect(rectWindow));
		if (nk_input_is_mouse_hovering_rect(&ctx->input, rectActiveWindow)) {
			isHoveringWindow = true;
			break;
		}
	}

	bool const isActiveWindow(isWindowEnabled | isHoveringWindow);

	{
		XMVECTOR const xmPosition(XMLoadFloat2((XMFLOAT2* const __restrict)&nk_input.mouse_pos));
		
		if (MinCity::VoxelWorld->OnMouseMotion(xmPosition, isActiveWindow)) { // Hi resolution mouse motion tracking (internally chechked for high precision delta of mouse coords)

			nk_input_motion(ctx, nk_input.mouse_pos.x, nk_input.mouse_pos.y);
				
			if (!p2D_sub(point2D_t(nk_input.mouse_pos.x, nk_input.mouse_pos.y),
				         point2D_t(nk_input.last_mouse_pos.x, nk_input.last_mouse_pos.y)).isZero()) { // bugfix: have clip cursor every time for it to stop at edges of screen properly
						
				nk_input.last_mouse_pos = nk_input.mouse_pos;  // only updated for whole pixel movement to improve clipping performance
				bInputGUIDelta = true; // mouse cursor
			}
		}
		else if (isActiveWindow) {
			nk_input_motion(ctx, nk_input.mouse_pos.x, nk_input.mouse_pos.y); // always update gui

			if (!p2D_sub(point2D_t(nk_input.mouse_pos.x, nk_input.mouse_pos.y),
				         point2D_t(nk_input.last_mouse_pos.x, nk_input.last_mouse_pos.y)).isZero()) { // bugfix: have clip cursor every time for it to stop at edges of screen properly
						
				nk_input.last_mouse_pos = nk_input.mouse_pos;  // only updated for whole pixel movement to improve clipping performance
				bInputGUIDelta = true; // mouse cursor
			}
		}
	}

	if (!nk_input.mouse_state.handled) {

		switch (nk_input.mouse_state.button_state)
		{
		case eMouseButtonState::LEFT_PRESSED:
			nk_input_button(ctx, NK_BUTTON_LEFT, nk_input.mouse_pos.x, nk_input.mouse_pos.y, true);
			bInputGUIDelta = isHoveringWindow;
			break;
		case eMouseButtonState::RIGHT_PRESSED:
			nk_input_button(ctx, NK_BUTTON_RIGHT, nk_input.mouse_pos.x, nk_input.mouse_pos.y, true);
			bInputGUIDelta = isHoveringWindow;
			break;
		case eMouseButtonState::RELEASED:
			if (nk_input.mouse_state.left_released) {
				nk_input_button(ctx, NK_BUTTON_LEFT, nk_input.mouse_pos.x, nk_input.mouse_pos.y, false);
				bInputGUIDelta = isHoveringWindow;
			}
			else if (nk_input.mouse_state.right_released) {
				nk_input_button(ctx, NK_BUTTON_RIGHT, nk_input.mouse_pos.x, nk_input.mouse_pos.y, false);
				bInputGUIDelta = isHoveringWindow;
			}
			break;
		default:
			break;
		}
	}
	else {
		// if already handled mark as INACTIVE if tStamp is older than CLICK_DELTA and is RELEASED
		if (eMouseButtonState::RELEASED == nk_input.mouse_state.button_state && ((tLocal - nk_input.mouse_state.tStamp) > MouseState::CLICK_DELTA))
			nk_input.mouse_state.button_state = eMouseButtonState::INACTIVE;
	}

	// y axis window mousewheel (smoother) scrolling
	static tTime tLast(tLocal);
	constinit static fp_seconds tAccumulate(zero_time_duration);
	constinit static float scroll(0.0f);
	constinit static bool scrolling(false);

	if (isHoveringWindow && (0.0f != nk_input.scroll.y)) {
		
		scroll = nk_input.scroll.y;
		tLast = tLocal;
		scrolling = true;
	}
	if (!isHoveringWindow) {
		scroll = 0.0f;
		scrolling = false;
	}
	if (scrolling)
	{
		static constexpr fp_seconds const interval(milliseconds(66));
		static constexpr float const inv_interval(1.0f/interval.count());

		fp_seconds const tDelta(tLocal - tLast);

		tAccumulate += tDelta;
		if (tAccumulate > interval) {

			tAccumulate = interval;
			scrolling = false; // reset (after this final iteration takes effect)

		}
		struct nk_vec2 scaled_scroll(0.0f, scroll);
		scaled_scroll.y *= tAccumulate.count() * inv_interval; // normalized to scale in the range [0.0f .... 1.0f] for n seconds.

		if (!scrolling) {
			tAccumulate = fp_seconds(inv_interval);
			scroll = 0.0f; // reset (after this final iteration takes effect)
		}

		nk_input_scroll(ctx, scaled_scroll);
		nk_input.scroll.y = 0.0f; // must zero once handled

		bInputGUIDelta = true; // scrolling window

		tLast = tLocal;
	}
	//#######################
	nk_input_end(ctx);
	//#######################
	
	// mouse handling for game input
	if (!nk_input.mouse_state.handled) {

		switch (nk_input.mouse_state.button_state)
		{
		case eMouseButtonState::LEFT_PRESSED:
			if (!isActiveWindow) {
				MinCity::VoxelWorld->OnMouseLeft(eMouseButtonState::LEFT_PRESSED);
			}
			break;
		case eMouseButtonState::RIGHT_PRESSED:
			if (!isActiveWindow) {
				MinCity::VoxelWorld->OnMouseRight(eMouseButtonState::RIGHT_PRESSED);
			}
			break;
		case eMouseButtonState::RELEASED:
			if (nk_input.mouse_state.left_released) {
				MinCity::VoxelWorld->OnMouseLeft(eMouseButtonState::RELEASED);
			}
			else if (nk_input.mouse_state.right_released) {
				MinCity::VoxelWorld->OnMouseRight(eMouseButtonState::RELEASED);
			}
			break;
		default:
			MinCity::VoxelWorld->OnMouseInactive();
			break;
		}

		if (!isHoveringWindow) {
			if (nk_input.mouse_state.left_clicked) {

				if (!isWindowEnabled) {
					MinCity::VoxelWorld->OnMouseLeftClick();
				}
				else {
					MinCity::DispatchEvent(eEvent::ESCAPE); // outside a window click - back to game
				}
			}
			else if (nk_input.mouse_state.right_clicked) {

				if (!isWindowEnabled) {
					MinCity::VoxelWorld->OnMouseRightClick();
				}
				else {
					MinCity::DispatchEvent(eEvent::ESCAPE); // outside a window click - back to game
				}
			}
			
			nk_input.skip_click = false; // always reset
		}
	
	} // handled ?
	else if (isHoveringWindow) {

		// only if current state is handled (no change between successive calls to this function)
		MinCity::VoxelWorld->OnMouseInactive();
	}

	// end mouse input
	if (!isHoveringWindow) { // must persist outside handled...
		if (0.0f != nk_input.scroll.y) {
			MinCity::VoxelWorld->OnMouseScroll(nk_input.scroll.y);
			nk_input.scroll.y = 0.0f; // must zero once handled
		}
	}
	nk_input.mouse_state.handled = true; // mark as handled
	//
	
	{ // rate limited keys
		static tTime tLast;
		static uint32_t LastPressed(0), WasPressed(0);

		if (WasPressed & 
			( (glfwGetKey(win, GLFW_KEY_LEFT) == GLFW_RELEASE)
			| (glfwGetKey(win, GLFW_KEY_RIGHT) == GLFW_RELEASE)
			| (glfwGetKey(win, GLFW_KEY_UP) == GLFW_RELEASE)
			| (glfwGetKey(win, GLFW_KEY_DOWN) == GLFW_RELEASE)
			| (glfwGetKey(win, GLFW_KEY_PAGE_UP) == GLFW_RELEASE)
			| (glfwGetKey(win, GLFW_KEY_PAGE_DOWN) == GLFW_RELEASE)
			| (glfwGetKey(win, GLFW_KEY_1) == GLFW_RELEASE)
			| (glfwGetKey(win, GLFW_KEY_2) == GLFW_RELEASE)
			| (glfwGetKey(win, GLFW_KEY_3) == GLFW_RELEASE)
			)
			)
		{
			if (WasPressed != LastPressed)
				tLast = zero_time_point;

			LastPressed = WasPressed;
			WasPressed = 0;
		}

		static tTime tLastPress(zero_time_point);
		if (tLocal - tLastPress > milliseconds(164)) { // repeat rate 
			
			static constexpr float ADJUST_SCALE = 0.01f;
			static XMVECTOR xmPushConstant{};
			
			bool Changed(false);
			if (glfwGetKey(win, GLFW_KEY_LEFT) == GLFW_PRESS) {
				xmPushConstant = XMVectorAdd(xmPushConstant, { -ADJUST_SCALE, 0.0f, 0.0f, 0.0f });
				
				WasPressed = GLFW_KEY_LEFT;
				Changed = true;
			}
			if (glfwGetKey(win, GLFW_KEY_RIGHT) == GLFW_PRESS) {
				xmPushConstant = XMVectorAdd(xmPushConstant, { ADJUST_SCALE, 0.0f, 0.0f, 0.0f });

				WasPressed = GLFW_KEY_RIGHT;
				Changed = true;
			}
			if (glfwGetKey(win, GLFW_KEY_UP) == GLFW_PRESS) {
				xmPushConstant = XMVectorAdd(xmPushConstant, { 0.0f, ADJUST_SCALE, 0.0f, 0.0f });
				if (glfwGetKey(win, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) {
					xmPushConstant = XMVectorAdd(xmPushConstant, { ADJUST_SCALE, 0.0f, 0.0f, 0.0f });
				}
				WasPressed = GLFW_KEY_UP;
				Changed = true;
			}
			if (glfwGetKey(win, GLFW_KEY_DOWN) == GLFW_PRESS) {
				xmPushConstant = XMVectorAdd(xmPushConstant, { 0.0f, -ADJUST_SCALE, 0.0f, 0.0f });
				if (glfwGetKey(win, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) {
					xmPushConstant = XMVectorAdd(xmPushConstant, { -ADJUST_SCALE, 0.0f, 0.0f, 0.0f });
				}
				WasPressed = GLFW_KEY_DOWN;
				Changed = true;
			}
			if (Changed) {
				setDebugVariable(XMVECTOR, DebugLabel::PUSH_CONSTANT_VECTOR, xmPushConstant);
				FMT_NUKLEAR_DEBUG(false, "push_constant_vector: x{:02f} , y{:02f}\n", XMVectorGetX(xmPushConstant), XMVectorGetY(xmPushConstant));
				tLastPress = tLocal;
			}

			/*
			static uint8_t uAdjust{0};
			if (glfwGetKey(win, GLFW_KEY_PAGE_UP) == GLFW_PRESS) {

				++uAdjust;

				setDebugVariable(uint8_t, DebugLabel::RAMP_CONTROL_BYTE, uAdjust);
;
				FMT_NUKLEAR_DEBUG(false, "ramp_control_byte: {:d} \n", uAdjust);

				WasPressed = GLFW_KEY_PAGE_UP;
				tLastPress = tLocal;
			}
			if (glfwGetKey(win, GLFW_KEY_PAGE_DOWN) == GLFW_PRESS) {

				--uAdjust;

				setDebugVariable(uint8_t, DebugLabel::RAMP_CONTROL_BYTE, uAdjust);

				FMT_NUKLEAR_DEBUG(false, "ramp_control_byte: {:d} \n", uAdjust);

				WasPressed = GLFW_KEY_PAGE_DOWN;
				tLastPress = tLocal;
			}

			static bool bToggle1(false), bToggle2(false), bToggle3(false);
			if (glfwGetKey(win, GLFW_KEY_1) == GLFW_PRESS) {

				bToggle1 = !bToggle1;

				setDebugVariable(bool, DebugLabel::TOGGLE_1_BOOL, bToggle1);

				FMT_NUKLEAR_DEBUG(false, "Toggle Group: ( {:d}, {:d}, {:d} ) \n", bToggle1, bToggle2, bToggle3);

				WasPressed = GLFW_KEY_1;
				tLastPress = tLocal;
			}
			if (glfwGetKey(win, GLFW_KEY_2) == GLFW_PRESS) {

				bToggle2 = !bToggle2;

				setDebugVariable(bool, DebugLabel::TOGGLE_2_BOOL, bToggle2);

				FMT_NUKLEAR_DEBUG(false, "Toggle Group: ( {:d}, {:d}, {:d} ) \n", bToggle1, bToggle2, bToggle3);

				WasPressed = GLFW_KEY_2;
				tLastPress = tLocal;
			}
			if (glfwGetKey(win, GLFW_KEY_3) == GLFW_PRESS) {

				bToggle3 = !bToggle3;;

				setDebugVariable(bool, DebugLabel::TOGGLE_3_BOOL, bToggle3);

				FMT_NUKLEAR_DEBUG(false, "Toggle Group: ( {:d}, {:d}, {:d} ) \n", bToggle1, bToggle2, bToggle3);

				WasPressed = GLFW_KEY_3;
				tLastPress = tLocal;
			}
			*/
			if (glfwGetKey(win, GLFW_KEY_BACKSPACE) == GLFW_PRESS) {

				xmPushConstant = XMVectorZero();

				setDebugVariable(XMVECTOR, DebugLabel::PUSH_CONSTANT_VECTOR, xmPushConstant);

				FMT_NUKLEAR_DEBUG(false, "<< all debug variables zeroed >>");

				WasPressed = GLFW_KEY_BACKSPACE;
				tLastPress = tLocal;
			}
		} // repeatrate
	}
	
	tLocalLast = tLocal;

	return(bInputGUIDelta);
}

bool const  cNuklear::UpdateInput()  // returns true on input delta that affects gui
{
	/* Input */
	return(::UpdateInput(_ctx, _win, _activeWindowRects, _bModalPrompted));
}

void cNuklear::InputEnableWorld(uint32_t const bits, bool const bEnable) const // cannot resolve mincity.h and cvoxelworld.h in the header file. work-around so it can be access from template function enableWindow()
{
	if (bEnable) {
		MinCity::VoxelWorld->InputEnable<true>(bits);
	}
	else {
		MinCity::VoxelWorld->InputEnable<false>(bits);
	}
}
#ifdef DEBUG_LUT_WINDOW
void cNuklear::draw_lut_window(tTime const& __restrict tNow, uint32_t const height_offset)
{
	static constexpr milliseconds const tUpdateInterval(50); // rate limited input to not stall program

	static std::vector<std::string> available_luts;
	static tTime tLastChanged(zero_time_point);
	static float tT(0.5f);
	static std::string selected_lut;
	static int32_t selected_luts[2]{};
	static bool bFirstShown(true), bUploadLUT(false);
	
	if (0 == available_luts.size()) // do once
	{
		available_luts.reserve(2);

		fs::path const cube_extension(L".cube");
		fs::path const path_to_textures{ TEXTURE_DIR };

		for (auto const& entry : fs::directory_iterator(path_to_textures)) {

			if (entry.path().extension() == cube_extension) {
				available_luts.push_back(entry.path().filename().string());
			}

		}

		if (!selected_lut.empty()) {

			selected_luts[0] = selected_luts[1] = 0;

			for (uint32_t i = 0; i < available_luts.size(); ++i) {

				if (selected_lut == available_luts[i]) {

					selected_luts[0] = selected_luts[1] = i;

				}

			}

			if (0 != selected_luts[0]) {
				bFirstShown = true;
			}
			selected_lut.clear();
		}
	}

	uint32_t const window_offset(25);
	uint32_t const window_width(_frameBufferSize.x - (window_offset << 1));
	uint32_t const window_height(200);

	constexpr eWindowName const windowName(eWindowName::WINDOW_LUT);
	if (nk_begin(_ctx, windowName._to_string(),
		nk_recti(window_offset, _frameBufferSize.y - height_offset - window_height, window_width, window_height),
		NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BACKGROUND | NK_WINDOW_NO_INPUT))
	{
		struct nk_rect const windowBounds(nk_window_get_bounds(_ctx));

		AddActiveWindowRect(r2D_set_by_width_height(windowBounds.x, windowBounds.y, windowBounds.w, windowBounds.h));	// must register any active window;

		float t = tT;
		bool bUpdateRequired(false);
		{
			nk_layout_row_static(_ctx, 32, (window_width >> 1), 2);

			for (uint32_t i = 0; i < 2; ++i) {
				int32_t const current_selection(selected_luts[i]);
				selected_luts[i] = nk_combo(_ctx, available_luts.data(), available_luts.size(), current_selection, 25, nk_vec2((window_width >> 1), 200));

				if (current_selection != selected_luts[i]) {
					t = 0.5f; // reset
					bUpdateRequired = true;
				}
			}

			if (bFirstShown) {
				t = 0.5f; // reset
				bUpdateRequired = true;
				bFirstShown = false;
			}
			//nk_label(_ctx, selected_luts[0].data(), NK_TEXT_LEFT);
			//nk_label(_ctx, selected_luts[1].data(), NK_TEXT_RIGHT);
		}

		{
			nk_layout_row_static(_ctx, 32, window_width, 2);
			nk_slider_float(_ctx, 0.0f, &t, 1.0f, 0.01f);

			nk_layout_row_static(_ctx, 32, window_width, 1);
			nk_labelf(_ctx, NK_TEXT_CENTERED, "lerp: %.02f", tT);

			nk_layout_row_static(_ctx, 32, (window_width >> 3), 2);
			if (nk_button_label(_ctx, "reset")) {
				t = 0.5f;
				bUpdateRequired = true;
			}
			if (nk_button_label(_ctx, "save")) {
				if (MinCity::PostProcess->SaveMixedLUT(selected_lut, available_luts[selected_luts[0]], available_luts[selected_luts[1]], tT)) {
					available_luts.clear();
				}
			}
			else {
				if (tT != t || bUpdateRequired) {
					
					tT = t;

					if (tNow - tLastChanged > tUpdateInterval || bUpdateRequired) {
						if (MinCity::PostProcess->MixLUT(available_luts[selected_luts[0]], available_luts[selected_luts[1]], tT)) {

							tLastChanged = tNow;
							bUploadLUT = true;
						}
					}
				}
				else if (bUploadLUT) {
					bUploadLUT = !MinCity::PostProcess->UploadLUT(); // must be done on main thread, only uploads when lut that was last mixed has completed
				}
			}
		}
	}
	nk_end(_ctx);
}
#endif

// *******************************************************************************************
// GUI has no app time paased in, and is purposely not aware of it.
// if time is used inside this function, it has to be local and the high_resolution clock
// do not use now(), delta() etc as that is the global app time
// timing inside of UpdateGUI should be for simple things like blinking cursors, etc
// this local time should not be passed to any child functions - only local to this function
// except for _ctx->delta_time_seconds
// ********************************************************************************************
void cNuklear::SetGUIDirty()
{
	_bGUIDirty[0] = _bGUIDirty[1] = true; // both buffers must be reset so that they are in sync from frame to frame
}

STATIC_INLINE_PURE struct nk_rect const __vectorcall make_centered_window_rect(int32_t const window_width, int32_t const window_height, point2D_t const frameBufferSize)
{
	return(nk_recti((frameBufferSize.x >> 1) - (window_width >> 1), (frameBufferSize.y >> 1) - (window_height >> 1), window_width, window_height));
}


void cNuklear::do_cyberpunk_mainmenu_window(std::string& __restrict szHint, bool& __restrict bResetHint, bool& __restrict bSmallHint)
{
	static constexpr uint32_t const rows(20), cols(9);
	static constexpr float const inv_cols(1.0f / (float)cols);

	constinit static char const legend[rows * (cols)] = {

		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x10, 0x11, 0x12, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x20, 0x21, 0x22, 0x23, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x30, 0x31, 0x32, 0x33, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x40, 0x41, 0x42, 0x43, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

	};
	static constexpr uint32_t const menu_item_count(4);
	constinit static char const* const menu_item[4] = {
		"NEW",
		"LOAD",
		"SAVE",
		"EXIT"
	};
	constinit static bool menu_descrambled[4] = {
		false,
		false,
		false,
		false
	};

	constinit static char const szMessage[] = { 'C', 'O', 'P', 'Y', 'R', 'I', 'G', 'H', 'T', ' ', 0x17, 0x17, 0x17, ' ', 'S', 'O', 'F', 'T', 'W', 'A', 'R', 'E', 0x00 };

	static constexpr uint32_t const count(_countof(gui::cyberpunk_glyphs));

	static constexpr fp_seconds const interval(CYBERPUNK_BEAT);
	
	constinit static tTime tLast(zero_time_point);
	constinit static int32_t seed(1);

	fp_seconds const accumulated(critical_now() - tLast);
	float const t = SFM::saturate(accumulated / interval);
	constinit static uint32_t descrambled_index(_countof(menu_descrambled)); // usage with seperate - 1

	if (accumulated > interval) {
		tLast = critical_now();
		
		seed = PsuedoRandomNumber32();

		if (0 == --descrambled_index) {
			descrambled_index = _countof(menu_descrambled);
		}
	}

	SetSeed(seed);

	if (!_bModalPrompted) {
		
		constexpr eWindowName const subwindowName(eWindowName::WINDOW_MAIN);

		if (nk_begin(_ctx, subwindowName._to_string(),
			make_centered_window_rect(190, 445, _frameBufferSize),
			NK_TEMP_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR))
		{
			static constexpr uint32_t const glyph_width(NK_FONT_WIDTH),
											glyph_height(NK_FONT_HEIGHT * 0.5f);

			struct nk_rect const windowBounds(nk_window_get_bounds(_ctx));

			AddActiveWindowRect(r2D_set_by_width_height(windowBounds.x, windowBounds.y, windowBounds.w, windowBounds.h));	// must register any active window;

			szHint = szMessage;
			bResetHint = false;
			bSmallHint = true;

			bool any_hovered(false);

			char glyph[2]{};

			for (uint32_t row = 0; row < rows; ++row) {

				nk_layout_space_begin(_ctx, NK_STATIC, 0.01f, cols);

				for (uint32_t col = 0; col < cols; ++col) {

					nk_layout_space_push(_ctx, nk_recti(col * glyph_width, row * glyph_height, glyph_width, glyph_height));

					char const key(legend[row * cols + col]);
					if (0 == key) {
						uint32_t const position = PsuedoRandomNumber(0, count - 1);
						glyph[0] = gui::cyberpunk_glyphs[position];

						nk_label(_ctx, glyph, NK_TEXT_CENTERED);

						glyph[0] = 0;
					}
					else {
						uint32_t const menu_item_index(((0xf0 & key) >> 4) - 1);
						uint32_t const menu_item_character_index(0x0f & key);

						if (menu_descrambled[menu_item_index]) {
							PsuedoRandomNumber(); // *bugfix - discard next random number, which maintains sequence of random numbers so there isn't a sudden change in all glyphs after this one.
							glyph[0] = menu_item[menu_item_index][menu_item_character_index];
						}
						else {
							uint32_t const position = PsuedoRandomNumber(0, count - 1);
							glyph[0] = gui::cyberpunk_glyphs[position];
						}

						bool bHovered(false);

						if (nk_button_label(_ctx, glyph, &bHovered)) {

							switch (menu_item_index) {
							case 0:
								szHint = "GENERATING...";
								bResetHint = false;
								bSmallHint = false;

								MinCity::DispatchEvent(eEvent::NEW);
								enableWindow<eWindowType::MAIN>(false);
								nk_window_close(_ctx, subwindowName._to_string());  // hides main window but does not close it in this case 
								break;
							case 1:
								enableWindow<eWindowType::MAIN>(false);
								enableWindow<eWindowType::LOAD>(true);
								nk_window_close(_ctx, subwindowName._to_string());
								break;
							case 2:
								enableWindow<eWindowType::MAIN>(false);
								enableWindow<eWindowType::SAVE>(true);
								nk_window_close(_ctx, subwindowName._to_string());
								break;
							case 3:
								_iWindowQuitSelection = eWindowQuit::SAVE_AND_QUIT;
								_bModalPrompted = true; // will (next frame) activate modal window to begin the exit process (main window->exit transition)
								nk_window_close(_ctx, subwindowName._to_string()); // hides main window but does not close it in this case 
								break;
							}
						}
						else if (bHovered) {
							menu_descrambled[0] = menu_descrambled[1] = menu_descrambled[2] = menu_descrambled[3] = false;
							menu_descrambled[menu_item_index] = true;
							any_hovered = true;
						}

						glyph[0] = 0;
					}
				}

				nk_layout_space_end(_ctx);
			}
			if (!any_hovered) {

				menu_descrambled[0] = menu_descrambled[1] = menu_descrambled[2] = menu_descrambled[3] = false;
				menu_descrambled[descrambled_index - 1] = true;
			}
		} // end quit/menu window
		nk_end(_ctx);

	}
	else { // modal

		// clear hint while modal is active
		szHint = "";
		bResetHint = false;

		std::string_view const szCityName(MinCity::getCityName());
		int const select = do_modal_window(fmt::format(FMT_STRING("SAVE {:s} "), (szCityName.empty() ? "CITY" : stringconv::toUpper(szCityName))), "YES", "NO");
		if (select >= 0) { // selection made

			enableWindow<eWindowType::MAIN>(false);

			if (select) {
				_iWindowQuitSelection = eWindowQuit::SAVE_AND_QUIT;
				enableWindow<eWindowType::SAVE>(true);
			}
			else {
				_iWindowQuitSelection = eWindowQuit::JUST_QUIT;
				MinCity::DispatchEvent(eEvent::EXIT);
			}

			_bModalPrompted = false; // required for any modal dialog prompt to reset itself when done.
		}
	}
}

void cNuklear::do_cyberpunk_newsave_slot(int32_t& __restrict selected, float const width, float const height,
										 char(&szEdit)[MAX_EDIT_LENGTH + 1], int32_t& __restrict szLength,
	                                     eWindowName const windowName, std::string& __restrict szHint, bool& __restrict bResetHint, bool& __restrict bSmallHint)
{
	nk_scroll scroll_dummy{};
	auto const& nameCities = MinCity::VoxelWorld->getLoadList();

	nk_layout_row_dynamic(_ctx, height, 1);

	if (nk_group_scrolled_begin(_ctx, &scroll_dummy, "new save slot", NK_WINDOW_NO_SCROLLBAR | (selected < 0 ? NK_WINDOW_BORDER : 0))) {

		nk_layout_row_dynamic(_ctx, height, 2);

		struct nk_rect bounds;
		nk_layout_peek(&bounds, _ctx); bounds.w = width;
		if (nk_is_mouse_down(_ctx, bounds, NK_BUTTON_LEFT)) {
			selected = -1;
		}

		if (nk_group_scrolled_begin(_ctx, &scroll_dummy, "new save thumbnail", NK_WINDOW_NO_SCROLLBAR)) {

			nk_layout_row_static(_ctx, offscreen_thumbnail_height, offscreen_thumbnail_width, 1);

			nk_draw_image(_ctx, _guiImages.offscreen);

			nk_group_scrolled_end(_ctx);
		}

		if (nk_group_scrolled_begin(_ctx, &scroll_dummy, "new info", NK_WINDOW_NO_SCROLLBAR)) {

			nk_layout_row_dynamic(_ctx, 40, 1);

			if (selected < 0) {
				nk_edit_focus(_ctx, 0); // default starting focus
			}
			else {
				nk_edit_unfocus(_ctx);
			}

			nk_flags const active = nk_edit_string(_ctx, NK_EDIT_SIMPLE | NK_EDIT_SELECTABLE | NK_EDIT_SIG_ENTER | NK_EDIT_GOTO_END_ON_ACTIVATE,
				szEdit, &szLength, MAX_EDIT_LENGTH, nk_filter_text_numbers);

			if ((NK_EDIT_ACTIVATED & active) || (NK_EDIT_ACTIVE & active)) {
				_bEditControlFocused = true; // used in input update
			}

			nk_style_push_font(_ctx, &_fonts[eNK_FONTS::SMALL]->handle);
			nk_label(_ctx, fmt::format(FMT_STRING("POPULATION  {:n}"), MinCity::City->getPopulation()).c_str(), NK_TEXT_LEFT);
			nk_label(_ctx, fmt::format(FMT_STRING("CASH  {:n}"), MinCity::City->getCash()).c_str(), NK_TEXT_LEFT);
			nk_style_pop_font(_ctx);

			nk_layout_row_dynamic(_ctx, 30, 1);
			nk_spacing(_ctx, 1);

			nk_layout_row_dynamic(_ctx, 40, 2);

			std::string save_button("");
			std::string active_str("");
			bool bStylePushed(false);

			if (selected < 0) {
				gui::add_cyberpunk_glyph(active_str, 13);
				save_button = "SAVE";

				if (szLength <= 0) {
					// visually disable button
					nk_style_push_color(_ctx, &_ctx->style.button.text_normal, DISABLED_TEXT_COLOR);
					nk_style_push_color(_ctx, &_ctx->style.button.text_hover, DISABLED_TEXT_COLOR);
					nk_style_push_color(_ctx, &_ctx->style.button.text_active, DISABLED_TEXT_COLOR);
					bStylePushed = true;
				}
			}
			else {
				gui::add_cyberpunk_glyph(active_str, 13, -1);
				gui::add_cyberpunk_glyph(save_button, 13, -1);

				// visually disable button
				nk_style_push_color(_ctx, &_ctx->style.button.text_normal, DEFAULT_TEXT_COLOR);
				nk_style_push_color(_ctx, &_ctx->style.button.text_hover, DEFAULT_TEXT_COLOR);
				nk_style_push_color(_ctx, &_ctx->style.button.text_active, DEFAULT_TEXT_COLOR);
				bStylePushed = true;
			}
			nk_label(_ctx, active_str.c_str(), NK_TEXT_RIGHT);

			if (nk_button_label(_ctx, save_button.c_str()) || (NK_EDIT_COMMITED & active)) {

				if (selected < 0) {
					if (szLength > 0) {
						_iWindowSaveSelection = eWindowSave::SAVE;

						szHint = "SAVING...";
						bResetHint = false;
						bSmallHint = false;

						MinCity::setCityName(szEdit);
						MinCity::DispatchEvent(eEvent::SAVE, new bool(eWindowQuit::SAVE_AND_QUIT == _iWindowQuitSelection));

						enableWindow<eWindowType::SAVE>(false);
						nk_window_close(_ctx, windowName._to_string());
					}
				}
			}

			if (bStylePushed) {
				// restore
				nk_style_pop_color(_ctx);
				nk_style_pop_color(_ctx);
				nk_style_pop_color(_ctx);
			}

			nk_group_scrolled_end(_ctx);
		}

		nk_group_scrolled_end(_ctx);
	}
}

void cNuklear::do_cyberpunk_loadsave_slot(bool const mode, int32_t& __restrict selected, uint32_t const index, int32_t const slot, float const width, float const height,
										  eWindowName const windowName, std::string& __restrict szHint, bool& __restrict bResetHint, bool& __restrict bSmallHint)
{
	nk_scroll scroll_dummy{};
	auto const& nameCities = MinCity::VoxelWorld->getLoadList();

	nk_layout_row_dynamic(_ctx, height, 1);

	if (nk_group_scrolled_begin(_ctx, &scroll_dummy, fmt::format(FMT_STRING("slot {:d}"), index).c_str(), NK_WINDOW_NO_SCROLLBAR | (selected == index ? NK_WINDOW_BORDER : 0))) {

		nk_layout_row_dynamic(_ctx, height, 2);

		struct nk_rect bounds;
		nk_layout_peek(&bounds, _ctx); bounds.w = width;
		if (nk_is_mouse_down(_ctx, bounds, NK_BUTTON_LEFT)) {

			if (mode) { // saving
				if (stringconv::case_insensitive_compare(nameCities[index], MinCity::getCityName())) {  // only allow selection of city with same name to overwrite
					selected = index;
				}
			}
			else { // loading
				selected = index;
			}
		}

		if (nk_group_scrolled_begin(_ctx, &scroll_dummy, fmt::format(FMT_STRING("thumbnail {:d}"), index).c_str(), NK_WINDOW_NO_SCROLLBAR)) {

			nk_layout_row_static(_ctx, offscreen_thumbnail_height, offscreen_thumbnail_width, 1);

			if (slot >= 0) {
				nk_draw_image(_ctx, _guiImages.load_thumbnail[slot]);
			}
			else {
				nk_draw_image(_ctx, _guiImages.static_screen);
			}

			nk_group_scrolled_end(_ctx);
		}

		if (nk_group_scrolled_begin(_ctx, &scroll_dummy, fmt::format(FMT_STRING("info {:d}"), index).c_str(), NK_WINDOW_NO_SCROLLBAR))
		{
			nk_layout_row_dynamic(_ctx, 40, 1);

			std::string szCityName(nameCities[index]);
			nk_label(_ctx, stringconv::toUpper(szCityName).c_str(), NK_TEXT_LEFT);

			nk_style_push_font(_ctx, &_fonts[eNK_FONTS::SMALL]->handle);
			nk_label(_ctx, fmt::format(FMT_STRING("POPULATION:  {:n}"), _loadsaveWindow.info_cities[index].population).c_str(), NK_TEXT_LEFT);
			nk_label(_ctx, fmt::format(FMT_STRING("CASH:  {:n}"), _loadsaveWindow.info_cities[index].cash).c_str(), NK_TEXT_LEFT);
			nk_style_pop_font(_ctx);

			nk_layout_row_dynamic(_ctx, 30, 1);
			nk_spacing(_ctx, 1);

			nk_layout_row_dynamic(_ctx, 40, 2);

			std::string active_button("");
			std::string active_str("");

			if (selected == index) {
				gui::add_cyberpunk_glyph(active_str, 13);
				if (mode) { // saving
					active_button = "SAVE";
				}
				else { // loading
					active_button = "LOAD";
				}
			}
			else {
				gui::add_cyberpunk_glyph(active_str, 13, (int64_t)index + 1);
				gui::add_cyberpunk_glyph(active_button, 13, (int64_t)index + 1);

				// visually disable button
				nk_style_push_color(_ctx, &_ctx->style.button.text_normal, DISABLED_TEXT_COLOR);
				nk_style_push_color(_ctx, &_ctx->style.button.text_hover, DISABLED_TEXT_COLOR);
				nk_style_push_color(_ctx, &_ctx->style.button.text_active, DISABLED_TEXT_COLOR);
			}
			nk_label(_ctx, active_str.c_str(), NK_TEXT_RIGHT);

			if (nk_button_label(_ctx, active_button.c_str())) {

				if (selected == index) {
					if (mode) {

						_iWindowSaveSelection = eWindowSave::SAVE;

						szHint = "SAVING...";
						bResetHint = false;
						bSmallHint = false;

						// overwrite existing save file with same city name
						MinCity::DispatchEvent(eEvent::SAVE, new bool(eWindowQuit::SAVE_AND_QUIT == _iWindowQuitSelection));

						enableWindow<eWindowType::SAVE>(false);
						nk_window_close(_ctx, windowName._to_string());
					}
					else {

						_iWindowLoadSelection = eWindowLoad::LOAD;

						szHint = "LOADING...";
						bResetHint = false;
						bSmallHint = false;

						MinCity::setCityName(nameCities[selected]);
						MinCity::DispatchEvent(eEvent::LOAD);

						enableWindow<eWindowType::LOAD>(false);
						nk_window_close(_ctx, windowName._to_string());
					}
				}
			}

			if (selected == index) {

			}
			else {
				// restore
				nk_style_pop_color(_ctx);
				nk_style_pop_color(_ctx);
				nk_style_pop_color(_ctx);
			}

			nk_group_scrolled_end(_ctx);
		}

		nk_group_scrolled_end(_ctx);
	}
}
void cNuklear::do_cyberpunk_loadsave_window(bool const mode, std::string& __restrict szHint, bool& __restrict bResetHint, bool& __restrict bSmallHint)  // false = load  true = save
{
	static constexpr int32_t const slot_height(offscreen_thumbnail_height);
	static constexpr float const slot_heightf((float)slot_height);
	
	eWindowName const subwindowName((mode ? eWindowName::WINDOW_SAVE : eWindowName::WINDOW_LOAD));

	if (nk_begin(_ctx, subwindowName._to_string(),
		make_centered_window_rect(1080, 900, _frameBufferSize),
		NK_TEMP_WINDOW_BORDER | NK_WINDOW_SCROLL_AUTO_HIDE))
	{
		constinit static char szEdit[MAX_EDIT_LENGTH + 1]{}; // always allocate 1 extra byte for edit control buffers for safety
		constinit static int32_t szLength(0);

		struct nk_rect const windowBounds(nk_window_get_bounds(_ctx));

		AddActiveWindowRect(r2D_set_by_width_height(windowBounds.x, windowBounds.y, windowBounds.w, windowBounds.h));	// must register any active window;

		int32_t existing(_loadsaveWindow.existing), // only used in saving mode
			    selected(_loadsaveWindow.selected); // used in saving & loading mode
		bool bReset(false);

		if (mode) { // saving
			// resets for saving go here //
			if (_loadsaveWindow.bReset) {

				szHint = " "; // make hint blank
				bResetHint = false;
				szLength = 0;
				memset(szEdit, 0, MAX_EDIT_LENGTH + 1);

				int32_t const city_name_length(MinCity::getCityName().length());

				if (city_name_length > 0) { // not a new city?
 					std::string szCityName(MinCity::getCityName());
					stringconv::toUpper(szCityName);
					strcpy_s(szEdit, szCityName.c_str());
					szLength = city_name_length;
				}

				_loadsaveWindow.reset();
				bReset = true;
				selected = -1; // default to first slot when saving
			}
		}
		else { // loading

			// resets for loading go here //
			if (_loadsaveWindow.bReset) {

				szHint = " "; // make hint blank
				bResetHint = false;

				_loadsaveWindow.reset();
				bReset = true;
				selected = 0; // default to first slot when loading
			}
		}

		static constexpr fp_seconds const interval(CYBERPUNK_BEAT);

		constinit static tTime tLast(zero_time_point);
		constinit static int32_t seed(1);

		fp_seconds const accumulated(critical_now() - tLast);
		float const t = SFM::saturate(accumulated / interval);

		if (accumulated > interval) {
			tLast = critical_now();

			seed = PsuedoRandomNumber32();
		}

		SetSeed(seed);

		auto const& name_cities = MinCity::VoxelWorld->getLoadList();

		// common
		if (bReset) {
			
			_loadsaveWindow.info_cities.reserve(name_cities.size());
			_loadsaveWindow.info_cities.resize(name_cities.size());
			
			existing = -1;
		}

		if (mode) { // saving

			do_cyberpunk_newsave_slot(selected, windowBounds.w, slot_heightf, szEdit, szLength, subwindowName, szHint, bResetHint, bSmallHint);

		} // end saving only

		int32_t const slot_offset((mode ? slot_height : 0));
		int32_t scroll_offset;
		{
			nk_uint offset{};
			nk_window_get_scroll(_ctx, nullptr, &offset);

			scroll_offset = (int32_t)offset - (slot_height >> 1); // if saving offset by one slot
		}

		static constexpr uint32_t const visible_slot_count(guiTextures::load_thumbnail::count); // pre-calculated with equivalent: SFM::ceil_to_u32(windowBounds.h / slot_height) + 1);
		static constexpr int32_t const epsilon(1);
		int32_t const
			count_city((int32_t)_loadsaveWindow.info_cities.size()),
			window_begin(scroll_offset - epsilon),  // [moving window]
			window_end(scroll_offset + (int32_t)windowBounds.h + epsilon);

		if (bReset || _loadsaveWindow.scroll_offset != scroll_offset)
		{
			int32_t local_offset(slot_offset);

			uint32_t index_visible_slot(0);
			
			for (int32_t index_city = 0; index_city < count_city; ++index_city) {

				int32_t const
					slot_begin(index_city * slot_height + local_offset),
					slot_end((index_city + 1) * slot_height + local_offset);

				//                fail                      fail
				if (!(window_begin >= slot_end || window_end < slot_begin)) { // not a failure case? [moving window]

					// auto skip city with same name as current city, if it exists - it is already displayed in saving mode
					if (mode) {

						if (bReset && stringconv::case_insensitive_compare(name_cities[index_city], MinCity::getCityName())) {  // only allow selection of city with same name to overwrite

							existing = index_city;
							local_offset -= slot_height; // need to account for index being one off on next iteration due to the skip. compensated by changing the local slot offset by one slot.
							continue; // only skip if saving mode
						}
					}
					
					if (bReset || !stringconv::case_insensitive_compare(_loadsaveWindow.visible_cities[index_visible_slot], name_cities[index_city])) { // only on reset or actual changes
						// update slot //
						_loadsaveWindow.visible_cities[index_visible_slot] = name_cities[index_city]; // cache city name so this code only runs when there are chasnges to the actual slots

						if (MinCity::VoxelWorld->PreviewWorld(name_cities[index_city], std::forward<CityInfo&&>(_loadsaveWindow.info_cities[index_city]), _guiTextures.load_thumbnail.image[index_visible_slot])) {

							// update texture for thumbnail from new imaging data returned from previewworld
							MinCity::TextureBoy->ImagingToTexture<true>(_guiTextures.load_thumbnail.image[index_visible_slot], _guiTextures.load_thumbnail.texture[index_visible_slot], _guiTextures.load_thumbnail.stagingBuffer);

						}
						else {

							FMT_LOG_FAIL(GAME_LOG, "could not preview thumbnail for city {:s} - corrupted save file!", name_cities[index_city]);
						}
					}

					if (++index_visible_slot == visible_slot_count) {
						break;
					}
				}
			}
		}

		int32_t local_offset(slot_offset);
		uint32_t index_visible_slot(0);

		for (int32_t index_city = 0; index_city < count_city; ++index_city) {

			// existing is always -1 in loading mode, this is only used in saving mode.
			if (existing == index_city) { // only in saving mode, exclude duplicate of current existing city save file
				local_offset -= slot_height; // need to account for index being one off on next iteration due to the skip. compensated by changing the local slot offset by one slot.
				continue;
			}

			int32_t const
				slot_begin(index_city * slot_height + local_offset),
				slot_end((index_city + 1) * slot_height + local_offset);

			int32_t slot(-1);
			//                fail                      fail
			if (!(window_begin >= slot_end || window_end < slot_begin)) { // not a failure case? [moving window]
				slot = index_visible_slot++;
			}

			do_cyberpunk_loadsave_slot(mode, selected, index_city, slot, windowBounds.w, slot_heightf, subwindowName, szHint, bResetHint, bSmallHint);

		} // end for

		// update stored selection index
		_loadsaveWindow.scroll_offset = scroll_offset;
		_loadsaveWindow.selected = selected;
		_loadsaveWindow.existing = existing;

	} // end loadsave window
	nk_end(_ctx);
}

bool const cNuklear::toggle_button(std::string_view const label, struct nk_image const* const img, bool const isActive, bool* const __restrict pbHovered) const
{
	if (nullptr == img->handle.ptr)
		return(nk_false);

	nk_style_push_style_item(_ctx, &_ctx->style.button.normal, nk_style_item_color(TRANSPARENT));
	nk_style_push_style_item(_ctx, &_ctx->style.button.hover, nk_style_item_color(TRANSPARENT));
	nk_style_push_style_item(_ctx, &_ctx->style.button.active, nk_style_item_color(TRANSPARENT));
	nk_style_push_color(_ctx, &_ctx->style.button.text_hover, HOVER_TEXT_COLOR);
	nk_style_push_color(_ctx, &_ctx->style.button.text_active, ACTIVE_TEXT_COLOR);

	if (!isActive) {
		nk_style_push_color(_ctx, &_ctx->style.button.text_normal, DEFAULT_TEXT_COLOR);
	}
	else {
		nk_style_push_color(_ctx, &_ctx->style.button.text_normal, ACTIVE_TEXT_COLOR);
	}

	nk_bool const ret = nk_button_image_label(_ctx, *img, label.data(), NK_TEXT_CENTERED, pbHovered);

	// restore
	nk_style_pop_color(_ctx);
	nk_style_pop_color(_ctx);
	nk_style_pop_color(_ctx);
	nk_style_pop_style_item(_ctx);
	nk_style_pop_style_item(_ctx);
	nk_style_pop_style_item(_ctx);

	return((bool const)ret);
}
void cNuklear::UpdateSequence(struct nk_image* const __restrict gui_image, ImagingSequence const* const __restrict sequence, sequenceFraming& __restrict framing) const
{
	// accurate frame timing
	fp_seconds const fTDelta(_ctx->delta_time_seconds);

	fp_seconds const fTDelay(milliseconds(sequence->images[framing.frame].delay));

	if ((framing.tAccumulateFrame += fTDelta) >= fTDelay) {

		if (++framing.frame == sequence->count) {
			framing.frame = 0;
		}

		framing.tAccumulateFrame -= fTDelay;

		// *major trick* animation frame is hidden in type (push constant), shader will interpret as (frame = max(0, int32_t(type) - ARRAY_IMAGE))
		((nk_image_extended* const)gui_image)->type = ARRAY_IMAGE + framing.frame; // **this is a safe cast** memory is actually allocated as nk_image_extended
	}
}
void cNuklear::UpdateSequences()
{
	constinit static uint32_t lastActiveTool(0), lastActiveSubTool(0);

	uint32_t const activeTool(MinCity::UserInterface->getActivatedToolType());
	uint32_t const activeSubTool(MinCity::UserInterface->getActivatedSubToolType());

	if (activeTool != lastActiveTool || activeSubTool != lastActiveSubTool) {

		// resets
		_sequenceImages.menu_item_framing.reset();

		lastActiveTool = activeTool;
		lastActiveSubTool = activeSubTool;
	}

	// sequences:
	if ( eTools::DEMO == activeTool ) { // DEMO
		UpdateSequence(_guiImages.demo, _sequenceImages.menu_item, _sequenceImages.menu_item_framing);
	}
	else {
		((nk_image_extended* const)_guiImages.demo)->type = RGBA_IMAGE;
	}

	if (eTools::ROADS == activeTool) { // ROAD
		UpdateSequence(_guiImages.road, _sequenceImages.menu_item, _sequenceImages.menu_item_framing);
	}
	else {
		((nk_image_extended* const)_guiImages.road)->type = RGBA_IMAGE;
	}
	
	if (eTools::ZONING == activeTool) { // ZONING

		// must reset *other* only - *bugfix maintains animation
		switch (activeSubTool)
		{
		case eSubTool_Zoning::RESIDENTIAL:
			((nk_image_extended* const)_guiImages.zoning[eSubTool_Zoning::COMMERCIAL-1])->type = RGBA_IMAGE;
			((nk_image_extended* const)_guiImages.zoning[eSubTool_Zoning::INDUSTRIAL-1])->type = RGBA_IMAGE;
			break;
		case eSubTool_Zoning::COMMERCIAL:
			((nk_image_extended* const)_guiImages.zoning[eSubTool_Zoning::RESIDENTIAL - 1])->type = RGBA_IMAGE;
			((nk_image_extended* const)_guiImages.zoning[eSubTool_Zoning::INDUSTRIAL - 1])->type = RGBA_IMAGE;
			break;
		case eSubTool_Zoning::INDUSTRIAL:
			((nk_image_extended* const)_guiImages.zoning[eSubTool_Zoning::RESIDENTIAL - 1])->type = RGBA_IMAGE;
			((nk_image_extended* const)_guiImages.zoning[eSubTool_Zoning::COMMERCIAL - 1])->type = RGBA_IMAGE;
			break;
		}

		UpdateSequence(_guiImages.zoning[activeSubTool - 1], _sequenceImages.menu_item, _sequenceImages.menu_item_framing);
	}
	else {
		for (uint32_t i = 0; i < 3; ++i) {
			((nk_image_extended* const)_guiImages.zoning[i])->type = RGBA_IMAGE;
		}
	}

	if (eTools::POWER == activeTool) { // POWER
		UpdateSequence(_guiImages.power, _sequenceImages.menu_item, _sequenceImages.menu_item_framing);
	}
	else {
		((nk_image_extended* const)_guiImages.power)->type = RGBA_IMAGE;
	}

	if (eTools::SECURITY == activeTool) { // SECURITY
		UpdateSequence(_guiImages.security, _sequenceImages.menu_item, _sequenceImages.menu_item_framing);
	}
	else {
		((nk_image_extended* const)_guiImages.security)->type = RGBA_IMAGE;
	}


	if (eTools::SCIENCE == activeTool) { // SCIENCE
		UpdateSequence(_guiImages.science, _sequenceImages.menu_item, _sequenceImages.menu_item_framing);
	}
	else {
		((nk_image_extended* const)_guiImages.science)->type = RGBA_IMAGE;
	}

	
	{ // static screen
		UpdateSequence(_guiImages.static_screen, _sequenceImages.static_screen, _sequenceImages.static_screen_framing);
	}
}

void cNuklear::do_hint_window(std::string_view const windowName, std::string_view const szHint, bool const bSmallHint)
{
	int32_t const checked_length(((int32_t)szHint.length()) - 1); // *bugfix - a string is not empty when special whitespace characters are within, however the length of the string seems to not count some of them. So this additional check is required.

	if (szHint.empty() || checked_length < 0)
		return;

	struct nk_rect bounds(make_centered_window_rect(360, 80, _frameBufferSize));
	bounds.y = _frameBufferSize.y - 80;

	if (nk_begin(_ctx, windowName.data(),
		bounds,
		NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BACKGROUND | NK_WINDOW_NO_INPUT))
	{
		struct nk_rect const windowBounds(nk_window_get_bounds(_ctx));

		AddActiveWindowRect(r2D_set_by_width_height(windowBounds.x, windowBounds.y, windowBounds.w, windowBounds.h));	// must register any active window;

		nk_layout_row_dynamic(_ctx, 40, 1);

		{
			static constexpr fp_seconds const interval(1.0f);

			constinit static uint32_t scramble_index(1);
			constinit static bool bBlink(false);
			constinit static tTime tLast(zero_time_point);

			fp_seconds const accumulated(critical_now() - tLast);
			float const t = SFM::saturate(accumulated / interval);

			std::string szText(szHint);

			if (accumulated > interval) {
				bBlink = !bBlink;
				tLast = critical_now();
				if (bBlink) {
					
					scramble_index = PsuedoRandomNumber(0, szText.length() - 1);
				}
			}

			if (bBlink) {
				szText[scramble_index] = stringconv::toLower(szText[scramble_index]);
			}

			gui::add_horizontal_bar(szText, t);

			if (bSmallHint) {
				nk_style_push_font(_ctx, &_fonts[eNK_FONTS::SMALL]->handle);
			}
			nk_label(_ctx, szText.c_str(), NK_TEXT_CENTERED);
			if (bSmallHint) {
				nk_style_pop_font(_ctx);
			}

			if (_uiPauseProgress) {
				nk_layout_row_dynamic(_ctx, 40, 1);

				nk_size progress(_uiPauseProgress);
				nk_progress(_ctx, &progress, 100, nk_false);
			}
		}
	}
	nk_end(_ctx); // end paused window
}

int const cNuklear::do_modal_window(std::string_view const prompt, std::string_view const option_succeed, std::string_view const option_fail)
{
	static constexpr eWindowName const windowName(eWindowName::WINDOW_MODAL);
	int iReturn(-1);

	if (nk_begin(_ctx, windowName._to_string(),
		make_centered_window_rect(_frameBufferSize.x, 150, _frameBufferSize),
		NK_TEMP_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR))
	{
		struct nk_rect const windowBounds(nk_window_get_bounds(_ctx));

		AddActiveWindowRect(r2D_set_by_width_height(windowBounds.x, windowBounds.y, windowBounds.w, windowBounds.h));	// must register any active window;

		nk_layout_row_dynamic(_ctx, 40, 1);

		{
			static constexpr fp_seconds const interval(1.0f);

			constinit static uint32_t scramble_index(1);
			constinit static bool bBlink(false);
			constinit static tTime tLast(zero_time_point);

			fp_seconds const accumulated(critical_now() - tLast);
			float const t = SFM::saturate(accumulated / interval);

			std::string szText(prompt);

			if (accumulated > interval) {
				bBlink = !bBlink;
				tLast = critical_now();
				if (bBlink) {

					scramble_index = PsuedoRandomNumber(0, szText.length() - 1);
				}
			}

			if (bBlink) {
				szText[scramble_index] = stringconv::toLower(szText[scramble_index]);
			}

			gui::add_horizontal_bar(szText, t);
			nk_label(_ctx, szText.c_str(), NK_TEXT_CENTERED);
		}

		nk_layout_row_dynamic(_ctx, 40, 2);

		if (nk_button_label(_ctx, option_succeed.data())) {

			iReturn = 1;
			nk_window_close(_ctx, windowName._to_string());
		}

		if (nk_button_label(_ctx, option_fail.data())) {

			iReturn = 0;
			nk_window_close(_ctx, windowName._to_string());
		}
	}
	nk_end(_ctx); // end paused window

	return(iReturn);
}

void cNuklear::do_cyberpunk_import_window(std::string& __restrict szHint, bool& __restrict bResetHint, bool& __restrict bSmallHint)
{
	constinit static world::cImportGameObject_Dynamic* model_dynamic(nullptr);
	constinit static world::cImportGameObject_Static* model_static(nullptr);

	static Volumetric::newVoxelModel voxelModel{};

	if (!_bModalPrompted) {

		eWindowName const windowName(eWindowName::WINDOW_IMPORT);
		
		struct nk_rect bounds = make_centered_window_rect(1080, 200, _frameBufferSize);
		bounds.y = (float)_frameBufferSize.y - bounds.h * 1.5f;

		if (nk_begin(_ctx, windowName._to_string(),
			bounds,
			NK_TEMP_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR))
		{
			struct nk_rect const windowBounds(nk_window_get_bounds(_ctx));

			AddActiveWindowRect(r2D_set_by_width_height(windowBounds.x, windowBounds.y, windowBounds.w, windowBounds.h));	// must register any active window;

			constinit static bool bFirstShown(true);

			if (bFirstShown) {
				_bModalPrompted = true;
				bFirstShown = false;
			}
			else {

				static constexpr fp_seconds const interval(1.0f);

				constinit static uint32_t scramble_index(1);
				constinit static bool bBlink(false);
				constinit static tTime tLast(zero_time_point);

				fp_seconds const accumulated(critical_now() - tLast);
				float const t = SFM::saturate(accumulated / interval);

				std::string szText(szHint);

				if (accumulated > interval) {
					tLast = critical_now();
				}

				auto& colors = world::cImportGameObject::getProxy().colors;

				if ((model_dynamic || model_static) && voxelModel.model) {

					using colormap = vector<Volumetric::ImportColor>;

					auto& previous(world::cImportGameObject::getProxy().previous);

					bool bApplyMaterial(false);
					
					{
						auto& colors = world::cImportGameObject::getProxy().colors;

						if (world::cImportGameObject::getProxy().active_color.color)
						{
							szHint = fmt::format(FMT_STRING("{:n} VOXELS"), world::cImportGameObject::getProxy().active_color.count);
							bResetHint = false;
							bSmallHint = true;
						}

						nk_layout_row_dynamic(_ctx, 48, 1);

						nk_group_begin(_ctx, "colormap", 0);

						nk_layout_row_static(_ctx, 48, 48, colors.size());
						
						for (auto i = colors.begin(); i != colors.end(); ++i) {

							uvec4_t rgba;
							SFM::unpack_rgba(i->color, rgba);

							if (i->material.Color == world::cImportGameObject::getProxy().active_color.material.Color) {
								SFM::unpack_rgba(SFM::lerp(0, i->color, t), rgba);
							}

							if (nk_button_color(_ctx, nk_rgb(rgba.r, rgba.g, rgba.b))) {

								if (world::cImportGameObject::getProxy().active_color.material.Color) {
									// "schedule" remove previous color, its done
									previous = std::lower_bound(colors.begin(), colors.end(), world::cImportGameObject::getProxy().active_color);
								}
								// set new active color
								world::cImportGameObject::getProxy().active_color = *i;
							}
						}

						if (colors.end() != previous) { // remove previously selected - 

							MinCity::DispatchEvent(eEvent::PAUSE_PROGRESS, new uint32_t(100.0f * (1.0f - (colors.size() * world::cImportGameObject::getProxy().inv_start_color_count))));

							colors.erase(previous); // remove previously selected color
							previous = colors.end(); // must reset
							bApplyMaterial = true;
							
							if (colors.empty()) { // last-one ?

								world::cImportGameObject::getProxy().save(voxelModel.name); // saves the current model to a cache file, model is now imported.

								// reset/disable proxy
								world::cImportGameObject::getProxy().active_color.color = 0;
								world::cImportGameObject::getProxy().active_color.count = 0;

								voxelModel.model = nullptr; // clear
							}
						}

						nk_group_end(_ctx);
					}
					nk_layout_row_dynamic(_ctx, 40, 4);

					// binarch search for selected color in colormap
					// access the ImportColor field n for current status, update on option selected.
					// require current color
					colormap::iterator iter_color(std::lower_bound(colors.begin(), colors.end(), world::cImportGameObject::getProxy().active_color));

					// ***now working on material*** access to color member forbidden.
					if (colors.end() != iter_color) {

						nk_style_push_font(_ctx, &_fonts[eNK_FONTS::SMALL]->handle);

						{
							nk_bool checked(iter_color->material.Metallic);
							if (nk_checkbox_label(_ctx, "METALLIC", &checked)) {
								iter_color->material.Metallic = checked;
								bApplyMaterial = true;
							}
						}
						{
							nk_bool checked(iter_color->material.Emissive);
							if (nk_checkbox_label(_ctx, "EMISSIVE", &checked)) {
								iter_color->material.Emissive = checked;
								iter_color->material.Video = false; // clear video checkbox on any change to emissive
								bApplyMaterial = true;
							}
						}
						{
							nk_bool checked(iter_color->material.Transparent);
							if (nk_checkbox_label(_ctx, "TRANSPARENCY", &checked)) {
								iter_color->material.Transparent = checked;
								bApplyMaterial = true;
							}
						}
						{
							nk_bool checked(iter_color->material.Video);
							if (nk_checkbox_label(_ctx, "VIDEO", &checked)) {
								iter_color->material.Video = checked;
								iter_color->material.Emissive = checked; // video is always emissive
								bApplyMaterial = true;
							}
						}
						
						nk_layout_row_dynamic(_ctx, 40, 1);
						
						{
							nk_size roughness(iter_color->material.Roughness);
							if (nk_progress(_ctx, &roughness, 0xf, nk_true, "ROUGHNESS")) {
								iter_color->material.Roughness = roughness;
								bApplyMaterial = true;
							}
						}
						
						if (bApplyMaterial) {

							world::cImportGameObject::getProxy().apply_material(iter_color);
						}

						nk_style_pop_font(_ctx);
					}
				}
				else {
					voxelModel.model = nullptr; // clear
				}

				if (nullptr == voxelModel.model) {
					// to goto next model
					
					if (model_dynamic && model_dynamic->getModelInstance()->getHash()) {
						MinCity::VoxelWorld->destroyImmediatelyVoxelModelInstance(model_dynamic->getModelInstance()->getHash());
						model_dynamic = nullptr;
					}
					if (model_static && model_static->getModelInstance()->getHash()) {
						MinCity::VoxelWorld->destroyImmediatelyVoxelModelInstance(model_static->getModelInstance()->getHash());
						model_static = nullptr;
					}

					_bModalPrompted = true; // this will either start an import for the next voxel model, or exit the import window
					nk_window_close(_ctx, windowName._to_string()); // hides the import window
				}
			}

		} // end import window
		nk_end(_ctx);
	}
	else // modal
	{
		// clear hint while modal is active
		szHint = "";
		bResetHint = false;

		bool bExitImport(Volumetric::isNewModelQueueEmpty() && nullptr == voxelModel.model); // this allows the current model to be prompted until user selects yes/no

		MinCity::DispatchEvent(eEvent::PAUSE_PROGRESS); // reset progress here!

		if (!bExitImport)
		{
			// only when clear (current model is done) does the queue pop off a new voxel model
			if (nullptr == voxelModel.model) { // clear
				auto& queue(Volumetric::getNewModelQueue());
				queue.try_pop(voxelModel);
			}

			if (nullptr != voxelModel.model && !voxelModel.name.empty()) {

				int const select = do_modal_window(fmt::format(FMT_STRING("IMPORT MODEL {:s} "), stringconv::toUpper(voxelModel.name)), "YES", "NO");
				if (select >= 0) { // selection made

					using flags = Volumetric::eVoxelModelInstanceFlags;
					constexpr uint32_t const common_flags(flags::GROUND_CONDITIONING | flags::INSTANT_CREATION | flags::DESTROY_EXISTING_DYNAMIC | flags::DESTROY_EXISTING_STATIC);

					if (select) {
						_iWindowImportSelection = eWindowImport::IMPORT;

						if (model_dynamic && model_dynamic->getModelInstance()->getHash()) {
							MinCity::VoxelWorld->destroyImmediatelyVoxelModelInstance(model_dynamic->getModelInstance()->getHash()); // destroy last
						}
						if (model_static && model_static->getModelInstance()->getHash()) {
							MinCity::VoxelWorld->destroyImmediatelyVoxelModelInstance(model_static->getModelInstance()->getHash()); // destroy last
						}

						point2D_t const origin{};
						rect2D_t vWorldArea(-128,-128,128,128);
						
						world::setVoxelsHeightAt(vWorldArea, 0);
						
						// leave a border of terrain around model
						vWorldArea = world::voxelArea_grow(vWorldArea, point2D_t(1, 1));

						// smooth border of model area with surrounding terrain
						world::smoothRect(vWorldArea);

						// go around perimeter doing lerp between border
						// *required* recompute adjacency for area as height of voxels has changed
						world::recomputeGroundAdjacency(vWorldArea);
						
						// will now show the import window and close this modal prompt
						// add gameobject for import, load the colormap into importproxy																						// safe down-cast, memory pointed to by the pointer is allocated as such, and previously a pointer of that type.
						if (voxelModel.dynamic) {
							model_dynamic = MinCity::VoxelWorld->placeUpdateableInstanceAt<world::cImportGameObject_Dynamic, true>(origin, reinterpret_cast<Volumetric::voxB::voxelModel<true> const* const>(voxelModel.model), common_flags);
						}
						else {
							model_static = MinCity::VoxelWorld->placeUpdateableInstanceAt<world::cImportGameObject_Static, false>(origin, reinterpret_cast<Volumetric::voxB::voxelModel<false> const* const>(voxelModel.model), common_flags);
						}

						// setup the scene //
						_uiWindowEnabled = eWindowType::DISABLED; // workaround pause lock-out temporarilly
						MinCity::Pause(false);
						_uiWindowEnabled = eWindowType::IMPORT;
						
						MinCity::VoxelWorld->resetCamera();
						//MinCity::VoxelWorld->translateCamera(point2D_t(0,13));	
						MinCity::VoxelWorld->setCameraTurnTable(true);
						
						if (voxelModel.dynamic) {
							if (model_dynamic) {
								MinCity::VoxelWorld->zoomCamera(XMLoadFloat3A(&model_dynamic->getModelInstance()->getModel()._Extents));
							}
						}
						else {
							if (model_static) {
								MinCity::VoxelWorld->zoomCamera(XMLoadFloat3A(&model_static->getModelInstance()->getModel()._Extents));
							}
						}
					}
					else {
						_iWindowImportSelection = eWindowImport::DISABLED;
						// user selected no, disable import and goto main window
						bExitImport = true;
					}

					_bModalPrompted = false; // required for any modal dialog prompt to reset itself when done.
				}

			}
		}

		if (bExitImport) { // if the queue is now empty

			if (eWindowImport::IMPORT != _iWindowImportSelection) {
				if (model_dynamic && model_dynamic->getModelInstance()->getHash() && !model_dynamic->getModelInstance()->destroyPending()) {
					MinCity::VoxelWorld->destroyImmediatelyVoxelModelInstance(model_dynamic->getModelInstance()->getHash()); // destroy last
				}
				if (model_static && model_static->getModelInstance()->getHash() && !model_static->getModelInstance()->destroyPending()) {
					MinCity::VoxelWorld->destroyImmediatelyVoxelModelInstance(model_static->getModelInstance()->getHash()); // destroy last
				}
			}
			model_dynamic = nullptr;
			model_static = nullptr;
			voxelModel.model = nullptr; // clear

			enableWindow<eWindowType::IMPORT>(false);

			MinCity::VoxelWorld->resetCamera();
						
			MinCity::DispatchEvent(eEvent::SHOW_MAIN_WINDOW);
			nk_input.skip_click = true; // bugfix for main window being closed shortly thereafter
		}
		// otherwise the next file to import will prompt up next frame
	}
}

void  cNuklear::UpdateGUI()
{
	tTime const tLocal(critical_now());
	static tTime tLocalLast(tLocal);

	[[unlikely]] if (!MinCity::Vulkan->isRenderingEnabled()) // bugfix - crash when out of focus
		return;

	_activeWindowRects.clear(); // reset active window rects
	nk_clear(_ctx); // ########## ONLY PLACE THIS IS CLEARED #########  //
	_ctx->delta_time_seconds = time_to_float(fp_seconds(tLocal - tLocalLast)); // update internal timing of context 
	_bEditControlFocused = false; // reset
	SetGUIDirty(); // signal upload is neccessary 

	UpdateSequences();

	/* GUI */
	constinit static bool bLastPaused(true);
	
	// resets and other states triggered by the unpaused <-> paused transitions
	{
		bool const bIsPaused(MinCity::isPaused());
		if (bLastPaused != bIsPaused) {
			_bHintReset = true;
			bLastPaused = bIsPaused;
		}
	}

	if (!MinCity::isPaused())
	{
		constexpr eWindowName const bgwindowName(eWindowName::WINDOW_HINT);
		static std::string szHint(""); // isolated from paused hint
		constinit static bool bSmallHint(false);

		// required to prevent temporal display of incorrect "feedback/hint" text. This is triggered on a pause or opening of quit window
		if (_bHintReset) {
			szHint.clear();
			_bHintReset = false;
			bSmallHint = false;
		}

		// hint bg window "paused" window
		do_hint_window(bgwindowName._to_string(), szHint, bSmallHint);

		bool bResetHint(true);

		if (eWindowType::IMPORT == _uiWindowEnabled) {

			do_cyberpunk_import_window(szHint, bResetHint, bSmallHint); // works in both paused & unpaused modes
		}
		else {
			// Main Bottom Menu //
			uint32_t const window_offset(100);
			uint32_t const window_width(_frameBufferSize.x - (window_offset << 1));
			uint32_t const window_height(50);

			nk_push_transparent_bg(_ctx);

			constexpr eWindowName const windowName(eWindowName::WINDOW_BOTTOM_MENU);
			if (nk_begin(_ctx, windowName._to_string(),
				nk_recti(window_offset, _frameBufferSize.y - window_height, window_width, window_height),
				NK_WINDOW_NO_SCROLLBAR))
			{
				struct nk_rect const windowBounds(nk_window_get_bounds(_ctx));

				AddActiveWindowRect(r2D_set_by_width_height(windowBounds.x, windowBounds.y, windowBounds.w, windowBounds.h));	// must register any active window;

				static constexpr int32_t const cols(8);
				nk_layout_row_static(_ctx, 48, 48, cols);

				//
				bool bHovered(false);
				uint32_t const activeTool(MinCity::UserInterface->getActivatedToolType());
				uint32_t const activeSubTool(MinCity::UserInterface->getActivatedSubToolType());

				bHovered = false; // reset
				if (toggle_button("p", _guiImages.demo, eTools::DEMO == activeTool, &bHovered)) {
					MinCity::UserInterface->setActivatedTool(eTools::DEMO);
				}
				else if (bHovered) {
					szHint = "DEMOLITION";
					bResetHint = false;
					bSmallHint = true;
				}

				bHovered = false; // reset
				if (toggle_button("e", _guiImages.road, eTools::ROADS == activeTool, &bHovered)) {
					MinCity::UserInterface->setActivatedTool(eTools::ROADS);
				}
				else if (bHovered) {
					szHint = "ROAD";
					bResetHint = false;
					bSmallHint = true;
				}

				bHovered = false; // reset
				if (toggle_button("r", _guiImages.zoning[eSubTool_Zoning::RESIDENTIAL - 1], eTools::ZONING == activeTool && eSubTool_Zoning::RESIDENTIAL == activeSubTool, &bHovered)) {
					MinCity::UserInterface->setActivatedTool(eTools::ZONING, eSubTool_Zoning::RESIDENTIAL);
				}
				else if (bHovered) {
					szHint = "RESIDENTIAL";
					bResetHint = false;
					bSmallHint = true;
				}

				bHovered = false; // reset
				if (toggle_button("c", _guiImages.zoning[eSubTool_Zoning::COMMERCIAL - 1], eTools::ZONING == activeTool && eSubTool_Zoning::COMMERCIAL == activeSubTool, &bHovered)) {
					MinCity::UserInterface->setActivatedTool(eTools::ZONING, eSubTool_Zoning::COMMERCIAL);
				}
				else if (bHovered) {
					szHint = "COMMERCIAL";
					bResetHint = false;
					bSmallHint = true;
				}

				bHovered = false; // reset
				if (toggle_button("i", _guiImages.zoning[eSubTool_Zoning::INDUSTRIAL - 1], eTools::ZONING == activeTool && eSubTool_Zoning::INDUSTRIAL == activeSubTool, &bHovered)) {
					MinCity::UserInterface->setActivatedTool(eTools::ZONING, eSubTool_Zoning::INDUSTRIAL);
				}
				else if (bHovered) {
					szHint = "INDUSTRIAL";
					bResetHint = false;
					bSmallHint = true;
				}

				bHovered = false; // reset
				if (toggle_button("s", _guiImages.power, eTools::POWER == activeTool, &bHovered)) {
					MinCity::UserInterface->setActivatedTool(eTools::POWER);
				}
				else if (bHovered) {
					szHint = "POWER";
					bResetHint = false;
					bSmallHint = true;
				}

				bHovered = false; // reset
				if (toggle_button("j", _guiImages.security, eTools::SECURITY == activeTool, &bHovered)) {
					MinCity::UserInterface->setActivatedTool(eTools::SECURITY);
				}
				else if (bHovered) {
					szHint = "SECURITY";
					bResetHint = false;
					bSmallHint = true;
				}

				bHovered = false; // reset
				if (toggle_button("x", _guiImages.science, eTools::SCIENCE == activeTool, &bHovered)) {
					MinCity::UserInterface->setActivatedTool(eTools::SCIENCE);
				}
				else if (bHovered) {
					szHint = "SCIENCE";
					bResetHint = false;
					bSmallHint = true;
				}
				//nk_layout_row_dynamic(_ctx, 36, cols);

				//int32_t pop = MinCity::City->getPopulation();
				//nk_property_int<true>(_ctx, "population ", 0, &pop, INT32_MAX, 1, 1);
			}
			nk_end(_ctx);

			nk_pop_transparent_bg(_ctx);

			// ######################################################################################################################################## //
		}

		// reset hint if not used
		if (bResetHint) {
			szHint.clear();
			bSmallHint = false;
		}
#ifdef DEBUG_LUT_WINDOW
		draw_lut_window(tLocal, window_height + 2);
#endif
	}
	else { // ### PAUSED ### //
		constexpr eWindowName const bgwindowName(eWindowName::WINDOW_PAUSED);

		static std::string szHint(""); // isolated from non-paused hint
		constinit static bool bSmallHint(false);

		// required to prevent temporal display of incorrect "feedback/hint" text. This is triggered on a pause or opening of quit window
		if (_bHintReset) {
			szHint.clear();
			_bHintReset = false;
			bSmallHint = false;
		}

		if (szHint.empty()) {
			szHint = "PAUSED";
		}
		// hint bg window "paused" window
		do_hint_window(bgwindowName._to_string(), szHint, bSmallHint);

		/* no longere used 
		if ( nk_canvas_begin(_ctx, "offscreen_overlay", _offscreen_canvas, nk_recti(0, 0, _frameBufferSize.x, _frameBufferSize.y),
			NK_WINDOW_BACKGROUND | NK_WINDOW_NO_INPUT, nk_rgba(0, 0, 0, 255)) ) {

			nk_draw_image(_offscreen_canvas->painter, _guiImages.offscreen);

		}
		nk_canvas_end(_ctx, _offscreen_canvas);
		*/

		// this locally resets the "feedback/hint" text when there is not context set below only // done after below //
		bool bResetHint(eWindowType::MAIN == _uiWindowEnabled); // only set to true so that changes to the hint persist into the save & load window when selected

		// window query user to quit
		if (eWindowType::MAIN == _uiWindowEnabled)
		{
			do_cyberpunk_mainmenu_window(szHint, bResetHint, bSmallHint);
		}
		else if (eWindowType::SAVE == _uiWindowEnabled) {

			MinCity::Vulkan->enableOffscreenRendering(true);  // enable rendering of offscreen rt for gui effect usage
			do_cyberpunk_loadsave_window(true, szHint, bResetHint, bSmallHint);
		}
		else if (eWindowType::LOAD == _uiWindowEnabled) {

			do_cyberpunk_loadsave_window(false, szHint, bResetHint, bSmallHint);
		}
		else if (eWindowType::IMPORT == _uiWindowEnabled) {

			do_cyberpunk_import_window(szHint, bResetHint, bSmallHint);
		}
		// ######################################################################################################################################## //

		// reset hint if not used
		if (bResetHint) {
			szHint.clear();
			bSmallHint = false;
		}
	} // else paused



	/// debugging windows ///
#ifdef DEBUG_EXPLOSION_WINDOW
	do_debug_explosion_window(tLocal);
#endif
	
#ifdef DEBUG_FPS_WINDOW
	do_debug_fps_window(tLocal);
#endif	

	tLocalLast = tLocal;
}

#ifndef NDEBUG	
void cNuklear::debug_out_nuklear(std::string const& message)
{
	tbb::spin_mutex::scoped_lock lock(lock_debug_message);
	_szDebugMessage = message;
}
void debug_out_nuklear(std::string const message)
{
	MinCity::Nuklear->debug_out_nuklear(message);
}
void debug_out_nuklear_off()
{
	std::string const szEmpty;
	MinCity::Nuklear->debug_out_nuklear(szEmpty);
}
#endif

/// ******* all debug windows belong here
#ifdef DEBUG_EXPLOSION_WINDOW
#include "cExplosionGameObject.h"
void cNuklear::do_debug_explosion_window(tTime const tLocal)
{
	struct nk_rect debug_window_rect(make_centered_window_rect(512, 300, getFramebufferSize()));
	debug_window_rect.y += 512;
	
	if (nk_begin(_ctx, "debug_explosion",
		debug_window_rect,
		NK_TEMP_WINDOW_BORDER | NK_WINDOW_SCROLL_AUTO_HIDE))
	{
		struct nk_rect const windowBounds(nk_window_get_bounds(_ctx));

		AddActiveWindowRect(r2D_set_by_width_height(windowBounds.x, windowBounds.y, windowBounds.w, windowBounds.h));	// must register any active window;

		world::cExplosionGameObject* pExplosionGObj(nullptr);
		pExplosionGObj = world::cExplosionGameObject::debug_explosion_game_object;
		
		if (pExplosionGObj) {
			nk_layout_row_dynamic(_ctx, 40, 1);

			{
				nk_size temperature_boost(SFM::saturate_to_u8(pExplosionGObj->getTemperatureBoost() * 255.0f));
				if (nk_progress(_ctx, &temperature_boost, 0xff, nk_true, "TEMPERATURE")) {
					pExplosionGObj->setTemperatureBoost(((float)temperature_boost) / 255.0f);
				}
			}
			{
				nk_size flame_boost(SFM::saturate_to_u8(pExplosionGObj->getFlameBoost() * 255.0f));
				if (nk_progress(_ctx, &flame_boost, 0xff, nk_true, "FLAMES")) {
					pExplosionGObj->setFlameBoost(((float)flame_boost) / 255.0f);
				}
			}
			{
				nk_size emissive_threshold(SFM::saturate_to_u8(pExplosionGObj->getEmissionThreshold() * 255.0f));
				nk_progress(_ctx, &emissive_threshold, 0xff, nk_false, "EMISSION");
			}
		}
	}
	nk_end(_ctx);

}
#endif

#ifdef DEBUG_FPS_WINDOW
void cNuklear::do_debug_fps_window(tTime const tLocal)
{
	static bool bMainWindowStartOpen(false), bCameraOpen(false), bInstancesOpen(false), bVoxelsOpen(false), bPerformanceOpen(false), bLightmapOpen(false);

	int32_t windowHeight = 240;
	if (bCameraOpen | bPerformanceOpen | bLightmapOpen | bInstancesOpen | bVoxelsOpen) {
		windowHeight = SFM::max(windowHeight, 800);
	}

	constexpr eWindowName const windowName(eWindowName::WINDOW_MINCITY);

	if (nk_begin_titled(_ctx, windowName._to_string(),
		fmt::format(FMT_STRING("MINCITY     {:.2f}S  {:.1f}MS"), fp_seconds(tLocal - start()).count(), fp_seconds(MinCity::Vulkan->frameTimeAverage()).count() * 1000.0f).c_str(),
		nk_recti(450, 50, 700, windowHeight),
		(!bMainWindowStartOpen ? NK_WINDOW_MINIMIZED : NK_WINDOW_BORDER) |
		NK_WINDOW_MINIMIZABLE | NK_WINDOW_TITLE))
	{
		{
			struct nk_rect const windowBounds(nk_window_get_bounds(_ctx));

			AddActiveWindowRect(r2D_set_by_width_height(windowBounds.x, windowBounds.y, windowBounds.w, windowBounds.h));	// must register any active window
		}
		nk_style_push_font(_ctx, &_fonts[eNK_FONTS::SMALL]->handle);

		//if (nk_tree_push(_ctx, NK_TREE_TAB, "Minimap", NK_MINIMIZED))
		//{
		//	nk_layout_row_static(_ctx, guiImages._miniMapImage->h, guiImages._miniMapImage->w, 1);

		//	struct nk_rect rectFocus;

		//	{;
		//	rect2D_t const visibleBounds = MinCity::VoxelWorld->getVisibleGridBounds();

		//	rectFocus = nk_recti(visibleBounds.left, visibleBounds.top, visibleBounds.width(), visibleBounds.height());
		//	}

		//	if (_bMinimapRenderingEnabled) {
		//		if (nk_minimap(_ctx, guiImages._miniMapImage, &rectFocus, nk_red)) {
					// set new origin accurately
		//			XMVECTOR const xmTopLeft = { rectFocus.x, rectFocus.y };
		//			XMVECTOR const xmWidthHeight = { rectFocus.w, rectFocus.h };

		//			XMVECTOR xmOrigin = XMVectorAdd(xmTopLeft, XMVectorScale(xmWidthHeight, 0.5f));
					// Change from (0,0) => (x,y)  to (-x,-y) => (x,y)
		//			xmOrigin = XMVectorSubtract(xmOrigin, { Iso::WORLD_GRID_HALFSIZE, Iso::WORLD_GRID_HALFSIZE });

					// update!
		//			MinCity::VoxelWorld->setCameraOrigin(xmOrigin);
		//		}
		//	}
		//	else {
		//		enableMinimapRendering(true);
		//	}

		//	nk_tree_pop(_ctx);
		//}
		//else {
		//	enableMinimapRendering(false);
		//}
#ifdef DEBUG_LIGHT_PROPAGATION
		if (nk_tree_push(_ctx, NK_TREE_TAB, "Lightmap", NK_MINIMIZED))
		{
			static constexpr milliseconds refresh(100);
			static tTime tLast(zero_time_point);
			static std::string szText[2];

			nk_layout_row_static(_ctx, _debugLightImage->h, _debugLightImage->w, 1);
			nk_draw_image(_ctx, _debugLightImage);

			nk_layout_row_dynamic(_ctx, 20, 1);
			NK_PUSH_TEXT_COLOR(_ctx, nk_rgb(127, 0, 255));
			nk_label(_ctx, "min / max :", NK_TEXT_LEFT);
			NK_POP_COLOR(_ctx);
			nk_layout_row_dynamic(_ctx, 25, 2);

			if (tNow - tLast > refresh) {
				XMFLOAT3A vResult;
				XMVECTOR xmResult;
				xmResult = MinCity::VoxelWorld->getVolumetricOpacity().getDebugMin();
				XMStoreFloat3A(&vResult, xmResult);
				szText[0] = fmt::format(FMT_STRING("({:.1f}, {:.1f}, {:.1f})"), vResult.x, vResult.y, vResult.z);
				xmResult = MinCity::VoxelWorld->getVolumetricOpacity().getDebugMax();
				XMStoreFloat3A(&vResult, xmResult);
				szText[1] = fmt::format(FMT_STRING("({:.1f}, {:.1f}, {:.1f})"), vResult.x, vResult.y, vResult.z);
				tLast = tNow;
			}
			nk_text(_ctx, szText[0].c_str(), szText[0].length(), NK_TEXT_ALIGN_LEFT);
			nk_text(_ctx, szText[1].c_str(), szText[1].length(), NK_TEXT_ALIGN_LEFT);

			nk_layout_row_dynamic(_ctx, 25, 1);
			nk_property_int(_ctx, "slice>", 0, &MinCity::VoxelWorld->getVolumetricOpacity().getDebugSliceIndex(), MinCity::VoxelWorld->getVolumetricOpacity().getLightHeight() - 1, 1, 1);


			nk_tree_pop(_ctx);
			bLightmapOpen = true;
		}
		else
			bLightmapOpen = false;
#endif			

#ifdef DEBUG_PERFORMANCE_VOXELINDEX_PIXMAP
		if (nk_tree_push(_ctx, NK_TREE_TAB, "Performance", NK_MAXIMIZED))
		{
			std::string szText;

			nk_layout_row_begin(_ctx, NK_STATIC, 32, 2);

			nk_layout_row_push(_ctx, 375);
			NK_PUSH_TEXT_COLOR(_ctx, nk_rgb(255, 255, 255));
			szText = fmt::format(FMT_STRING("HoverVoxel > {:d} us"), getDebugVariable(microseconds, DebugLabel::HOVERVOXEL_US).count());
			nk_text(_ctx, szText.c_str(), szText.length(), NK_TEXT_ALIGN_LEFT);
			NK_POP_COLOR(_ctx);

			nk_layout_row_end(_ctx);

			nk_layout_row_begin(_ctx, NK_STATIC, 32, 2);

			nk_layout_row_push(_ctx, 675);
			NK_PUSH_TEXT_COLOR(_ctx, nk_rgb(255, 255, 255));
			szText = fmt::format(FMT_STRING("queryVoxelIndexPixMap > {:d} us"), getDebugVariable(microseconds, DebugLabel::QUERY_VOXELINDEX_PIXMAP_US).count());
			nk_text(_ctx, szText.c_str(), szText.length(), NK_TEXT_ALIGN_LEFT);
			NK_POP_COLOR(_ctx);

			nk_layout_row_end(_ctx);

			bPerformanceOpen = true;
			nk_tree_pop(_ctx);
		}
		else
			bPerformanceOpen = false;
#endif
		// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! //
#ifdef DEBUG_PERFORMANCE_VOXEL_SUBMISSION // *** chart causes flickering mouse and other anomolies when updated, good nuff for debugging just don't mistake it for something new....
		static constexpr milliseconds DISPLAY_INTERVAL = milliseconds(8);
		static constexpr size_t const GRAPH_LENGTH = 64;
		static tTime tLastInterval = zero_time_point;
		static std::queue<PerformanceResult> queuePerf;

		static PerformanceResult intervalResult,
			resolvedResult;

		{
			PerformanceResult& reference(getDebugVariableReference(PerformanceResult, DebugLabel::PERFORMANCE_VOXEL_SUBMISSION));
			PerformanceResult result(reference);

			if (high_resolution_clock::now() - tLastInterval > (DISPLAY_INTERVAL)) {
				reference.reset();
			}

			intervalResult += result;
		}
		if (high_resolution_clock::now() - tLastInterval > DISPLAY_INTERVAL) {

			resolvedResult.reset();
			resolvedResult += intervalResult;
			resolvedResult.resolve();
			intervalResult.reset();

			queuePerf.push(resolvedResult);

			if (queuePerf.size() >= GRAPH_LENGTH) {
				queuePerf.pop();
			}

			tLastInterval = high_resolution_clock::now();
		}

		if (nk_tree_push(_ctx, NK_TREE_TAB, "PERFORMANCE", NK_MAXIMIZED))
		{
			nk_layout_row_begin(_ctx, NK_STATIC, 200, 1);
			nk_layout_row_push(_ctx, 750);

			size_t queueLength(queuePerf.size());

			static float
				y_max = 1.0f,
				y_max_avg = y_max;
			float
				y_max_sum = 0.0f,
				y_count = 0.0f;

			if (nk_chart_begin_colored(_ctx, NK_CHART_LINES, nk_rgb(235, 255, 155), nk_rgb(235, 255, 155),
				queueLength, 0.0f, y_max)) {
				nk_chart_add_slot_colored(_ctx, NK_CHART_LINES, nk_rgb(122, 204, 255), nk_rgb(122, 204, 255),
					queueLength, 0.0f, y_max);

				float const last_y_max_avg(y_max_avg);
				y_max_avg = 0.0f;

				while (0 != queueLength) {

					PerformanceResult const recorded_result = queuePerf.front();
					queuePerf.pop();

					// filter out large spikes that screw up the graph, by clamping them to the current average maximum
					// but still add the spike to the sum used to calculate the new average maximum used next frame
					float const linear_time = (float)duration_cast<microseconds>(recorded_result.total_operations * recorded_result.avg_operation_duration).count();
					nk_chart_push_slot(_ctx, (linear_time > last_y_max_avg ? 0.0f : linear_time), 0);  // linear time, if an op was executed in a single thread * n ooperations

					float const parallel_time = (float)duration_cast<microseconds>(recorded_result.grid_duration).count();
					nk_chart_push_slot(_ctx, (parallel_time > last_y_max_avg ? 0.0f : parallel_time), 1); // actual time to proess in parallel all operations plus the ground which is not included in the linear time

					float y_cur_max = y_max;
					y_cur_max = SFM::max(y_cur_max, linear_time);
					y_cur_max = SFM::max(y_cur_max, parallel_time);
					y_max_sum += y_cur_max;
					++y_count;

					queuePerf.push(recorded_result);
					--queueLength;
				}
				if (y_count > 0.0f) {
					y_max_avg = (y_max_sum / y_count);
				}

				y_max = y_max_avg;// SFM::max(y_max, y_max_avg);

				nk_chart_end(_ctx);
			}
			nk_layout_row_end(_ctx);

			std::string szText;

			nk_layout_row_begin(_ctx, NK_STATIC, 32, 2);

			nk_layout_row_push(_ctx, 700);
			NK_PUSH_TEXT_COLOR(_ctx, nk_rgb(255, 255, 255));
			szText = fmt::format(FMT_STRING("GRID DURATION> {:d} US"), duration_cast<microseconds>(resolvedResult.grid_duration).count());
			nk_text(_ctx, szText.c_str(), szText.length(), NK_TEXT_ALIGN_LEFT);
			NK_POP_COLOR(_ctx);

			nk_layout_row_end(_ctx);

			nk_layout_row_begin(_ctx, NK_STATIC, 32, 2);

			nk_layout_row_push(_ctx, 375);
			NK_PUSH_TEXT_COLOR(_ctx, nk_rgb(255, 255, 255));
			szText = fmt::format(FMT_STRING("OPERATIONS> {:d}"), resolvedResult.total_operations);
			nk_text(_ctx, szText.c_str(), szText.length(), NK_TEXT_ALIGN_LEFT);
			NK_POP_COLOR(_ctx);

			nk_layout_row_push(_ctx, 375);
			NK_PUSH_TEXT_COLOR(_ctx, nk_rgb(255, 255, 255));
			szText = fmt::format(FMT_STRING("THREADS> {:d}"), resolvedResult.getThreadCount());
			nk_text(_ctx, szText.c_str(), szText.length(), NK_TEXT_ALIGN_LEFT);
			NK_POP_COLOR(_ctx);

			nk_layout_row_end(_ctx);

			nk_layout_row_begin(_ctx, NK_STATIC, 32, 1);

			nk_layout_row_push(_ctx, 700);
			NK_PUSH_TEXT_COLOR(_ctx, nk_rgb(255, 255, 255));
			szText = fmt::format(FMT_STRING("AVERAGE OP DURATION> {:d} US"), duration_cast<microseconds>(resolvedResult.avg_operation_duration).count());
			nk_text(_ctx, szText.c_str(), szText.length(), NK_TEXT_ALIGN_LEFT);
			NK_POP_COLOR(_ctx);

			nk_layout_row_end(_ctx);
			nk_layout_row_begin(_ctx, NK_STATIC, 32, 1);

			nk_layout_row_push(_ctx, 700);
			NK_PUSH_TEXT_COLOR(_ctx, nk_rgb(255, 255, 255));
			szText = fmt::format(FMT_STRING("MAX OP DURATION> {:d} US"), duration_cast<microseconds>(resolvedResult.max_operation_duration).count());
			nk_text(_ctx, szText.c_str(), szText.length(), NK_TEXT_ALIGN_LEFT);
			NK_POP_COLOR(_ctx);

			nk_layout_row_end(_ctx);

			nk_layout_row_begin(_ctx, NK_STATIC, 32, 1);

			nk_layout_row_push(_ctx, 700);
			NK_PUSH_TEXT_COLOR(_ctx, nk_rgb(255, 255, 255));
			szText = fmt::format(FMT_STRING("AVERAGE ITERATIONS> {:d}"), resolvedResult.avg_iterations);
			nk_text(_ctx, szText.c_str(), szText.length(), NK_TEXT_ALIGN_LEFT);
			NK_POP_COLOR(_ctx);

			nk_layout_row_end(_ctx);
			nk_layout_row_begin(_ctx, NK_STATIC, 32, 1);

			nk_layout_row_push(_ctx, 700);
			NK_PUSH_TEXT_COLOR(_ctx, nk_rgb(255, 255, 255));
			szText = fmt::format(FMT_STRING("MAX ITERATIONS> {:d}"), resolvedResult.max_iterations);
			nk_text(_ctx, szText.c_str(), szText.length(), NK_TEXT_ALIGN_LEFT);
			NK_POP_COLOR(_ctx);

			nk_layout_row_end(_ctx);

			nk_layout_row_begin(_ctx, NK_STATIC, 32, 1);

			nk_layout_row_push(_ctx, 700);
			NK_PUSH_TEXT_COLOR(_ctx, nk_rgb(255, 255, 255));
			szText = fmt::format(FMT_STRING("AVERAGE ITERATION DURATION> {:d} US"), duration_cast<microseconds>(resolvedResult.avg_iteration_duration).count());
			nk_text(_ctx, szText.c_str(), szText.length(), NK_TEXT_ALIGN_LEFT);
			NK_POP_COLOR(_ctx);

			nk_layout_row_end(_ctx);
			nk_layout_row_begin(_ctx, NK_STATIC, 32, 1);

			nk_layout_row_push(_ctx, 700);
			NK_PUSH_TEXT_COLOR(_ctx, nk_rgb(255, 255, 255));
			szText = fmt::format(FMT_STRING("MAX ITERATION DURATION> {:d} US"), duration_cast<microseconds>(resolvedResult.max_iteration_duration).count());
			nk_text(_ctx, szText.c_str(), szText.length(), NK_TEXT_ALIGN_LEFT);
			NK_POP_COLOR(_ctx);

			nk_layout_row_end(_ctx);

			bPerformanceOpen = true;
			nk_tree_pop(_ctx);
		}
		else
			bPerformanceOpen = false;

#endif

#ifndef NDEBUG
		if (nk_tree_push(_ctx, NK_TREE_TAB, "CAMERA", NK_MINIMIZED))
		{
			XMFLOAT2A vIsoCenterOffset;
			std::string szText;

			XMStoreFloat2A(&vIsoCenterOffset, getDebugVariable(XMVECTOR, DebugLabel::CAMERA_FRACTIONAL_OFFSET));

			//NK_PUSH_FONT(_ctx, eNK_FONTS::CRISP);

			nk_layout_row_begin(_ctx, NK_STATIC, 32, 1);
			nk_layout_row_push(_ctx, 550);
			NK_PUSH_TEXT_COLOR(_ctx, nk_rgb(127, 0, 255));
			nk_label(_ctx, "X AXIS:", NK_TEXT_LEFT);
			NK_POP_COLOR(_ctx);
			nk_layout_row_end(_ctx);

			nk_layout_row_begin(_ctx, NK_STATIC, 32, 2);

			nk_layout_row_push(_ctx, 550);
			nk_slider_float(_ctx, 0.0f, &vIsoCenterOffset.x, 1.0f, 0.01f);

			nk_layout_row_push(_ctx, 150);

			szText = fmt::format(FMT_STRING("{:f}"), vIsoCenterOffset.x);
			nk_text(_ctx, szText.c_str(), szText.length(), NK_TEXT_ALIGN_LEFT);

			nk_layout_row_end(_ctx);

			nk_layout_row_begin(_ctx, NK_STATIC, 32, 1);
			nk_layout_row_push(_ctx, 550);
			NK_PUSH_TEXT_COLOR(_ctx, nk_rgb(127, 0, 255));
			nk_label(_ctx, "Z AXIS:", NK_TEXT_LEFT);
			NK_POP_COLOR(_ctx);
			nk_layout_row_end(_ctx);

			nk_layout_row_begin(_ctx, NK_STATIC, 32, 2);

			nk_layout_row_push(_ctx, 550);
			nk_slider_float(_ctx, 0.0f, &vIsoCenterOffset.y, 1.0f, 0.01f);

			nk_layout_row_push(_ctx, 150);

			szText = fmt::format(FMT_STRING("{:f}"), vIsoCenterOffset.y);
			nk_text(_ctx, szText.c_str(), szText.length(), NK_TEXT_ALIGN_LEFT);

			nk_layout_row_end(_ctx);

			//NK_POP_FONT(_ctx);

			nk_tree_pop(_ctx);
			bCameraOpen = true;
		}
		else {
			bCameraOpen = false;
		}

		if (bCameraOpen)
		{
			//NK_PUSH_FONT(_ctx, eNK_FONTS::CRISP);
			nk_layout_row_dynamic(_ctx, 20, 1);
			{
				static constexpr uint32_t const COLOR_REFRESH = 120,
					MINMAX_REFRESH = COLOR_REFRESH * 10;
				NK_PUSH_TEXT_COLOR(_ctx, nk_rgb(127, 0, 255));
				nk_label(_ctx, "VOXEL CAMERA ORIGIN OFFSET:", NK_TEXT_LEFT);
				NK_POP_COLOR(_ctx);

				nk_layout_row_dynamic(_ctx, 25, 2);

				std::string szText;

				static tTime tLast(zero_time_point);
				static XMFLOAT2A vMax, vMin;
				bool bSkipColor(false);
				XMFLOAT2A voxelCameraOriginOffset;

				XMVECTOR const xmOffset = getDebugVariable(XMVECTOR, DebugLabel::CAMERA_FRACTIONAL_OFFSET);

				if (tLocal - tLast >= milliseconds(MINMAX_REFRESH)) {
					XMStoreFloat2A(&vMax, _mm_set1_ps(-FLT_MAX));
					XMStoreFloat2A(&vMin, _mm_set1_ps(FLT_MAX));
					bSkipColor = true;
					tLast = tLocal;
				}
				static XMFLOAT2A vLastMax, vLastMin;

				XMVECTOR xmMax = XMLoadFloat2A(&vMax);  xmMax = XMVectorMax(xmMax, xmOffset);
				XMVECTOR xmMin = XMLoadFloat2A(&vMin);  xmMin = XMVectorMin(xmMin, xmOffset);

				static PackedVector::XMCOLOR colorMinX, colorMinY, colorMaxX, colorMaxY;
				static tTime tLastColor;

				if (!bSkipColor && tLocal - tLastColor >= milliseconds(COLOR_REFRESH)) {

					uint32_t Result;
					XMVectorEqualR(&Result, XMVectorSplatX(xmMin), XMVectorSplatX(XMLoadFloat2A(&vLastMin)));
					if (XMComparisonAllFalse(Result)) {
						XMStoreColor(&colorMinX, XMVectorSelect(DirectX::Colors::Red, DirectX::Colors::LimeGreen,
							XMVectorGreater(XMVectorSplatX(xmMin), XMVectorSplatX(XMLoadFloat2A(&vLastMin)))));
					}
					else {
						colorMinX.r = colorMinX.g = colorMinX.b = colorMinX.a = 255;
					}
					XMVectorEqualR(&Result, XMVectorSplatY(xmMin), XMVectorSplatY(XMLoadFloat2A(&vLastMin)));
					if (XMComparisonAllFalse(Result)) {
						XMStoreColor(&colorMinY, XMVectorSelect(DirectX::Colors::Red, DirectX::Colors::LimeGreen,
							XMVectorGreater(XMVectorSplatY(xmMin), XMVectorSplatY(XMLoadFloat2A(&vLastMin)))));
					}
					else {
						colorMinY.r = colorMinY.g = colorMinY.b = colorMinY.a = 255;
					}

					XMVectorEqualR(&Result, XMVectorSplatX(xmMax), XMVectorSplatX(XMLoadFloat2A(&vLastMax)));
					if (XMComparisonAllFalse(Result)) {
						XMStoreColor(&colorMaxX, XMVectorSelect(DirectX::Colors::Red, DirectX::Colors::LimeGreen,
							XMVectorGreater(XMVectorSplatX(xmMax), XMVectorSplatX(XMLoadFloat2A(&vLastMax)))));
					}
					else {
						colorMaxX.r = colorMaxX.g = colorMaxX.b = colorMaxX.a = 255;
					}
					XMVectorEqualR(&Result, XMVectorSplatY(xmMax), XMVectorSplatY(XMLoadFloat2A(&vLastMax)));
					if (XMComparisonAllFalse(Result)) {
						XMStoreColor(&colorMaxY, XMVectorSelect(DirectX::Colors::Red, DirectX::Colors::LimeGreen,
							XMVectorGreater(XMVectorSplatY(xmMax), XMVectorSplatY(XMLoadFloat2A(&vLastMax)))));
					}
					else {
						colorMaxY.r = colorMaxY.g = colorMaxY.b = colorMaxY.a = 255;
					}
					tLastColor = tLocal;
				}
				vLastMax = vMax;
				vLastMin = vMin;
				XMStoreFloat2A(&vMax, xmMax);
				XMStoreFloat2A(&vMin, xmMin);
				XMStoreFloat2A(&voxelCameraOriginOffset, xmOffset);

				szText = fmt::format(FMT_STRING("{:f}"), voxelCameraOriginOffset.x);
				nk_text(_ctx, szText.c_str(), szText.length(), NK_TEXT_ALIGN_LEFT);

				szText = fmt::format(FMT_STRING("{:f}"), voxelCameraOriginOffset.y);
				nk_text(_ctx, szText.c_str(), szText.length(), NK_TEXT_ALIGN_LEFT);

				nk_layout_row_dynamic(_ctx, 25, 4);
				szText = fmt::format(FMT_STRING("MIN({:f})"), vMin.x);
				nk_text_colored(_ctx, szText.c_str(), szText.length(), NK_TEXT_ALIGN_LEFT, nk_rgb_from_XMCOLOR(colorMinX));

				szText = fmt::format(FMT_STRING("MAX({:f})"), vMax.x);
				nk_text_colored(_ctx, szText.c_str(), szText.length(), NK_TEXT_ALIGN_LEFT, nk_rgb_from_XMCOLOR(colorMaxX));


				szText = fmt::format(FMT_STRING("MIN({:f})"), vMin.y);
				nk_text_colored(_ctx, szText.c_str(), szText.length(), NK_TEXT_ALIGN_LEFT, nk_rgb_from_XMCOLOR(colorMinY));

				szText = fmt::format(FMT_STRING("MAX({:f})"), vMax.y);
				nk_text_colored(_ctx, szText.c_str(), szText.length(), NK_TEXT_ALIGN_LEFT, nk_rgb_from_XMCOLOR(colorMaxY));
			}

			nk_layout_row_dynamic(_ctx, 20, 1);
			{
				NK_PUSH_TEXT_COLOR(_ctx, nk_rgb(127, 0, 255));
				nk_label(_ctx, "VOXEL WORLD ORIGIN:", NK_TEXT_LEFT);
				NK_POP_COLOR(_ctx);

				nk_layout_row_dynamic(_ctx, 25, 2);

				std::string szText;

				XMFLOAT2A voxelWorldOrigin;
				XMStoreFloat2A(&voxelWorldOrigin, XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(world::getOrigin()));

				szText = fmt::format(FMT_STRING("{:f}"), voxelWorldOrigin.x);
				nk_text(_ctx, szText.c_str(), szText.length(), NK_TEXT_ALIGN_LEFT);

				szText.clear();

				szText = fmt::format(FMT_STRING("{:f}"), voxelWorldOrigin.y);
				nk_text(_ctx, szText.c_str(), szText.length(), NK_TEXT_ALIGN_LEFT);
			}

			nk_layout_row_dynamic(_ctx, 20, 1);
			{
				NK_PUSH_TEXT_COLOR(_ctx, nk_rgb(127, 0, 255));
				nk_label(_ctx, "VOXEL MOUSE HOVER INDEX:", NK_TEXT_LEFT);
				NK_POP_COLOR(_ctx);

				nk_layout_row_dynamic(_ctx, 32, 2);

				std::string szText;

				point2D_t const voxelIndexHovered(MinCity::VoxelWorld->getHoveredVoxelIndex());

				szText = fmt::format(FMT_STRING("{:d}"), voxelIndexHovered.x);
				nk_text(_ctx, szText.c_str(), szText.length(), NK_TEXT_ALIGN_LEFT);

				szText.clear();

				szText = fmt::format(FMT_STRING("{:d}"), voxelIndexHovered.y);
				nk_text(_ctx, szText.c_str(), szText.length(), NK_TEXT_ALIGN_LEFT);
			}
			//NK_POP_FONT(_ctx);
		} // bCameraOpen

		if (nk_tree_push(_ctx, NK_TREE_TAB, "INSTANCES", NK_MINIMIZED))
		{
			nk_layout_row_dynamic(_ctx, 32, 3);
			{
				NK_PUSH_TEXT_COLOR(_ctx, nk_rgb(127, 0, 255));
				nk_label(_ctx, "DYNAMIC", NK_TEXT_LEFT);
				nk_label(_ctx, "STATIC", NK_TEXT_LEFT);
				nk_label(_ctx, "ROOT INDICES", NK_TEXT_LEFT);
				NK_POP_COLOR(_ctx);

				nk_layout_row_dynamic(_ctx, 32, 3);

				std::string szText;

				szText = fmt::format(FMT_STRING("{:d}"), MinCity::VoxelWorld->numDynamicModelInstances());
				nk_text(_ctx, szText.c_str(), szText.length(), NK_TEXT_ALIGN_LEFT);

				szText.clear();

				szText = fmt::format(FMT_STRING("{:d}"), MinCity::VoxelWorld->numStaticModelInstances());
				nk_text(_ctx, szText.c_str(), szText.length(), NK_TEXT_ALIGN_LEFT);

				szText.clear();

				szText = fmt::format(FMT_STRING("{:d}"), MinCity::VoxelWorld->numRootIndices());
				nk_text(_ctx, szText.c_str(), szText.length(), NK_TEXT_ALIGN_LEFT);

				szText.clear();
			}
			nk_tree_pop(_ctx);
			bInstancesOpen = true;
}
		else {
			bInstancesOpen = false;
		}

		if (nk_tree_push(_ctx, NK_TREE_TAB, "VOXELS", NK_MINIMIZED))
		{
			nk_layout_row_dynamic(_ctx, 32, 4);
			{
				NK_PUSH_TEXT_COLOR(_ctx, nk_rgb(127, 0, 255));
				nk_label(_ctx, "DYNAMIC", NK_TEXT_LEFT);
				nk_label(_ctx, "STATIC", NK_TEXT_LEFT);
				nk_label(_ctx, "TERRAIN", NK_TEXT_LEFT);
				nk_label(_ctx, "LIGHT", NK_TEXT_LEFT);
				NK_POP_COLOR(_ctx);

				nk_layout_row_dynamic(_ctx, 32, 4);

				std::string szText;

				szText = fmt::format(FMT_STRING("{:d}"), MinCity::VoxelWorld->numDynamicVoxelsRendered());
				nk_text(_ctx, szText.c_str(), szText.length(), NK_TEXT_ALIGN_LEFT);

				szText.clear();

				szText = fmt::format(FMT_STRING("{:d}"), MinCity::VoxelWorld->numStaticVoxelsRendered());
				nk_text(_ctx, szText.c_str(), szText.length(), NK_TEXT_ALIGN_LEFT);

				szText.clear();

				szText = fmt::format(FMT_STRING("{:d}"), MinCity::VoxelWorld->numTerrainVoxelsRendered());
				nk_text(_ctx, szText.c_str(), szText.length(), NK_TEXT_ALIGN_LEFT);

				szText.clear();

				szText = fmt::format(FMT_STRING("{:d}"), MinCity::VoxelWorld->numLightVoxelsRendered());
				nk_text(_ctx, szText.c_str(), szText.length(), NK_TEXT_ALIGN_LEFT);

				szText.clear();
		}
			nk_tree_pop(_ctx);
			bVoxelsOpen = true;
		}
		else {
			bVoxelsOpen = false;
		}
#endif
		bMainWindowStartOpen = true;
		nk_style_pop_font(_ctx);
	}
	else {
		bMainWindowStartOpen = false;  // reset state
	}
	nk_end(_ctx);

#ifndef NDEBUG	
	std::string const szDebugMessage(_szDebugMessage); // copy out (thread safe read-only)
	if (!szDebugMessage.empty()) {
		tTime const tNow(high_resolution_clock::now());
		static tTime tLast(tNow);

		static nk_color signal_color(nk_rgb(255, 0, 0));

		milliseconds const tDelta(duration_cast<milliseconds>(tNow - tLast));

		signal_color.r -= (nk_byte)tDelta.count(); // rolls over on a byte

		nk_style_push_float(_ctx, &_ctx->style.window.header.minimize_button.border, 40.0f);
		nk_style_push_color(_ctx, &_ctx->style.window.header.minimize_button.border_color, signal_color);
		nk_style_push_color(_ctx, &_ctx->style.window.header.label_normal, nk_rgb(255, 255, 255));

		constexpr eWindowName const windowName(eWindowName::WINDOW_TOP_DEBUG);
		if (nk_begin_titled(_ctx, windowName._to_string(), stringconv::toUpper(szDebugMessage).c_str(), nk_rect(0, 0, Globals::DEFAULT_SCREEN_WIDTH, 24),
			NK_WINDOW_NO_INPUT | NK_WINDOW_BACKGROUND |
			NK_WINDOW_TITLE | NK_WINDOW_MINIMIZABLE))
		{

		}
		nk_style_pop_color(_ctx);
		nk_style_pop_color(_ctx);
		nk_style_pop_float(_ctx);
		nk_end(_ctx);

		tLast = tNow;
	}
#endif
}
#endif /*DEBUG_FPS_WINDOW*/

// last: dtor
void  cNuklear::CleanUp()
{
	// release nk images
	for (auto& image : _imageVector) {

		auto pDel = image;
		image = nullptr;

		SAFE_DELETE(pDel);
	}

	// cleanup custom canvas'
	SAFE_DELETE(_offscreen_canvas);

	// cleanup all guitextures
	SAFE_DELETE(_guiTextures.menu_item);
	SAFE_DELETE(_guiTextures.static_screen);

	SAFE_DELETE(_guiTextures.load_thumbnail.stagingBuffer);
	for (uint32_t i = 0; i < guiTextures::load_thumbnail::count; ++i) {
		SAFE_DELETE(_guiTextures.load_thumbnail.texture[i]);
	}

	// sequence images
	if (_sequenceImages.menu_item) {
		ImagingDelete(_sequenceImages.menu_item);
	}
	if (_sequenceImages.static_screen) {
		ImagingDelete(_sequenceImages.static_screen);
	}

	if (_vertices) {
		scalable_aligned_free(_vertices);
		_vertices = nullptr;
	}
	if (_indices) {
		scalable_aligned_free(_indices);
		_indices = nullptr;
	}

	if (nullptr != _atlas) {
		_atlas = nullptr;
	}

	if (nullptr != _cmds) {
		nk_buffer_free(_cmds);
		_cmds = nullptr;
	}
	if (nullptr != _ctx) {
		nk_free(_ctx);
		_ctx = nullptr;
	}
	SAFE_DELETE(_fontTexture);
}