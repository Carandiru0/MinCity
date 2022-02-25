#pragma once

#include "cNonUpdateableGameObject.h"
#include <Utility/type_colony.h>
#include "ImageAnimation.h"
#include <map>

// forward decl
namespace Volumetric
{
	namespace voxB
	{
		template<bool const Dynamic>
		class voxelModel;

		struct voxelDescPacked;
	}
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
			return(color < rhs.color);
		}

	} ImportColor;
	typedef struct ImportProxy
	{
		using colormap = vector<ImportColor>;
		
		Volumetric::voxB::voxelDescPacked* __restrict voxels;
		uint32_t									  numVoxels;
		
		voxB::voxelModelBase* model;

		colormap		colors;
		ImportColor		active_color;
		float			inv_start_color_count;

		bool			lights_on = true;

		void reset(uint32_t const numVoxels_) {

			if (voxels) {
				scalable_aligned_free(voxels);
				voxels = nullptr;
			}

			numVoxels = numVoxels_;
			voxels = (Volumetric::voxB::voxelDescPacked* const __restrict)scalable_aligned_malloc(numVoxels_ * sizeof(Volumetric::voxB::voxelDescPacked), alignof(Volumetric::voxB::voxelDescPacked));

			model = nullptr;

			colors.clear();
			active_color.color = 0;
			active_color.count = 0;
			inv_start_color_count = 0.0f;
			lights_on = true;
		}

		void apply_color(colormap::iterator const& iter_current_color);
		void apply_material(colormap::iterator const& iter_current_color);
		void load(voxB::voxelModelBase const* const model);
		void save(std::string_view const name) const;

	} ImportProxy;
}

// these classes are intentionally simple to support a minimal amount of difference between the two types that had to be seperate. (maintainability)
namespace world
{
	class cImportGameObject
	{
	public:
		static Volumetric::ImportProxy&		getProxy() { return(_proxy); }
	protected:
		static Volumetric::ImportProxy		_proxy;
		
	public:
		cImportGameObject() = default;
	};

	class cImportGameObject_Dynamic : public tNonUpdateableGameObject<Volumetric::voxelModelInstance_Dynamic>, public type_colony<cImportGameObject_Dynamic>, private cImportGameObject
	{
	public:
		constexpr virtual types::game_object_t const to_type() const override {
			return(types::game_object_t::NonSaveable);
		}
		
		static VOXEL_EVENT_FUNCTION_RETURN __vectorcall OnVoxel(VOXEL_EVENT_FUNCTION_PARAMETERS);
		VOXEL_EVENT_FUNCTION_RETURN __vectorcall OnVoxel(VOXEL_EVENT_FUNCTION_RESOLVED_PARAMETERS) const;

	public:
		cImportGameObject_Dynamic(cImportGameObject_Dynamic&& src) noexcept;
		cImportGameObject_Dynamic& operator=(cImportGameObject_Dynamic&& src) noexcept;
	private:
		ImageAnimation*						_videoscreen;
	public:
		cImportGameObject_Dynamic(Volumetric::voxelModelInstance_Dynamic* const __restrict& __restrict instance_);
		~cImportGameObject_Dynamic();
	};

	STATIC_INLINE_PURE void swap(cImportGameObject_Dynamic& __restrict left, cImportGameObject_Dynamic& __restrict right) noexcept
	{
		cImportGameObject_Dynamic tmp{ std::move(left) };
		left = std::move(right);
		right = std::move(tmp);

		left.revert_free_ownership();
		right.revert_free_ownership();
	}

	class cImportGameObject_Static : public tNonUpdateableGameObject<Volumetric::voxelModelInstance_Static>, public type_colony<cImportGameObject_Static>, private cImportGameObject
	{
	public:
		constexpr virtual types::game_object_t const to_type() const override {
			return(types::game_object_t::NonSaveable);
		}

		static VOXEL_EVENT_FUNCTION_RETURN __vectorcall OnVoxel(VOXEL_EVENT_FUNCTION_PARAMETERS);
		VOXEL_EVENT_FUNCTION_RETURN __vectorcall OnVoxel(VOXEL_EVENT_FUNCTION_RESOLVED_PARAMETERS) const;

	public:
		cImportGameObject_Static(cImportGameObject_Static&& src) noexcept;
		cImportGameObject_Static& operator=(cImportGameObject_Static&& src) noexcept;
	private:
		ImageAnimation*						_videoscreen;
	public:
		cImportGameObject_Static(Volumetric::voxelModelInstance_Static* const __restrict& __restrict instance_);
		~cImportGameObject_Static();
	};

	STATIC_INLINE_PURE void swap(cImportGameObject_Static& __restrict left, cImportGameObject_Static& __restrict right) noexcept
	{
		cImportGameObject_Static tmp{ std::move(left) };
		left = std::move(right);
		right = std::move(tmp);

		left.revert_free_ownership();
		right.revert_free_ownership();
	}

	
 } // end ns


