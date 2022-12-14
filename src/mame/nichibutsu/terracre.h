// license:BSD-3-Clause
// copyright-holders:Carlos A. Lozano
#ifndef MAME_INCLUDES_TERRACRE_H
#define MAME_INCLUDES_TERRACRE_H

#pragma once

#include "nb1412m2.h"
#include "machine/gen_latch.h"
#include "sound/flt_biquad.h"
#include "sound/mixer.h"
#include "video/bufsprite.h"
#include "emupal.h"
#include "tilemap.h"

class terracre_state : public driver_device
{
public:
	terracre_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_maincpu(*this, "maincpu")
		, m_audiocpu(*this, "audiocpu")
		, m_gfxdecode(*this, "gfxdecode")
		, m_palette(*this, "palette")
		, m_spriteram(*this, "spriteram")
		, m_soundlatch(*this, "soundlatch")
		, m_dacfilter1(*this, "dacfilter1")
		, m_dacfilter2(*this, "dacfilter2")
		, m_ymfilter(*this, "ymfilter")
		, m_ssgmixer(*this, "ssgmixer")
		, m_ssgfilter_abfilt(*this, "ssg_abfilt")
		, m_ssgfilter_abgain(*this, "ssg_abgain")
		, m_ssgfilter_cgain(*this, "ssg_cgain")
		, m_bg_videoram(*this, "bg_videoram")
		, m_fg_videoram(*this, "fg_videoram")
	{ }

	void amazon_base(machine_config &config);
	void horekidb2(machine_config &config);
	void tc_base(machine_config &config);
	void ym2203(machine_config &config);
	void ym3526(machine_config &config);

protected:
	void amazon_base_map(address_map &map);

	required_device<cpu_device> m_maincpu;
	required_device<cpu_device> m_audiocpu;

private:
	required_device<gfxdecode_device> m_gfxdecode;
	required_device<palette_device> m_palette;
	required_device<buffered_spriteram16_device> m_spriteram;
	required_device<generic_latch_8_device> m_soundlatch;
	required_device<filter_biquad_device> m_dacfilter1;
	required_device<filter_biquad_device> m_dacfilter2;
	required_device<filter_biquad_device> m_ymfilter;
	optional_device<mixer_device> m_ssgmixer;
	optional_device<filter_biquad_device> m_ssgfilter_abfilt;
	optional_device<filter_biquad_device> m_ssgfilter_abgain;
	optional_device<filter_biquad_device> m_ssgfilter_cgain;

	required_shared_ptr<uint16_t> m_bg_videoram;
	required_shared_ptr<uint16_t> m_fg_videoram;

	uint16_t m_xscroll = 0;
	uint16_t m_yscroll = 0;
	tilemap_t *m_background = nullptr;
	tilemap_t *m_foreground = nullptr;
	void amazon_sound_w(uint16_t data);
	uint8_t soundlatch_clear_r();
	void amazon_background_w(offs_t offset, uint16_t data, uint16_t mem_mask = ~0);
	void amazon_foreground_w(offs_t offset, uint16_t data, uint16_t mem_mask = ~0);
	void amazon_flipscreen_w(offs_t offset, uint16_t data, uint16_t mem_mask = ~0);
	void amazon_scrolly_w(offs_t offset, uint16_t data, uint16_t mem_mask = ~0);
	void amazon_scrollx_w(offs_t offset, uint16_t data, uint16_t mem_mask = ~0);
	TILE_GET_INFO_MEMBER(get_bg_tile_info);
	TILE_GET_INFO_MEMBER(get_fg_tile_info);
	virtual void video_start() override;
	void terracre_palette(palette_device &palette) const;
	uint32_t screen_update_amazon(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect);
	void draw_sprites(bitmap_ind16 &bitmap, const rectangle &cliprect );

	void horekidb2_map(address_map &map);
	void sound_2203_io_map(address_map &map);
	void sound_3526_io_map(address_map &map);
	void sound_map(address_map &map);
	void terracre_map(address_map &map);
};

class amazon_state : public terracre_state
{
public:
	amazon_state(const machine_config &mconfig, device_type type, const char *tag) :
		terracre_state(mconfig, type, tag),
		m_prot(*this, "prot_chip")
	{ }

	void amazon_1412m2(machine_config &config);
	void amazon_1412m2_map(address_map &map);
private:
	required_device<nb1412m2_device> m_prot;
};

#endif // MAME_INCLUDES_TERRACRE_H
