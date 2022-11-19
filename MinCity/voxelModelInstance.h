#pragma once
#include <Math/superfastmath.h>
#include <Math/v2_rotation_t.h>
#include <Math/point2D_t.h>
#include "tTime.h"
#include "IsoVoxel.h"
#include "voxelKonstants.h"
#include "Declarations.h"
#include <Random/superrandom.hpp>
#include "types.h"
#include "voxelModel.h"
#include "Interpolator.h"

namespace Volumetric
{
	typedef void (*release_event_function)(void const* const __restrict eventTarget);

	class voxelModelInstanceBase
	{
	public:
		uint32_t const											      getHash() const { return(hashID); }

		XMVECTOR const __vectorcall									  getLocation() const { return(XMLoadFloat3A(&(XMFLOAT3A const&)vLoc)); }

		float const __vectorcall									  getElevation() const { return(XMVectorGetY(XMLoadFloat3A(&(XMFLOAT3A const&)vLoc))); } // additional "height above ground"

		point2D_t const	__vectorcall								  getVoxelIndex() const { return(v2_to_p2D(XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(getLocation()))); }	// returns voxel index occupied by the origin of this instance
			
		void __vectorcall											  resetElevation(float const fElevation_) { Interpolator.reset_component<COMPONENT_Y>(vLoc, fElevation_); }								// this is synchronized as the "root voxel" index by the voxel world
		void __vectorcall											  setElevation(float const fElevation_) { Interpolator.set_component<COMPONENT_Y>(vLoc, fElevation_); }									// this must use a floor type operation on the floating point vector to be correct
																																																			// which v2_to_p2D does
		tTime const& getCreationTime() const { return(tCreation); }
		tTime const& getDestructionTime() const { return(tDestruction); }

		milliseconds const											  getCreationSequenceLength() const { return(tSequenceLengthCreation); }
		milliseconds const											  getDestructionSequenceLength() const { return(tSequenceLengthDestruction); }

		void														  setCreationSequenceLength(milliseconds const length) { tSequenceLengthCreation = length; }
		void														  setDestructionSequenceLength(milliseconds const length) { tSequenceLengthDestruction = length; }

		bool const												      isFadeable() const { return(!(eVoxelModelInstanceFlags::NOT_FADEABLE == (eVoxelModelInstanceFlags::NOT_FADEABLE & flags))); }

		// The gameobject that uses this instance should setOwnerGameObject in it's ctor
		// acceptable - gameobject is a leaf/final class, not a base class 
		template<typename TGameObject>
		TGameObject* const getOwnerGameObject() const {
			return(static_cast<TGameObject* const>(owner_gameobject));
		}
		uint32_t const getOwnerGameObjectType() const {
			return(owner_gameobject_type);
		}
		template<typename TGameObject>
		void setOwnerGameObject(TGameObject* const& owner_, release_event_function const eventHandler) {
			if (owner_) {
				owner_gameobject_type = owner_->to_type();
				owner_gameobject = static_cast<void* const>(owner_);
			}
			else {
				owner_gameobject_type = world::types::game_object_t::NoOwner;
				owner_gameobject = nullptr;
			}
			eOnRelease = eventHandler;
		}

		bool const destroyPending() const { return(zero_time_point != tDestruction); }
		void destroy() { if (zero_time_point == tDestruction) tDestruction = now(); } // instance scheduled for destruction (normally there is a destruction sequence)
		void destroy(milliseconds const length) { destroy(); if (length != tSequenceLengthDestruction) tSequenceLengthDestruction = length; } // instance scheduled for destruction (normally there is a destruction sequence) definable sequence length override.


	protected:
		void destroyInstance() const;
		
	protected:	
		uint32_t const										hashID;
		uint32_t											flags;
		tTime const											tCreation;
		tTime												tDestruction;
		milliseconds 										tSequenceLengthCreation,
															tSequenceLengthDestruction;

		interpolated<XMFLOAT3A>								vLoc;

		uint32_t											owner_gameobject_type;
		void*												owner_gameobject;
		release_event_function								eOnRelease;
	protected:
		explicit voxelModelInstanceBase(uint32_t const hash, point2D_t const voxelIndex, uint32_t const flags_);
		virtual ~voxelModelInstanceBase() {

			Interpolator.remove(vLoc);

			if (owner_gameobject && eOnRelease) {
				eOnRelease(owner_gameobject);
				owner_gameobject = nullptr;
				eOnRelease = nullptr;
			}
		}
	};

	// all parameters are passed by value in registers (__vectorcall) - fastest - no pointers or references to the data that matters "voxel" (optimization)
#define VOXEL_EVENT_FUNCTION_PARAMETERS XMVECTOR& __restrict xmIndex, Volumetric::voxB::voxelDescPacked voxel, void const* const __restrict _this, uint32_t const vxl_index
#define VOXEL_EVENT_FUNCTION_RESOLVED_PARAMETERS XMVECTOR& __restrict xmIndex, Volumetric::voxB::voxelDescPacked voxel, uint32_t const vxl_index
#define VOXEL_EVENT_FUNCTION_RETURN Volumetric::voxB::voxelDescPacked const

	typedef VOXEL_EVENT_FUNCTION_RETURN(__vectorcall* voxel_event_function)(VOXEL_EVENT_FUNCTION_PARAMETERS);

	template< bool const Dynamic >
	class voxelModelInstance : public voxelModelInstanceBase
	{
	public:
		__inline voxB::voxelModel<Dynamic> const& __restrict		  getModel() const { return(model); }
		uint32_t const												  getFlags() const { return(flags);}
		bool const													  isFaded() const { return(faded); }
		bool const													  isEmissionOnly() const { return(emission_only); }

		uint32_t const												  getTransparency() const { return(transparency); } // use eVoxelTransparency enum
		void														  setFaded(bool const faded_) { if (isFadeable()) faded = faded_; } // makes instance transparent
		void														  setEmissionOnly(bool const emission_only_) { emission_only = emission_only_; } // makes instance hidden except for lights still added to lightbuffer

		/// Transparency only has affect if loaded model has state groups defining the voxels that are transparent at load model time
		void														  setTransparency(uint32_t const transparency_) { transparency = transparency_; } // use eVoxelTransparency enum
		void														  setVoxelEventFunction(voxel_event_function const eventHandler) { eOnVoxel = eventHandler; }

		uint32_t const												  getVoxelOffset() const { return(vxl_offset); }
		uint32_t const												  getVoxelCount() const { return(vxl_count); }
		uint32_t const												  getVoxelTransparentCount() const { return(vxl_transparent_count); }
		void														  setVoxelOffsetCount(uint32_t const vxl_offset_, uint32_t const vxl_count_) { vxl_offset = vxl_offset_; vxl_count = vxl_count_; } // for sequence animation control
		void														  setVoxelTransparentCount(uint32_t const vxl_transparent_count_) { vxl_transparent_count = vxl_transparent_count_; } // *** this count must be accurate otherwise "flicker" of any transparent voxels will occur, don't mess around.

	public:
		__inline bool const Validate() const;
		__inline VOXEL_EVENT_FUNCTION_RETURN __vectorcall OnVoxel(VOXEL_EVENT_FUNCTION_RESOLVED_PARAMETERS) const;

	protected:
		voxB::voxelModel<Dynamic> const& __restrict 		model;
		voxel_event_function								eOnVoxel;
		bool												faded, emission_only;
		uint32_t											transparency;	// 4 distinct levels of transparency supported - see eVoxelTransparency enum - however all values between 0 - 255 will be correctly converted to transparency level that is closest
		uint32_t											vxl_offset, vxl_count, vxl_transparent_count; // current voxel offset and number of voxels to render
	public:
		inline explicit voxelModelInstance(voxB::voxelModel<Dynamic> const& __restrict refModel, uint32_t const hash, point2D_t const voxelIndex, uint32_t const flags_)
			: voxelModelInstanceBase(hash, voxelIndex, flags_), model(refModel), faded(false), emission_only(false), transparency(Volumetric::Konstants::DEFAULT_TRANSPARENCY), eOnVoxel(nullptr),
			vxl_offset(0), vxl_count(refModel._numVoxels), vxl_transparent_count(refModel._numVoxelsTransparent) // defaults to single "frame" mode
		{}
	};

	template<bool const Dynamic>
	__inline bool const voxelModelInstance<Dynamic>::Validate() const
	{
		if (zero_time_point != tDestruction) {

			// sequence length for destruction is scaled by height of voxel model
			milliseconds const tSequenceLength(tSequenceLengthDestruction * model._maxDimensions.y);

			if (now() - tDestruction > tSequenceLength) {

				destroyInstance(); // here it is - destroys the voxel model instance in a concurrency safe manner
				return(false);
			}
		}
		return(true);
	}
	template<bool const Dynamic>
	__inline VOXEL_EVENT_FUNCTION_RETURN __vectorcall voxelModelInstance<Dynamic>::OnVoxel(VOXEL_EVENT_FUNCTION_RESOLVED_PARAMETERS) const
	{
		if (eOnVoxel) {
			return((*eOnVoxel)(xmIndex, voxel, owner_gameobject, vxl_index));
		}
		return(voxel);
	}

	class alignas(16) voxelModelInstance_Dynamic : public voxelModelInstance<voxB::DYNAMIC>
	{
	private: // global application convention is the order Pitch (x), Yaw (y), Roll (z)
		v2_rotation_t								 _vPitch, _vYaw, _vRoll;

	public:
		v2_rotation_t const& __vectorcall getPitch() const { return(_vPitch); } // rotate (x)
		v2_rotation_t const& __vectorcall getYaw() const { return(_vYaw); }		// rotate (y)
		v2_rotation_t const& __vectorcall getRoll() const { return(_vRoll); }	// rotate (z)
		
		void __vectorcall resetLocation(FXMVECTOR const xmLoc) { Interpolator.reset(vLoc, xmLoc); synchronize(xmLoc); }						
		void __vectorcall setLocation(FXMVECTOR const xmLoc) { synchronize(xmLoc); }					// location does affect synchronization	

		void __vectorcall setPitch(v2_rotation_t const xPitch) { _vPitch = xPitch; }					// pitch doesn't affect synchronization
		void __vectorcall setYaw(v2_rotation_t const yYaw) { synchronize(yYaw); }						// yaw does affect synchronization
		void __vectorcall setRoll(v2_rotation_t const zRoll) { _vRoll = zRoll; }						// row doesn't affect synchronization
		
		void __vectorcall setPitchYawRoll(v2_rotation_t const& xPitch, v2_rotation_t const& yYaw, v2_rotation_t const& zRoll) { _vPitch = xPitch; _vRoll = zRoll; synchronize(yYaw); }

	public:
		__inline void XM_CALLCONV Render(FXMVECTOR xmVoxelOrigin, point2D_t const voxelIndex,
			tbb::atomic<VertexDecl::VoxelNormal*>& __restrict voxels_static,
			tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict voxels_dynamic,
			tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict voxels_trans) const;

	private:
		bool const __vectorcall synchronize(FXMVECTOR const xmLoc, v2_rotation_t const vYaw) const; // internally used only
		void __vectorcall synchronize(FXMVECTOR const xmLoc);		// must be called whenever a change in location is intended 
		void __vectorcall synchronize(v2_rotation_t const vYaw);	// must be called whenever a change in only *yaw* rotation is intended 

		// model must be loaded b4 any instance creation!
		explicit voxelModelInstance_Dynamic(voxB::voxelModel<voxB::DYNAMIC> const& __restrict refModel, uint32_t const hash, point2D_t const voxelIndex, uint32_t const flags_);
	public:
		// helper static method //
		static voxelModelInstance_Dynamic * const __restrict XM_CALLCONV create(voxB::voxelModel<voxB::DYNAMIC> const& __restrict refModel, uint32_t const hash, point2D_t const voxelIndex, uint32_t const flags_ = 0)
		{
			return(new voxelModelInstance_Dynamic(refModel, hash, voxelIndex, flags_));
		}
	};
	// easter egg idea: have clickable screeen on building relipacte pattern from Razer Chroma - interact zoom in on screen ripple? on successive mouse clicks
	// shows animation of keyboard leds - unlocks the building that is 100% leds / emissive with building reflecting the same dynamic effect!
	class alignas(16) voxelModelInstance_Static : public voxelModelInstance<voxB::STATIC>
	{
	public:
		__inline void XM_CALLCONV Render(FXMVECTOR xmVoxelOrigin, point2D_t const voxelIndex,
			tbb::atomic<VertexDecl::VoxelNormal*>& __restrict voxels_static,
			tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict voxels_dynamic,
			tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict voxels_trans) const;

	private:
		// model must be loaded b4 any instance creation!
		explicit voxelModelInstance_Static(voxB::voxelModel<voxB::STATIC> const& __restrict refModel, uint32_t const hash, point2D_t const voxelIndex, uint32_t const flags_);
		
	public:
		// helper static method //
		static voxelModelInstance_Static * const __restrict XM_CALLCONV create(voxB::voxelModel<voxB::STATIC> const& __restrict refModel, uint32_t const hash, point2D_t const voxelIndex, uint32_t const flags_ = 0)
		{
			return( new voxelModelInstance_Static(refModel, hash, voxelIndex, flags_));
		}

	};


	__inline void XM_CALLCONV voxelModelInstance_Dynamic::Render(FXMVECTOR xmVoxelOrigin, point2D_t const voxelIndex,
		tbb::atomic<VertexDecl::VoxelNormal*>& __restrict voxels_static,
		tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict voxels_dynamic,
		tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict voxels_trans) const
	{
		quat_t const orientation(getPitch(), getYaw(), getRoll()); // only applies to dynamic model instances, otherwise this is ignored

		//* bugfix - hoisted out of parallel loop, don't change.
		if (isEmissionOnly()) {
			model.Render<true, false>(xmVoxelOrigin, orientation.v4(), voxelIndex, *this, voxels_static, voxels_dynamic, voxels_trans);
		}
		else if (isFaded()) {
			model.Render<false, true>(xmVoxelOrigin, orientation.v4(), voxelIndex, *this, voxels_static, voxels_dynamic, voxels_trans);
		}
		else {
			model.Render<false, false>(xmVoxelOrigin, orientation.v4(), voxelIndex, *this, voxels_static, voxels_dynamic, voxels_trans);
		}
	}

	__inline void XM_CALLCONV voxelModelInstance_Static::Render(FXMVECTOR xmVoxelOrigin, point2D_t const voxelIndex,
		tbb::atomic<VertexDecl::VoxelNormal*>& __restrict voxels_static,
		tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict voxels_dynamic,
		tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict voxels_trans) const
	{
		//* bugfix - hoisted out of parallel loop, don't change.
		if (isEmissionOnly()) {
			model.Render<true, false>(xmVoxelOrigin, XMVectorZero(), voxelIndex, *this, voxels_static, voxels_dynamic, voxels_trans);
		}
		else if (isFaded()) {
			model.Render<false, true>(xmVoxelOrigin, XMVectorZero(), voxelIndex, *this, voxels_static, voxels_dynamic, voxels_trans);
		}
		else {
			model.Render<false, false>(xmVoxelOrigin, XMVectorZero(), voxelIndex, *this, voxels_static, voxels_dynamic, voxels_trans);
		}
	}
} // end ns


