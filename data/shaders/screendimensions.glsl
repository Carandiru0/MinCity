#ifndef _SCREENDIMENSIONS_GLSL
#define _SCREENDIMENSIONS_GLSL

layout (constant_id = 0) const float ScreenResWidth = 1.0f;
layout (constant_id = 1) const float ScreenResHeight = 1.0f;
layout (constant_id = 2) const float InvScreenResWidth = 1.0f;
layout (constant_id = 3) const float InvScreenResHeight = 1.0f;

#define ScreenResDimensions (vec2(ScreenResWidth, ScreenResHeight))
#define InvScreenResDimensions (vec2(InvScreenResWidth, InvScreenResHeight))

#endif // _SCREENDIMENSIONS_GLSL

