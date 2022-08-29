#pragma once

// This file is common to saveworld.cpp & loadworld.cpp only
// describes the raw structures that apply to the file format used for saving and loading
#define CITY_EXT L".city"

// ** absolutely no specially aligned types (eg.) XMFLOAT3A) allowed in any of these data structures ** //
static constexpr uint32_t const file_delim_zero_count(64);

typedef struct voxelWorldDesc
{
	char			tag[4];
	uint64_t		secure_seed;
	uint32_t		name_length;
	size_t			grid_compressed_size;
	uint32_t		voxel_count;
	
} voxelWorldDesc;

static constexpr uint32_t const 
	offscreen_thumbnail_width(456), 
	offscreen_thumbnail_height(256); // 16:9 default thumbnail size


struct model_root_index {
	uint32_t	   hash;
	point2D_t	   voxelIndex;

};
template<bool const Dynamic>	// do not use directly, use derived classes model_state_instance_static & model_state_instance_dynamic instead.
struct model_state_instance {
	uint32_t									hash;
	uint32_t									gameobject_type;
	Volumetric::voxB::voxelModelIdent<Dynamic>	identity;
	// additional *common* varying model instance data
	float										elevation;

	model_state_instance()
		: hash(0), gameobject_type(0), identity{}, elevation(0.0f)
	{}

	explicit model_state_instance(uint32_t const hash_, uint32_t const gameobject_type_, Volumetric::voxB::voxelModelIdent<Dynamic> const& identity_)
		: hash(hash_), gameobject_type(gameobject_type_), identity(identity_)
	{}
};

struct model_state_instance_static : model_state_instance<false> {

	// additional varying static model instance data

	model_state_instance_static()
	{}

	explicit model_state_instance_static(uint32_t const hash_, uint32_t const gameobject_type_, Volumetric::voxB::voxelModelIdent<false> const& identity_)
		: model_state_instance(hash_, gameobject_type_, identity_)
	{}
};

struct model_state_instance_dynamic : model_state_instance<true> {

	// additional varying dynamic model instance data
	XMFLOAT2 location;
	XMFLOAT3 roll, pitch, yaw;

	model_state_instance_dynamic()
		: location{}
	{}

	explicit model_state_instance_dynamic(uint32_t const hash_, uint32_t const gameobject_type_, Volumetric::voxB::voxelModelIdent<true> const& identity_)
		: model_state_instance(hash_, gameobject_type_, identity_), location{}
	{}
};