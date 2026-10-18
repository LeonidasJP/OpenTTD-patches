/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file train_coupling_cmd.h Command definitions related to coupling and splitting trains. */

#ifndef TRAIN_COUPLING_CMD_H
#define TRAIN_COUPLING_CMD_H

#include "command_type.h"
#include "vehicle_type.h"

DEF_CMD_TUPLE_LT (Commands::CoupleTrains,       CmdCoupleTrains,       {}, CommandType::VehicleManagement, CmdDataT<VehicleID, VehicleID, bool>)
DEF_CMD_TUPLE_LT (Commands::SplitCoupledTrain,  CmdSplitCoupledTrain,  {}, CommandType::VehicleManagement, CmdDataT<VehicleID>)
DEF_CMD_TUPLE_LT (Commands::CutTrain,           CmdCutTrain,           {}, CommandType::VehicleManagement, CmdDataT<VehicleID>)
DEF_CMD_TUPLE_LT (Commands::SetTrainCouplingTarget, CmdSetTrainCouplingTarget, {}, CommandType::VehicleManagement, CmdDataT<VehicleID, VehicleID, bool>)

#endif /* TRAIN_COUPLING_CMD_H */
