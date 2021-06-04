#include "pch.h"
#include "cNuklear.h"
#include "Declarations.h"
#include "MinCity.h"
#include "cTextureBoy.h"
#include "cVulkan.h"
#include "Declarations.h"
#include "cVoxelWorld.h"
#include "cUserInterface.h"
#include "cPostProcess.h"
#include "CityInfo.h"
#include "cCity.h"
#include "data.h"

#include <filesystem>

#include <Imaging/Imaging/Imaging.h>

#define NK_IMPLEMENTATION  // the only place this is defined
#include "nk_include.h"
#include "nk_style.h"
#ifdef DEBUG_PERFORMANCE_VOXEL_SUBMISSION
#include <queue>
#endif

namespace fs = std::filesystem;

static constexpr uint32_t const 
	SDF_IMAGE = 0,
	RGBA_IMAGE = 1,
	ARRAY_IMAGE = 2;

typedef struct nk_image_extended : nk_image
{
	uint32_t type;

} nk_image_extended;

typedef struct alignas(16) sMouseState
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
		: button_state(eMouseButtonState::INACTIVE),
		tStamp(high_resolution_clock::now()),
		left_clicked(false), right_clicked(false),
		left_released(false), right_released(false),
		handled(false)
	{}
} MouseState;

NK_API const nk_rune*
nk_font_mincity_glyph_ranges(void)
{
	NK_STORAGE const nk_rune ranges[] = {
		0x0020, 0x007F,
		0x00A0, 0x00A9,
		0
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
	if (MinCity::TextureBoy.KTXFileExists(FONT_DIR L"FontAtlas.ktx"))
	{
		// replace with cached BC7 version instead
		bLoaded = MinCity::TextureBoy.LoadKTXTexture(texture, FONT_DIR L"FontAtlas.ktx");
	}

	if (!bLoaded) { // build new compressed texture //
		FMT_LOG(TEX_LOG, "Compressing Texture, please wait....");

		Imaging imgRef = ImagingLoadFromMemoryBGRA(memToLoad, width, height);
		Imaging imgFontBC7 = ImagingCompressBGRAToBC7(imgRef);

		ImagingSaveCompressedBC7ToKTX(imgFontBC7, FONT_DIR L"FontAtlas.ktx");

		ImagingDelete(imgFontBC7);
		ImagingDelete(imgRef);

		if (MinCity::TextureBoy.KTXFileExists(FONT_DIR L"FontAtlas.ktx"))
		{
			// replace with cached BC7 version instead
			bLoaded = MinCity::TextureBoy.LoadKTXTexture(texture, FONT_DIR L"FontAtlas.ktx");
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

static inline struct alignas(16) // double clicking is purposely not supported for ease of use (touch tablet or touch screen)
{
	static constexpr uint32_t const MAX_TYPED = 64; // lots of room for multiple key actions / frame

	bool input_focused;

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

static void
nk_clip_cursor(GLFWwindow* const win) {
	HWND const hWnd = glfwGetWin32Window(win);
	RECT clipRect;
	GetClientRect(hWnd, &clipRect);
	::SetLastError(ERROR_SUCCESS);
	if (0 == MapWindowPoints(hWnd, nullptr, (POINT * const)& clipRect, 2)) {
		if (ERROR_SUCCESS == ::GetLastError()) {
			ClipCursor(&clipRect);
		}
	}
	else {
		ClipCursor(&clipRect);
	}
}

static void 
nk_center_clip_cursor(GLFWwindow* const win) {

	nk_clip_cursor(win);
	nk_center_cursor(win);
}

void
cNuklear::nk_focus_callback(GLFWwindow *const win, int const focused)
{
	if (!focused) {
		glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		ClipCursor(nullptr);

		nk_input.input_focused = false;
	}
	else {

		glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);  // must be GLFW_CURSOR_HIDDEN for mouse to clip accurately with manual clipping done

		nk_center_clip_cursor(win);

		nk_input.input_focused = true;
	}
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
				state.left_clicked = (state.tStamp - nk_input.mouse_state.tStamp < MouseState::CLICK_DELTA);
				state.left_released = true;
			}
			else if (eMouseButtonState::RIGHT_PRESSED == nk_input.mouse_state.button_state) {
				state.right_clicked = (state.tStamp - nk_input.mouse_state.tStamp < MouseState::CLICK_DELTA);
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
	if (nk_input.input_focused) {
		nk_input.mouse_pos.x = (float)xpos;
		nk_input.mouse_pos.y = (float)ypos;
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
	_bGUIDirty(false), _bEditControlFocused(false),
	_uiWindowEnabled(0), _iWindowQuitSelection(0), _iWindowSaveSelection(0), _iWindowLoadSelection(0),
	_uiPauseProgress(0),
	_bHintReset(false), _saveWindow{}, _loadWindow{},
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
	alignas(16) static struct {
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
	
	glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_HIDDEN); // must be GLFW_CURSOR_HIDDEN for mouse to clip accurately with manual clipping done
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
	config.range = nk_font_mincity_glyph_ranges();
	config.oversample_h = 4;
	config.oversample_v = 4;
	//config.pixel_snap = true;

	// SDF Texture for fonts is 256x256, which has enough resolution for 4 fonts
	_fonts[eNK_FONTS::DEFAULT]		 = nk_font_atlas_add_from_file(_atlas, FONT_DIR L"mincity_mono.ttf", NK_FONT_HEIGHT, &config);
	
	_fonts[eNK_FONTS::SMALL]		 = nk_font_atlas_add_from_file(_atlas, FONT_DIR L"mincity_mono.ttf", NK_FONT_HEIGHT * 0.8f, &config);
	
	_fonts[eNK_FONTS::ENCRYPT]		 = nk_font_atlas_add_from_file(_atlas, FONT_DIR L"axiss_mono.ttf", NK_FONT_HEIGHT * 0.85f, &config); //!do not change ! - size set so aligned with DEFAULT

	_fonts[eNK_FONTS::ENCRYPT_SMALL] = nk_font_atlas_add_from_file(_atlas, FONT_DIR L"axiss_mono.ttf", NK_FONT_HEIGHT * 0.675f, &config); //!do not change ! - size set so aligned with SMALL
	
	_atlas->default_font = _fonts[eNK_FONTS::DEFAULT];

	// custom canvas'
	_offscreen_canvas = new nk_canvas();

	_vertices = (VertexDecl::nk_vertex* const __restrict)scalable_aligned_malloc(sizeof(VertexDecl::nk_vertex) * NK_MAX_VERTEX_COUNT, 16ULL);
	_indices = (uint16_t* const __restrict)scalable_aligned_malloc(sizeof(uint16_t) * NK_MAX_INDEX_COUNT, 16ULL);

	LoadGUITextures();
}

void cNuklear::SetSpecializationConstants(std::vector<vku::SpecializationConstant>& __restrict constants)
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
	// Load Textures (SDFs)
	MinCity::TextureBoy.LoadKTXTexture(_guiTextures.create, GUI_DIR L"create.ktx");

	MinCity::TextureBoy.LoadKTXTexture(_guiTextures.open, GUI_DIR L"open.ktx");

	MinCity::TextureBoy.LoadKTXTexture(_guiTextures.save, GUI_DIR L"save.ktx");

	MinCity::TextureBoy.LoadKTXTexture(_guiTextures.road, GUI_DIR L"road.ktx");

	MinCity::TextureBoy.LoadKTXTexture(_guiTextures.zoning, GUI_DIR L"zoning.ktx");

	// Load Image Sequences
	_sequenceImages.static_screen[0] = ImagingLoadGIFSequence(GUI_DIR L"static.gif");
	_sequenceImages.static_screen[1] = ImagingLoadGIFSequence(GUI_DIR L"static2.gif");
	_sequenceImages.static_screen[2] = ImagingLoadGIFSequence(GUI_DIR L"static3.gif");

	// Assign Images or Image Sequences to Textures

	{ // load thumbnail
		
		// pre-allocations of .image & .sequence required
		_guiTextures.load_thumbnail.image = ImagingNew(MODE_BGRA, offscreen_thumbnail_width, offscreen_thumbnail_height);
		ImagingClear(_guiTextures.load_thumbnail.image);
		_guiTextures.load_thumbnail.sequence = new guiSequence[sequenceImages::STATIC_IMAGE_COUNT];

		uint32_t max_count(0);

		for (uint32_t i = 0; i < sequenceImages::STATIC_IMAGE_COUNT; ++i) {
			_guiTextures.load_thumbnail.sequence[i].sequence = ImagingResample(_sequenceImages.static_screen[i], offscreen_thumbnail_width, offscreen_thumbnail_height, IMAGING_TRANSFORM_BICUBIC);
		
			// default to sequence with longest count - (so texture has enough layers to support that many frames)
			if (_guiTextures.load_thumbnail.sequence[i].sequence->count > max_count) {
				max_count = _guiTextures.load_thumbnail.sequence[i].sequence->count;
				_guiTextures.load_thumbnail.select = i;
			}
		}
		
		MinCity::TextureBoy.ImagingSequenceToTexture(_guiTextures.load_thumbnail.sequence[_guiTextures.load_thumbnail.select].sequence, _guiTextures.load_thumbnail.texture);
	}

	static constexpr uint32_t ICON_DIMENSIONS = 128U;

	// assigned *temporarily* the image view pointer
	using nk_image = struct nk_image;

	// first slot in array (0) reserved for default texture
	_imageVector.emplace_back( new nk_image_extended
	{	nullptr,	// this data is replaced when font texture is loaded, however this reserves the first slot for the default texture
		0, 0,
		{ 0, 0, 0, 0 },
		SDF_IMAGE
	});

	_guiImages.create	= _imageVector.emplace_back( new nk_image_extended
	{	_guiTextures.create->imageView(),
		ICON_DIMENSIONS, ICON_DIMENSIONS, // the size here can be customized
		{ 0, 0, ICON_DIMENSIONS, ICON_DIMENSIONS },
		SDF_IMAGE
	});
	_guiImages.open		= _imageVector.emplace_back( new nk_image_extended
	{	_guiTextures.open->imageView(),
		ICON_DIMENSIONS, ICON_DIMENSIONS, // the size here can be customized
		{ 0, 0, ICON_DIMENSIONS, ICON_DIMENSIONS },
		SDF_IMAGE
	});
	_guiImages.save		= _imageVector.emplace_back( new nk_image_extended
	{	_guiTextures.save->imageView(),
		ICON_DIMENSIONS, ICON_DIMENSIONS, // the size here can be customized
		{ 0, 0, ICON_DIMENSIONS, ICON_DIMENSIONS },
		SDF_IMAGE
	});
	_guiImages.road		= _imageVector.emplace_back( new nk_image_extended
	{	_guiTextures.road->imageView(),
		ICON_DIMENSIONS, ICON_DIMENSIONS, // the size here can be customized
		{ 0, 0, ICON_DIMENSIONS, ICON_DIMENSIONS },
		SDF_IMAGE
	});
	_guiImages.zoning	= _imageVector.emplace_back( new nk_image_extended
	{	_guiTextures.zoning->imageView(), 
		ICON_DIMENSIONS, ICON_DIMENSIONS, // the size here can be customized
		{ 0, 0, ICON_DIMENSIONS, ICON_DIMENSIONS },
		SDF_IMAGE
	});
	_guiImages.load_thumbnail	= _imageVector.emplace_back( new nk_image_extended
	{	_guiTextures.load_thumbnail.texture->imageView(), 
		offscreen_thumbnail_width, offscreen_thumbnail_width, // the size here can be customized
		{ 0, 0, offscreen_thumbnail_width, offscreen_thumbnail_width },
		ARRAY_IMAGE // initial layout
	});

	_guiImages.offscreen = _imageVector.emplace_back( new nk_image_extended
	{	MinCity::Vulkan.offscreenImageView2DArray(), 
		(uint16_t)_frameBufferSize.x, (uint16_t)_frameBufferSize.y,
		{ 0, 0, (uint16_t)_frameBufferSize.x, (uint16_t)_frameBufferSize.y },
		RGBA_IMAGE
	});
}

void  cNuklear::Upload(vk::CommandBuffer& __restrict cb_transfer,
					   vku::DynamicVertexBuffer& __restrict vbo, vku::DynamicIndexBuffer& __restrict ibo, vku::UniformBuffer& __restrict ubo)
{	
	static constexpr size_t const VERTICES_SZ = sizeof(VertexDecl::nk_vertex) * NK_MAX_VERTEX_COUNT;
	static constexpr size_t const INDICES_SZ = sizeof(uint16_t) * NK_MAX_INDEX_COUNT;

	vk::CommandBufferBeginInfo bi(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);	// eOneTimeSubmit = cb updated every frame
	cb_transfer.begin(bi); VKU_SET_CMD_BUFFER_LABEL(cb_transfer, vkNames::CommandBuffer::OVERLAY_TRANSFER);

	if (_bGUIDirty)
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

			MinCity::Vulkan.upload_BufferDeferred(vbo, cb_transfer, _stagingBuffer[0], _vertices, vbuf.needed, VERTICES_SZ);
			MinCity::Vulkan.upload_BufferDeferred(ibo, cb_transfer, _stagingBuffer[1], _indices, ebuf.needed, INDICES_SZ);

			{ // ## RELEASE ## //
				static constexpr size_t const buffer_count(2ULL);
				std::array<vku::GenericBuffer const* const, buffer_count> const buffers{ &vbo, &ibo };
				vku::GenericBuffer::barrier(buffers, // ## RELEASE ## // batched 
					cb_transfer, vk::PipelineStageFlagBits::eHost, vk::PipelineStageFlagBits::eVertexInput,
					vk::DependencyFlagBits::eByRegion,
					vk::AccessFlagBits::eHostWrite, vk::AccessFlagBits::eVertexAttributeRead, MinCity::Vulkan.getTransferQueueIndex(), MinCity::Vulkan.getGraphicsQueueIndex()
				);
			}
			_bAcquireRequired = true;
		}

		if (!_bUniformSet) {

			SetUniformBuffer(cb_transfer, ubo);
			
			ubo.barrier( // ## RELEASE ## //
				cb_transfer, vk::PipelineStageFlagBits::eHost, vk::PipelineStageFlagBits::eVertexShader,
				vk::DependencyFlagBits::eByRegion,
				vk::AccessFlagBits::eHostWrite, vk::AccessFlagBits::eUniformRead, MinCity::Vulkan.getTransferQueueIndex(), MinCity::Vulkan.getGraphicsQueueIndex()
			);

			_bUniformSet = true;  // only needs to be uploaded once
			_bUniformAcquireRequired = true; // flag that acquire operation needs to take place to complement previous release operation
		}

		_bGUIDirty = false;
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
			vk::AccessFlagBits::eHostWrite, vk::AccessFlagBits::eVertexAttributeRead, MinCity::Vulkan.getTransferQueueIndex(), MinCity::Vulkan.getGraphicsQueueIndex()
		);
		_bAcquireRequired = false; // must reset *here*
	}
	if (_bUniformAcquireRequired) {
		// transfer queue ownership
		ubo.barrier(	// ## ACQUIRE ## //
			cb_render, vk::PipelineStageFlagBits::eHost, vk::PipelineStageFlagBits::eVertexShader,
			vk::DependencyFlagBits::eByRegion,
			vk::AccessFlagBits::eHostWrite, vk::AccessFlagBits::eUniformRead, MinCity::Vulkan.getTransferQueueIndex(), MinCity::Vulkan.getGraphicsQueueIndex()
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
				(uint32_t)0U, (uint32_t)sizeof(UniformDecl::NuklearPushConstants), (void const* const)&PushConstants);

			last_texture_id = texture_id;
		}

		vk::Rect2D const scissor(
				vk::Offset2D(SFM::round_to_i32(SFM::max(cmd->clip_rect.x, 0.0f)),
							 SFM::round_to_i32(SFM::max(cmd->clip_rect.y, 0.0f))),

				vk::Extent2D(SFM::round_to_u32(cmd->clip_rect.w),
					         SFM::round_to_u32(cmd->clip_rect.h))
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
static bool const UpdateInput(struct nk_context* const __restrict ctx, GLFWwindow* const __restrict win, std::vector<rect2D_t> const& __restrict activeWindowRects)
{
	tTime const tLocal(high_resolution_clock::now());
	static tTime tLocalLast(tLocal);

	/* Input */
	nk_input.reset(); // important to call b4 polling events !
	glfwPollEvents();

	bool bInputGUIDelta(false); // *** signals GUI to update (returned from this function) *** remeber to set when GUI is affected by input !!

	if (glfwGetKey(win, GLFW_KEY_ESCAPE) == GLFW_PRESS) {		// always process escape key

		MinCity::Quit(); bInputGUIDelta = true;
	}

	if (!nk_input.input_focused) { // only continue to process input if window is focused
		MinCity::VoxelWorld.OnMouseInactive();
		return(false);
	}
	
	//############################
	nk_input_begin(ctx);
	//############################

	if (MinCity::Nuklear.isEditControlFocused()) { // nuklear input

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
				MinCity::VoxelWorld.OnKey(nk_input.keys[i].key, down, ctrl);
				break;
			}
		}
	}

	// handling mouse input
	{
		XMVECTOR const xmPosition(XMLoadFloat2((XMFLOAT2* const __restrict)&nk_input.mouse_pos));
		
		if (MinCity::VoxelWorld.OnMouseMotion(xmPosition)) { // Hi resolution mouse motion tracking (internally chechked for high precision delta of mouse coords)

			nk_input_motion(ctx, nk_input.mouse_pos.x, nk_input.mouse_pos.y);
				
			if (!p2D_sub(point2D_t(nk_input.mouse_pos.x, nk_input.mouse_pos.y),
				         point2D_t(nk_input.last_mouse_pos.x, nk_input.last_mouse_pos.y)).isZero()) { // bugfix: have clip cursor every time for it to stop at edges of screen properly
																											// good enough one whole pixels to save some performance
				nk_clip_cursor(win);

				nk_input.last_mouse_pos = nk_input.mouse_pos;  // only updated for whole pixel movement to improve clipping performance
				bInputGUIDelta = true; // mouse cursor
			}
		}
	}

	// check active window(s) if mouse was pressed/clicked
	bool isHoveringWindow(false);

	for (auto const& rectWindow : activeWindowRects) {
		alignas(16) struct nk_rect rectActiveWindow;
		XMStoreFloat4A(reinterpret_cast<XMFLOAT4A*>(&rectActiveWindow), r2D_to_nk_rect(rectWindow));
		if (nk_input_is_mouse_hovering_rect(&ctx->input, rectActiveWindow)) {
			isHoveringWindow = true;
			break;
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

	if (isHoveringWindow && (0.0f != nk_input.scroll.y)) {
		nk_input_scroll(ctx, nk_input.scroll);
		nk_input.scroll.y = 0.0f; // must zero once handled

		bInputGUIDelta = true; // scrolling window
	}
	//#######################
	nk_input_end(ctx);
	//#######################
	
	// mouse handling for game input
	if (!nk_input.mouse_state.handled) {

		switch (nk_input.mouse_state.button_state)
		{
		case eMouseButtonState::LEFT_PRESSED:
			if (!isHoveringWindow) {
				MinCity::VoxelWorld.OnMouseLeft(eMouseButtonState::LEFT_PRESSED);
			}
			break;
		case eMouseButtonState::RIGHT_PRESSED:
			if (!isHoveringWindow) {
				MinCity::VoxelWorld.OnMouseRight(eMouseButtonState::RIGHT_PRESSED);
			}
			break;
		case eMouseButtonState::RELEASED:
			if (nk_input.mouse_state.left_released) {
				MinCity::VoxelWorld.OnMouseLeft(eMouseButtonState::RELEASED);
			}
			else if (nk_input.mouse_state.right_released) {
				MinCity::VoxelWorld.OnMouseRight(eMouseButtonState::RELEASED);
			}
			break;
		default:
			MinCity::VoxelWorld.OnMouseInactive();
			break;
		}

		if (!isHoveringWindow) {
			if (nk_input.mouse_state.left_clicked) {
				MinCity::VoxelWorld.OnMouseLeftClick();
			}
			else if (nk_input.mouse_state.right_clicked) {
				MinCity::VoxelWorld.OnMouseRightClick();
			}
		}
	
	} // handled ?
	else if (isHoveringWindow) {

		// only if current state is handled (no change between successive calls to this function)
		MinCity::VoxelWorld.OnMouseInactive();
	}

	// end mouse input
	if (!isHoveringWindow) { // must persist outside handled...
		if (0.0f != nk_input.scroll.y) {
			MinCity::VoxelWorld.OnMouseScroll(nk_input.scroll.y);
			nk_input.scroll.y = 0.0f; // must zero once handled
		}
	}
	nk_input.mouse_state.handled = true;
	//
	
	/*
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

			if (glfwGetKey(win, GLFW_KEY_BACKSPACE) == GLFW_PRESS) {

				xmPushConstant = XMVectorZero();
				uAdjust = 0;
				bToggle1 = bToggle2 = bToggle3 = false;

				setDebugVariable(XMVECTOR, DebugLabel::PUSH_CONSTANT_VECTOR, xmPushConstant);
				setDebugVariable(uint8_t, DebugLabel::RAMP_CONTROL_BYTE, uAdjust);
				setDebugVariable(bool, DebugLabel::TOGGLE_1_BOOL, bToggle1);
				setDebugVariable(bool, DebugLabel::TOGGLE_2_BOOL, bToggle2);
				setDebugVariable(bool, DebugLabel::TOGGLE_3_BOOL, bToggle3);

				FMT_NUKLEAR_DEBUG(false, "<< all debug variables zeroed >>", uAdjust);

				WasPressed = GLFW_KEY_BACKSPACE;
				tLastPress = tLocal;
			}
		} // repeatrate
	}
	*/
	tLocalLast = tLocal;

	return(bInputGUIDelta);
}

bool const  cNuklear::UpdateInput()  // returns true on input delta that affects gui
{
	/* Input */
	return(::UpdateInput(_ctx, _win, _activeWindowRects));
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
				if (MinCity::PostProcess.SaveMixedLUT(selected_lut, available_luts[selected_luts[0]], available_luts[selected_luts[1]], tT)) {
					available_luts.clear();
				}
			}
			else {
				if (tT != t || bUpdateRequired) {
					
					tT = t;

					if (tNow - tLastChanged > tUpdateInterval || bUpdateRequired) {
						if (MinCity::PostProcess.MixLUT(available_luts[selected_luts[0]], available_luts[selected_luts[1]], tT)) {

							tLastChanged = tNow;
							bUploadLUT = true;
						}
					}
				}
				else if (bUploadLUT) {
					bUploadLUT = !MinCity::PostProcess.UploadLUT(); // must be done on main thread, only uploads when lut that was last mixed has completed
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
void  cNuklear::UpdateGUI()
{
	tTime const tLocal(high_resolution_clock::now());
	static tTime tLocalLast(tLocal);

	[[unlikely]] if (!MinCity::Vulkan.isRenderingEnabled()) // bugfix - crash when out of focus
		return;

	_activeWindowRects.clear(); // reset active window rects
	nk_clear(_ctx); // ########## ONLY PLACE THIS IS CLEARED #########  //
	_ctx->delta_time_seconds = fp_seconds(tLocal - tLocalLast).count(); // update internal timing of context 
	_bEditControlFocused = false; // reset
	_bGUIDirty = true; // signal upload is neccessary 

	/* GUI */
	static bool bLastPaused(true);

	if (!MinCity::isPaused())
	{ // Bottom Menu //
		bLastPaused = true;

		uint32_t const window_offset(100);
		uint32_t const window_width(_frameBufferSize.x - (window_offset << 1));
		uint32_t const window_height(100);

		constexpr eWindowName const windowName(eWindowName::WINDOW_BOTTOM_MENU);
		if (nk_begin(_ctx, windowName._to_string(),
			nk_recti(window_offset, _frameBufferSize.y - window_height, window_width, window_height),
			NK_WINDOW_NO_SCROLLBAR))
		{
			struct nk_rect const windowBounds(nk_window_get_bounds(_ctx));

			AddActiveWindowRect(r2D_set_by_width_height(windowBounds.x, windowBounds.y, windowBounds.w, windowBounds.h));	// must register any active window;

			static constexpr int32_t const cols(4);
			static constexpr int32_t const icon(48);
			nk_layout_row_static(_ctx, icon, icon, cols);

			//
			uint32_t const activeSubTool(MinCity::UserInterface.getActivatedSubToolType());

			if (nk_sdf_hover_button(_ctx, _guiImages.road, eSubTool_Zoning::RESERVED == activeSubTool)) {
				MinCity::UserInterface.setActivatedTool(eTools::ROADS);
			}
			if (nk_sdf_hover_button(_ctx, _guiImages.zoning, eSubTool_Zoning::RESIDENTIAL == activeSubTool)) {
				MinCity::UserInterface.setActivatedTool(eTools::ZONING, eSubTool_Zoning::RESIDENTIAL);
			}
			if (nk_sdf_hover_button(_ctx, _guiImages.zoning, eSubTool_Zoning::COMMERCIAL == activeSubTool)) {
				MinCity::UserInterface.setActivatedTool(eTools::ZONING, eSubTool_Zoning::COMMERCIAL);
			}
			if (nk_sdf_hover_button(_ctx, _guiImages.zoning, eSubTool_Zoning::INDUSTRIAL == activeSubTool)) {
				MinCity::UserInterface.setActivatedTool(eTools::ZONING, eSubTool_Zoning::INDUSTRIAL);
			}
			
			nk_layout_row_dynamic(_ctx, 36, cols);

			int32_t pop = MinCity::City->getPopulation();
			nk_property_int<true>(_ctx, "population ", 0, &pop, INT32_MAX, 1, 1);
		}
		nk_end(_ctx);
		
#ifdef DEBUG_LUT_WINDOW
		draw_lut_window(tNow, window_height + 2);
#endif
	}
	else { // ### PAUSED ### //
		static std::string szHint("");

		// resets and other states triggered by the unpaused -> paused transition
		if (bLastPaused) {
			_bHintReset = true;
			bLastPaused = false;
		}

		// required to prevent temporal display of incorrect "feedback/hint" text. This is triggered on a pause or opening of quit window
		if (_bHintReset) {
			szHint.clear();
			_bHintReset = false;
		}

		// this locally resets the "feedback/hint" text when there is not context set below only // done after below //
		bool bResetHint(eWindowType::QUIT == _uiWindowEnabled); // only set to true so that changes to the hint persist into the save & load window when selected

		if ( nk_canvas_begin(_ctx, "offscreen_overlay", _offscreen_canvas, nk_recti(0, 0, _frameBufferSize.x, _frameBufferSize.y),
			NK_WINDOW_BACKGROUND | NK_WINDOW_NO_INPUT, nk_rgba(0, 0, 0, 255)) ) {

			nk_draw_image(_offscreen_canvas->painter, _guiImages.offscreen);

		}
		nk_canvas_end(_ctx, _offscreen_canvas);

		// main bg window "paused" window
		constexpr eWindowName const bgwindowName(eWindowName::WINDOW_PAUSED);
		constexpr uint32_t const window_offset(1);
		uint32_t const window_width(_frameBufferSize.x - (window_offset << 1));
		constexpr uint32_t const window_height(180);

		if (nk_begin(_ctx, bgwindowName._to_string(),
			nk_recti(window_offset, _frameBufferSize.y - window_height, window_width, window_height),
			NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BACKGROUND | NK_WINDOW_NO_INPUT))
		{
			struct nk_rect const windowBounds(nk_window_get_bounds(_ctx));

			AddActiveWindowRect(r2D_set_by_width_height(windowBounds.x, windowBounds.y, windowBounds.w, windowBounds.h));	// must register any active window;

			nk_layout_row_dynamic(_ctx, 120, 1);

			{
				if (szHint.empty()) {

					static bool bBlink(false);
					static tTime tLast(zero_time_point);

					if (critical_now() - tLast > milliseconds(1000)) {
						bBlink = !bBlink;
						tLast = critical_now();
					}
					if (bBlink) {
						nk_label(_ctx, " PAUSED ", NK_TEXT_CENTERED);
					}
					else {
						nk_label(_ctx, "-      -", NK_TEXT_CENTERED);
					}
				}
				else {
					nk_label(_ctx, szHint.c_str(), NK_TEXT_CENTERED);
				}

				if (_uiPauseProgress) {
					nk_layout_row_dynamic(_ctx, 40, 1);
					
					nk_size progress(_uiPauseProgress);
					nk_progress(_ctx, &progress, 100, nk_false);
				}
			}
		}
		nk_end(_ctx); // end paused window

		// window query user to quit
		if (eWindowType::QUIT == _uiWindowEnabled)
		{
			// nested window //
			constexpr eWindowName const subwindowName(eWindowName::WINDOW_QUIT);

			constexpr uint32_t const subwindow_offset(20);
			uint32_t const subwindow_width(window_width - (subwindow_offset << 1));
			constexpr uint32_t const subwindow_height(window_height);

			static struct nk_rect const s = { subwindow_offset, _frameBufferSize.y - subwindow_height, subwindow_width, subwindow_height };

			if (nk_begin(_ctx, subwindowName._to_string(), s,
				NK_WINDOW_NO_SCROLLBAR))
			{
				struct nk_rect const windowBounds(nk_window_get_bounds(_ctx));

				AddActiveWindowRect(r2D_set_by_width_height(windowBounds.x, windowBounds.y, windowBounds.w, windowBounds.h));	// must register any active window;

				nk_layout_row_begin(_ctx, NK_DYNAMIC, subwindow_height, 3);

				{
					nk_layout_row_push(_ctx, 0.425f); // group "prompt" width

					nk_group_begin(_ctx, "quit prompt", 0);

					nk_layout_row_dynamic(_ctx, 30, 1);
					nk_label(_ctx, "exit game ?", NK_TEXT_LEFT);
					nk_layout_row_dynamic(_ctx, 32, 3);

					if (nk_button_label(_ctx, "quit")) {
						_iWindowQuitSelection = eWindowQuit::JUST_QUIT;

						MinCity::DispatchEvent(eEvent::QUIT);

						enableWindow<eWindowType::QUIT>(false);
						nk_window_close(_ctx, subwindowName._to_string());
					}
					if (nk_button_label(_ctx, "save & quit")) {
						_iWindowQuitSelection = eWindowQuit::SAVE_AND_QUIT;

						enableWindow<eWindowType::QUIT>(false);
						enableWindow<eWindowType::SAVE>(true);
						nk_window_close(_ctx, subwindowName._to_string());
					}
					if (nk_button_label(_ctx, "cancel")) {

						MinCity::DispatchEvent(eEvent::PAUSE, new bool(false));

						enableWindow<eWindowType::QUIT>(false);
						nk_window_close(_ctx, subwindowName._to_string());
					}

					nk_group_end(_ctx);
				}

				{
					nk_seperator(_ctx, 0.5355f);
				}

				{
					nk_layout_row_push(_ctx, 0.0375f); // group "menu" width
					nk_group_begin(_ctx, "menu", NK_WINDOW_BORDER|NK_WINDOW_NO_SCROLLBAR);

					static constexpr int32_t const cols(1);
					static constexpr int32_t const icon(36);
					nk_layout_row_static(_ctx, icon, icon, cols);

					{
						bool hovered(false);
						if (nk_sdf_hover_button(_ctx, _guiImages.create, false, &hovered)) {

							// todo
							//enableWindow<eWindowType::QUIT>(false);
							//enableWindow<eWindowType::SAVE>(true);
							//nk_window_close(_ctx, subwindowName._to_string());
						}
 						else if (hovered) {
							szHint = "New City";
							bResetHint = false;
						}
					}

					{
						bool hovered(false);
						if (nk_sdf_hover_button(_ctx, _guiImages.open, false, &hovered)) {

							enableWindow<eWindowType::QUIT>(false);
							enableWindow<eWindowType::LOAD>(true);
							nk_window_close(_ctx, subwindowName._to_string());
						}
						else if (hovered) {
							szHint = "Load City";
							bResetHint = false;
						}
					}

					{
						bool hovered(false);
						if (nk_sdf_hover_button(_ctx, _guiImages.save, false, &hovered)) {

							enableWindow<eWindowType::QUIT>(false);
							enableWindow<eWindowType::SAVE>(true);
							nk_window_close(_ctx, subwindowName._to_string());
						}
						else if (hovered) {
							szHint = "Save City";
							bResetHint = false;
						}
					}
					nk_group_end(_ctx);
				}

				nk_layout_row_end(_ctx);
			} // end quit/menu window
			nk_end(_ctx); 
		}
		else if (eWindowType::SAVE == _uiWindowEnabled) {

			// virtually-nested window //
			constexpr eWindowName const subwindowName(eWindowName::WINDOW_SAVE);

			constexpr uint32_t const subwindow_offset(20);
			uint32_t const subwindow_width((window_width - (subwindow_offset << 1)));
			constexpr uint32_t const subwindow_height(window_height);

			static constexpr int32_t const MAX_EDIT = 31;
			static char szEdit[MAX_EDIT + 1]{};
			static int32_t szLength(0);
			static bool bNewCity(false);

			static struct nk_rect const s = { subwindow_offset, _frameBufferSize.y - subwindow_height, subwindow_width, subwindow_height };

			if (nk_begin(_ctx, subwindowName._to_string(), s,
				NK_WINDOW_NO_SCROLLBAR))
			{
				struct nk_rect const windowBounds(nk_window_get_bounds(_ctx));

				AddActiveWindowRect(r2D_set_by_width_height(windowBounds.x, windowBounds.y, windowBounds.w, windowBounds.h));	// must register any active window;

				// resets for saving go here //
				if (_saveWindow.bReset) {

					szHint = " "; // make hint blank
					bResetHint = false;
					bNewCity = false;
					szLength = 0;
					memset(szEdit, 0, MAX_EDIT + 1);

					std::string_view const szCityName(MinCity::getCityName());

					bNewCity = (0 == szCityName.length()); // current save a new city

					_saveWindow.reset();
				}

				if (bNewCity) {

					nk_layout_row_begin(_ctx, NK_DYNAMIC, subwindow_height, 3);
						
					{
						nk_layout_row_push(_ctx, 0.3f);
						nk_group_begin(_ctx, "save thumbnail", 0);
						
						static constexpr int32_t const cols(1);
						static constexpr int32_t const thumbnail(subwindow_height); 
						//nk_layout_row_static(_ctx, thumbnail, thumbnail, cols);
						nk_layout_row_dynamic(_ctx, thumbnail, cols);

						nk_draw_image(_ctx, _guiImages.offscreen);

						nk_group_end(_ctx);
					}
						
					{
						nk_layout_row_push(_ctx, 0.4f); // group "prompt" width

						nk_group_begin(_ctx, "save prompt", 0);

						nk_layout_row_dynamic(_ctx, 30, 1);
						nk_label(_ctx, "save city", NK_TEXT_LEFT);

						nk_layout_row_dynamic(_ctx, 40, 1);
						nk_edit_focus(_ctx, 0);
						nk_flags const active = nk_edit_string(_ctx, NK_EDIT_SIMPLE | NK_EDIT_SELECTABLE | NK_EDIT_SIG_ENTER | NK_EDIT_GOTO_END_ON_ACTIVATE,
																szEdit, &szLength, MAX_EDIT, nk_filter_text_numbers);

						if ((NK_EDIT_ACTIVATED & active) || (NK_EDIT_ACTIVE & active)) {
							_bEditControlFocused = true; // used in input update
						}

						nk_layout_row_dynamic(_ctx, 32, 3);
						nk_spacing(_ctx, 1);
						
						if (nk_button_label(_ctx, "save") || (NK_EDIT_COMMITED & active)) {
							
							if (szLength > 0) {
								_iWindowSaveSelection = eWindowSave::SAVE;

								szHint = "SAVING...";
								bResetHint = false;
								
								MinCity::setCityName(szEdit);
								MinCity::DispatchEvent(eEvent::SAVE, new bool(eWindowQuit::SAVE_AND_QUIT == _iWindowQuitSelection));

								enableWindow<eWindowType::SAVE>(false);
								nk_window_close(_ctx, subwindowName._to_string());
							}

						}
						if (nk_button_label(_ctx, "cancel")) {

							szLength = 0; // reset

							MinCity::DispatchEvent(eEvent::PAUSE, new bool(false));

							enableWindow<eWindowType::SAVE>(false);
							nk_window_close(_ctx, subwindowName._to_string());
						}

						nk_group_end(_ctx);
					}

					{
						nk_seperator(_ctx, 0.3f);
					}

					nk_layout_row_end(_ctx);
				}
				else { // just show that its saving, no city name entry
					_iWindowSaveSelection = eWindowSave::SAVE;

					szHint = "SAVING...";
					bResetHint = false;
						
					MinCity::DispatchEvent(eEvent::SAVE, new bool(eWindowQuit::SAVE_AND_QUIT == _iWindowQuitSelection));

					enableWindow<eWindowType::SAVE>(false);
					nk_window_close(_ctx, subwindowName._to_string());
				}
			} // end save window
			nk_end(_ctx); 

			// special animated text over thumbnail //
			if (bNewCity) {
				
				std::string const szCityName( szEdit );
				size_t const checkedLength( SFM::min(szCityName.length(), szLength) );

				nk_text_animated(_ctx, s, 0.3f, _fonts[eNK_FONTS::SMALL], _fonts[eNK_FONTS::ENCRYPT_SMALL], _saveWindow.thumbnail_text_state,
					szCityName.substr(0, checkedLength), 
					fmt::format(FMT_STRING("population:  {:n}"), MinCity::City->getPopulation()), 
					fmt::format(FMT_STRING("cash:  {:n}"), MinCity::City->getCash()));
			}
		}
		else if (eWindowType::LOAD == _uiWindowEnabled) {

			// virtually-nested window //
			constexpr eWindowName const subwindowName(eWindowName::WINDOW_SAVE);

			constexpr uint32_t const subwindow_offset(20);
			uint32_t const subwindow_width((window_width - (subwindow_offset << 1)));
			constexpr uint32_t const subwindow_height(window_height);

			static std::string szSelectedCityName;
			static CityInfo infoSelectedCity{};

			static struct nk_rect const s = { subwindow_offset, _frameBufferSize.y - subwindow_height, subwindow_width, subwindow_height };

			if (nk_begin(_ctx, subwindowName._to_string(), s,
				NK_WINDOW_NO_SCROLLBAR))
			{
				struct nk_rect const windowBounds(nk_window_get_bounds(_ctx));

				AddActiveWindowRect(r2D_set_by_width_height(windowBounds.x, windowBounds.y, windowBounds.w, windowBounds.h));	// must register any active window;

				// resets for loading go here //
				if (_loadWindow.bReset) {

					szHint = " "; // make hint blank
					bResetHint = false;
					szSelectedCityName.clear();

					_loadWindow.reset();
				}

				nk_layout_row_begin(_ctx, NK_DYNAMIC, subwindow_height, 2);

				{
					nk_layout_row_push(_ctx, 0.3f);
					nk_group_begin(_ctx, "load thumbnail", 0);

					static constexpr int32_t const cols(1);
					static constexpr int32_t const thumbnail(subwindow_height); 
					//nk_layout_row_static(_ctx, thumbnail, thumbnail, cols);
					nk_layout_row_dynamic(_ctx, thumbnail, cols);

					nk_draw_image(_ctx, _guiImages.load_thumbnail);

					nk_group_end(_ctx);
				}

				{
					nk_layout_row_push(_ctx, 0.7f); // group "prompt" width

					nk_group_begin(_ctx, "load prompt", 0);

					nk_layout_row_dynamic(_ctx, 30, 1);
					nk_label(_ctx, "load city", NK_TEXT_LEFT);

					nk_layout_row_dynamic(_ctx, 96, 1);
					{
						nk_group_begin(_ctx, "load list", 0);

						nk_layout_row_dynamic(_ctx, 32, 4);

						int32_t bSelected(false);
						auto const& loadList = MinCity::VoxelWorld.getLoadList();
						for (auto const& city : loadList) {

							bSelected = (city == szSelectedCityName);
							if (nk_checkbox_label(_ctx, city.c_str(), &bSelected))	// returns on change in bSelected = true, no change = false
							{
								_loadWindow.thumbnail_text_state.reset(); // force reset everytime a new selection is made

								if (bSelected) { // now selected
									if (city != szSelectedCityName) { // if not already saved selection
										szSelectedCityName = city;

										// get thumbnail and other city info
										if (MinCity::VoxelWorld.PreviewWorld(szSelectedCityName, std::forward<CityInfo&&>(infoSelectedCity), _guiTextures.load_thumbnail.image)) {

											// setup type for static 2D image
											((nk_image_extended* const)_guiImages.load_thumbnail)->type = RGBA_IMAGE; // **this is a safe cast** memory is actually allocated as nk_image_extended

											// update texture for thumbnail from new imaging data returned from previewworld
											MinCity::TextureBoy.ImagingToTexture<false>(_guiTextures.load_thumbnail.image, _guiTextures.load_thumbnail.texture);
										}
									}
								}
								else { // now de-selected
									if (city == szSelectedCityName) { // if saved selection is current deselection
										szSelectedCityName.clear(); // reset selection
									}
								}
							}

						} // for

						// no selection ?
						if (szSelectedCityName.empty()) {

							// is this state new?
							if (((nk_image_extended* const)_guiImages.load_thumbnail)->type < ARRAY_IMAGE) {

								// setup thumbnail for default display of static animation
								((nk_image_extended* const)_guiImages.load_thumbnail)->type = ARRAY_IMAGE; // **this is a safe cast** memory is actually allocated as nk_image_extended

								uint32_t const select(PsuedoRandomNumber(0, sequenceImages::STATIC_IMAGE_COUNT - 1)); // indices

								_guiTextures.load_thumbnail.select = select;

								// update texture for thumbnail from new imaging data returned from previewworld
								MinCity::TextureBoy.ImagingSequenceToTexture(_guiTextures.load_thumbnail.sequence[select].sequence, _guiTextures.load_thumbnail.texture);

								// reset animation sequence
								_guiTextures.load_thumbnail.sequence[select].tAccumulateFrame = zero_time_duration;
								_guiTextures.load_thumbnail.sequence[select].frame = 0;
							}
							else { // animate sequence

								uint32_t const select(_guiTextures.load_thumbnail.select);

								fp_seconds const fTDelta(_ctx->delta_time_seconds);
								fp_seconds const fTDelay(milliseconds(_guiTextures.load_thumbnail.sequence[select].sequence->images[_guiTextures.load_thumbnail.sequence[select].frame].delay));

								// accurate frame timing
								if ((_guiTextures.load_thumbnail.sequence[select].tAccumulateFrame += fTDelta) >= fTDelay) {

									if (++_guiTextures.load_thumbnail.sequence[select].frame == _guiTextures.load_thumbnail.sequence[select].sequence->count) {
										_guiTextures.load_thumbnail.sequence[select].frame = 0;
									}

									_guiTextures.load_thumbnail.sequence[select].tAccumulateFrame -= fTDelay;

									// animation frame is hidden in type (push constant), shader will interpret as (frame = max(0, int32_t(type) - ARRAY_IMAGE))
									((nk_image_extended* const)_guiImages.load_thumbnail)->type = ARRAY_IMAGE + _guiTextures.load_thumbnail.sequence[select].frame; // **this is a safe cast** memory is actually allocated as nk_image_extended
								}

							}
						}
						nk_group_end(_ctx);
					}

					nk_layout_row_dynamic(_ctx, 32, 4);
					nk_spacing(_ctx, 2);

					if (nk_button_label(_ctx, "load")) {

						if (!szSelectedCityName.empty()) {
							_iWindowLoadSelection = eWindowLoad::LOAD;

							szHint = "LOADING...";
							bResetHint = false;

							MinCity::setCityName(szSelectedCityName);
							MinCity::DispatchEvent(eEvent::LOAD);

							enableWindow<eWindowType::LOAD>(false);
							nk_window_close(_ctx, subwindowName._to_string());
						}
					}
					if (nk_button_label(_ctx, "cancel")) {

						szSelectedCityName.clear(); // reset

						MinCity::DispatchEvent(eEvent::PAUSE, new bool(false));

						enableWindow<eWindowType::LOAD>(false);
						nk_window_close(_ctx, subwindowName._to_string());
					}

					nk_group_end(_ctx);
				}

				//{
				//	nk_seperator(_ctx, 0.3f);
				//}

				nk_layout_row_end(_ctx);

			} // end load window
			nk_end(_ctx); 

			// special animated text over thumbnail //
			if (!szSelectedCityName.empty()) {

				nk_text_animated(_ctx, s, 0.3f, _fonts[eNK_FONTS::SMALL], _fonts[eNK_FONTS::ENCRYPT_SMALL], _loadWindow.thumbnail_text_state,
					szSelectedCityName, 
					fmt::format(FMT_STRING("population:  {:n}"), infoSelectedCity.population), 
					fmt::format(FMT_STRING("cash:  {:n}"), infoSelectedCity.cash));
			}
		} // end load window active
		
		// ######################################################################################################################################## //

		// reset hint if not used
		if (bResetHint) {
			szHint.clear();
		}
	} // else paused



	/// debugging windows ///
#ifdef DEBUG_FPS_WINDOW
	static bool bMainWindowStartOpen(false), bCameraOpen(false), bInstancesOpen(false), bPerformanceOpen(false), bLightmapOpen(false);

	int32_t windowHeight = 240;
	if (bCameraOpen | bPerformanceOpen | bLightmapOpen) {
		windowHeight = SFM::max(windowHeight, 660);
	}
	else if (bInstancesOpen) {
		windowHeight = SFM::max(windowHeight, 300);
	}
	constexpr eWindowName const windowName(eWindowName::WINDOW_MINCITY);

	if (nk_begin_titled(_ctx, windowName._to_string(), 
		fmt::format(FMT_STRING("MINCITY  ({:.2f} s -- {:.1f} ms)"), fp_seconds(tLocal - start()).count(), fp_seconds(MinCity::Vulkan.frameTimeAverage()).count() * 1000.0f).c_str(),
		nk_recti(450, 50, 600, windowHeight),
		(!bMainWindowStartOpen ? NK_WINDOW_MINIMIZED : NK_WINDOW_BORDER) |
		NK_WINDOW_MINIMIZABLE | NK_WINDOW_TITLE))
	{
		{
			struct nk_rect const windowBounds(nk_window_get_bounds(_ctx));

			AddActiveWindowRect(r2D_set_by_width_height(windowBounds.x, windowBounds.y, windowBounds.w, windowBounds.h));	// must register any active window
		}
		//if (nk_tree_push(_ctx, NK_TREE_TAB, "Minimap", NK_MINIMIZED))
		//{
		//	nk_layout_row_static(_ctx, guiImages._miniMapImage->h, guiImages._miniMapImage->w, 1);

		//	struct nk_rect rectFocus;

		//	{;
		//	rect2D_t const visibleBounds = MinCity::VoxelWorld.getVisibleGridBounds();

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
		//			MinCity::VoxelWorld.setCameraOrigin(xmOrigin);
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
				xmResult = MinCity::VoxelWorld.getVolumetricOpacity().getDebugMin();
				XMStoreFloat3A(&vResult, xmResult);
				szText[0] = fmt::format(FMT_STRING("({:.1f}, {:.1f}, {:.1f})"), vResult.x, vResult.y, vResult.z);
				xmResult = MinCity::VoxelWorld.getVolumetricOpacity().getDebugMax();
				XMStoreFloat3A(&vResult, xmResult);
				szText[1] = fmt::format(FMT_STRING("({:.1f}, {:.1f}, {:.1f})"), vResult.x, vResult.y, vResult.z);
				tLast = tNow;
			}
			nk_text(_ctx, szText[0].c_str(), szText[0].length(), NK_TEXT_ALIGN_LEFT);
			nk_text(_ctx, szText[1].c_str(), szText[1].length(), NK_TEXT_ALIGN_LEFT);

			nk_layout_row_dynamic(_ctx, 25, 1);
			nk_property_int(_ctx, "slice>", 0, &MinCity::VoxelWorld.getVolumetricOpacity().getDebugSliceIndex(), MinCity::VoxelWorld.getVolumetricOpacity().getLightHeight() - 1, 1, 1);


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
		
		if (nk_tree_push(_ctx, NK_TREE_TAB, "Performance", NK_MAXIMIZED))
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

			nk_layout_row_push(_ctx, 375);
			NK_PUSH_TEXT_COLOR(_ctx, nk_rgb(255, 255, 255));
			szText = fmt::format(FMT_STRING("Grid Duration> {:d} us"), duration_cast<microseconds>(resolvedResult.grid_duration).count());
			nk_text(_ctx, szText.c_str(), szText.length(), NK_TEXT_ALIGN_LEFT);
			NK_POP_COLOR(_ctx);

			nk_layout_row_end(_ctx);

			nk_layout_row_begin(_ctx, NK_STATIC, 32, 2);

			nk_layout_row_push(_ctx, 375);
			NK_PUSH_TEXT_COLOR(_ctx, nk_rgb(255, 255, 255));
			szText = fmt::format(FMT_STRING("Operations> {:d}"), resolvedResult.total_operations);
			nk_text(_ctx, szText.c_str(), szText.length(), NK_TEXT_ALIGN_LEFT);
			NK_POP_COLOR(_ctx);

			nk_layout_row_push(_ctx, 375);
			NK_PUSH_TEXT_COLOR(_ctx, nk_rgb(255, 255, 255));
			szText = fmt::format(FMT_STRING("Threads> {:d}"), resolvedResult.getThreadCount());
			nk_text(_ctx, szText.c_str(), szText.length(), NK_TEXT_ALIGN_LEFT);
			NK_POP_COLOR(_ctx);

			nk_layout_row_end(_ctx);

			nk_layout_row_begin(_ctx, NK_STATIC, 32, 2);

			nk_layout_row_push(_ctx, 375);
			NK_PUSH_TEXT_COLOR(_ctx, nk_rgb(255, 255, 255));
			szText = fmt::format(FMT_STRING("Average Op Duration> {:d} us"), duration_cast<microseconds>(resolvedResult.avg_operation_duration).count());
			nk_text(_ctx, szText.c_str(), szText.length(), NK_TEXT_ALIGN_LEFT);
			NK_POP_COLOR(_ctx);

			nk_layout_row_push(_ctx, 375);
			NK_PUSH_TEXT_COLOR(_ctx, nk_rgb(255, 255, 255));
			szText = fmt::format(FMT_STRING("Max Op Duration> {:d} us"), duration_cast<microseconds>(resolvedResult.max_operation_duration).count());
			nk_text(_ctx, szText.c_str(), szText.length(), NK_TEXT_ALIGN_LEFT);
			NK_POP_COLOR(_ctx);

			nk_layout_row_end(_ctx);

			nk_layout_row_begin(_ctx, NK_STATIC, 32, 2);

			nk_layout_row_push(_ctx, 375);
			NK_PUSH_TEXT_COLOR(_ctx, nk_rgb(255, 255, 255));
			szText = fmt::format(FMT_STRING("Average Iterations> {:d}"), resolvedResult.avg_iterations);
			nk_text(_ctx, szText.c_str(), szText.length(), NK_TEXT_ALIGN_LEFT);
			NK_POP_COLOR(_ctx);

			nk_layout_row_push(_ctx, 375);
			NK_PUSH_TEXT_COLOR(_ctx, nk_rgb(255, 255, 255));
			szText = fmt::format(FMT_STRING("Max Iterations> {:d}"), resolvedResult.max_iterations);
			nk_text(_ctx, szText.c_str(), szText.length(), NK_TEXT_ALIGN_LEFT);
			NK_POP_COLOR(_ctx);

			nk_layout_row_end(_ctx);

			nk_layout_row_begin(_ctx, NK_STATIC, 32, 2);

			nk_layout_row_push(_ctx, 375);
			NK_PUSH_TEXT_COLOR(_ctx, nk_rgb(255, 255, 255));
			szText = fmt::format(FMT_STRING("Average Iteration Duration> {:d} us"), duration_cast<microseconds>(resolvedResult.avg_iteration_duration).count());
			nk_text(_ctx, szText.c_str(), szText.length(), NK_TEXT_ALIGN_LEFT);
			NK_POP_COLOR(_ctx);

			nk_layout_row_push(_ctx, 375);
			NK_PUSH_TEXT_COLOR(_ctx, nk_rgb(255, 255, 255));
			szText = fmt::format(FMT_STRING("Max Iteration Duration> {:d} us"), duration_cast<microseconds>(resolvedResult.max_iteration_duration).count());
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
		if (nk_tree_push(_ctx, NK_TREE_TAB, "Camera", NK_MINIMIZED))
		{
			XMFLOAT2A vIsoCenterOffset;
			std::string szText;

			XMStoreFloat2A(&vIsoCenterOffset, getDebugVariable(XMVECTOR, DebugLabel::CAMERA_FRACTIONAL_OFFSET));

			//NK_PUSH_FONT(_ctx, eNK_FONTS::CRISP);

			nk_layout_row_begin(_ctx, NK_STATIC, 32, 1);
			nk_layout_row_push(_ctx, 550);
			NK_PUSH_TEXT_COLOR(_ctx, nk_rgb(127, 0, 255));
			nk_label(_ctx, "x axis:", NK_TEXT_LEFT);
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
			nk_label(_ctx, "z axis:", NK_TEXT_LEFT);
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
				nk_label(_ctx, "voxel camera origin offset:", NK_TEXT_LEFT);
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
				szText = fmt::format(FMT_STRING("min({:f})"), vMin.x);
				nk_text_colored(_ctx, szText.c_str(), szText.length(), NK_TEXT_ALIGN_LEFT, nk_rgb_from_XMCOLOR(colorMinX));

				szText = fmt::format(FMT_STRING("max({:f})"), vMax.x);
				nk_text_colored(_ctx, szText.c_str(), szText.length(), NK_TEXT_ALIGN_LEFT, nk_rgb_from_XMCOLOR(colorMaxX));


				szText = fmt::format(FMT_STRING("min({:f})"), vMin.y);
				nk_text_colored(_ctx, szText.c_str(), szText.length(), NK_TEXT_ALIGN_LEFT, nk_rgb_from_XMCOLOR(colorMinY));

				szText = fmt::format(FMT_STRING("max({:f})"), vMax.y);
				nk_text_colored(_ctx, szText.c_str(), szText.length(), NK_TEXT_ALIGN_LEFT, nk_rgb_from_XMCOLOR(colorMaxY));
			}

			nk_layout_row_dynamic(_ctx, 20, 1);
			{
				NK_PUSH_TEXT_COLOR(_ctx, nk_rgb(127, 0, 255));
				nk_label(_ctx, "voxel world origin:", NK_TEXT_LEFT);
				NK_POP_COLOR(_ctx);

				nk_layout_row_dynamic(_ctx, 25, 2);

				std::string szText;

				XMFLOAT2A voxelWorldOrigin;
				XMStoreFloat2A(&voxelWorldOrigin, MinCity::VoxelWorld.getOrigin());

				szText = fmt::format(FMT_STRING("{:f}"), voxelWorldOrigin.x);
				nk_text(_ctx, szText.c_str(), szText.length(), NK_TEXT_ALIGN_LEFT);

				szText.clear();

				szText = fmt::format(FMT_STRING("{:f}"), voxelWorldOrigin.y);
				nk_text(_ctx, szText.c_str(), szText.length(), NK_TEXT_ALIGN_LEFT);
			}

			nk_layout_row_dynamic(_ctx, 20, 1);
			{
				NK_PUSH_TEXT_COLOR(_ctx, nk_rgb(127, 0, 255));
				nk_label(_ctx, "voxel mouse hover index:", NK_TEXT_LEFT);
				NK_POP_COLOR(_ctx);

				nk_layout_row_dynamic(_ctx, 32, 2);

				std::string szText;

				point2D_t const voxelIndexHovered(MinCity::VoxelWorld.getHoveredVoxelIndex());

				szText = fmt::format(FMT_STRING("{:d}"), voxelIndexHovered.x);
				nk_text(_ctx, szText.c_str(), szText.length(), NK_TEXT_ALIGN_LEFT);

				szText.clear();

				szText = fmt::format(FMT_STRING("{:d}"), voxelIndexHovered.y);
				nk_text(_ctx, szText.c_str(), szText.length(), NK_TEXT_ALIGN_LEFT);
			}
			//NK_POP_FONT(_ctx);
		} // bCameraOpen

		if (nk_tree_push(_ctx, NK_TREE_TAB, "Instances", NK_MINIMIZED))
		{
			nk_layout_row_dynamic(_ctx, 32, 3);
			{
				NK_PUSH_TEXT_COLOR(_ctx, nk_rgb(127, 0, 255));
				nk_label(_ctx, "dynamic", NK_TEXT_LEFT);
				nk_label(_ctx, "static", NK_TEXT_LEFT);
				nk_label(_ctx, "root indices", NK_TEXT_LEFT);
				NK_POP_COLOR(_ctx);

				nk_layout_row_dynamic(_ctx, 32, 3);

				std::string szText;

				szText = fmt::format(FMT_STRING("{:d}"), MinCity::VoxelWorld.numDynamicModelInstances());
				nk_text(_ctx, szText.c_str(), szText.length(), NK_TEXT_ALIGN_LEFT);

				szText.clear();

				szText = fmt::format(FMT_STRING("{:d}"), MinCity::VoxelWorld.numStaticModelInstances());
				nk_text(_ctx, szText.c_str(), szText.length(), NK_TEXT_ALIGN_LEFT);

				szText.clear();

				szText = fmt::format(FMT_STRING("{:d}"), MinCity::VoxelWorld.numRootIndices());
				nk_text(_ctx, szText.c_str(), szText.length(), NK_TEXT_ALIGN_LEFT);

				szText.clear();
			}
			nk_tree_pop(_ctx);
			bInstancesOpen = true;
		}
		else {
			bInstancesOpen = false;
		}
#endif
		bMainWindowStartOpen = true;
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
		if (nk_begin_titled(_ctx, windowName._to_string(), szDebugMessage.c_str(), nk_rect(0, 0, Globals::DEFAULT_SCREEN_WIDTH, 24),
			NK_WINDOW_NO_INPUT | NK_WINDOW_BACKGROUND |
			NK_WINDOW_TITLE | NK_WINDOW_MINIMIZABLE ))
		{

		}
		nk_style_pop_color(_ctx);
		nk_style_pop_color(_ctx);
		nk_style_pop_float(_ctx);
		nk_end(_ctx);

		tLast = tNow;
	}
#endif
#endif /*DEBUG_FPS_WINDOW*/

	tLocalLast = tLocal;
}

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
	SAFE_DELETE(_guiTextures.create);
	SAFE_DELETE(_guiTextures.open);
	SAFE_DELETE(_guiTextures.save);
	SAFE_DELETE(_guiTextures.road);
	SAFE_DELETE(_guiTextures.zoning);
	SAFE_DELETE(_guiTextures.load_thumbnail.texture);

	// sequence images
	for (uint32_t i = 0; i < sequenceImages::STATIC_IMAGE_COUNT; ++i) {
		if (_sequenceImages.static_screen[i]) {
			ImagingDelete(_sequenceImages.static_screen[i]);
		}
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

#ifndef NDEBUG	
void cNuklear::debug_out_nuklear(std::string const& message)
{
	tbb::spin_mutex::scoped_lock lock(lock_debug_message);
	_szDebugMessage = message;
}
void debug_out_nuklear(std::string const message)
{
	MinCity::Nuklear.debug_out_nuklear(message);
}
void debug_out_nuklear_off()
{
	std::string const szEmpty;
	MinCity::Nuklear.debug_out_nuklear(szEmpty);
}
#endif