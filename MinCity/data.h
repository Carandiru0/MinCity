#pragma once

// This file is common to saveworld.cpp & loadworld.cpp only
// describes the raw structures that apply to the file format used for saving and loading
#define SAVE_DIR L"saves\\"
#define CITY_EXT L".city"

typedef struct voxelWorldDesc
{
	char			tag[4];
	uint32_t		name_length;
	size_t			grid_compressed_size;
	uint32_t		voxel_count;
	
} voxelWorldDesc;

static constexpr uint32_t const 
	offscreen_thumbnail_width(456), 
	offscreen_thumbnail_height(256); // 16:9 default thumbnail size