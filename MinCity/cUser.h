#pragma once
#include <Utility/class_helper.h>

// forward decl
namespace world {

	class cYXIGameObject;
	class cYXISphereGameObject;
	class cLightConeGameObject;

} // end ns

class cUser : no_copy
{
	constinit static uint64_t user_count;

	static constexpr fp_seconds const
		BEACON_LAUNCH_INTERVAL = fp_seconds(milliseconds(3333));

	static constexpr uint32_t const
		LEFT_ENGINE = 0,
		RIGHT_ENGINE = 1,
		ENGINE_COUNT = 2;

public:
	// accessors
	bool const isDestroyed() const { return(_destroyed); }


	// methods
	void Update(tTime const& __restrict tNow, fp_seconds const& __restrict tDelta);
	void Paint();

	void KeyAction(int32_t const key, bool const down, bool const ctrl);

	// creation / destruction
	void create();
	void destroy();
	/*
	void __vectorcall LeftMousePressAction(FXMVECTOR const xmMousePos);
	void __vectorcall LeftMouseReleaseAction(FXMVECTOR const xmMousePos);
	void __vectorcall LeftMouseClickAction(FXMVECTOR const xmMousePos);
	void __vectorcall LeftMouseDragAction(FXMVECTOR const xmMousePos, FXMVECTOR const xmLastDragPos, tTime const& __restrict tDragStart);
	void __vectorcall MouseMoveAction(FXMVECTOR const xmMousePos);
	*/
private:
	bool const secure_validation() const;

private:
	world::cYXIGameObject*				_ship;
	world::cYXISphereGameObject			*_shipRingX[ENGINE_COUNT], *_shipRingY[ENGINE_COUNT], *_shipRingZ[ENGINE_COUNT];
	world::cLightConeGameObject*		_light_cone;

	// security //
	uintptr_t							_shipAlias;
	uintptr_t							_shipRingXAlias[ENGINE_COUNT], _shipRingYAlias[ENGINE_COUNT], _shipRingZAlias[ENGINE_COUNT];
	uintptr_t							_light_cone_alias;
	uint64_t							_id;

	// end security //

	fp_seconds                          _beacon_accumulator;
	float const							_sphere_engine_offset;
	float								_total_mass;

	bool								_destroyed,
		                                _beacon_loaded;
public:
	cUser();
	~cUser();
};