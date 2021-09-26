#pragma once
#include <Math/superfastmath.h>
#include <Math/v2_rotation_t.h>
#include <Math/point2D_t.h>
#include "tTime.h"
#include "IsoVoxel.h"
#include "voxelKonstants.h"
#include "voxelState.h"
#include "Declarations.h"
#include <Random/superrandom.hpp>
#include "types.h"

// forward decl
namespace Volumetric
{
	namespace voxB
	{
		template<bool const Dynamic>
		class voxelModel;

		struct voxelDescPacked;
	} // end ns
} // end ns

namespace Volumetric
{
	typedef void (*release_event_function)(void const* const __restrict eventTarget);

	class voxelModelInstanceBase
	{
	public:
		uint32_t const											      getHash() const { return(hashID); }
		XMVECTOR const __vectorcall									  getLocation() const { return(XMLoadFloat2A(&vLoc)); }
		XMVECTOR const __vectorcall									  getLocation3D() const { return(XMVectorSet(vLoc.x, fElevation, vLoc.y, 0.0f)); }
		float const __vectorcall									  getElevation() const { return(fElevation); } // additional "height above ground"
		
		point2D_t const	__vectorcall								  getVoxelIndex() const { return(v2_to_p2D(getLocation())); }	// returns voxel index occupied by the origin of this instance
																																	// this is synchronized as the "root voxel" index by the voxel world
		void __vectorcall setElevation(float const fElevation_) { fElevation = fElevation_; }																															// this must use a floor type operation on the floating point vector to be correct
																																	// which v2_to_p2D does
		tTime const& getCreationTime() const { return(tCreation); }
		tTime const& getDestructionTime() const { return(tDestruction); }

		voxelModelInstanceBase* const __restrict& __restrict getChild() const {	return(child); }
		void setChild(voxelModelInstanceBase* const child_) { child = child_; }	// to simplify state - only *once* can a child be set, and that child then remains a child of this instance until this instance is destroyed, where it is destroyed aswell.
		
		// The gameobject that uses this instance should setOwnerGameObject in it's ctor
		// acceptable - gameobject is a leaf/final class, not a base class 
		template<typename TGameObject>
		TGameObject* const getOwnerGameObject() const {
			return( static_cast<TGameObject* const>(owner_gameobject) );
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
		void destroy() { if (zero_time_point == tDestruction) tDestruction = now(); } // instance scheduled for destruction
		
	protected:
		void destroyInstance() const;
		
	protected:	
		uint32_t const										hashID;
		uint32_t											flags;
		tTime const											tCreation;
		tTime												tDestruction;

		XMFLOAT2A											vLoc;
		alignas(16) float									fElevation;

		voxelModelInstanceBase*								child;

		uint32_t											owner_gameobject_type;
		void*												owner_gameobject;
		release_event_function								eOnRelease;
	protected:
		explicit voxelModelInstanceBase(uint32_t const hash, point2D_t const voxelIndex, uint32_t const flags_);
		virtual ~voxelModelInstanceBase() {

			SAFE_DELETE(child); // child instances belong to a parent voxelModelInstance - they are not managed memory outside of parent instance
								// parent manages memory of child only, where the parent is managed memory 

			if (owner_gameobject && eOnRelease) {
				eOnRelease(owner_gameobject);
				owner_gameobject = nullptr;
				eOnRelease = nullptr;
			}
		}
	};

	typedef Volumetric::voxB::voxelState const(* voxel_event_function)(Volumetric::voxB::voxelDescPacked& __restrict voxel, Volumetric::voxB::voxelState const& __restrict rOriginalVoxelState, void const* const __restrict eventTarget, uint32_t const vxl_index);

	template< bool const Dynamic >
	class voxelModelInstance : public voxelModelInstanceBase
	{
	public:
		__inline voxB::voxelModel<Dynamic> const& __restrict		  getModel() const { return(model); }
		uint32_t const												  getFlags() const { return(flags);}
		bool const													  isFaded() const { return(faded); }

		uint32_t const												  getTransparency() const { return(transparency); } // use eVoxelTransparency enum
		void														  setFaded(bool const faded_) { faded = faded_; }

		/// Transparency only has affect if loaded model has state groups defining the voxels that are transparent at load model time
		void														  setTransparency(uint32_t const transparency_) { transparency = transparency_; } // use eVoxelTransparency enum
		void														  setVoxelEventFunction(voxel_event_function const eventHandler) { eOnVoxel = eventHandler; }
	public:
		__inline bool const Validate() const;
	public:
		__inline void XM_CALLCONV Render(FXMVECTOR xmVoxelOrigin, point2D_t voxelIndex,
			Iso::Voxel const&__restrict oVoxel,
			tbb::atomic<VertexDecl::VoxelNormal*>& __restrict voxels_static,
			tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict voxels_dynamic,
			tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict voxels_trans) const;
		__inline Volumetric::voxB::voxelState const XM_CALLCONV OnVoxel(voxB::voxelDescPacked& __restrict voxel, Volumetric::voxB::voxelState const& __restrict rOriginalVoxelState, uint32_t const vxl_index) const;
	protected:
		voxB::voxelModel<Dynamic> const& __restrict 		model;
		voxel_event_function								eOnVoxel;
		bool												faded;
		uint32_t											transparency;	// 4 distinct levels of transparency supported - see eVoxelTransparency enum - however all values between 0 - 255 will be correctly converted to transparency level that is closest
	public:
		inline explicit voxelModelInstance(voxB::voxelModel<Dynamic> const& __restrict refModel, uint32_t const hash, point2D_t const voxelIndex, uint32_t const flags_)
			: voxelModelInstanceBase(hash, voxelIndex, flags_), model(refModel), faded(false), transparency(Volumetric::Konstants::DEFAULT_TRANSPARENCY), eOnVoxel(nullptr)
		{}
	};

	template<bool const Dynamic>
	__inline bool const voxelModelInstance<Dynamic>::Validate() const
	{
		if (zero_time_point != tDestruction) {

			// sequence length for destruction is scaled by height of voxel model
			milliseconds const tSequenceLength(model._maxDimensions.y * Konstants::DESTRUCTION_SEQUENCE_LENGTH);

			if (now() - tDestruction > tSequenceLength) {

				destroyInstance(); // here it is - destroys the voxel model instance in a concurrency safe manner
				return(false);
			}
		}
		return(true);
	}
	template<bool const Dynamic>
	__inline void XM_CALLCONV voxelModelInstance<Dynamic>::Render(FXMVECTOR xmVoxelOrigin, point2D_t const voxelIndex,
		Iso::Voxel const&__restrict oVoxel,
		tbb::atomic<VertexDecl::VoxelNormal*>& __restrict voxels_static,
		tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict voxels_dynamic,
		tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict voxels_trans) const
	{
		model.Render(xmVoxelOrigin, voxelIndex, oVoxel, *this, voxels_static, voxels_dynamic, voxels_trans);
		if (child) {
			// safe down cast
			static_cast<voxelModelInstance<Dynamic> const* const __restrict>(child)->Render(xmVoxelOrigin, voxelIndex, oVoxel, voxels_static, voxels_dynamic, voxels_trans);
		}
	}
	template<bool const Dynamic>
	__inline Volumetric::voxB::voxelState const XM_CALLCONV voxelModelInstance<Dynamic>::OnVoxel(voxB::voxelDescPacked& __restrict voxel, Volumetric::voxB::voxelState const& __restrict rOriginalVoxelState, uint32_t const vxl_index) const
	{
		if (eOnVoxel) {
			return((*eOnVoxel)(voxel, rOriginalVoxelState, owner_gameobject, vxl_index));
		}
		return(rOriginalVoxelState);
	}

	class alignas(16) voxelModelInstance_Dynamic : public voxelModelInstance<voxB::DYNAMIC>
	{
	private:
		v2_rotation_t								_vAzimuth, _vPitch;	// Azimuth = rotation about Y Axis. Pitch = rotation about Z Axis

	public:
		v2_rotation_t const& __vectorcall getAzimuth() const { return(_vAzimuth); }
		v2_rotation_t const& __vectorcall getPitch() const { return(_vPitch); }

		void __vectorcall setLocation(FXMVECTOR const xmLoc) { synchronize(xmLoc, _vAzimuth); }			//-ok
		void __vectorcall setLocation3D(FXMVECTOR const xmLoc) { fElevation = XMVectorGetY(xmLoc); synchronize(XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(xmLoc), _vAzimuth); }		//-ok
		void __vectorcall setAzimuth(v2_rotation_t const vAzi) { synchronize(getLocation(), vAzi); }	//-ok
		void __vectorcall setPitch(v2_rotation_t const vPit) { _vPitch = vPit; }						// pitch doesn't affect synchronization
		void __vectorcall setLocationAzimuth(FXMVECTOR const xmLoc, v2_rotation_t const vAzi) { synchronize(xmLoc, vAzi); }  //*best
		void __vectorcall setLocation3DAzimuth(FXMVECTOR const xmLoc, v2_rotation_t const vAzi) { fElevation = XMVectorGetY(xmLoc); synchronize(XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(xmLoc), vAzi); }  //*best
	public:
		void __vectorcall synchronize(FXMVECTOR const xmLoc, v2_rotation_t const vAzi);	// must be called whenever a change in location/rotation is intended 
	private:
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

} // end ns


