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
		// Load the sphere.
		XMVECTOR const vRadius(XMVectorNegate(XMVectorReplicate(fRadius * ERROR_COMPENSATION)));	// allow error % over

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
		// Load the box.		// used to negate vector, now negation is included in scale
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
}
