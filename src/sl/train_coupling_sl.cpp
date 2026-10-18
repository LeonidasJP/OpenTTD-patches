/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file train_coupling_sl.cpp Code handling saving and loading of train couplings. */

#include "../stdafx.h"
#include "../train_coupling.h"

#include "saveload.h"

#include "../safeguards.h"

static const NamedSaveLoad _train_coupling_desc[] = {
	NSL("head",               SLE_VAR(TrainCoupling, head,               SLE_UINT32)),
	NSL("passive_head",       SLE_VAR(TrainCoupling, passive_head,       SLE_UINT32)),
	NSL("passive_group",      SLE_VAR(TrainCoupling, passive_group,      SLE_UINT16)),
	NSL("identities_swapped", SLE_VAR(TrainCoupling, identities_swapped, SLE_BOOL)),
};

static void Save_TCPL()
{
	SaveLoadTableData slt = SlTableHeader(_train_coupling_desc);

	uint index = 0;
	for (auto &it : _train_couplings) {
		SlSetArrayIndex(index++);
		SlObjectSaveFiltered(&it.second, slt);
	}
}

static void Load_TCPL()
{
	SaveLoadTableData slt = SlTableHeaderOrRiff(_train_coupling_desc);

	_train_couplings.clear();
	TrainCoupling tc{};
	while (SlIterateArray() != -1) {
		SlObjectLoadFiltered(&tc, slt);
		_train_couplings[tc.head] = tc;
	}
}

extern const ChunkHandler train_coupling_chunk_handlers[] = {
	{ 'TCPL', Save_TCPL, Load_TCPL, nullptr, nullptr, ChunkType::Table },
};

extern const ChunkHandlerTable _train_coupling_chunk_handlers(train_coupling_chunk_handlers);
