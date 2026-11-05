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
#include "order_type.h"
#include "cargo_type.h"
#include "date_type.h"
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
Train *SplitCombination(Train *head, std::optional<bool> leader_on_second = std::nullopt);

void OnTrainCouplingVehicleDeleted(Train *v);
void ValidateTrainCouplings();
std::vector<Vehicle *> GetFrontAndCoupledPassiveVehicles();

enum class OrderCouplingMode : uint8_t;
bool IsCouplingOrderSequenceValid(const OrderList *list, uint index, OrderCouplingMode new_mode, bool insert = false, bool cyclic = false);
bool IsCouplingFilterDefined(const Order *order);
const Order *GetCurrentCouplingOrder(const Train *v);

/** The scheduled dispatch departure of a train at a station order, see OrderCouplingFilter::DispatchSlot. */
struct CouplingDispatchSlot {
	const OrderList *order_list = nullptr; ///< Order list of the dispatch schedule.
	uint schedule_index = 0;               ///< Index of the dispatch schedule in the order list.
	StationID station = StationID::Invalid(); ///< Station of the order.
	StateTicks departure{};                ///< Departure time of the slot.
	bool coupling = false;                 ///< The slot is a coupling slot (DispatchSlot::SDSF_COUPLE).

	/**
	 * Do both trains depart in the same slot?
	 * For now both trains must share the order list and the dispatch schedule.
	 * Trains with separate order lists could be allowed later by only comparing the station and the departure time.
	 */
	bool IsSameDeparture(const CouplingDispatchSlot &other) const
	{
		return this->station == other.station && this->departure == other.departure &&
				this->order_list == other.order_list && this->schedule_index == other.schedule_index;
	}
};

std::optional<CouplingDispatchSlot> GetCouplingDispatchSlot(const Train *v, const Order *order);
std::optional<std::pair<StateTicks, int>> GetCouplingArrivalDispatchSlot(const Vehicle *v, uint schedule_index);
void UpdateCouplingApproachFromOrders(Train *v);
void OnCombinationEnteredStation(Train *head, StationID station);
void OnCombinationLeavingStation(Train *head);
void TickCoupledPassiveTrain(Train *head);
bool IsPrimaryOrCoupledPassiveVehicle(const Vehicle *v);
bool IsCoupledPassiveTrain(const Vehicle *v);
std::vector<Train *> GetCoupledPassiveTrains();
GroupID GetCoupledPassiveTrainGroup(const Train *passive);
void SetCoupledPassiveTrainGroup(const Train *passive, GroupID group);
std::vector<VehicleOrderID> GetMirroredCouplingOrders(const Train *passive, const Train **leader);
const Vehicle *GetCargoUnitEnd(const Vehicle *unit_front);
const Vehicle *GetCargoUnitFront(const Vehicle *v);
bool IsCoupledPassiveTrainStoppingAt(const Vehicle *passive, StationID station);

/** Where a coupled combination splits, and where both parts of the combination go next. */
struct CouplingSplitRoute {
	StationID split_station = StationID::Invalid(); ///< Station where the combination splits.
	StationID first_after = StationID::Invalid();   ///< Next station of the first part (from the head of the combination) after the split.
	StationID second_after = StationID::Invalid();  ///< Next station of the second part (from the passive head) after the split.

	/** Do both trains go to different, known stations after the split? */
	bool IsValid() const
	{
		return this->split_station != StationID::Invalid() && this->first_after != StationID::Invalid() &&
				this->second_after != StationID::Invalid() && this->first_after != this->second_after;
	}
};

struct CargoPacket;
CouplingSplitRoute GetCouplingSplitRoute(const Train *head);
StationID GetCouplingSplitVia(const CargoPacket *cp, StationID split_station, CargoType cargo);
bool WillCombinationSplitAt(const Train *head, StationID station);
void ExchangeCargoBeforeSplit(Train *head);
bool IsCouplingSplitStaging();

/** While this scope exists, the cargo is staged at a station where a combination splits, see GetCouplingSplitVia. */
struct CouplingSplitStagingScope {
	bool previous;
	CouplingSplitStagingScope(bool active);
	~CouplingSplitStagingScope();
};

Train *GetCouplingApproachTarget(const Train *v);
bool IsCouplingApproachPlatformTile(const Train *v, TileIndex tile);
bool IsTrainPlatformTile(const Train *target, TileIndex tile);
const Train *FindCouplingPartner(const Train *v);
std::optional<int> GetCouplingStopLocation(const Train *v, DiagDirection dir);
std::optional<int> GetCouplingStopDistance(const Train *v);
bool IsTrainBeingApproachedForCoupling(const Train *target);
bool IsTrainCouplingTargetOfOtherTrain(const Train *target, const Train *v, bool committed_only);
void MarkCouplingApproachSignal(const Train *v);
void RebuildCouplingSignalTiles();
std::vector<TileIndex> GetCouplingSignalTiles();
void AnimateCouplingSignals();
bool IsCouplingSignalFlashOff();
void ClearCouplingApproach(Train *v);
void OnCouplingTrainEnteredStation(Train *consist);
void ProcessPendingTrainCouplings();
bool IsCouplingApproachCollision(const Train *a, const Train *b);
bool IsTrainBlockedBySplitPartner(const Train *v);
void UpdateSplitPartner(Train *v);

#endif /* TRAIN_COUPLING_H */
