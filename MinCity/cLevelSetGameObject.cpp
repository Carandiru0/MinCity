#include "pch.h"
#include "globals.h"
#include "cLevelSetGameObject.h"
#include "MinCity.h"
#include "cVoxelWorld.h"
#include <tbb/tbb.h>

namespace world
{
	static void OnRelease(void const* const __restrict _this) // private to this file
	{
		if (_this) {
			cLevelSetGameObject::remove(static_cast<cLevelSetGameObject const* const>(_this));
		}
	}

	cLevelSetGameObject::cLevelSetGameObject(Volumetric::voxelModelInstance_Dynamic* const& instance_, Volumetric::voxelModel_Dynamic* const& model_)
		: tProceduralGameObject(instance_, model_)
	{
		instance_->setOwnerGameObject<cLevelSetGameObject>(this, &OnRelease);
		instance_->setVoxelEventFunction(&cLevelSetGameObject::OnVoxel);

		if (nullptr == _bits) {

			_bits = local_volume::create();
		}
	}

	cLevelSetGameObject::cLevelSetGameObject(cLevelSetGameObject&& src) noexcept
		: tProceduralGameObject(std::forward<tProceduralGameObject&&>(src))
	{
		src.free_ownership();

		// important
		if (Instance && *Instance) {
			(*Instance)->setOwnerGameObject<cLevelSetGameObject>(this, &OnRelease);
			(*Instance)->setVoxelEventFunction(&cLevelSetGameObject::OnVoxel);
		}
		// important
		if (src.Instance && *src.Instance) {
			(*src.Instance)->setOwnerGameObject<cLevelSetGameObject>(nullptr, nullptr);
			(*src.Instance)->setVoxelEventFunction(nullptr);
		}
	}
	cLevelSetGameObject& cLevelSetGameObject::operator=(cLevelSetGameObject&& src) noexcept
	{
		tProceduralGameObject::operator=(std::forward<tProceduralGameObject&&>(src));
		src.free_ownership();
		
		// important
		if (Instance && *Instance) {
			(*Instance)->setOwnerGameObject<cLevelSetGameObject>(this, &OnRelease);
			(*Instance)->setVoxelEventFunction(&cLevelSetGameObject::OnVoxel);
		}
		// important
		if (src.Instance && *src.Instance) {
			(*src.Instance)->setOwnerGameObject<cLevelSetGameObject>(nullptr, nullptr);
			(*src.Instance)->setVoxelEventFunction(nullptr);
		}

		return(*this);
	}

	read_only inline float const _xmInvBounds(1.0f / (float)Volumetric::LEVELSET_MAX_DIMENSIONS_XYZ);

	// If currently visible event:
	VOXEL_EVENT_FUNCTION_RETURN __vectorcall cLevelSetGameObject::OnVoxel(VOXEL_EVENT_FUNCTION_PARAMETERS)
	{
		return(reinterpret_cast<cLevelSetGameObject const* const>(_this)->OnVoxel(xmIndex, voxel, vxl_index));
	}
	// ***** watchout - thread safety is a concern here this method is executed in parallel ******
	VOXEL_EVENT_FUNCTION_RETURN __vectorcall cLevelSetGameObject::OnVoxel(VOXEL_EVENT_FUNCTION_RESOLVED_PARAMETERS) const
	{
		tTime const tNow(now());

		uvec4_v const localIndex(voxel.x, voxel.y, voxel.z);
		
		voxel.setAdjacency(encode_adjacency(localIndex)); // apply adjacency

		XMVECTOR xmUVW(XMVectorMultiply(localIndex.v4f(), XMVectorReplicate(_xmInvBounds)));

		xmUVW = SFM::__fms(xmUVW, XMVectorReplicate(2.0f), XMVectorReplicate(1.0f));
		
		uvec4_t rgba;
		SFM::saturate_to_u8(XMVectorScale(XMVector3Length(xmUVW), 255.0f), rgba);
		
		bool const odd(((voxel.x & 1) & (voxel.y & 1) & (voxel.z & 1)));
		voxel.Color = SFM::pack_rgba(rgba);
		//voxel.setMetallic(true);
		voxel.setRoughness(0.5f);
		//voxel.Emissive = odd;
		//voxel.Transparent = true;
		//voxel.Hidden = !odd;
		
		return(voxel);
	}

	STATIC_INLINE_PURE bool const __vectorcall op(uvec4_v const voxel, float const t)
	{				
		XMVECTOR xmUVW(XMVectorMultiply(voxel.v4f(), XMVectorReplicate(_xmInvBounds)));
		
		//xmUVW = SFM::__fms(xmUVW, XMVectorReplicate(2.0f), XMVectorReplicate(1.0f));
		
		XMFLOAT3A vUVW;
		XMStoreFloat3A(&vUVW, xmUVW);
		
		XMVECTOR xmSDF;
		
		//p.x += sin(p.y + PI2 * fract(iTime * 0.8)) * 0.3;
		//vec2 q = vec2(length(p.xz), p.y);
		//return smoothstep(0.0, 1.2, q.x) - 0.1 * q.y;
		
		//xmSDF = XMVector3Length(xmUVW);
		//xmSDF = XMVectorSubtract(xmSDF, XMVectorReplicate(0.75f));
		
		//XMFLOAT3A vSDF;
		//XMStoreFloat3A(&vSDF, xmSDF);
		
		XMFLOAT3A vSaved(vUVW);
		vUVW.z = SFM::sincos(&vUVW.x, -vUVW.y + XM_2PI * SFM::fract(t * 0.8f));
				
		vUVW.x = vSaved.x + vUVW.x * 0.3f;
		vUVW.z = vSaved.z + vUVW.z * 0.3f;
		
		xmSDF = XMVectorSet(XMVectorGetX(XMVector2Length(XMVectorSet(vUVW.x, vUVW.z, 0.0f, 0.0f))), -vUVW.y, 0.0f, 0.0f);
		
		xmSDF = SFM::triangle_wave(XMVectorReplicate(-1.0f), XMVectorReplicate(1.0f), xmUVW);
		xmSDF = SFM::__sin(XMVectorScale(xmSDF, XM_2PI * SFM::__sin(t * 0.125f)));
		//xmSDF = XMVectorSubtract(xmSDF, XMVectorReplicate(0.25f));
		
		XMFLOAT3A vSDF;
		XMStoreFloat3A(&vSDF, xmSDF);
		
		float const fSDF(vSDF.x + vSDF.y + vSDF.z);
		
		//float const fSDF(SFM::smoothstep(0.0f, 1.2f, vSDF.x) - 0.1f * vSDF.y);
		
		
		return(fSDF < 0.0f && fSDF > -Iso::MINI_VOX_SIZE);  // less than zero is inside, added > -Iso::MINI_VOX_SIZE * 0.5f makes the resulting voxel model hollow.
	}
	void cLevelSetGameObject::OnUpdate(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta)
	{
		typedef struct alignas(16) no_vtable sRenderFuncBlockChunk {

		private:
			alignas(CACHE_LINE_BYTES) local_volume* const __restrict	bits;
			tbb::atomic<uint32_t>& __restrict							active_voxel_count; // only valid after all parallel operations
			Volumetric::voxB::voxelDescPacked* const __restrict			linear_voxel_array;
			
			sRenderFuncBlockChunk& operator=(const sRenderFuncBlockChunk&) = delete;
		public:
			__forceinline sRenderFuncBlockChunk(local_volume* const __restrict bits_, tbb::atomic<uint32_t>& __restrict active_voxel_count_, Volumetric::voxB::voxelDescPacked* const __restrict linear_voxel_array_)
				: bits(bits_), active_voxel_count(active_voxel_count_), linear_voxel_array(linear_voxel_array_)
			{}

			void __vectorcall operator()(tbb::blocked_range3d<uint32_t, uint32_t, uint32_t> const& r) const {

				uint32_t const	// pull out into registers from memory
					z_begin(r.pages().begin()),
					z_end(r.pages().end()),
					y_begin(r.rows().begin()),
					y_end(r.rows().end()),
					x_begin(r.cols().begin()),
					x_end(r.cols().end());

				float const tNow(duration_cast<fp_seconds>(now() - start()).count());
				
				for (uint32_t z = z_begin; z < z_end; ++z) {

					for (uint32_t y = y_begin; y < y_end; ++y) {

						for (uint32_t x = x_begin; x < x_end; ++x) {

							bool const inside(op(uvec4_v(x, z, y), tNow));
							bits->write_bit(x, z, y, inside); // clear or set required
							
							if (inside) {
								Volumetric::voxB::voxelDescPacked voxel{};
								voxel.x = x; voxel.y = z; voxel.z = y;
								linear_voxel_array[active_voxel_count++] = voxel;
							}
						}
					}
				}
			}
		} const RenderFuncBlockChunk;
		
		tbb::atomic<uint32_t> numVoxels(0); // always reset
		
		tbb::auto_partitioner part; // load balancing - do NOT change - adapts to variance of whats in the voxel grid
		tbb::parallel_for(tbb::blocked_range3d<uint32_t, uint32_t, uint32_t>(
			0, Volumetric::LEVELSET_MAX_DIMENSIONS_XYZ, 8,	// page
			0, Volumetric::LEVELSET_MAX_DIMENSIONS_XYZ, 8,	// row
			0, Volumetric::LEVELSET_MAX_DIMENSIONS_XYZ, 8	// col
			),
			RenderFuncBlockChunk(_bits, numVoxels, getModel()->_Voxels), part
		);
	
		getModel()->_numVoxels = numVoxels;
		
		getModelInstance()->setAzimuth(getModelInstance()->getAzimuth() + 0.1f * tDelta.count());
	}

	cLevelSetGameObject::~cLevelSetGameObject()
	{
		SAFE_DELETE(Model); // *required*

		if (_bits) {

			local_volume::destroy(_bits);
			_bits = nullptr;
		}
	}
} // end ns