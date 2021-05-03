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
	public:
		static constexpr float const MIN_LIGHT_RADIUS_SCALAR = 3.00f; // Using inverse square law, light would be 10% of its original intensity

	public:
		// accessors //
		__inline XMMATRIX const XM_CALLCONV getViewMatrix() const { return(XMLoadFloat4x4A(&_matView)); }
		__inline XMMATRIX const XM_CALLCONV getProjectionMatrix() const { return(XMLoadFloat4x4A(&_matProj)); } 
		
		STATIC_INLINE_PURE XMMATRIX const XM_CALLCONV getProjectionMatrix(float const Width, float const Height,
																		  float const MinZ, float const MaxZ);

		// main methods //
		bool const XM_CALLCONV SphereTestFrustum(FXMVECTOR const xmPosition, float const fRadius) const;
		bool const XM_CALLCONV AABBTestFrustum(FXMVECTOR const xmPosition, FXMVECTOR const xmExtents) const;

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



	__inline __declspec(noalias) XMMATRIX const XM_CALLCONV volumetricVisibility::getProjectionMatrix(float const Width, float const Height,
																						              float const MinZ, float const MaxZ)
	{
		return(XMMatrixOrthographicLH(Width, Height, MinZ, MaxZ));
	}
}
