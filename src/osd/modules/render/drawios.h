// license:BSD-3-Clause
// copyright-holders:Aaron Giles
//============================================================
//
//  drawios.h - ios app draw
//
//============================================================

#ifndef __DRAWIOS__
#define __DRAWIOS__

#include "window.h"

class renderer_ios : public osd_renderer
{
public:
	renderer_ios(std::shared_ptr<osd_window> window)
	: osd_renderer(window, FLAG_NONE) { }

	virtual ~renderer_ios() { }

    static bool init(running_machine &machine);
    static void exit();

	virtual int create() override { return 0; }
	virtual render_primitive_list *get_primitives() override;
    virtual int draw(const int update) override;
	virtual void save() override { }
	virtual void record() override { }
	virtual void toggle_fsfx() override { }
};

#endif // __DRAWIOS__
