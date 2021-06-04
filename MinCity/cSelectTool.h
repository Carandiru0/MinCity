#pragma once
#include "globals.h"
#include "cAbstractToolMethods.h"
#include "UndoVoxel.h"
#include <vector>

class cSelectTool final : public cAbstractToolMethods
{
public:
	uint32_t const toolType() const final {
		return(eTools::SELECT);
	}
	virtual void deactivate() final;
	virtual void activate() final;

	void KeyAction(int32_t const key, bool const down, bool const ctrl) final;
	void __vectorcall ClickAction(FXMVECTOR const xmMousePos) final;

private:
	void clear_selection();
private:
	point2D_t	_selectedVoxelIndex;
	uint32_t	_selectedInstanceHash;
	bool		_selectedDynamic;
public:
	cSelectTool();
	virtual ~cSelectTool() = default;
};
