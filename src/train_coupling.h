/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file train_coupling.h Coupling and splitting of trains at stations. */

#ifndef TRAIN_COUPLING_H
#define TRAIN_COUPLING_H

#include "command_type.h"
#include "group_type.h"
#include "vehicle_type.h"
#include "direction_type.h"
#include "tile_type.h"
#include "station_type.h"
#include <optional>
#include <vector>
#include "3rdparty/cpp-btree/btree_map.h"

struct Train;
struct Vehicle;
struct Order;
struct OrderList;

/**
 * Coupling state of a combination of two trains (services).
 *
 * The chain of the combination consists of two parts:
 *  - the first part, starting at the combination head (the primary vehicle of the chain),
 *  - the second part, starting at the head of the passive train (a non-front engine).
 *
 * The combination head always carries the identity (orders, timetable, unit number, ...) of the leading train.
 * The passive head keeps the identity of the passive train, it is only stripped of its front engine status.
 * If the leading train was physically the second part when coupling, the identities of both heads are swapped,
 * and swapped back when splitting.
 */
struct TrainCoupling {
	VehicleID head;             ///< Head of the combination (first vehicle of the chain), carries the leader identity.
	VehicleID passive_head;     ///< Head of the second part of the chain, carries the passive identity.
	GroupID passive_group;      ///< Group of the passive train.
	bool identities_swapped;    ///< The identities of the two heads were swapped when coupling.
};

extern btree::btree_map<VehicleID, TrainCoupling> _train_couplings;

const TrainCoupling *GetTrainCoupling(const Train *head);
Train *GetCoupledPassiveHead(const Train *head);
Train *GetCouplingCombinationHead(const Train *passive_head);
bool IsTrainCoupled(const Train *head);
bool IsTrainInCombination(const Train *t);
bool IsOrderListUsedByCombination(const Vehicle *v);
bool ShouldWarnCoupledDepotStop(const Vehicle *v, const Order *order);
enum class OrderCouplingRole : uint8_t;
bool HasTrainCouplingRole(const Train *t, OrderCouplingRole role);
void OnTrainLeavingCouplingStation(Train *t);

CommandCost CheckCanCoupleTrains(const Train *waiting, const Train *arriving);
void CoupleTrains(Train *waiting, Train *arriving, bool arriving_leads);
CommandCost CheckCanSplitCombination(const Train *head);
Train *SplitCombination(Train *head);

void OnTrainCouplingVehicleDeleted(Train *v);
void ValidateTrainCouplings();
std::vector<Vehicle *> GetFrontAndCoupledPassiveVehicles();

enum class OrderCouplingMode : uint8_t;
bool IsCouplingOrderSequenceValid(const OrderList *list, uint index, OrderCouplingMode new_mode);
const Order *GetCurrentCouplingOrder(const Train *v);
void UpdateCouplingApproachFromOrders(Train *v);
void OnCombinationEnteredStation(Train *head, StationID station);

Train *GetCouplingApproachTarget(const Train *v);
bool IsCouplingApproachPlatformTile(const Train *v, TileIndex tile);
std::optional<int> GetCouplingStopLocation(const Train *v, DiagDirection dir);
std::optional<int> GetCouplingStopDistance(const Train *v);
bool IsTrainBeingApproachedForCoupling(const Train *target);
void ClearCouplingApproach(Train *v);
void OnCouplingTrainEnteredStation(Train *consist);
void ProcessPendingTrainCouplings();
bool IsCouplingApproachCollision(const Train *a, const Train *b);
bool IsTrainBlockedBySplitPartner(const Train *v);
void UpdateSplitPartner(Train *v);

#endif /* TRAIN_COUPLING_H */
