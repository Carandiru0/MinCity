#pragma once

// forward decl
namespace world
{
	class cImportGameObject;
}

namespace Volumetric
{
	typedef struct ImportMaterial // half of what Volumetric::voxB::voxelDescPacked is, same order
	{
		union
		{
			struct
			{
				uint32_t // (largest type uses 24 bits)
					Color : 24,						// RGB 16.78 million voxel colors
					Video : 1,						// Videoscreen
					Emissive : 1,					// Emission
					Transparent : 1,				// Transparency
					Metallic : 1,					// Metallic
					Roughness : 4;					// Roughness
			};

			uint32_t RGBM;
		};

	} ImportMaterial;

	typedef struct ImportColor
	{
		union
		{
			uint32_t color;				// if color is set naively, the material bits will be reset. all modifications to color must happen prior to material modifications for this to work. (union) this also initializes the material bits to zero due to the strict order.
			ImportMaterial material;
		};
		uint32_t count;


		ImportColor()
			: color(0), count(0)
		{}
		ImportColor(uint32_t const color_)
			: color(color_), count(0)
		{}
		__inline bool const operator<(ImportColor const& rhs) {  // for std::lower_bound & std::binary_search
			return(material.Color < rhs.material.Color);
		}

	} ImportColor;
	typedef struct ImportProxy
	{
		using colormap = vector<ImportColor>;

		voxB::voxelModelBase*			model;
		world::cImportGameObject*		game_object;

		colormap::iterator	previous;
		colormap			colors;
		ImportColor			active_color;
		float				inv_start_color_count;

		DirectX::BoundingSphere bounds;

		void reset(world::cImportGameObject* const game_object_);
		void apply_material(colormap::iterator const& iter_current_color);
		void load(world::cImportGameObject* const game_object_, voxB::voxelModelBase const* const model);
		void save(std::string_view const name) const;

	} ImportProxy;
}