#include "pch.h"
#include "voxelModelInstance.h"
#include "cVoxelWorld.h"

namespace Volumetric
{
	voxelModelInstanceBase::voxelModelInstanceBase(uint32_t const hash, point2D_t const voxelIndex, uint32_t const flags_)
		: hashID(hash), flags(flags_),
		tCreation(now()), tDestruction{},
		vLoc{}, fElevation(0.0f),
		child(nullptr), 
		owner_gameobject_type{}, owner_gameobject(nullptr), eOnRelease(nullptr),
		tSequenceLengthCreation(Konstants::CREATION_SEQUENCE_LENGTH), tSequenceLengthDestruction(Konstants::DESTRUCTION_SEQUENCE_LENGTH)  // sequence length for destruction is scaled by height of voxel model
	{
		XMStoreFloat2A(&vLoc, p2D_to_v2(voxelIndex));

		// by default all instances are brought to elevation that matches the ground. This is only done once on initialization. It is intentionally not updated/frame. 
		Iso::Voxel const* const pVoxel = world::getVoxelAt(voxelIndex);
		if (pVoxel) {
			Iso::Voxel const oVoxel(*pVoxel);

			fElevation = Iso::getRealHeight(oVoxel);
		}
	} 

	voxelModelInstance_Dynamic::voxelModelInstance_Dynamic(voxB::voxelModel<voxB::DYNAMIC> const& __restrict refModel, uint32_t const hash, point2D_t const voxelIndex, uint32_t const flags_)
		: voxelModelInstance(refModel, hash, voxelIndex, flags_)
	{
	}

	voxelModelInstance_Static::voxelModelInstance_Static(voxB::voxelModel<voxB::STATIC> const& __restrict refModel, uint32_t const hash, point2D_t const voxelIndex, uint32_t const flags_)
		: voxelModelInstance(refModel, hash, voxelIndex, flags_)
	{
	}

	void voxelModelInstanceBase::destroyInstance() const
	{
		if (hashID) {
			Volumetric::VolumetricLink->World.destroyVoxelModelInstance(hashID);
		}
		// instance will not be rendered anymore, deletion is managed by World instance cleanup queue
	}

	void __vectorcall voxelModelInstance_Dynamic::synchronize(FXMVECTOR const xmLoc, v2_rotation_t const vAzi)  // only for dynamic instances
	{
		if (hashID) {

			point2D_t* const __restrict pRootVoxel(Volumetric::VolumetricLink->World.acquireVoxelModelInstanceRootIndex(hashID));

			if (nullptr != pRootVoxel) {

				point2D_t const old_rootVoxel(*pRootVoxel);
				point2D_t const new_rootVoxel(v2_to_p2D(xmLoc)); // matches getVoxelIndex() method

				// this could be a slight move (fractional) that does not change the voxelindex (integer)
				if (new_rootVoxel != old_rootVoxel || vAzi != _vYaw) {

					Iso::Voxel const* const pVoxelNew = world::getVoxelAt(new_rootVoxel);
					if (nullptr != pVoxelNew) {
						
						// clear and set areas 1st, then update root voxels
						rect2D_t const vLocalArea(getModel()._LocalArea);

						// clear old area of the hash id only			   
						world::resetVoxelsHashAt(r2D_add(vLocalArea, old_rootVoxel), hashID, _vYaw); // using old location & Yaw

						/// ////////////////////////////////////////////////////////////////////////////////////
						Iso::Voxel oVoxelNew(*pVoxelNew);
						uint32_t const new_index(Iso::getNextAvailableHashIndex<true>(oVoxelNew)); // must capture index here 1st

						// set new area of the hash id only
						world::setVoxelsHashAt(r2D_add(vLocalArea, new_rootVoxel), hashID, vAzi); // using new location & Yaw

						// update the new owner //
						Iso::setAsOwner(oVoxelNew, new_index);
						Iso::setHash(oVoxelNew, new_index, hashID);

						// do not need to check return as previously did already when getVoxelAt was called, and non-nullptr was returned
						world::setVoxelAt(new_rootVoxel, std::forward<Iso::Voxel const&& __restrict>(oVoxelNew));

						// update root voxel index at the global map //
						pRootVoxel->v = new_rootVoxel.v;   // updates the hash map of root voxels corresponding to voxel model instance
															// getVoxelIndex() is the occupied voxel at the origin of this voxel model instance
					}
					else { // exception - new voxel index, derived from new location outside bounds of world - this may or may not be an error - instance could be intended to just die off
#ifndef NDEBUG
						FMT_LOG_WARN(VOX_LOG, "Possible error - could not synchronize root voxel index of voxel model instance: {:d} at ({:d},{:d}) -> ({:d},{:d}), *voxel model instance new voxel index / location does not exist*", hashID, old_rootVoxel.x, old_rootVoxel.y, new_rootVoxel.x, new_rootVoxel.y);
#endif
						destroy(milliseconds(0)); // signalled for destruction //
						// bugfix: NOT SAFE to destroyInstance() (DIRECT Deletion) voxelmodel instance here, would be inside of a gameobject's update method, where it expects this voxelmodel instance to exist until its update method is complete 
					}
				}
				
				// this also covers fractional moves only (where voxelIndex does not change)
				// and updates the location / Yaw for where the above case is true or false
				XMStoreFloat2A(&vLoc, xmLoc);
				_vYaw = vAzi;

			}
			else
			{
#ifndef NDEBUG
				FMT_LOG_FAIL(VOX_LOG, "Fatal - could not synchronize root voxel index of voxel model instance: {:d}, *voxel model instance hash not registered*", hashID);
#endif		
				destroy(milliseconds(0)); // signalled for destruction //
				// bugfix: NOT SAFE to destroyInstance() (DIRECT Deletion) voxelmodel instance here, would be inside of a gameobject's update method, where it expects this voxelmodel instance to exist until its update method is complete
			}
		}
		else {
			// *** secondary child attachment voxelmodelinstance ***
			// ie.) Copter Body -> Copter Prop
			//      Only parent is included in tracking of its hovered voxels
			//      (oriented bounding rect equal in size to the models local area)
			//      Child (prop) is "gridless", and just for animation
			//		so model instance only cares about the location & rotation.
			XMStoreFloat2A(&vLoc, xmLoc);
			_vYaw = vAzi;
		}
	}
} //end ns volumetric