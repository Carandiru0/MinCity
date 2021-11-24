#include "pch.h"
#include "cRockStageGameObject.h"
#include "voxelModelInstance.h"

#ifdef GIF_MODE

namespace world
{
	static void OnRelease(void const* const __restrict _this) // private to this file
	{
		if (_this) {
			cRockStageGameObject::remove(static_cast<cRockStageGameObject const* const>(_this));
		}
	}

	cRockStageGameObject::cRockStageGameObject(cRockStageGameObject&& src) noexcept
		: tUpdateableGameObject(std::forward<tUpdateableGameObject&&>(src)), _odd_mini_light(src._odd_mini_light)
	{
		src.free_ownership();

		// important
		if (Instance && *Instance) {
			(*Instance)->setOwnerGameObject<cRockStageGameObject>(this, &OnRelease);
			(*Instance)->setVoxelEventFunction(&cRockStageGameObject::OnVoxel);
		}
		// important
		if (src.Instance && *src.Instance) {
			(*src.Instance)->setOwnerGameObject<cRockStageGameObject>(nullptr, nullptr);
			(*src.Instance)->setVoxelEventFunction(nullptr);
		}
	}
	cRockStageGameObject& cRockStageGameObject::operator=(cRockStageGameObject&& src) noexcept
	{
		tUpdateableGameObject::operator=(std::forward<tUpdateableGameObject&&>(src));

		src.free_ownership();

		// important
		if (Instance && *Instance) {
			(*Instance)->setOwnerGameObject<cRockStageGameObject>(this, &OnRelease);
			(*Instance)->setVoxelEventFunction(&cRockStageGameObject::OnVoxel);
		}
		// important
		if (src.Instance && *src.Instance) {
			(*src.Instance)->setOwnerGameObject<cRockStageGameObject>(nullptr, nullptr);
			(*src.Instance)->setVoxelEventFunction(nullptr);
		}

		return(*this);
	}

	cRockStageGameObject::cRockStageGameObject(Volumetric::voxelModelInstance_Static* const __restrict& __restrict instance_)
		: tUpdateableGameObject(instance_), _glass_color(MASK_GLASS_COLOR), _bulb_color(MASK_BULB_COLOR), _mini_light_color(0xffffff),
		_accumulator(0.0f), _direction(1.0f),
		_accumulator_strobe{},
		_odd_mini_light(PsuedoRandom5050())
	{
		instance_->setOwnerGameObject<cRockStageGameObject>(this, &OnRelease);
		instance_->setVoxelEventFunction(&cRockStageGameObject::OnVoxel);
	}

	// If currently visible event:
	Volumetric::voxB::voxelState const cRockStageGameObject::OnVoxel(FXMVECTOR xmIndex, Volumetric::voxB::voxelDescPacked& __restrict voxel, Volumetric::voxB::voxelState const& __restrict rOriginalVoxelState, void const* const __restrict _this, uint32_t const vxl_index)
	{
		return(reinterpret_cast<cRockStageGameObject const* const>(_this)->OnVoxel(xmIndex, voxel, rOriginalVoxelState, vxl_index));
	}
	// ***** watchout - thread safety is a concern here this method is executed in parallel ******
	Volumetric::voxB::voxelState const cRockStageGameObject::OnVoxel(FXMVECTOR xmIndex, Volumetric::voxB::voxelDescPacked& __restrict voxel, Volumetric::voxB::voxelState const& __restrict rOriginalVoxelState, uint32_t const vxl_index) const
	{
		Volumetric::voxelModelInstance_Static const* const __restrict instance(getModelInstance());

		Volumetric::voxB::voxelState voxelState(rOriginalVoxelState);

#ifdef GIF_MODE

		if (MASK_GLASS_COLOR == voxel.Color) {
			if (voxelState.Transparent) {
				voxel.Color = _glass_color;
			}
			else {
				voxel.Color = _mini_light_color;
			}
		}
		else if (MASK_BULB_COLOR == voxel.Color) {
			voxel.Color = _bulb_color;
		}

#else


#endif
		return(voxelState);
	}

	void cRockStageGameObject::OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta)
	{
		_accumulator += tDelta.count() * _direction * 0.333f;

		if (_direction >= 0.0f) {
			if (_accumulator >= 1.0f) {

				_direction = -_direction;
			}
		}
		else {
			if (_accumulator <= 0.0f) {

				_direction = -_direction;
			}
		}

		uvec4_v const black_body(MinCity::VoxelWorld.blackbody(_accumulator));
		uvec4_t color;

		SFM::unpack_rgba(BULB_COLOR, color);

		uvec4_v bulb_color(color);
		bulb_color = SFM::modulate(bulb_color, black_body);

		bulb_color.rgba(color);
		_bulb_color = SFM::pack_rgba(color);

		SFM::unpack_rgba(GLASS_COLOR, color);
		color.a = SFM::float_to_u8(_accumulator * 0.5f);

		uvec4_v glass_color(color);
		glass_color = SFM::blend(glass_color, bulb_color);

		glass_color.rgba(color);
		_glass_color = SFM::pack_rgba(color);

		{
			fp_seconds const STROBE_TIME = fp_seconds(milliseconds(150)) * _accumulator;
			float const INV_STROBE_TIME = 1.0f / fp_seconds(STROBE_TIME).count();

			if ((_accumulator_strobe += tDelta) >= fp_seconds(STROBE_TIME)) {

				_accumulator_strobe -= fp_seconds(STROBE_TIME);
			}

			float start(0.0f), end(1.0f);
			if (_odd_mini_light) {
				std::swap(start, end);
			}
			//float const light = 1.0f - SFM::ease_inout_circular(start, end, _accumulator_strobe.count() * INV_STROBE_TIME);
			static constexpr float const
				o = 2.7f,
				p = -8.0f,
				n = 2.9f;

			float light;
			float const t(_accumulator_strobe.count());

			light = SFM::__exp2(-n * t) * SFM::__cos(o * n * t) + p * t;
			light = SFM::max(0.0f, light) + SFM::max(0.0f, -light);

			uint32_t const luma = SFM::float_to_u8(1.0f - light);
			_mini_light_color = SFM::pack_rgba(luma);
		}
	}
} // end ns world


#endif
