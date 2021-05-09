
//============================================================
//
//  input.cpp - IOS input handling
//
//============================================================

// MAME headers
#include "emu.h"
#include "inputdev.h"
#include "ui/uimain.h"
//#include "emuopts.h"
//#include "uiinput.h"

// MAMEOS headers
#include "iososd.h"

static input_item_id key_map(myosd_keycode key);

//============================================================
// global input state
//============================================================
static myosd_input_state g_input;

//============================================================
//  get_xxx
//============================================================

static int get_key(void *device, void *item)
{
    return *(uint8_t*)item != 0;
}

static int get_axis(void *device, void *item)
{
    float value = *(float*)item;
    return (int)(value * 32767 * 2);
}

static int get_axis_neg(void *device, void *item)
{
    float value = *(float*)item;
    return (int)(value * 32767 * -2);
}

static int get_mouse(void *device, void *item)
{
    float value = *(float*)item;
    return (int)round(value);
}

#define BTN(off,mask) (void*)(intptr_t)(((intptr_t)mask<<32) | offsetof(myosd_input_state,off))
static int get_button(void *device, void *item)
{
    _Static_assert(sizeof(intptr_t) == 8, "");
    intptr_t off  = (intptr_t)item & 0xFFFFFFFF;
    intptr_t mask = (intptr_t)item>>32;
    unsigned long status = *(unsigned long *)((uint8_t*)&g_input + off);
    return (status & mask) != 0;
}

//============================================================
//  init
//============================================================

void ios_osd_interface::input_init()
{
    osd_printf_verbose("ios_osd_interface::input_init\n");
    
    memset(&g_input, 0, sizeof(g_input));
    
    char name[32];
    
    // keyboard
    snprintf(name, sizeof(name)-1, "Keyboard");
    input_device* keyboard = machine().input().device_class(DEVICE_CLASS_KEYBOARD).add_device(name, name);
    
    // all keys
    for (int key = MYOSD_KEY_FIRST; key <= MYOSD_KEY_LAST; key++)
    {
        input_item_id itemid = key_map((myosd_keycode)key);

        if (itemid == ITEM_ID_INVALID)
            continue;

        const char* name = machine().input().standard_token(itemid);
        keyboard->add_item(name, itemid, get_key, &g_input.keyboard[key]);
    }
    
    // joystick
    for (int i=0; i<NUM_JOY; i++)
    {
        snprintf(name, sizeof(name)-1, "Joy %d", i+1);
        input_device* joystick = machine().input().device_class(DEVICE_CLASS_JOYSTICK).add_device(name, name);
        
        joystick->add_item("LX Axis", ITEM_ID_XAXIS,  get_axis, &g_input.joy_analog[i][MYOSD_AXIS_LX]);
        joystick->add_item("LY Axis", ITEM_ID_YAXIS,  get_axis_neg, &g_input.joy_analog[i][MYOSD_AXIS_LY]);
        joystick->add_item("LZ Axis", ITEM_ID_ZAXIS,  get_axis, &g_input.joy_analog[i][MYOSD_AXIS_LZ]);
        joystick->add_item("RX Axis", ITEM_ID_RXAXIS, get_axis, &g_input.joy_analog[i][MYOSD_AXIS_RX]);
        joystick->add_item("RY Axis", ITEM_ID_RYAXIS, get_axis_neg, &g_input.joy_analog[i][MYOSD_AXIS_RY]);
        joystick->add_item("RZ Axis", ITEM_ID_RZAXIS, get_axis, &g_input.joy_analog[i][MYOSD_AXIS_RZ]);
     
        joystick->add_item("A", ITEM_ID_BUTTON1, get_button, BTN(joy_status[i], MYOSD_A));
        joystick->add_item("B", ITEM_ID_BUTTON2, get_button, BTN(joy_status[i], MYOSD_B));
        joystick->add_item("Y", ITEM_ID_BUTTON3, get_button, BTN(joy_status[i], MYOSD_Y));
        joystick->add_item("X", ITEM_ID_BUTTON4, get_button, BTN(joy_status[i], MYOSD_X));
        joystick->add_item("L", ITEM_ID_BUTTON5, get_button, BTN(joy_status[i], MYOSD_L1));
        joystick->add_item("R", ITEM_ID_BUTTON6, get_button, BTN(joy_status[i], MYOSD_R1));
        
        joystick->add_item("L2", ITEM_ID_BUTTON7, get_button, BTN(joy_status[i], MYOSD_L2));
        joystick->add_item("R2", ITEM_ID_BUTTON8, get_button, BTN(joy_status[i], MYOSD_R2));
        joystick->add_item("L3", ITEM_ID_BUTTON9, get_button, BTN(joy_status[i], MYOSD_L3));
        joystick->add_item("R3", ITEM_ID_BUTTON10,get_button, BTN(joy_status[i], MYOSD_R3));

        joystick->add_item("Select", ITEM_ID_SELECT, get_button, BTN(joy_status[i], MYOSD_SELECT));
        joystick->add_item("Start",  ITEM_ID_START,  get_button, BTN(joy_status[i], MYOSD_START));
        
        joystick->add_item("D-Pad Up",    ITEM_ID_HAT1UP,    get_button, BTN(joy_status[i], MYOSD_UP));
        joystick->add_item("D-Pad Down",  ITEM_ID_HAT1DOWN,  get_button, BTN(joy_status[i], MYOSD_DOWN));
        joystick->add_item("D-Pad Left",  ITEM_ID_HAT1LEFT,  get_button, BTN(joy_status[i], MYOSD_LEFT));
        joystick->add_item("D-Pad Right", ITEM_ID_HAT1RIGHT, get_button, BTN(joy_status[i], MYOSD_RIGHT));
    }
    
    // mice
    for (int i=0; i<NUM_JOY; i++)
    {
        snprintf(name, sizeof(name)-1, "Mouse %d", i+1);
        input_device* mouse = machine().input().device_class(DEVICE_CLASS_MOUSE).add_device(name, name);

        mouse->add_item("X Axis", ITEM_ID_XAXIS, get_mouse, &g_input.mouse_x[i]);
        mouse->add_item("Y Axis", ITEM_ID_YAXIS, get_mouse, &g_input.mouse_y[i]);
        mouse->add_item("Z Axis", ITEM_ID_ZAXIS, get_mouse, &g_input.mouse_z[i]);
        mouse->add_item("Left",   ITEM_ID_BUTTON1, get_button, BTN(mouse_status[i], MYOSD_A));
        mouse->add_item("Middle", ITEM_ID_BUTTON2, get_button, BTN(mouse_status[i], MYOSD_Y));
        mouse->add_item("Right",  ITEM_ID_BUTTON3, get_button, BTN(mouse_status[i], MYOSD_B));
    }
    
    // lightgun
    for (int i=0; i<NUM_JOY; i++)
    {
        snprintf(name, sizeof(name)-1, "Lightgun %d", i+1);
        input_device* lightgun = machine().input().device_class(DEVICE_CLASS_LIGHTGUN).add_device(name, name);
        
        lightgun->add_item("X Axis", ITEM_ID_XAXIS, get_axis, &g_input.lightgun_x[i]);
        lightgun->add_item("Y Axis", ITEM_ID_YAXIS, get_axis_neg, &g_input.lightgun_y[i]);
        lightgun->add_item("A", ITEM_ID_BUTTON1, get_button, BTN(lightgun_status[i], MYOSD_A));
        lightgun->add_item("B", ITEM_ID_BUTTON2, get_button, BTN(lightgun_status[i], MYOSD_B));
    }
}


//============================================================
//  exit
//============================================================

void ios_osd_interface::input_exit()
{
    osd_printf_verbose("ios_osd_interface::input_exit\n");
    
    if (m_callbacks.input_exit != NULL)
        m_callbacks.input_exit();
}

//============================================================
//  update
//============================================================
void ios_osd_interface::input_update()
{
    osd_printf_verbose("ios_osd_interface::input_update\n");
    
    // fill in the input profile the first time
    if (g_input.num_players == 0)
    {
        // TODO: set these correctly!
        g_input.num_players = 2;
        g_input.num_coins = 2;
        g_input.num_inputs = 2;
        g_input.num_buttons = 4;
        g_input.num_ways = 4;
        g_input.num_mouse = 0;
        g_input.num_lightgun = 0;
        g_input.num_keyboard = 0;
        // TODO: set these correctly!
        
        if (m_callbacks.input_init != NULL)
            m_callbacks.input_init(&g_input, sizeof(g_input));
    }

    bool in_menu = machine().phase() == machine_phase::RUNNING && machine().ui().is_menu_active();

    // set the current input mode(s)
    g_input.input_mode = in_menu ? MYOSD_INPUT_MODE_UI : MYOSD_INPUT_MODE_NORMAL;
    g_input.keyboard_mode = MYOSD_KEYBOARD_MODE_NORMAL;

    // and call host
    if (m_callbacks.input_poll != NULL)
        m_callbacks.input_poll(&g_input, sizeof(g_input));
    
    //poll_inputs(machine());
    //check_osd_inputs(machine());
}

//============================================================
//  customize_input_type_list
//============================================================
void ios_osd_interface::customize_input_type_list(std::vector<input_type_entry> &typelist)
{
    // This function is called on startup, before reading the
    // configuration from disk. Scan the list, and change the
    // default control mappings you want. It is quite possible
    // you won't need to change a thing.
    
    //!!! OSDOPTION_UIMODEKEY

    // loop over the defaults
    for (input_type_entry &entry : typelist)
        switch (entry.type())
        {
            /*
            // Retro:  Select + X => UI_CONFIGURE (Menu)
            case IPT_UI_CONFIGURE:
                entry.defseq(SEQ_TYPE_STANDARD).set(KEYCODE_TAB, input_seq::or_code, JOYCODE_SELECT, JOYCODE_BUTTON3);
                break;

            // Retro:  Select + Start => CANCEL
            case IPT_UI_CANCEL:
                entry.defseq(SEQ_TYPE_STANDARD).set(KEYCODE_ESC, input_seq::or_code, JOYCODE_SELECT, JOYCODE_START);
                break;
            */
                
            // leave everything else alone
            default:
                break;
        }
}

//============================================================
//  osd_set_aggressive_input_focus
//============================================================

void osd_set_aggressive_input_focus(bool aggressive_focus)
{
    // dummy implementation for now?
}

//============================================================
//  convert myosd_keycode -> input_item_id
//============================================================

static input_item_id key_map(myosd_keycode key)
{
    _Static_assert(MYOSD_KEY_A == (int)ITEM_ID_A, "");
    _Static_assert(MYOSD_KEY_0 == (int)ITEM_ID_0, "");
    _Static_assert(MYOSD_KEY_F1 == (int)ITEM_ID_F1, "");
    _Static_assert(MYOSD_KEY_F15 == (int)ITEM_ID_F15, "");
    if (key >= MYOSD_KEY_A && key <= MYOSD_KEY_F15)
        return (input_item_id)key;
    
    // missing F16-F20
    _Static_assert(MYOSD_KEY_ESC == (int)ITEM_ID_ESC-5, "");
    _Static_assert(MYOSD_KEY_ENTER_PAD == (int)ITEM_ID_ENTER_PAD-5, "");
    if (key >= MYOSD_KEY_ESC && key <= MYOSD_KEY_ENTER_PAD)
        return (input_item_id)(key+5);

    // missing BS_PAD, TAB_PAD, 00_PAD, 000_PAD, COMMA_PAD, EQUALS_PAD
    _Static_assert(MYOSD_KEY_PRTSCR == (int)ITEM_ID_PRTSCR-11, "");
    _Static_assert(MYOSD_KEY_CANCEL == (int)ITEM_ID_CANCEL-11, "");
    if (key >= MYOSD_KEY_PRTSCR && key <= MYOSD_KEY_CANCEL)
        return (input_item_id)(key+11);
    
    return ITEM_ID_INVALID;
}






