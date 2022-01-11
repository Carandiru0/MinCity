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
#include "rain.h"
#include "volumetricradialgrid.h"

#include "cVoxelWorld.h"
#include "MinCity.h"

#include <Imaging/Imaging/Imaging.h>
#include <vector>

// ##### DO NOT CHANGE ANY OF THESE VALUES - TWEAKED FOR PERFORMANCE AND GOOD VISUAL COMPROMISE

static constexpr int32_t const RAIN_VOLUME_DENSITY = 26;  // percentage of volume that is visible
static constexpr milliseconds const LIFETIME_ANIMATION = milliseconds(99999999999);

static constexpr float const MAX_STREAK_LENGTH = 11.0f;

static constexpr float const SPEED_SCALE = 0.1333333f * SFM::GOLDEN_RATIO_ZERO * Iso::MINI_VOX_SIZE;
static constexpr float const MAX_DROP_SPEED = 9.0f * SPEED_SCALE,
                             MIN_DROP_SPEED = 7.0f * SPEED_SCALE;

inline vector<Volumetric::xRow>	RainInstance::RainRows;  // vector memory is persistance across instances
inline Imaging RainInstance::_imgRain[eRainImageType::COUNT]{ nullptr };   // only one global instance for rain is required, so only need one image                                                                                                                                                                                                                                 // only one shockwave instance is active at a time                                                                                                                                                                                                                                       // this improves performance and stability

STATIC_INLINE_PURE void newDrop(uint8_t& __restrict rdrop_speed)
{
    static constexpr int32_t const RAIN_DENSITY = RAIN_VOLUME_DENSITY;  // percentage of volume that is visible

    if (PsuedoRandomNumber(-(100 - RAIN_DENSITY), RAIN_DENSITY) >= 0) { // rendered rain drop count reduction by %
        rdrop_speed = PsuedoRandomNumber16(1, 255);
    }
    else {
        rdrop_speed = 0;
    }
}

__vectorcall sRainInstance::sRainInstance(FXMVECTOR const WorldCoordOrigin, float const Radius)
    : sRadialGridInstance(WorldCoordOrigin, Radius, LIFETIME_ANIMATION, RainRows)
{
    setScale((float)Volumetric::voxelOpacity::getHeight());
    setStepScale((float)Volumetric::Konstants::VOXEL_RAIN_SCALE);

    constexpr uint32_t const width(Volumetric::voxelOpacity::getWidth()), height(Volumetric::voxelOpacity::getDepth());

    // only created once and shared among instances //
    if (nullptr == _imgRain[0]) {
        Imaging imgRain[COUNT];

        for (uint32_t i = 0; i < COUNT; ++i) {
            imgRain[i] = ImagingNew(MODE_L, width, height);
        }

        // initialize w/ blue noise
        constexpr float const invDimensionX = (float(width) / float(supernoise::cBlueNoise::DIMENSIONS)) / float(width);  // blue noise image repeats n times over rain image
        constexpr float const invDimensionY = (float(height) / float(supernoise::cBlueNoise::DIMENSIONS)) / float(height);

        float fX(0.0f), fY(0.0f);
        for (uint32_t y = 0; y < height; ++y) {

            fX = 0.0f;
            for (uint32_t x = 0; x < width; ++x) {

                uint32_t const pixel(y * width + x);

                // gives the rain drop it's starting fall height which is determined by the blue noise data (aka gray scale value)
                imgRain[DISTANCE]->block[pixel] = SFM::saturate_to_u8(supernoise::blue.get2D(fX, fY) * 255.0f);
                
                newDrop(imgRain[SPEED]->block[pixel]);

                fX += invDimensionX;
            }
            fY += invDimensionY;
        }

        for (uint32_t i = 0; i < COUNT; ++i) {
            _imgRain[i] = std::move(imgRain[i]); // pointer only moved
        }
    }
}

// voxel_op_fnRain
read_only inline XMVECTORF32 const _xmRainDimensions{ float(Volumetric::voxelOpacity::getWidth()), float(Volumetric::voxelOpacity::getDepth()), 1.0f, 1.0f };
__forceinline __declspec(noalias) bool const __vectorcall sRainInstance::op(FXMVECTOR const vDisplacement, float const tLocal, Volumetric::voxelShaderDesc&& __restrict out) const
{
    static constexpr float const Inv255 = 1.0f / 255.0f;

    // *frame progression based algorithm* does not depend on time progression, only on frame progression - so any call to this function advances the rain drop simulation for each raindrop new or old 
    // - can't be "paused" -
    // *bugfix: now pauses properly see MinCity.cpp StageResources()
    
    // vDisplacement is in -1.0f...1.0f range, convert to 0.0f...1.0f range and scale by rain image dimensions
    XMVECTOR xmPixel = XMVectorMultiply(SFM::__fma(vDisplacement, _mm_set1_ps(0.5f), _mm_set1_ps(0.5f)), _xmRainDimensions);
    // clamp to image dimensions //
    xmPixel = SFM::min(_xmRainDimensions, xmPixel);
    xmPixel = SFM::max(XMVectorZero(), xmPixel);

    point2D_t const pixel2D(SFM::floor_to_u32(xmPixel).v);
    uint32_t const pixel(pixel2D.y * Volumetric::voxelOpacity::getWidth() + pixel2D.x);
    
    uint8_t& __restrict rdrop_distance(sRainInstance::_imgRain[sRainInstance::DISTANCE]->block[pixel]);
    uint32_t const drop_distance(rdrop_distance);

    if (0 == drop_distance) { // drop has hit bottom
        // * current drop
        rdrop_distance = 255;

        // * new drop
        newDrop(sRainInstance::_imgRain[sRainInstance::SPEED]->block[pixel]);
            
        return(false); // this voxel is skipped
    }

    uint32_t const drop_speed(sRainInstance::_imgRain[sRainInstance::SPEED]->block[pixel]);

    float const fdrop_distance = ((float)drop_distance) * Inv255;
    float const fdrop_speed = SFM::lerp(MIN_DROP_SPEED, MAX_DROP_SPEED, ((float)drop_speed) * Inv255);

    // v = d/t 
    float fnew_drop_distance = fdrop_distance - fdrop_speed * time_to_float(getLocalTimeDelta());

    // wrap around if less than zero, but calculate a new random speed for new drop if so
    if (fnew_drop_distance < 0.0f) { // drop has hit bottom

        // * current drop
        fnew_drop_distance = -fnew_drop_distance;
        rdrop_distance = 255 - SFM::saturate_to_u8(fnew_drop_distance * 255.0f);  // position reset to value for a new drop some time from now

        // * new drop
        newDrop(sRainInstance::_imgRain[sRainInstance::SPEED]->block[pixel]);
        
        return(false); // this voxel is skipped
    }

    // speed remains constant, update saved distance
    rdrop_distance = SFM::saturate_to_u8(fnew_drop_distance * 255.0f);

    if (fnew_drop_distance - 0.25f >= 0.0f) { // cull voxels higher than 128.0f
        return(false); // this voxel is skipped
    }

    if (0 != drop_speed) {

        // only visible drops need to care about streak length
        float const drop_speed_norm(((float)drop_speed) * Inv255);
        uint32_t const drop_streak = SFM::floor_to_u32(MAX_STREAK_LENGTH * drop_speed_norm); // need normalized value here

        if (0 == drop_streak) {
            // this has the effect of still updating the position of this drop, but no render for the duration of it being alive
            // when it wraps around to be top a new speed is calculated and then it may become visible
            return(false); // this voxel is skipped
        }

        // v = d/t, t = d/v 
        // d1 = 1.0f, d0 = current distance 
        out.distance = -fnew_drop_distance;     // ** wicked ** unique rotation/rain drop voxel w/o using sincos (quadratic interpolation approximation)
        out.rotation = v2_rotation_t::lerp(v2_rotation_t(), v2_rotation_constants::v90, (1.0f - fnew_drop_distance) / fdrop_speed);
        out.alpha = Volumetric::eVoxelTransparency::ALPHA_50; // rain drop starts @ this transparency level, and fades from that to lowest transparency index on the "tail"
        out.emissive = true;
        out.column_sz = drop_streak + 1;

        return(true);
    }
    //else { // updated still on the non-visible distance map, but never rendered until moved to a visible distance map //
           // this is done to time the raindrops that have dropped all the way to have a new starting position on xz axis
           // ie.) non visible drop is updated, is below ground - move to next distance map (could be visible next frame)
           //      visible drop is updated, is below ground - move to next distance map (could be visible or non-visible next frame)
        
        // not visible //
    //    return(false); // this voxel is skipped
    //}

    return(false); // this voxel is skipped
}

bool const UpdateRain(tTime const tNow, RainInstance* const __restrict Instance)
{
    // Update base first (required)
    std::optional<tTime const> const tLast(Instance->Update(tNow));

    if (std::nullopt != tLast)	// still alive?
    {
        Instance->setLocation(XMVectorSwizzle<XM_SWIZZLE_X, XM_SWIZZLE_Z, XM_SWIZZLE_Y, XM_SWIZZLE_W>(world::getOrigin()));

        Instance->getRotation() = world::getAzimuth();  // bugfix:need root of rain transform billboarded, each rain drop has a unique transform added to this later, otherwise while rotating view the rain can all dissappear at certain angles

#ifdef DEBUG_RANGE
        static uint32_t tLastDebug;	// place holder testing
        if (tNow - tLastDebug > 250) {

            if (Instance->RangeMax > -FLT_MAX && Instance->RangeMin < FLT_MAX) {
                //	DebugMessage("%.03f to %.03f", Instance->RangeMin, Instance->RangeMax);
            }
            tLastDebug = tNow;
        }
#endif

        return(true);
    }

    return(false); // dead instance
}

void RenderRain(RainInstance const* const __restrict Instance, tbb::atomic<VertexDecl::VoxelDynamic*>& __restrict voxelDynamic)
{
    Volumetric::renderRadialGrid<
        Volumetric::eRadialGridRenderOptions::CULL_CHECKERBOARD | Volumetric::eRadialGridRenderOptions::NO_TOPS |
        Volumetric::eRadialGridRenderOptions::FILL_COLUMNS_UP | Volumetric::eRadialGridRenderOptions::FADE_COLUMNS>
        (Instance, voxelDynamic);
}


sRainInstance::~sRainInstance()
{
    for (uint32_t iDx = 0; iDx < eRainImageType::COUNT; ++iDx) {
        if (_imgRain[iDx]) {
            ImagingDelete(_imgRain[iDx]); _imgRain[iDx] = nullptr;
        }
    }
}




