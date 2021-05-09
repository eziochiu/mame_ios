// license:BSD-3-Clause
// copyright-holders:Aaron Giles
//============================================================
//
//  drawios.cpp - ios render
//
//============================================================

// MAME headers
#include "emu.h"

#include "drawios.h"

//============================================================
//  init, exit
//============================================================

bool renderer_ios::init(running_machine &machine)
{
    osd_printf_verbose("renderer_ios::init\n");
    return true;
}

void renderer_ios::exit()
{
    osd_printf_verbose("renderer_ios::exit\n");
}

//============================================================
//  window_get_primitives
//============================================================

render_primitive_list *renderer_ios::get_primitives()
{
	auto win = try_getwindow();
    
	if (win == nullptr)
		return nullptr;

    /*
	RECT client;
#if defined(OSD_WINDOWS)
	GetClientRect(std::static_pointer_cast<win_window_info>(win)->platform_window(), &client);
#elif defined(OSD_UWP)
	auto bounds = std::static_pointer_cast<uwp_window_info>(win)->platform_window()->Bounds;
	client.left = bounds.Left;
	client.right = bounds.Right;
	client.top = bounds.Top;
	client.bottom = bounds.Bottom;
#endif
	if ((rect_width(&client) == 0) || (rect_height(&client) == 0))
		return nullptr;
	win->target()->set_bounds(rect_width(&client), rect_height(&client), win->pixel_aspect());
    */
    
	return &win->target()->get_primitives();
}

//============================================================
//  draw
//============================================================

int renderer_ios::draw(int update)
{
    //osd_printf_verbose("renderer_ios::draw(%d)\n", update);

    if (video_config.novideo)
    {
        return 0;
    }

    auto win = assert_window();
    //osd_dim wdim = win->get_size();

    win->m_primlist->acquire_lock();

    // now draw
    for (render_primitive &prim : *win->m_primlist)
    {
        switch (prim.type)
        {
            case render_primitive::LINE:
                break;
            case render_primitive::QUAD:
                break;
            default:
                throw emu_fatalerror("Unexpected render_primitive type\n");
        }
    }

    win->m_primlist->release_lock();

    return 0;
}


