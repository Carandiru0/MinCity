#include "pch.h"
#include "volumetricVisibility.h"
#include "MinCity.h"
#include "cVoxelWorld.h"

namespace Volumetric
{
	STATIC_INLINE_PURE XMMATRIX const XM_CALLCONV getProjectionMatrix(float const Width, float const Height,
																	  float const MinZ, float const MaxZ)
	{
		return(XMMatrixOrthographicLH(Width, Height, MinZ, MaxZ));
	}
	STATIC_INLINE XMMATRIX const XM_CALLCONV UpdateProjection(float const ZoomFactor, size_t const framecount)
	{
		static constexpr float const BASE_ZOOM_LEVEL = 17.79837387625f;	// do not change, calculated from Golden Ratio

		static tTime tLast(critical_now());
		static fp_seconds tLastDelta(delta());
		static float LastZoomFactor(ZoomFactor);

		point2D_t const framebufferSz(MinCity::getFramebufferSize());
		XMVECTOR xmFrameBufferSz;

		xmFrameBufferSz =  p2D_to_v2(framebufferSz);

		// with blue noise jittered offset added to frame buffer width/height, calculate new aspect ratio (width/height)
		float jitter = supernoise::blue.get1D(framecount);
										// always negative			// always positive
		jitter = ((framecount & 1) ? -SFM::abs(jitter - 0.5f) : SFM::abs(jitter - 0.5f));

		// add signed jitter to width / height, will be subpixel jitter
		xmFrameBufferSz = XMVectorAdd(xmFrameBufferSz, XMVectorDivide(_mm_set1_ps(jitter), xmFrameBufferSz));

		// aspect ratio
		XMFLOAT2A vFrameBufferSz;
		XMStoreFloat2A(&vFrameBufferSz, xmFrameBufferSz);
		float const fAspect = vFrameBufferSz.x / vFrameBufferSz.y;

		// lerp the zooming for extra smooth zoom
		tTime const tNow(critical_now());
		fp_seconds const tDelta(tNow - tLast);

		float const smoothzoom = SFM::lerp(LastZoomFactor, ZoomFactor, tDelta / (fp_seconds(delta()) + tLastDelta));

		if (ZoomFactor != LastZoomFactor) {

			if (0.0f != tDelta.count()) {
				tLastDelta = tDelta;
				LastZoomFactor = smoothzoom;
			}
		}
		tLast = tNow;

		// return new projection matrix
		return(getProjectionMatrix(fAspect * BASE_ZOOM_LEVEL * smoothzoom, BASE_ZOOM_LEVEL * smoothzoom,
			                       Globals::MINZ_DEPTH, Globals::MAXZ_DEPTH));
	}

	volumetricVisibility::volumetricVisibility()
		: _matProj{}, _Plane{}
	{
	}

	void volumetricVisibility::Initialize()
	{
		// Create Default Orthographic Projection matrix used by MinCity
		UpdateProjection(Globals::DEFAULT_ZOOM_SCALAR, 0);

		// Extract the 6 clipping planes and save them for frustum culling
		UpdateFrustum(XMMatrixIdentity(), Globals::DEFAULT_ZOOM_SCALAR, 0);
	}

	/*void XM_CALLCONV volumetricVisibility::CreateFromMatrixLH(FXMMATRIX const Projection)
	{
		// Corners of the projection frustum in homogenous space.
		inline XMVECTORF32 const HomogenousPoints[6] =
		{
			{  1.0f,  0.0f, 1.0f, 1.0f },   // right (at far plane)
			{ -1.0f,  0.0f, 1.0f, 1.0f },   // left
			{  0.0f,  1.0f, 1.0f, 1.0f },   // top
			{  0.0f, -1.0f, 1.0f, 1.0f },   // bottom

			{ 0.0f, 0.0f, 0.0f, 1.0f },     // near
			{ 0.0f, 0.0f, 1.0f, 1.0f }      // far
		};

		XMVECTOR Determinant;
		XMMATRIX matInverse = XMMatrixInverse(&Determinant, Projection);

		// Compute the frustum corners in world space.
		XMVECTOR Points[6];

		for (size_t i = 0; i < 6; ++i)
		{
			// Transform point.
			Points[i] = XMVector4Transform(HomogenousPoints[i], matInverse);
		}

		// Compute the slopes.
		Points[0] = Points[0] * XMVectorReciprocal(XMVectorSplatZ(Points[0]));
		Points[1] = Points[1] * XMVectorReciprocal(XMVectorSplatZ(Points[1]));
		Points[2] = Points[2] * XMVectorReciprocal(XMVectorSplatZ(Points[2]));
		Points[3] = Points[3] * XMVectorReciprocal(XMVectorSplatZ(Points[3]));

		_Slope[ePlane::P_RIGHT] = XMVectorGetX(Points[0]);
		_Slope[ePlane::P_LEFT] = XMVectorGetX(Points[1]);
		_Slope[ePlane::P_TOP] = XMVectorGetY(Points[2]);
		_Slope[ePlane::P_BOTTOM] = XMVectorGetY(Points[3]);

		// Compute near and far.
		Points[4] = Points[4] * XMVectorReciprocal(XMVectorSplatW(Points[4]));
		Points[5] = Points[5] * XMVectorReciprocal(XMVectorSplatW(Points[5]));

		_Slope[ePlane::P_NEAR] = XMVectorGetZ(Points[4]);
		_Slope[ePlane::P_FAR] = XMVectorGetZ(Points[5]);
	}
	*/
	STATIC_INLINE_PURE bool const XM_CALLCONV FastIntersectSpherePlane(_In_ FXMVECTOR const Center, _In_ FXMVECTOR const Radius, _In_ FXMVECTOR const Plane)
	{
		XMVECTOR const Dist(_mm_dp_ps(Center, Plane, 0xff));
		
		return(XMVector4EqualInt(XMVectorLess(Dist, Radius), XMVectorFalseInt()));  // function returns false when OUTSIDE plane, true when INSIDE
	}
	
	bool const XM_CALLCONV volumetricVisibility::SphereTestFrustum(FXMVECTOR const xmPosition, float const fRadius) const
	{
		// Load the sphere.
		XMVECTOR const vRadius( XMVectorNegate(XMVectorReplicate(fRadius)) );

		// Set w of the center to one so we can dot4 with all consecutive planes.
		XMVECTOR const vCenter( XMVectorInsert<0, 0, 0, 0, 1>(xmPosition, XMVectorSplatOne()) );

		// Test against each plane.

		// **** If the sphere is outside any plane it is outside.

		if (!FastIntersectSpherePlane(vCenter, vRadius, XMLoadFloat4A(&_Plane[ePlane::P_NEAR])))
			return(false);

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

	bool const XM_CALLCONV volumetricVisibility::AABBTestFrustum(FXMVECTOR const xmPosition, FXMVECTOR const xmExtents) const
	{
		// Load the box.
		XMVECTOR const vExtents(XMVectorNegate(xmExtents));

		// Set w of the center to one so we can dot4 with all consecutive planes.
		XMVECTOR const vCenter(XMVectorInsert<0, 0, 0, 0, 1>(xmPosition, XMVectorSplatOne()));

		// Test against each plane.

		// **** If the box is outside any plane it is outside.

		if (!FastIntersectAABBPlane(vCenter, vExtents, XMLoadFloat4A(&_Plane[ePlane::P_NEAR])))
			return(false);

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

	void XM_CALLCONV volumetricVisibility::UpdateFrustum(FXMMATRIX const xmView, float const ZoomFactor, size_t const framecount)
	{
		// always update jittered projection
		XMMATRIX const xmProj = UpdateProjection(ZoomFactor, framecount);

		XMMATRIX const xmViewProj( XMMatrixMultiply(xmView, xmProj) ); // always using the first projection matrix to prevent temporal visibility changes
		// save view
		XMStoreFloat4x4A(&_matView, xmView);
		// save projection
		XMStoreFloat4x4A(&_matProj, xmProj);

		XMFLOAT4X4A vp;
		XMStoreFloat4x4A(&vp, xmViewProj);
		
		// this is the best way to build the frustum culling volume, as we can use the combined view projection matrix
		// and this will result in a culling volume in world space coordinates !

		// *BUGFIX - planes must be normalized. do NOT change. They work when not normalized, but at a higly reduced precision. Normalization is neccessary to maintain accuracy/precision. (As seen by usage of sphere frustum check function)		
		XMStoreFloat4A(&_Plane[ePlane::P_NEAR],		XMPlaneNormalize(XMVectorSet(vp._13,  vp._23, vp._33, vp._43 )));
		XMStoreFloat4A(&_Plane[ePlane::P_FAR],		XMPlaneNormalize(XMVectorSet(vp._14 - vp._13, vp._24 - vp._23, vp._34 - vp._33, vp._44 - vp._43)));
		XMStoreFloat4A(&_Plane[ePlane::P_RIGHT],	XMPlaneNormalize(XMVectorSet(vp._14 - vp._11, vp._24 - vp._21, vp._34 - vp._31, vp._44 - vp._41)));
		XMStoreFloat4A(&_Plane[ePlane::P_LEFT],		XMPlaneNormalize(XMVectorSet(vp._14 + vp._11, vp._24 + vp._21, vp._34 + vp._31, vp._44 + vp._41)));
		XMStoreFloat4A(&_Plane[ePlane::P_TOP],		XMPlaneNormalize(XMVectorSet(vp._14 - vp._12, vp._24 - vp._22, vp._34 - vp._32, vp._44 - vp._42)));
		XMStoreFloat4A(&_Plane[ePlane::P_BOTTOM],	XMPlaneNormalize(XMVectorSet(vp._14 + vp._12, vp._24 + vp._22, vp._34 + vp._32, vp._44 + vp._42)));
	}

} // end ns


