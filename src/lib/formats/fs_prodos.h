// license:BSD-3-Clause
// copyright-holders:Olivier Galibert

// Creation of Apple ProDOS floppy images

#ifndef MAME_FORMATS_FS_PRODOS_H
#define MAME_FORMATS_FS_PRODOS_H

#pragma once

#include "fsmgr.h"

class fs_prodos : public filesystem_manager_t {
public:
	class impl : public filesystem_t {
	public:
		impl(fsblk_t &blockdev) : filesystem_t(blockdev) {}
		virtual ~impl() = default;
		
		virtual void format() override;

	private:
		static const u8 boot[512];
	};

	fs_prodos() : filesystem_manager_t() {}

	virtual void enumerate(floppy_enumerator &fe, uint32_t form_factor, const std::vector<uint32_t> &variants) const override;
	virtual std::unique_ptr<filesystem_t> mount(fsblk_t &blockdev) const override;

	virtual bool can_format() const override;
	virtual bool can_read() const override;
	virtual bool can_write() const override;
};

extern const filesystem_manager_type FS_PRODOS;

#endif
