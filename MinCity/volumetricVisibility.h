#pragma once
/* Copyright (C) 20xx Jason Tully - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License
 * http://www.supersinfulsilicon.com/
 *
This work is licensed under the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
To view a copy of this license, visit http://creativecommons.org/licenses/by-nc-sa/4.0/
or send a letter to Creative Commons, PO Box 1866, Mountain View, CA 94042, USA.
 */
#include "globals.h"
#include "tTime.h"
#include <Math/DirectXCollision.aligned.h>

namespace Volumetric
{
	BETTER_ENUM(ePlane, uint32_t const,

		P_NEAR = 0,
		P_FAR,
		P_RIGHT,
		P_LEFT,
		P_TOP,
		P_BOTTOM

	);
	
	class alignas(16) volumetricVisibility
	{
		static constexpr float const ERROR_COMPENSATION = 1.05; // 5% error removed          // radius - hardcoded values are checked on init to be correct for a matching voxel size. If mismatched, an error is logged (debug builds only)
		static constexpr float const VOX_RADIUS = 866.025403784438646764e-3f, // : 1/1       // std::hypot(Iso::VOX_SIZE, Iso::VOX_SIZE, Iso::VOX_SIZE) where Iso::VOX_SIZE == 0.5f
                                     MINI_VOX_RADIUS = 433.012701892219323382e-3f; // : 1/2  // std::hypot(Iso::MINI_VOX_SIZE, Iso::MINI_VOX_SIZE, Iso::MINI_VOX_SIZE) where Iso::MINI_VOX_SIZE == 0.25f
	public:                          //                216.506350946109661691e-3f; // : 1/4  // *bugfix: only alternative adds unneccesary overhead of a static (non-constinit) and a function call, this was queried repeatedly.
		static inline constexpr float const getVoxelRadius() { return(VOX_RADIUS); }         // now uses a constexpr immediate value (hard-coded compile-time value) - voxel size, once selected, never changes after selection!
		static inline constexpr float const getMiniVoxelRadius() { return(MINI_VOX_RADIUS); }

		// accessors //
		__inline XMMATRIX const XM_CALLCONV getWorldMatrix() const { return(XMLoadFloat4x4A(&_matWorld)); }
		__inline XMMATRIX const XM_CALLCONV getViewMatrix() const { return(XMLoadFloat4x4A(&_matView)); }
		__inline XMMATRIX const XM_CALLCONV getProjectionMatrix() const { return(XMLoadFloat4x4A(&_matProj)); } 
		
		// main methods //
		template <bool const skip_near_plane = false>
		__inline bool const XM_CALLCONV SphereTestFrustum(FXMVECTOR const xmPosition, float const fRadius) const;
		template <bool const skip_near_plane = false>
		__inline bool const XM_CALLCONV AABBTestFrustum(FXMVECTOR const xmPosition, FXMVECTOR const xmExtents) const;
		template <bool const skip_near_plane = false>
		__inline int const XM_CALLCONV AABBIntersectFrustum(FXMVECTOR const xmPosition, FXMVECTOR const xmExtents) const;
		
		void XM_CALLCONV UpdateFrustum(FXMMATRIX const xmView, float const ZoomFactor, size_t const framecount);

		void Initialize();
		void XM_CALLCONV SetWorldOrigin(FXMVECTOR xmOrigin);
	private:
		XMFLOAT4A			_Plane[6];			

		XMFLOAT4X4A			_matProj,
			                _matView,
			                _matWorld;
	public:
		volumetricVisibility();
		~volumetricVisibility() = default;
	};

	STATIC_INLINE_PURE bool const XM_CALLCONV FastIntersectSpherePlane(_In_ FXMVECTOR const Center, _In_ FXMVECTOR const Radius, _In_ FXMVECTOR const Plane)
	{
		XMVECTOR const Dist(_mm_dp_ps(Center, Plane, 0xff));

		return(!XMVector4EqualInt(XMVectorLess(Dist, Radius), XMVectorTrueInt()));  // function returns false when OUTSIDE plane, true otherwise
	}

	template <bool const skip_near_plane>
	__inline bool const XM_CALLCONV volumetricVisibility::SphereTestFrustum(FXMVECTOR const xmPosition, float const fRadius) const
	{
		// Load the sphere.		                 // must negate vector here for sphere frustum test
		XMVECTOR const vRadius(XMVectorReplicate(-fRadius * ERROR_COMPENSATION));	// allow error % over

		// Set w of the center to one so we can dot4 with all consecutive planes.
		XMVECTOR const vCenter(XMVectorInsert<0, 0, 0, 0, 1>(xmPosition, XMVectorSplatOne()));

		// Test against each plane.

		// **** If the sphere is outside any plane it is outside.
		if constexpr (!skip_near_plane) {
			if (!FastIntersectSpherePlane(vCenter, vRadius, XMLoadFloat4A(&_Plane[ePlane::P_NEAR])))
				return(false);
		}

		if (!FastIntersectSpherePlane(vCenter, vRadius, XMLoadFloat4A(&_Plane[ePlane::P_FAR])))
			return(false);

		if (!FastIntersectSpherePlane(vCenter, vRadius, XMLoadFloat4A(&_Plane[ePlane::P_RIGHT])))
			return(false);

		if (!FastIntersectSpherePlane(vCenter, vRadius, XMLoadFloat4A(&_Plane[ePlane::P_LEFT])))
			return(false);

		if (!FastIntersectSpherePlane(vCenter, vRadius, XMLoadFloat4A(&_Plane[ePlane::P_TOP])))
			return(false);

		if (!FastIntersectSpherePlane(vCenter, vRadius, XMLoadFloat4A(&_Plane[ePlane::P_BOTTOM])))
			return(false);

		// **** If the sphere is inside all planes it is inside.
		return(true);
	}

	STATIC_INLINE_PURE void XM_CALLCONV FastIntersectAABBPlane(_In_ FXMVECTOR const Center, _In_ FXMVECTOR const Extents, _In_ FXMVECTOR const Plane,
															   _Out_ XMVECTOR& Outside, _Out_ XMVECTOR& Inside)
	{
		XMVECTOR const Dist(_mm_dp_ps(Center, Plane, 0xff));
		XMVECTOR const Radius(_mm_dp_ps(Extents, SFM::abs(Plane), 0x7f));

		Outside = XMVectorLess(Dist, -Radius);
		Inside = XMVectorGreater(Dist, Radius); 
	}
		
	// more expensive aabb frustum intersection test - https://gist.github.com/Kinwailo/d9a07f98d8511206182e50acda4fbc9b
	// returns:
	// 1 if box is inside frustum 
	// 0 if box is outside frustum
	// -1 if box is intersecting frustum
	template <bool const skip_near_plane>
	__inline int const XM_CALLCONV volumetricVisibility::AABBIntersectFrustum(FXMVECTOR const xmPosition, FXMVECTOR const xmExtents) const
	{
		// Load the box.
		XMVECTOR const vExtents(XMVectorScale(xmExtents, ERROR_COMPENSATION)); // allow error % over 

		// Set w of the center to one so we can dot4 with all consecutive planes.
		XMVECTOR const vCenter(XMVectorInsert<0, 0, 0, 0, 1>(xmPosition, XMVectorSplatOne()));

		XMVECTOR Outside, Inside;
		XMVECTOR AnyOutside(XMVectorFalseInt()), AllInside(XMVectorTrueInt());

		if constexpr (!skip_near_plane) {
			FastIntersectAABBPlane(vCenter, vExtents, XMLoadFloat4A(&_Plane[ePlane::P_NEAR]), Outside, Inside);
			AnyOutside = XMVectorOrInt(AnyOutside, Outside);
			AllInside = XMVectorAndInt(AllInside, Inside);
		}

		FastIntersectAABBPlane(vCenter, vExtents, XMLoadFloat4A(&_Plane[ePlane::P_FAR]), Outside, Inside);
		AnyOutside = XMVectorOrInt(AnyOutside, Outside);
		AllInside = XMVectorAndInt(AllInside, Inside);


		FastIntersectAABBPlane(vCenter, vExtents, XMLoadFloat4A(&_Plane[ePlane::P_RIGHT]), Outside, Inside);
		AnyOutside = XMVectorOrInt(AnyOutside, Outside);
		AllInside = XMVectorAndInt(AllInside, Inside);


		FastIntersectAABBPlane(vCenter, vExtents, XMLoadFloat4A(&_Plane[ePlane::P_LEFT]), Outside, Inside);
		AnyOutside = XMVectorOrInt(AnyOutside, Outside);
		AllInside = XMVectorAndInt(AllInside, Inside);

		
		FastIntersectAABBPlane(vCenter, vExtents, XMLoadFloat4A(&_Plane[ePlane::P_TOP]), Outside, Inside);
		AnyOutside = XMVectorOrInt(AnyOutside, Outside);
		AllInside = XMVectorAndInt(AllInside, Inside);

		
		FastIntersectAABBPlane(vCenter, vExtents, XMLoadFloat4A(&_Plane[ePlane::P_BOTTOM]), Outside, Inside);
		AnyOutside = XMVectorOrInt(AnyOutside, Outside);
		AllInside = XMVectorAndInt(AllInside, Inside);
		
		// If the box is outside any plane it is outside.
		if (XMVector4EqualInt(AnyOutside, XMVectorTrueInt()))
			return(0); // completely outside

		// If the box is inside all planes it is inside.
		if (XMVector4EqualInt(AllInside, XMVectorTrueInt()))
			return(1); // completely inside

		// The box is not inside all planes or outside a plane, it may intersect.
		return(-1); // intersecting
	}

	// simple in or out
	template <bool const skip_near_plane>
	__inline bool const XM_CALLCONV volumetricVisibility::AABBTestFrustum(FXMVECTOR const xmPosition, FXMVECTOR const xmExtents) const
	{
		return(0 != AABBIntersectFrustum<skip_near_plane>(xmPosition, xmExtents));
	}
}
