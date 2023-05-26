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
	STATIC_INLINE XMMATRIX const XM_CALLCONV UpdateProjection(float ZoomFactor, size_t const framecount)
	{
		static constexpr float const BASE_ZOOM_LEVEL = 17.79837387625f;	// do not change, calculated from Golden Ratio

		/// *********************************************************************************** do not change, precise subpixel offset (verified) ****************************************************************************///
		XMVECTOR const xmFrameBufferSz(p2D_to_v2(MinCity::getFramebufferSize()));

		// aspect ratio
		XMFLOAT2A vFrameBufferSz;
		XMStoreFloat2A(&vFrameBufferSz, xmFrameBufferSz);
		float const fAspect = vFrameBufferSz.x / vFrameBufferSz.y;

		// with blue noise jittered offset added to frame buffer width/height, calculate new aspect ratio (width/height)
		float jitter = supernoise::blue.get1D(framecount);
										// always negative			// always positive
		jitter = ((framecount & 1) ? -SFM::abs(jitter - 0.5f) : SFM::abs(jitter - 0.5f)); // half-pixel offset maximum magnitude in either direction, total distance between two extremes is one pixel.

		// simplify & pre-scale
		ZoomFactor = ZoomFactor * BASE_ZOOM_LEVEL;

		XMVECTOR xmJitter = XMVectorDivide(XMVectorReplicate(jitter), xmFrameBufferSz); 
		xmJitter = XMVectorScale(xmJitter, ZoomFactor); // simplify & pre-scale //*bugfix - keep it at half pixel

		XMFLOAT2A vJitterSz;
		XMStoreFloat2A(&vJitterSz, xmJitter);

		// return new projection matrix - jitter applied - Temporal Antialiasing Post Process shader can use the subpixel jitter and produce a better neighbourhood clamping result. Effectively the subpixel component of the anti-aliasing.
		return(getProjectionMatrix(SFM::__fma(fAspect, ZoomFactor, vJitterSz.x), ZoomFactor + vJitterSz.y,
			                       Iso::VOX_MINZ_SCALAR * Globals::MINZ_DEPTH, Globals::MAXZ_DEPTH));
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

#ifndef NDEBUG
		// hard-coded radius value check
		static constexpr float const RADII_EPSILON = 0.01f; // looking for the numbers to significantly differ while not being prone to floating point error or roundoff
		// std::hypot(Iso::VOX_SIZE, Iso::VOX_SIZE, Iso::VOX_SIZE)
		assert_print(std::abs(getVoxelRadius() - std::hypot(Iso::VOX_SIZE, Iso::VOX_SIZE, Iso::VOX_SIZE)) < RADII_EPSILON, "Voxel radius is incorrect for current voxel size, update required!");
		// std::hypot(Iso::MINI_VOX_SIZE, Iso::MINI_VOX_SIZE, Iso::MINI_VOX_SIZE)
		assert_print(std::abs(getMiniVoxelRadius() - std::hypot(Iso::MINI_VOX_SIZE, Iso::MINI_VOX_SIZE, Iso::MINI_VOX_SIZE)) < RADII_EPSILON, "Mini Voxel radius is incorrect for current minivoxel size, update required!");
#endif

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
	
	void XM_CALLCONV volumetricVisibility::UpdateFrustum(FXMMATRIX const xmView, float const ZoomFactor, size_t const framecount)
	{
		// always update jittered projection
		XMMATRIX const xmProj = UpdateProjection(ZoomFactor, framecount);

		// save view
		XMStoreFloat4x4A(&_matView, xmView);
		// save projection
		XMStoreFloat4x4A(&_matProj, xmProj);

		XMFLOAT4X4A vp;
		XMStoreFloat4x4A(&vp, XMMatrixMultiply(xmView, xmProj));
		
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


