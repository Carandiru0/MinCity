/* Copyright (C) 20xx Jason Tully - All Rights Reserved
 * You may use, distribute and modify this code under the
 * terms of the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License
 * http://www.supersinfulsilicon.com/
 *
This work is licensed under the Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License.
To view a copy of this license, visit http://creativecommons.org/licenses/by-nc-sa/4.0/
or send a letter to Creative Commons, PO Box 1866, Mountain View, CA 94042, USA.
 */
#include "pch.h"
#include "globals.h"
#include "shockwave.h"
#include "volumetricradialgrid.h"

#include <vector>

static constexpr milliseconds const LIFETIME_ANIMATION = milliseconds(12000);

inline std::vector<Volumetric::xRow, tbb::scalable_allocator<Volumetric::xRow>>	ShockwaveInstance::ShockwaveRows;  // vector memory is persistance across instances
																																																										 // only one shockwave instance is active at a time
																																																										 // this improves performance and stability
																																																										 // by avoiding reallocation of vector
																																																										 // for any new or deleted shockwave instance
__vectorcall sShockwaveInstance::sShockwaveInstance( FXMVECTOR const WorldCoordOrigin, float const Radius)
		: sRadialGridInstance(WorldCoordOrigin, Radius, LIFETIME_ANIMATION, ShockwaveRows)
{
    setScale((float)ShockwaveInstance::SHOCKWAVE_AMPLITUDE_MAX);

}
		
STATIC_INLINE_PURE float const __vectorcall singlewave(float const x, float const t)
{
	float const X( x - t * t );
	return( -SFM::__cos(X) * SFM::__exp(-X * X));

	// return( -SFM::__cos(X) * (SFM::__exp(-X * (X)) - x*0.01f)); // repeating wave with gradually increasing amplitude
}

// op does not r/w any global memory voxel_op_fnDispersion
__forceinline __declspec(noalias) bool const __vectorcall sShockwaveInstance::op(FXMVECTOR const vDisplacement, float const t_, Volumetric::voxelShaderDesc&& __restrict out) const
{
    out.distance = singlewave(_mm_cvtss_f32(XMVector2Length(XMVectorScale(vDisplacement, getRadius()))), t_);
    out.emissive = true;

	return(true);
}

bool const isShockwaveVisible( ShockwaveInstance const* const __restrict Instance )
{
	// approximation, floored location while radius is ceiled, overcompensated, to use interger based visibility test
	return( !Volumetric::isRadialGridNotVisible(Instance->getLocation(), Instance->getRadius())  );
}

bool const UpdateShockwave( tTime const tNow, ShockwaveInstance* const __restrict Instance )
{
	// Update base first (required)
	std::optional<tTime const> const tLast(Instance->Update(tNow));

	if ( std::nullopt != tLast )	// still alive?
	{
		fp_seconds const tDelta(tNow - *tLast);

        //Instance->getRotation() = Volumetric::RadialGridInstance::DEFAULT_ROTATION;
#ifdef DEBUG_RANGE
		static uint32_t tLastDebug;	// place holder testing
		if (tNow - tLastDebug > 250) {

			if ( Instance->RangeMax > -FLT_MAX && Instance->RangeMin < FLT_MAX ) {
			//	DebugMessage("%.03f to %.03f", Instance->RangeMin, Instance->RangeMax);
			}
			tLastDebug = tNow;
		}
#endif
		
		return(true);
	}
	
	return(false); // dead instance
}

void RenderShockwave( ShockwaveInstance const* const __restrict Instance, tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict voxelDynamic )
{	
	Volumetric::renderRadialGrid<
                            Volumetric::eRadialGridRenderOptions::FILL_COLUMNS_DOWN | Volumetric::eRadialGridRenderOptions::FOLLOW_GROUND_HEIGHT>
							(Instance, voxelDynamic); 
}


/*
static constexpr uint32_t const FRAMEBUFFER_Width_SATBITS = Constants::SATBIT_64,
														    FRAMEBUFFER_Height_SATBITS = Constants::SATBIT_32;
static constexpr float const FRAMEBUFFER_WIDTH = (1 << FRAMEBUFFER_Width_SATBITS),
														 FRAMEBUFFER_HEIGHT = (1 << FRAMEBUFFER_Height_SATBITS);

static constexpr float const INV_FRAMEBUFFER_WIDTH = 1.0f / (float)FRAMEBUFFER_WIDTH,
														 INV_FRAMEBUFFER_HEIGHT = 1.0f / (float)FRAMEBUFFER_HEIGHT;

static float localtime = 0.0f;

STATIC_INLINE_PURE float const singlewave(float const x, float const t)
{
    float const X = x - t * t;
    return( -arm_cos_f32(X) * __expf(-X * X) );
}

STATIC_INLINE_PURE float const dispersion2d(vec2_t const v, float const t) // testing FabriceNeyret2's waves dispersion function (source: https://www.shadertoy.com/view/MtBSDG)
{
    float const r = ((0.16f/t)*(((v.x*v.x+v.y*v.y))));

	//single wave
    return( singlewave(r, t) );// dispersion for gravity waves (original)
       
}

STATIC_INLINE_PURE float const fn(vec3_t v)
{
	v = v3_muls(v, 8.0f);
  return( (v.z) + clampf(dispersion2d(vec2_t(v.x, v.y), localtime), -1.0f, 1.0f) );
}

STATIC_INLINE_PURE float const comb(float const v)
{
    return( 4.5f - 0.05f / __powf(0.5f + 0.5f * arm_cos_f32(v * 2.0f * M_PI), 10.0f) );
}

STATIC_INLINE_PURE float texGrid(vec3_t v)
{
    v.x = __fractf(v.x * 16.0f);
    v.y = __fractf(v.y * 16.0f);
    float q = 0.0f;
    q = max(q, comb(v.x));
    q = max(q, comb(v.y));
    return q;
}

STATIC_INLINE_PURE vec2_t const texHeight(vec3_t v)
{
    v = v3_fmas( 0.5f, v3_clamp(v, -1.0f, 1.0f), 0.5f );
    return( vec2_t(v.z, 1.0f - v.z) ); //(v.z + (1.0 - v.z)) * 0.5f;
}

STATIC_INLINE_PURE vec3_t const camera(vec2_t const uv, float const depth)
{
    //float phi = iTime * 0.0;
    //float phi = 2.0 * iMouse.x / iResolution.x - 1.0;
    vec3_t v = vec3_t(uv.x, uv.y, depth);
    
    // isometry
    vec3_t iso;
    iso.x =  v.x - v.y - v.z;
    iso.y = -v.x - v.y - v.z;
    iso.z =        v.y - v.z;
    
    //iso.xy = uv.xy;
    //iso.z = depth;
    return(iso);
}

STATIC_INLINE_PURE vec3_t const bisection(vec3_t a, vec3_t b) // slow root-finder, but simple & stable
{
		static constexpr uint32_t const n = 11;
	
    float fa = fn(a);
    float fb = fn(b);
   
    for(uint32_t i = n; i != 0; --i)
    {
        //vec3 c = (a + b) / 2.0;
				vec3_t const c = v3_muls( v3_add(a, b), 0.5f );
			
        float const fc = fn(c);
        if(fc * fa > 0.0f)
        {
            a = c;
            fa = fc;
        }
        else if (fc * fb > 0.0f)
        {
            b = c;
            fb = fc;
        }
				else
            break;
    }
    
    return( v3_muls( v3_add(a, b), 0.5f ) );
}

STATIC_INLINE_PURE vec3_t const illinois(vec3_t a, vec3_t b) // slow root-finder, but simple & stable
{
    static constexpr uint32_t const n = 4;
	
		float fa = fn(a);
    float fb = fn(b);
    
    int side = 0;
        
    for(uint32_t i = n; i != 0; --i)  // slighly faster root finder (illinois vs bisection) migth want to revert to bisection for simpler math
    {
        // c = (fa*b - fb*a) / (fa - fb);
				vec3_t const c = v3_divs( v3_sub( v3_muls(b,fa), v3_muls(a,fb) ), (fa - fb) );
        float const fc = fn(c);
        
        if(fc * fa > 0.0f)
        {
            a = c;
            fa = fc;
            if (side>0) 
                fb *= 0.5f;
         	side = -1;
        }
        else if (fc * fb > 0.0f)
        {
            b = c;
            fb = fc;
            if (side<0) 
                fa *= 0.5f;
         	side = +1;
        }
        else
            break;
    }
    
		// one more convergence
    return( v3_divs( v3_sub( v3_muls(b,fa), v3_muls(a,fb) ), (fa - fb) ) );
}

void RenderShockwavePP(float const tNow)
{
	uint8_t * const __restrict EffectFrameBuffer( OLED::getEffectFrameBuffer_0() );
	uint32_t yPixel(FRAMEBUFFER_HEIGHT); // inverted y-axis

	vec2_t uv;
	while (0 != yPixel)
	{
		uint32_t xPixel(FRAMEBUFFER_WIDTH);

		uint8_t * __restrict UserFrameBuffer = EffectFrameBuffer + (yPixel << FRAMEBUFFER_Width_SATBITS);

		uv.y = lerp(-1.0f, 1.0f, 1.0f - (float)yPixel * INV_FRAMEBUFFER_HEIGHT);

		while (0 != xPixel)
		{
			float shade(0.0f);
			
			uv.x = lerp(-1.0f, 1.0f, (float)xPixel * INV_FRAMEBUFFER_WIDTH);

			localtime = __fractf(tNow / 18.0f) * 16.0f;

			vec2_t uvo = uv;//v2_subs( v2_muls( v2_mul(uv, vec2_t(INV_FRAMEBUFFER_WIDTH, INV_FRAMEBUFFER_HEIGHT)), 2.0f), 1.0f); // 2.0 * uv / iResolution.xy - 1.0;
			uvo.x *= (FRAMEBUFFER_WIDTH / FRAMEBUFFER_HEIGHT);
			uvo = v2_muls(uvo, 0.16f);
		
			vec3_t a = camera(uvo,0.5f);
			vec3_t b = camera(uvo,-0.5f);
			
			vec3_t v = illinois(a, b);
			//vec3_t v = bisection(a, b);

			vec2_t const height = texHeight(v);
			
			if (height.x - height.y > 0.005f) {
					shade = ((height.x + (1.0f - height.y)) * 0.5f) + 0.09f * texGrid(v);
			//else
			//		shade = texGrid(v);
						//color = vec3(0);
				
			*UserFrameBuffer = __USAT(int32::__roundf(shade * Constants::nf255), Constants::SATBIT_256 );
		}
			++UserFrameBuffer;

			--xPixel;
		}
		
		--yPixel;
	}
	
	//OLED::Render8bitScreenLayer_Back(EffectFrameBuffer);
	OLED::RenderScreenLayer_Upscale<uint8_t, FRAMEBUFFER_Width_SATBITS, FRAMEBUFFER_Height_SATBITS>(getBackBuffer(), EffectFrameBuffer);
}

float localtime;

//float sfract(float val, float scale)
//{
//    return (2.0 * fract(0.5 + 0.5 * val / scale) - 1.0) * scale;
//}

float singlewave(float x, float t)
{
    float X = x - t * t;
    return -cos(X) * exp(-X * X);
}
float wave(float x, float k, float c, float t)
{
    float X = x - c * t;
    return -cos(k * X) * exp(-X * X);
}

float dispersion2d(vec2 v, float t) // testing FabriceNeyret2's waves dispersion function (source: https://www.shadertoy.com/view/MtBSDG)
{
    float r = ((0.16/t)*(((v.x*v.x+v.y*v.y))));
    //float r = length(v);
    
  //  const float n = 3.0;
  //  float sum = 0.0;
  //  for (float k = 1.0; k < n; k++)
  ///  {
       // sum += wave(r, k, sqrt(k), t) / k + wave(-r, k, sqrt(k), t) / k; // dispersion for capillary waves (no-"kink"-tweak)
    //   sum += wave((r), k, (1.0 / (k/t)), t) / k;// dispersion for gravity waves (original)
       //sum -= wave(abs(r), k, (k/t), t) / k; // dispersion for capillary waves (original)
       
        // sum += wave(r, k, 1.0 / sqrt(k), t) / k + wave(-r, k, 1.0 / sqrt(k), t) / k;// dispersion for gravity waves (no-"kink"-tweak)
   // }
    
    //return v.y < 0.0 ? n * sum / r : sum; // comparing variant *1 with variant *n/d looks rather similar in this scale
    //return sum; // NOTE: using the simple 1d variant instead of the correct 2d variant here (looks similar in this scale)

	//single wave
    return singlewave(r, t);// dispersion for gravity waves (original)
       
}

float fn(vec3 v)
{
    v *= 8.0;
    return (v.z) + clamp(dispersion2d(v.xy, localtime), -1.0, 1.0);
}

//vec3 nrm(vec3 v)
//{
//    v *= 10.0;
//    float d = 0.01;
//    vec2 grad;
//    grad.x = dispersion2d(v.xy + vec2(d, 0.0), localtime) - dispersion2d(v.xy + vec2(-d, 0.0), localtime);
 //   grad.y = dispersion2d(v.xy + vec2(0.0, d), localtime) - dispersion2d(v.xy + vec2(0.0, -d), localtime);
//    return normalize(vec3(-grad, d));
//}

float comb(float v)
{
    return 4.5f - 0.05f / pow(0.5 + 0.5 * cos(v * 2.0 * pi), 10.0);
}

vec3 texGrid(vec3 v)
{
    v.x = fract(v.x * 8.0);
    v.y = fract(v.y * 8.0);
    float q = 0.0;
    q = max(q, comb(v.x));
    q = max(q, comb(v.y));
    return vec3(q);
}

vec3 texHeight(vec3 v)
{
    v = 0.5 + 0.5 * clamp(v, -1.0, 1.0);
    return vec3(0.0, v.z, 1.0 - v.z);
}

vec3 texLight(vec3 v)
{
    return vec3(1.0,1.0,1.0);
}

vec3 camera(vec2 uv, float depth)
{
    //float phi = iTime * 0.0;
    //float phi = 2.0 * iMouse.x / iResolution.x - 1.0;
    vec3 v = vec3(uv, depth);
    
    // isometry
    vec3 iso;
    iso.x =  v.x - v.y - v.z;
    iso.y = -v.x - v.y - v.z;
    iso.z =        v.y - v.z;
    
    //iso.xy = uv.xy;
    //iso.z = depth;
    return iso;
}

vec3 illinois(vec3 a, vec3 b) // slow root-finder, but simple & stable
{
    float fa = fn(a);
    float fb = fn(b);
    const int n = 3;
    int side = 0;
    
    vec3 c;
    
    for(int i = 0; i < n; i++)
    {
        c = (a + b) * 0.5;
        c = (fa*b - fb*a) / (fa - fb);
        float fc = fn(c);
        
        if(fc * fa > 0.0)
        {
            a = c;
            fa = fc;
            if (side>0) 
                fb *= 0.5;
         	side = -1;
        }
        else if (fc * fb > 0.0)
        {
            b = c;
            fb = fc;
            if (side<0) 
                fa *= 0.5;
         	side = +1;
        }
        else
            break;
    }
    
   // return (a + b) * 0.5; // one more convergence
    return (fa*b - fb*a) / (fa - fb);
}

void mainImage( out vec4 fragColor, in vec2 fragCoord )
{
    localtime = fract(iTime / 18.0) * 16.0;

    vec2 uv = 2.0 * fragCoord.xy / iResolution.xy - 1.0;
    uv.x *= iResolution.x / iResolution.y;
    uv /= 2.0;
	
    vec3 a = camera(uv.xy,0.5);
    vec3 b = camera(uv.xy,-0.5);
    
    vec3 color = vec3(0);
 
        vec3 v = illinois(a, b);

        vec3 height = texHeight(v);
        
        if (height.y - height.z > 0.005f)
            color = texLight(v) * height + 0.09 * texGrid(v);
        else
            color = texGrid(v);
        	//color = vec3(0);
    
    //fragColor = vec4(color * vec3(v*4.), 1.0);
    fragColor = vec4(color, 1.0);
}
*/