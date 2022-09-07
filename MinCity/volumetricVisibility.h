#pragma once
#include "globals.h"
#include "tTime.h"

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
		static constexpr float const ERROR_COMPENSATION = 1.05; // 5% error removed
	public:
		// accessors //
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

	private:
		XMFLOAT4A			_Plane[6];			

		XMFLOAT4X4A			_matProj;
		XMFLOAT4X4A			_matView;			// view matrix is saved here and is current for every frame
	public:
		volumetricVisibility();
		~volumetricVisibility() = default;
	};

	STATIC_INLINE_PURE bool const XM_CALLCONV FastIntersectSpherePlane(_In_ FXMVECTOR const Center, _In_ FXMVECTOR const Radius, _In_ FXMVECTOR const Plane)
	{
		XMVECTOR const Dist(_mm_dp_ps(Center, Plane, 0xff));

		return(XMVector4EqualInt(XMVectorLess(Dist, Radius), XMVectorFalseInt()));  // function returns false when OUTSIDE plane, true when INSIDE
	}

	template <bool const skip_near_plane>
	__inline bool const XM_CALLCONV volumetricVisibility::SphereTestFrustum(FXMVECTOR const xmPosition, float const fRadius) const
	{
		// Load the sphere.			// used to negate vector, now negation is included from ERROR_COMPENSATION
		XMVECTOR const vRadius(XMVectorReplicate(fRadius * -ERROR_COMPENSATION));	// allow error % over

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

	STATIC_INLINE_PURE bool const XM_CALLCONV FastIntersectAABBPlane(_In_ FXMVECTOR const Center, _In_ FXMVECTOR const Extents, _In_ FXMVECTOR const Plane)
	{
		XMVECTOR const Dist(_mm_dp_ps(Center, Plane, 0xff));
		XMVECTOR const Radius(_mm_dp_ps(Extents, SFM::abs(Plane), 0x7f));

		return(XMVector4EqualInt(XMVectorLess(Dist, Radius), XMVectorFalseInt())); // function returns false when OUTSIDE plane, true when INSIDE
	}

	template <bool const skip_near_plane>
	__inline bool const XM_CALLCONV volumetricVisibility::AABBTestFrustum(FXMVECTOR const xmPosition, FXMVECTOR const xmExtents) const
	{
		// Load the box.		// used to negate vector, now negation is included from ERROR_COMPENSATION
		XMVECTOR const vExtents(XMVectorScale(xmExtents, -ERROR_COMPENSATION)); // allow error % over 

		// Set w of the center to one so we can dot4 with all consecutive planes.
		XMVECTOR const vCenter(XMVectorInsert<0, 0, 0, 0, 1>(xmPosition, XMVectorSplatOne()));

		// Test against each plane.

		// **** If the box is outside any plane it is outside.
		if constexpr (!skip_near_plane) {
			if (!FastIntersectAABBPlane(vCenter, vExtents, XMLoadFloat4A(&_Plane[ePlane::P_NEAR])))
				return(false);
		}

		if (!FastIntersectAABBPlane(vCenter, vExtents, XMLoadFloat4A(&_Plane[ePlane::P_FAR])))
			return(false);

		if (!FastIntersectAABBPlane(vCenter, vExtents, XMLoadFloat4A(&_Plane[ePlane::P_RIGHT])))
			return(false);

		if (!FastIntersectAABBPlane(vCenter, vExtents, XMLoadFloat4A(&_Plane[ePlane::P_LEFT])))
			return(false);

		if (!FastIntersectAABBPlane(vCenter, vExtents, XMLoadFloat4A(&_Plane[ePlane::P_TOP])))
			return(false);

		if (!FastIntersectAABBPlane(vCenter, vExtents, XMLoadFloat4A(&_Plane[ePlane::P_BOTTOM])))
			return(false);

		// **** If the box is inside all planes it is inside.
		return(true);
	}
		
	// more expensive aabb frustum intersection test - https://gist.github.com/Kinwailo/d9a07f98d8511206182e50acda4fbc9b
	// returns:
	// 1 if box is inside frustum 
	// 0 if box is outside frustum
	// -1 if box is intersecting frustum
	template <bool const skip_near_plane>
	__inline int const XM_CALLCONV volumetricVisibility::AABBIntersectFrustum(FXMVECTOR const xmPosition, FXMVECTOR const xmExtents) const
	{
		/*
		int iContainment(1); // default to INSIDE
		
		// Load the box.		
		XMVECTOR const vExtents(XMVectorScale(xmExtents, -ERROR_COMPENSATION)); // allow error % over 

		// Test against each plane.
		XMVECTOR const xmMin(XMVectorSubtract(xmPosition, vExtents)), xmMax(XMVectorAdd(xmPosition, vExtents));
		XMVECTOR vmin(XMVectorZero()), vmax(XMVectorZero());
		
		uint32_t start;
		if constexpr (skip_near_plane) {
			start = 1;
		}
		else {
			start = 0;
		}
		for (uint32_t i = start; i < 6; ++i) {
			// X axis 
			if (_Plane[i].x > 0) {
				vmin = XMVectorInsert<0, 1, 0, 0, 0>(vmin, XMVectorSplatX(xmMin)); // vmin.x = mins.x;
				vmax = XMVectorInsert<0, 1, 0, 0, 0>(vmax, XMVectorSplatX(xmMax)); // vmax.x = maxs.x;
			}
			else {
				vmin = XMVectorInsert<0, 1, 0, 0, 0>(vmin, XMVectorSplatX(xmMax)); // vmin.x = maxs.x;
				vmax = XMVectorInsert<0, 1, 0, 0, 0>(vmax, XMVectorSplatX(xmMin)); // vmax.x = mins.x;
			}

			// Y axis 
			if (_Plane[i].y > 0) {
				vmin = XMVectorInsert<0, 0, 1, 0, 0>(vmin, XMVectorSplatY(xmMin)); // vmin.y = mins.y;
				vmax = XMVectorInsert<0, 0, 1, 0, 0>(vmax, XMVectorSplatY(xmMax)); // vmax.y = maxs.y;
			}
			else {
				vmin = XMVectorInsert<0, 0, 1, 0, 0>(vmin, XMVectorSplatY(xmMax)); // vmin.y = maxs.y;
				vmax = XMVectorInsert<0, 0, 1, 0, 0>(vmax, XMVectorSplatY(xmMin)); // vmax.y = mins.y;
			}

			// Z axis 
			if (_Plane[i].z > 0) {
				vmin = XMVectorInsert<0, 0, 0, 1, 0>(vmin, XMVectorSplatZ(xmMin)); // vmin.z = mins.z;
				vmax = XMVectorInsert<0, 0, 0, 1, 0>(vmax, XMVectorSplatZ(xmMax)); // vmax.z = maxs.z;
			}
			else {
				vmin = XMVectorInsert<0, 0, 0, 1, 0>(vmin, XMVectorSplatZ(xmMax)); // vmin.z = maxs.z;
				vmax = XMVectorInsert<0, 0, 0, 1, 0>(vmax, XMVectorSplatZ(xmMin)); // vmax.z = mins.z;
			}

			// Set w of the min/max bounds to one so we can dot4 with all consecutive planes.
			vmin = XMVectorInsert<0, 0, 0, 0, 1>(vmin, XMVectorSplatOne());
			vmax = XMVectorInsert<0, 0, 0, 0, 1>(vmax, XMVectorSplatOne());
			
			XMVECTOR dp;
			dp = _mm_dp_ps(XMLoadFloat4A(&_Plane[i]), vmin, 0xff);
			if (XMVectorGetX(dp) > 0.0f)	// **** If the box is outside any plane it is outside.
				return(0); // OUTSIDE

			dp = _mm_dp_ps(XMLoadFloat4A(&_Plane[i]), vmax, 0xff);
			if (XMVectorGetX(dp) >= 0.0f)
				iContainment = -1; // INTERSECTING
		}
	
		return(iContainment);
		*/

		// Load the box.		// used to negate vector, now negation is included from ERROR_COMPENSATION
		XMVECTOR const vExtents(XMVectorScale(xmExtents, -ERROR_COMPENSATION)); // allow error % over 

		// Set w of the center to one so we can dot4 with all consecutive planes.
		XMVECTOR const vCenter(XMVectorInsert<0, 0, 0, 0, 1>(xmPosition, XMVectorSplatOne()));

		bool Outside(false), Inside(false);	
		bool AnyOutside(false), AllInside(true);

		if constexpr (!skip_near_plane) {
			Inside = FastIntersectAABBPlane(vCenter, vExtents, XMLoadFloat4A(&_Plane[ePlane::P_NEAR]));
			Outside = !Inside;

			AnyOutside |= Outside;
			AllInside &= Inside;
		}

		Inside = FastIntersectAABBPlane(vCenter, vExtents, XMLoadFloat4A(&_Plane[ePlane::P_FAR]));
		Outside = !Inside;
		
		AnyOutside |= Outside;
		AllInside &= Inside;

		Inside = FastIntersectAABBPlane(vCenter, vExtents, XMLoadFloat4A(&_Plane[ePlane::P_RIGHT]));
		Outside = !Inside;
		
		AnyOutside |= Outside;
		AllInside &= Inside;

		Inside = FastIntersectAABBPlane(vCenter, vExtents, XMLoadFloat4A(&_Plane[ePlane::P_LEFT]));
		Outside = !Inside;
		
		AnyOutside |= Outside;
		AllInside &= Inside;
		
		Inside = FastIntersectAABBPlane(vCenter, vExtents, XMLoadFloat4A(&_Plane[ePlane::P_TOP]));
		Outside = !Inside;
		
		AnyOutside |= Outside;
		AllInside &= Inside;
		
		Inside = FastIntersectAABBPlane(vCenter, vExtents, XMLoadFloat4A(&_Plane[ePlane::P_BOTTOM]));
		Outside = !Inside;
		
		AnyOutside |= Outside;
		AllInside &= Inside;
		
		// If the box is outside any plane it is outside.
		if (AnyOutside)
			return(0);

		// If the box is inside all planes it is inside.
		if (AllInside)
			return(1);

		// The box is not inside all planes or outside a plane, it may intersect.
		return(-1);
	}
}
