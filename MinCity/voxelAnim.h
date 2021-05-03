#pragma once
#include "tTime.h"
#include "IsoVoxel.h"
#include "voxelModel.h"
#include "eStateGroups.h"
#include <Random/superrandom.hpp>

namespace Volumetric
{
	static constexpr milliseconds const UPDATE_ANIM_INTERVAL_DEFAULT( 33 );

	class voxelAnimStateGroup
	{
	public:
		milliseconds& interval() { return(tUpdateInterval); }

	public:
		void update(tTime const& tNow) {

			if ((tNow - tLastUpdate) > tUpdateInterval) {

				updateState(tNow);

				tLastUpdate = tNow;
			}
		}
	protected:
		virtual void updateState(tTime const&) = 0;

	protected:
		voxB::voxelStateGroup const&		 Group;
		tTime								 tLastUpdate;
		milliseconds						 tUpdateInterval;

	public:
		voxelAnimStateGroup(voxB::voxelStateGroup const& refGroup)
			: Group(refGroup), tUpdateInterval(UPDATE_ANIM_INTERVAL_DEFAULT)
		{}
	};

	class voxelAnimTheater : public voxelAnimStateGroup
	{
	protected:
		virtual void updateState(tTime const& tNow) override final {
			
			uint32_t LastEmission(Iso::EMISSION_SHADING);

			if (++Start >= Group.groupedstates.size()) {
				Start = 0;
			}

			if (!bFlash && !bAll && PsuedoRandomNumber(0, 100) < 25) {

				for (uint32_t iDx = 0; iDx < Group.groupedstates.size(); ++iDx) {

					Group.groupedstates[iDx]->Emissive = Iso::EMISSION_SHADING;
				}
				bAll = true;
				return;
			}
			else if (bAll) {
				for (uint32_t iDx = Start; iDx < Group.groupedstates.size(); ++iDx) {

					Group.groupedstates[iDx]->Emissive = Iso::EMISSION_SHADING_NONE;
				}
				bAll = false;
				bFlash = true;
				return;
			}

			bFlash = false;

			for (uint32_t iDx = Start; iDx < Group.groupedstates.size(); ++iDx) {

				Group.groupedstates[iDx]->Emissive = EmissionState ? Iso::EMISSION_SHADING : Iso::EMISSION_SHADING_NONE;

				EmissionState = !EmissionState;
			}

			for (uint32_t iDx = 0; iDx < Start; ++iDx) {

				Group.groupedstates[iDx]->Emissive = Group.groupedstates[iDx]->Emissive | LastEmission;

				if (EmissionState)
					LastEmission = 0;
			}
		}
	public:
		voxelAnimTheater(voxB::voxelStateGroup const& refGroup)
			: voxelAnimStateGroup(refGroup), Start(0),
			EmissionState(false), bAll(false), bFlash(false)
		{}
	private:
		uint32_t Start;
		bool EmissionState, bAll, bFlash;
	};

	void UpdateAllVoxelAnimations(tTime const& tNow);
	void CleanUpAllVoxelAnimations();
} // end ns;


#ifdef VOXEL_ANIM_IMPLEMENTATION
static std::vector< Volumetric::voxelAnimStateGroup* > _animations;

namespace Volumetric
{
	template<typename... Args>
	static Volumetric::voxelAnimStateGroup* const& AddAnimation(Args&&... args) {
		return( _animations.emplace_back(std::forward<Args>(args)...) );
	}

	void UpdateAllVoxelAnimations(tTime const& tNow)
	{
		for (auto const i : _animations) {
			i->update(tNow);
		}
	}
	void CleanUpAllVoxelAnimations()
	{
		for (auto& i : _animations) {
			SAFE_DELETE(i);
		}
		_animations.clear(); _animations.shrink_to_fit();
	}
} // end ns
#endif
