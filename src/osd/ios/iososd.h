#ifndef _iososd_h_
#define _iososd_h_

#include "modules/lib/osdobj_common.h"
#include "modules/osdmodule.h"
#include "modules/font/font_module.h"
#include "../frontend/mame/ui/menuitem.h"

#include "libmame.h"

//============================================================
// DebugLog
//============================================================
#define DebugLog 0
#if DebugLog == 0
#define osd_printf_debug(...) (void)0
#endif
#if DebugLog <= 1
#define osd_printf_verbose(...) (void)0
#endif

//============================================================
// MYOSD globals
//============================================================
extern int myosd_display_width;
extern int myosd_display_height;

//============================================================
//  OPTIONS
//============================================================

#define OPTION_HISCORE  "hiscore"
#define OPTION_BEAM     "beam"
#define OPTION_BENCH    "bench"
#define OPTION_SOUND    "sound"
#define OPTION_VIDEO    "video"
#define OPTION_NUMPROCESSORS "numprocessors"

//============================================================
//  TYPE DEFINITIONS
//============================================================

class ios_osd_interface : public osd_interface, osd_output
{
public:
	// construction/destruction
	ios_osd_interface(emu_options &options, myosd_callbacks &callbacks);
	virtual ~ios_osd_interface();

    // general overridables
    virtual void init(running_machine &machine) override;
    virtual void update(bool skip_redraw) override;
    virtual void input_update() override;
    virtual void set_verbose(bool verbose) override { m_verbose = verbose; }

    // debugger overridables
    virtual void init_debugger() override {}
    virtual void wait_for_debugger(device_t &device, bool firststop) override {}

    // audio overridables
    virtual void update_audio_stream(const int16_t *buffer, int samples_this_frame) override;
    virtual void set_mastervolume(int attenuation) override;
    virtual bool no_sound() override;

    // input overridables
    virtual void customize_input_type_list(std::vector<input_type_entry> &typelist) override;

    // video overridables
    virtual void add_audio_to_recording(const int16_t *buffer, int samples_this_frame) override {}
    virtual std::vector<ui::menu_item> get_slider_list() override {
        return std::vector<ui::menu_item>();
    }

    // font interface
    virtual osd_font::ptr font_alloc() override { return nullptr; }
    virtual bool get_font_families(std::string const &font_path, std::vector<std::pair<std::string, std::string> > &result) override { return false; }

    // command option overrides
    virtual bool execute_command(const char *command) override {return true;}

    // midi interface
    virtual std::unique_ptr<osd_midi_device> create_midi_device() override {return nullptr;}
    
    // osd_output
    virtual void output_callback(osd_output_channel channel, const util::format_argument_pack<std::ostream> &args) override;
    
    // getters
    running_machine &machine() const { assert(m_machine != nullptr); return *m_machine; }
    render_target *target() const { assert(m_target != nullptr); return m_target; }
    emu_options &options() { return m_options; }

private:
    void video_init();
    void video_exit();
    
    void input_init();
    void input_exit();

    void sound_init();
    void sound_exit();

    void machine_exit();

    // internal state
    running_machine *m_machine;
    emu_options &m_options;
    bool m_verbose;

    // video
    render_target * m_target;
    int m_min_width, m_min_height;
    int m_vis_width, m_vis_height;
    int m_video_none;

    // audio
    int m_attenuation;
    int m_sample_rate;

    // host ios app callbacks
    myosd_callbacks m_callbacks;
};

//============================================================
//  work.cpp
//============================================================

extern int osd_num_processors;

#endif
