#include "pch.h"
#include "cSelectTool.h"
#include "cVoxelWorld.h"
#include "cBuildingGameObject.h"
#include "MinCity.h"

cSelectTool::cSelectTool()
	: _selectedVoxelIndex{}, _selectedInstanceHash(0), _selectedDynamic(false)
{

}

void cSelectTool::KeyAction(int32_t const key, bool const down, bool const ctrl)
{
	static constexpr float const 
		MOVE_SELECTION_SPEED = 15.0f,
		ROTATE_SELECTION_SPEED = 2.0f;

	if (_selectedDynamic && 0 != _selectedInstanceHash) {

		auto const instance = MinCity::VoxelWorld.lookupVoxelModelInstance<true>(_selectedInstanceHash);

		if (instance) {

			v2_rotation_t azimuth(world::getAzimuth());
			XMVECTOR vR(azimuth.v2());
			vR = SFM::sgn(vR);
			azimuth = XMVector2Normalize(vR);


			XMVECTOR const xmLoc(instance->getLocation());
			float fElevation = instance->getElevation();

			float const delta(fp_seconds(fixed_delta_duration).count());

			switch (key)
			{
			case GLFW_KEY_UP:
				if (down) { // pressed
					XMVECTOR xmDisplacement = XMVectorSet(0.0f, delta * MOVE_SELECTION_SPEED, 0.0f, 0.0f);
					// orient displacement in direction of camera
					xmDisplacement = v2_rotate(xmDisplacement, azimuth);
					instance->setLocation(XMVectorAdd(xmLoc, xmDisplacement));
				}
				break;
			case GLFW_KEY_DOWN:
				if (down) { // pressed
					XMVECTOR xmDisplacement = XMVectorSet(0.0f, -delta * MOVE_SELECTION_SPEED, 0.0f, 0.0f);
					// orient displacement in direction of camera
					xmDisplacement = v2_rotate(xmDisplacement, azimuth);
					instance->setLocation(XMVectorAdd(xmLoc, xmDisplacement));;
				}
				break;
			case GLFW_KEY_LEFT:
				if (down) { // pressed

					if (ctrl) {
						instance->setAzimuth(instance->getAzimuth() + delta * ROTATE_SELECTION_SPEED);
					}
					else {
						XMVECTOR xmDisplacement = XMVectorSet(-delta * MOVE_SELECTION_SPEED, 0.0f, 0.0f, 0.0f);
						// orient displacement in direction of camera
						xmDisplacement = v2_rotate(xmDisplacement, azimuth);
						instance->setLocation(XMVectorAdd(xmLoc, xmDisplacement));
					}
				}
				break;
			case GLFW_KEY_RIGHT:
				if (down) { // pressed

					if (ctrl) {
						instance->setAzimuth(instance->getAzimuth() - delta * ROTATE_SELECTION_SPEED);
					}
					else {
						XMVECTOR xmDisplacement = XMVectorSet(delta * MOVE_SELECTION_SPEED, 0.0f, 0.0f, 0.0f);
						// orient displacement in direction of camera
						xmDisplacement = v2_rotate(xmDisplacement, azimuth);
						instance->setLocation(XMVectorAdd(xmLoc, xmDisplacement));
					}
				}
				break;
			case GLFW_KEY_PAGE_UP:
				if (down) { // pressed
					fElevation += delta * MOVE_SELECTION_SPEED;
					instance->setElevation(fElevation);
				}
				break;
			case GLFW_KEY_PAGE_DOWN:
				if (down) { // pressed
					fElevation -= delta * MOVE_SELECTION_SPEED;
					instance->setElevation(fElevation);
				}
				break;
			default:
				// no further processing by this tool
				break;
			}
#ifndef NDEBUG
			point2D_t const voxelIndex(instance->getVoxelIndex());
			FMT_NUKLEAR_DEBUG(false, "voxelIndex({:d}, {:d})  elevation({:f})  angle({:f})  ", voxelIndex.x, voxelIndex.y, instance->getElevation(), instance->getAzimuth().angle());
#endif
		}
	}
}

void cSelectTool::clear_selection()
{
	if (0 != _selectedInstanceHash) {

		if (_selectedDynamic) {
			auto const instance = MinCity::VoxelWorld.lookupVoxelModelInstance<true>(_selectedInstanceHash);
			if (instance) {
				instance->setHighlighted(false);
			}
		}
		else {
			auto const instance = MinCity::VoxelWorld.lookupVoxelModelInstance<false>(_selectedInstanceHash);
			if (instance) {
				instance->setHighlighted(false);
			}
		}

		_selectedVoxelIndex = point2D_t{}; // reset
		_selectedInstanceHash = 0;
	}
}

void __vectorcall cSelectTool::ClickAction(FXMVECTOR const xmMousePos)
{
	// clear selection
	clear_selection();

	// indicate selection ...
	if (MinCity::VoxelWorld.isHoveredVoxelIndexOk()) {

		uint32_t const mouseBufferMode(MinCity::Vulkan.getMouseBufferMode());
		point2D_t const voxelIndex(MinCity::VoxelWorld.getHoveredVoxelIndex());

		Iso::Voxel const* const pVoxel = world::getVoxelAt(voxelIndex);

		if (pVoxel) {

			Iso::Voxel const oVoxel(*pVoxel);

			if (Iso::isOwnerAny(oVoxel)) {

				uint32_t hash(0), index(0);

				uint32_t const start(eMouseBufferMode::STATIC_VOXELS == (eMouseBufferMode::STATIC_VOXELS & mouseBufferMode) ? Iso::STATIC_HASH : Iso::DYNAMIC_HASH);
				uint32_t const end(eMouseBufferMode::DYNAMIC_VOXELS == (eMouseBufferMode::DYNAMIC_VOXELS & mouseBufferMode) ? Iso::HASH_COUNT : Iso::DYNAMIC_HASH);
				for (uint32_t i = start; i < end; ++i) {

					if (Iso::isOwner(oVoxel, i)) {

						// get hash, which should be the voxel model instance ID
						hash = Iso::getHash(oVoxel, i);
						index = i;
					}
				}
				// will always select dynamic over static if both are enabled in mousebuffer mode
				if (0 != hash) {

					if (Iso::STATIC_HASH == index) {
						auto const instance = MinCity::VoxelWorld.lookupVoxelModelInstance<false>(hash);

						if (instance) {
							instance->setHighlighted(true);

							_selectedInstanceHash = hash;
							_selectedVoxelIndex = voxelIndex;
							_selectedDynamic = false;
						}
					}
					else {
						auto const instance = MinCity::VoxelWorld.lookupVoxelModelInstance<true>(hash);

						if (instance) {
							instance->setHighlighted(true);

							_selectedInstanceHash = hash;
							_selectedVoxelIndex = voxelIndex;
							_selectedDynamic = true;
						}
					}

					
				}
			}
		}
	}
}

// these functions should be defined last
void cSelectTool::deactivate()
{
	clear_selection();
	MinCity::Vulkan.setMouseBufferMode(eMouseBufferMode::GROUND_VOXELS);
}

void cSelectTool::activate()
{
	MinCity::Vulkan.setMouseBufferMode(eMouseBufferMode::DYNAMIC_VOXELS | eMouseBufferMode::STATIC_VOXELS);
}


