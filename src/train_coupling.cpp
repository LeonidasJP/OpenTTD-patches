/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file train_coupling.cpp Coupling and splitting of trains at stations. */

#include "stdafx.h"
#include "train_coupling.h"
#include "train_coupling_cmd.h"
#include "train.h"
#include "command_func.h"
#include "company_base.h"
#include "company_func.h"
#include "economy_base.h"
#include "group.h"
#include "station_base.h"
#include "newgrf_debug.h"
#include "order_backup.h"
#include "order_base.h"
#include "station_base.h"
#include "station_map.h"
#include "texteff.hpp"
#include "tracerestrict.h"
#include "vehicle_func.h"
#include "vehicle_gui.h"
#include "window_func.h"

#include "table/strings.h"

#include "safeguards.h"

btree::btree_map<VehicleID, TrainCoupling> _train_couplings;

/* Helpers from train_cmd.cpp */
void NormaliseSubtypes(Train *chain);
void FlipTrainForCoupling(Train *consist);

/** Vehicle flags which belong to the train identity (service), and not to the physical vehicles. */
static constexpr VehicleFlags VEHICLE_FLAGS_IDENTITY = {
	VehicleFlag::TimetableStarted, VehicleFlag::AutofillTimetable, VehicleFlag::AutofillPreserveWaitTime,
	VehicleFlag::PathfinderLost, VehicleFlag::ServiceIntervalIsCustom, VehicleFlag::ServiceIntervalIsPercent,
	VehicleFlag::SeparationActive, VehicleFlag::ScheduledDispatch, VehicleFlag::TimetableSeparation,
	VehicleFlag::AutomateTimetable, VehicleFlag::HaveSlot, VehicleFlag::ConditionalOrderWait, VehicleFlag::ReplacementPending,
	VehicleFlag::LoadingFinished, VehicleFlag::StopLoading,
};

/**
 * Get the coupling state of a combination.
 * @param head The head of the combination.
 * @return The coupling state, or nullptr if the train is not a combination.
 */
const TrainCoupling *GetTrainCoupling(const Train *head)
{
	if (!head->flags.Test(VehicleRailFlag::CoupledCombination)) return nullptr;
	auto it = _train_couplings.find(head->index);
	return it != _train_couplings.end() ? &it->second : nullptr;
}

/**
 * Is this train a combination of two coupled trains?
 * @param head The head of the train.
 * @return True if the train is a combination.
 */
bool IsTrainCoupled(const Train *head)
{
	return head->flags.Test(VehicleRailFlag::CoupledCombination);
}

/**
 * Get the head of the passive train of a combination.
 * @param head The head of the combination.
 * @return The head of the passive train, or nullptr if the train is not a combination.
 */
Train *GetCoupledPassiveHead(const Train *head)
{
	const TrainCoupling *tc = GetTrainCoupling(head);
	return tc != nullptr ? Train::Get(tc->passive_head) : nullptr;
}

/**
 * Get the head of the combination of which this vehicle is the passive head.
 * @param passive_head The head of the passive train.
 * @return The head of the combination, or nullptr if the vehicle is not a passive head.
 */
Train *GetCouplingCombinationHead(const Train *passive_head)
{
	if (!passive_head->flags.Test(VehicleRailFlag::CoupledPassiveHead)) return nullptr;
	return passive_head->First();
}

/**
 * Swap the identities (orders, timetable, unit number, name, group, statistics, loading state, ...) of two train heads.
 * The physical vehicles stay where they are.
 * @param a The first head.
 * @param b The second head.
 */
static void SwapTrainIdentities(Train *a, Train *b)
{
	/* Only the identity flags are swapped, physical vehicle flags stay with the vehicle. */
	const VehicleFlags a_identity_flags = a->vehicle_flags & VEHICLE_FLAGS_IDENTITY;
	const VehicleFlags b_identity_flags = b->vehicle_flags & VEHICLE_FLAGS_IDENTITY;
	a->vehicle_flags.Reset(VEHICLE_FLAGS_IDENTITY).Set(b_identity_flags);
	b->vehicle_flags.Reset(VEHICLE_FLAGS_IDENTITY).Set(a_identity_flags);

	std::swap(a->name, b->name);
	std::swap(a->dispatch_records, b->dispatch_records);
	std::swap(a->current_order_time, b->current_order_time);
	std::swap(a->lateness_counter, b->lateness_counter);
	std::swap(a->timetable_start, b->timetable_start);
	std::swap(a->service_interval, b->service_interval);
	std::swap(a->cur_real_order_index, b->cur_real_order_index);
	std::swap(a->cur_implicit_order_index, b->cur_implicit_order_index);
	std::swap(a->cur_timetable_order_index, b->cur_timetable_order_index);

	a->SwapSharedOrderListMembership(b);

	std::swap(a->unitnumber, b->unitnumber);
	std::swap(a->group_id, b->group_id);
	std::swap(a->current_order, b->current_order);
	std::swap(a->dest_tile, b->dest_tile);
	std::swap(a->profit_this_year, b->profit_this_year);
	std::swap(a->profit_last_year, b->profit_last_year);
	std::swap(a->profit_lifetime, b->profit_lifetime);
	std::swap(a->current_loading_time, b->current_loading_time);
	std::swap(a->last_station_visited, b->last_station_visited);
	std::swap(a->load_unload_ticks, b->load_unload_ticks);
	std::swap(a->unbunch_state, b->unbunch_state);
	std::swap(a->speed_restriction, b->speed_restriction);
	{
		const bool a_stopped = a->vehstatus.Test(VehState::Stopped);
		a->vehstatus.Set(VehState::Stopped, b->vehstatus.Test(VehState::Stopped));
		b->vehstatus.Set(VehState::Stopped, a_stopped);
	}
	{
		const bool a_exempt = a->flags.Test(VehicleRailFlag::SpeedAdaptationExempt);
		a->flags.Set(VehicleRailFlag::SpeedAdaptationExempt, b->flags.Test(VehicleRailFlag::SpeedAdaptationExempt));
		b->flags.Set(VehicleRailFlag::SpeedAdaptationExempt, a_exempt);
	}

	/* Loading state */
	std::swap(a->cargo_payment, b->cargo_payment);
	if (a->cargo_payment != nullptr) a->cargo_payment->front = a;
	if (b->cargo_payment != nullptr) b->cargo_payment->front = b;
	HideFillingPercent(&a->fill_percent_te_id);
	HideFillingPercent(&b->fill_percent_te_id);
	for (Station *st : Station::Iterate()) {
		for (Vehicle *&v : st->loading_vehicles) {
			if (v == a) {
				v = b;
			} else if (v == b) {
				v = a;
			}
		}
	}

	if (a->vehicle_flags.Test(VehicleFlag::HaveSlot) || b->vehicle_flags.Test(VehicleFlag::HaveSlot)) {
		TraceRestrictSwapVehicleOccupantInAllSlots(a->index, b->index);
	}

	a->InvalidateNewGRFCacheOfChain();
	b->InvalidateNewGRFCacheOfChain();
}

/**
 * Close the windows which show a train head, used when its identity changes or it stops being a primary vehicle.
 * @param v The vehicle.
 */
static void CloseTrainHeadWindows(const Train *v)
{
	CloseWindowById(WindowClass::VehicleView, v->index);
	CloseWindowById(WindowClass::VehicleOrders, v->index);
	CloseWindowById(WindowClass::VehicleRefit, v->index);
	CloseWindowById(WindowClass::VehicleDetails, v->index);
	CloseWindowById(WindowClass::VehicleTimetable, v->index);
	CloseWindowById(WindowClass::ScheduledDispatchSlots, v->index);
	CloseWindowById(WindowClass::VehicleOrderImportErrors, v->index);
	DeleteNewGRFInspectWindow(GrfSpecFeature::Trains, v->index.base());
}

/** Result of the geometric check whether two trains can be coupled. */
struct CouplingArrangement {
	Train *first = nullptr;  ///< Head of the train which forms the first part of the combination chain.
	Train *second = nullptr; ///< Head of the train which forms the second part of the combination chain.
};

/**
 * Check whether vehicle \a next is positioned directly behind vehicle \a prev, as if they were consecutive vehicles in a chain.
 * @param prev The last vehicle of the first part.
 * @param next The first vehicle of the second part.
 * @return True if the vehicles are touching and aligned.
 */
static bool AreVehiclesConsecutive(const Train *prev, const Train *next)
{
	if (prev->direction != next->direction || !IsDiagonalDirection(prev->direction)) return false;
	if (prev->z_pos != next->z_pos) return false;
	if ((prev->track | next->track) & (TRACK_BIT_WORMHOLE | TRACK_BIT_DEPOT)) return false;

	const TileIndexDiffC diff = TileIndexDiffCByDiagDir(DirToDiagDir(prev->direction));
	const int x_diff = prev->x_pos - next->x_pos;
	const int y_diff = prev->y_pos - next->y_pos;

	/* The next vehicle must be behind the previous one on the same line. */
	int dist;
	if (diff.x != 0) {
		if (y_diff != 0) return false;
		dist = x_diff * diff.x;
	} else {
		if (x_diff != 0) return false;
		dist = y_diff * diff.y;
	}

	/* Expected distance between the centres of consecutive vehicles, see Train::CalcNextVehicleOffset. */
	const int expected_fwd = prev->gcache.cached_veh_length / 2 + (next->gcache.cached_veh_length + 1) / 2;
	const int expected_bwd = (prev->gcache.cached_veh_length + 1) / 2 + next->gcache.cached_veh_length / 2;
	return dist == expected_fwd || dist == expected_bwd;
}

/**
 * Find how two trains can be arranged into one chain.
 * @param a The first train.
 * @param b The second train.
 * @return The arrangement, or an empty arrangement if the trains are not positioned such that they can be coupled.
 */
static CouplingArrangement FindCouplingArrangement(Train *a, Train *b)
{
	if (AreVehiclesConsecutive(a->Last(), b)) return { a, b };
	if (AreVehiclesConsecutive(b->Last(), a)) return { b, a };
	return {};
}

/**
 * Check whether all vehicles of a train are inside platforms of the given station.
 * @param t The train.
 * @param station The station.
 * @return True if the train is completely within the station.
 */
static bool IsTrainCompletelyInStation(const Train *t, StationID station)
{
	for (const Train *u = t; u != nullptr; u = u->Next()) {
		if (!IsRailStationTile(u->tile) || GetStationIndex(u->tile) != station) return false;
	}
	return true;
}

/**
 * Check common preconditions for a train taking part in coupling.
 * @param t The train.
 * @return Error or success.
 */
static CommandCost CheckTrainCanTakePartInCoupling(const Train *t)
{
	if (!t->IsFrontEngine()) return CMD_ERROR;
	if (t->vehstatus.Test(VehState::Crashed)) return CMD_ERROR;
	if (t->IsVirtual()) return CMD_ERROR;
	if (t->cur_speed != 0) return CommandCost(STR_ERROR_TRAIN_COUPLING_MUST_BE_STOPPED_IN_STATION);
	if (t->flags.Any(VehicleRailFlagsIsBroken)) return CommandCost(STR_ERROR_TRAIN_COUPLING_MUST_BE_STOPPED_IN_STATION);
	if (!IsRailStationTile(t->tile) || !IsTrainCompletelyInStation(t, GetStationIndex(t->tile))) {
		return CommandCost(STR_ERROR_TRAIN_COUPLING_MUST_BE_STOPPED_IN_STATION);
	}
	return CommandCost();
}

/**
 * Check whether two trains can be coupled.
 * @param waiting The train which is waiting in the platform.
 * @param arriving The train which arrived to couple.
 * @return Error or success.
 */
CommandCost CheckCanCoupleTrains(const Train *waiting, const Train *arriving)
{
	if (waiting == arriving) return CMD_ERROR;
	if (waiting->owner != arriving->owner) return CMD_ERROR;

	CommandCost ret = CheckTrainCanTakePartInCoupling(waiting);
	if (ret.Failed()) return ret;
	ret = CheckTrainCanTakePartInCoupling(arriving);
	if (ret.Failed()) return ret;

	if (GetStationIndex(waiting->tile) != GetStationIndex(arriving->tile)) return CommandCost(STR_ERROR_TRAIN_COUPLING_MUST_BE_STOPPED_IN_STATION);

	if (IsTrainCoupled(waiting) || IsTrainCoupled(arriving)) return CommandCost(STR_ERROR_TRAIN_COUPLING_ALREADY_COUPLED);

	CouplingArrangement arrangement = FindCouplingArrangement(const_cast<Train *>(waiting), const_cast<Train *>(arriving));
	if (arrangement.first == nullptr) return CommandCost(STR_ERROR_TRAIN_COUPLING_NOT_ADJACENT);

	if (static_cast<uint>(waiting->gcache.cached_total_length + arriving->gcache.cached_total_length) > static_cast<uint>(_settings_game.vehicle.max_train_length * TILE_SIZE)) {
		return CommandCost(STR_ERROR_TRAIN_TOO_LONG);
	}

	return CommandCost();
}

/**
 * Copy the last loading station of the head to all vehicles of the train, if the vehicles do not have it separately.
 * @param head The head of the train.
 */
static void SeparateLastLoadingStation(Train *head)
{
	if (head->vehicle_flags.Test(VehicleFlag::LastLoadStationSeparate)) return;
	for (Train *u = head->Next(); u != nullptr; u = u->Next()) {
		u->last_loading_station = head->last_loading_station;
		u->last_loading_tick = head->last_loading_tick;
	}
	head->vehicle_flags.Set(VehicleFlag::LastLoadStationSeparate);
}

/**
 * Let a train which is about to be coupled as passive train leave the station.
 * @param t The train.
 */
static void LeaveStationForCoupling(Train *t)
{
	if (t->current_order.IsAnyLoadingType()) {
		const StationID station = t->last_station_visited;
		t->LeaveStation();

		/* Advance to the next order if we were loading at the current one, as is done when leaving normally. */
		const Order *order = t->GetOrder(t->cur_implicit_order_index);
		if (order != nullptr && order->IsType(OT_GOTO_STATION) && order->GetDestination() == station) {
			t->IncrementImplicitOrderIndex();
		}
	}
	t->current_order.Free();
	HideFillingPercent(&t->fill_percent_te_id);
}

/**
 * Couple two trains into one combination.
 * @param waiting The train which is waiting in the platform.
 * @param arriving The train which arrived to couple.
 * @param arriving_leads Whether the arriving train leads the combination.
 * @pre CheckCanCoupleTrains(waiting, arriving).Succeeded()
 */
void CoupleTrains(Train *waiting, Train *arriving, bool arriving_leads)
{
	CouplingArrangement arrangement = FindCouplingArrangement(waiting, arriving);
	assert(arrangement.first != nullptr);

	Train *first = arrangement.first;
	Train *second = arrangement.second;
	Train *leader = arriving_leads ? arriving : waiting;
	Train *passive = arriving_leads ? waiting : arriving;

	/* Free any path reservations, they are re-made below for the whole combination. */
	for (Train *t : { first, second }) {
		FreeTrainTrackReservation(t);
		t->lookahead.reset();
		t->force_proceed = TFP_NONE;
		t->flags.Reset({VehicleRailFlag::Stuck, VehicleRailFlag::Reversing, VehicleRailFlag::LeavingStation});
		t->wait_counter = 0;
	}

	LeaveStationForCoupling(passive);

	SeparateLastLoadingStation(first);
	SeparateLastLoadingStation(second);

	/* The passive train stops being a primary vehicle. */
	GroupStatistics::CountVehicle(passive, -1);

	TrainCoupling tc{};
	tc.passive_group = passive->group_id;
	tc.identities_swapped = (leader == second);

	CloseTrainHeadWindows(second);
	if (tc.identities_swapped) {
		CloseTrainHeadWindows(first);
		SwapTrainIdentities(first, second);
	}

	/* The combination keeps the driving direction of its first part. */
	const bool driving_backwards = first->vehicle_flags.Test(VehicleFlag::DrivingBackwards);
	for (Train *u = second; u != nullptr; u = u->Next()) {
		u->vehicle_flags.Set(VehicleFlag::DrivingBackwards, driving_backwards);
	}

	first->Last()->SetNext(second);
	NormaliseSubtypes(first);

	second->flags.Set(VehicleRailFlag::CoupledPassiveHead);
	first->flags.Set(VehicleRailFlag::CoupledCombination);

	tc.head = first->index;
	tc.passive_head = second->index;
	_train_couplings[first->index] = tc;

	UpdateTrainGroupID(first);
	first->ConsistChanged(CCF_TRACK);
	CheckBreakdownFlags(first);
	first->flags.Set(VehicleRailFlag::ConsistSpeedReduction);
	InvalidateVehicleTickCaches();

	first->ReserveTrackUnderConsist();

	first->MarkDirty();
	SetWindowDirty(WindowClass::VehicleView, first->index);
	SetWindowDirty(WindowClass::VehicleDetails, first->index);
	InvalidateWindowData(WindowClass::VehicleRefit, first->index, VIWD_CONSIST_CHANGED);
	SetWindowDirty(WindowClass::Company, first->owner);
	InvalidateVehicleListWindows(VehicleType::Train);
}

/**
 * Let a train which was split off depart from the station it is standing in, in the same way as after loading.
 * The departure handles the orders, reversing and the path reservation.
 * @param t The train.
 */
static void PrepareDepartureAfterSplit(Train *t)
{
	t->cur_speed = 0;
	t->subspeed = 0;
	t->wait_counter = 0;
	t->force_proceed = TFP_NONE;
	t->flags.Reset({VehicleRailFlag::Stuck, VehicleRailFlag::Reversing});
	t->lookahead.reset();

	const Train *moving_front = t->GetMovingFront();
	if (IsRailStationTile(moving_front->tile)) {
		const StationID station = GetStationIndex(moving_front->tile);
		t->last_station_visited = station;

		/* When this station is the current order of the train, load here according to that order. */
		const Order *order = t->GetOrder(t->cur_real_order_index);
		if (order != nullptr && order->IsType(OT_GOTO_STATION) && order->GetDestination() == station) {
			t->current_order = *order;
			t->BeginLoading();
			return;
		}

		t->current_order.MakeLeaveStation();
		t->current_order.SetDestination(station);
		t->flags.Set(VehicleRailFlag::LeavingStation);
	} else {
		t->current_order.Free();
	}
}

/**
 * Check whether a combination can be split.
 * @param head The head of the combination.
 * @return Error or success.
 */
CommandCost CheckCanSplitCombination(const Train *head)
{
	if (!head->IsFrontEngine()) return CMD_ERROR;
	if (!IsTrainCoupled(head)) return CommandCost(STR_ERROR_TRAIN_COUPLING_NOT_COUPLED);
	if (head->vehstatus.Test(VehState::Crashed)) return CMD_ERROR;
	if (head->cur_speed != 0) return CommandCost(STR_ERROR_TRAIN_COUPLING_MUST_BE_STOPPED_IN_STATION);
	for (const Train *u = head; u != nullptr; u = u->Next()) {
		if (u->track & (TRACK_BIT_WORMHOLE | TRACK_BIT_DEPOT)) return CommandCost(STR_ERROR_TRAIN_COUPLING_MUST_BE_STOPPED_IN_STATION);
	}
	return CommandCost();
}

/**
 * Split a combination back into the two original trains.
 * @param head The head of the combination.
 * @return The train which was the passive train.
 * @pre CheckCanSplitCombination(head).Succeeded()
 */
Train *SplitCombination(Train *head)
{
	auto it = _train_couplings.find(head->index);
	assert(it != _train_couplings.end());
	const TrainCoupling tc = it->second;
	_train_couplings.erase(it);

	Train *second = Train::Get(tc.passive_head);
	assert(second->First() == head);

	FreeTrainTrackReservation(head);
	head->lookahead.reset();

	second->Previous()->SetNext(nullptr);
	NormaliseSubtypes(second);

	head->flags.Reset(VehicleRailFlag::CoupledCombination);
	second->flags.Reset(VehicleRailFlag::CoupledPassiveHead);

	/* Restore the group of the passive train, which was overwritten by the group of the combination. */
	second->group_id = tc.passive_group;

	Train *passive = second;
	if (tc.identities_swapped) {
		CloseTrainHeadWindows(head);
		SwapTrainIdentities(head, second);
		passive = head;
	}

	for (Train *t : { head, second }) {
		UpdateTrainGroupID(t);
		t->ConsistChanged(CCF_TRACK);
		CheckBreakdownFlags(t);
		t->flags.Set(VehicleRailFlag::ConsistSpeedReduction);
		t->vehicle_flags.Set(VehicleFlag::LastLoadStationSeparate);
	}

	/* The passive train is a primary vehicle again, it continues in the same running state as the combination. */
	GroupStatistics::CountVehicle(passive, 1);
	Train *leader = (passive == head) ? second : head;
	passive->vehstatus.Set(VehState::Stopped, leader->vehstatus.Test(VehState::Stopped));
	PrepareDepartureAfterSplit(passive);
	passive->flags.Set(VehicleRailFlag::SplitOffLast);
	leader->flags.Reset(VehicleRailFlag::SplitOffLast);

	/* Neither train may drive into the other until they have moved apart. */
	head->split_partner = second->index;
	second->split_partner = head->index;

	InvalidateVehicleTickCaches();

	head->ReserveTrackUnderConsist();
	second->ReserveTrackUnderConsist();

	for (Train *t : { head, second }) {
		t->MarkDirty();
		SetWindowDirty(WindowClass::VehicleView, t->index);
		SetWindowDirty(WindowClass::VehicleDetails, t->index);
		InvalidateWindowData(WindowClass::VehicleRefit, t->index, VIWD_CONSIST_CHANGED);
	}
	SetWindowDirty(WindowClass::Company, head->owner);
	InvalidateVehicleListWindows(VehicleType::Train);

	return passive;
}

/**
 * A vehicle which might take part in a coupling is about to be deleted.
 * @param v The vehicle.
 */
void OnTrainCouplingVehicleDeleted(Train *v)
{
	if (v->IsFrontEngine()) {
		for (Train *t : Train::IterateFrontOnly()) {
			if (t->coupling_target == v->index) ClearCouplingApproach(t);
			if (t->split_partner == v->index) t->split_partner = VehicleID::Invalid();
		}
	}
	if (v->flags.Test(VehicleRailFlag::CoupledCombination)) {
		_train_couplings.erase(v->index);
		v->flags.Reset(VehicleRailFlag::CoupledCombination);
	}
	if (v->flags.Test(VehicleRailFlag::CoupledPassiveHead)) {
		for (auto it = _train_couplings.begin(); it != _train_couplings.end(); ++it) {
			if (it->second.passive_head == v->index) {
				Train *head = Train::GetIfValid(it->second.head);
				if (head != nullptr) head->flags.Reset(VehicleRailFlag::CoupledCombination);
				_train_couplings.erase(it);
				break;
			}
		}
		v->flags.Reset(VehicleRailFlag::CoupledPassiveHead);
	}
}

/**
 * Get all vehicles which can own an order list: the front vehicles, and the heads of the passive trains in coupled combinations.
 * The heads of passive trains keep their orders while coupled, but are not visited by Vehicle::IterateFrontOnly.
 * @return The vehicles.
 */
std::vector<Vehicle *> GetFrontAndCoupledPassiveVehicles()
{
	std::vector<Vehicle *> result;
	for (Vehicle *v : Vehicle::IterateFrontOnly()) result.push_back(v);
	for (const auto &it : _train_couplings) {
		Train *passive = Train::GetIfValid(it.second.passive_head);
		if (passive != nullptr) result.push_back(passive);
	}
	return result;
}

/**
 * Check that an order list never has two couple orders without a split order in between, which would couple a combination again.
 * @param list The order list.
 * @param index The index of the order which gets a new coupling mode, or where a new order is inserted.
 * @param new_mode The new coupling mode of that order.
 * @param insert True if a new station order with  new_mode is inserted at  index.
 * @return True if the sequence of coupling orders is valid.
 */
bool IsCouplingOrderSequenceValid(const OrderList *list, uint index, OrderCouplingMode new_mode, bool insert)
{
	/* Coupling modes of the station orders, in order list order. */
	std::vector<OrderCouplingMode> modes;
	const uint count = list != nullptr ? list->GetNumOrders() : 0;
	for (uint idx = 0; idx <= count; idx++) {
		if (idx == index) {
			modes.push_back(new_mode);
			if (!insert) continue;
		}
		if (idx == count) break;
		const Order *o = list->GetOrderAt(idx);
		if (o->IsType(OT_GOTO_STATION)) modes.push_back(o->GetCouplingMode());
	}

	/* A single couple order is always valid. */
	if (std::count(modes.begin(), modes.end(), OrderCouplingMode::Couple) <= 1) return true;

	/* Walk the (cyclic) order list twice, the state is only reliable during the second pass. */
	bool coupled = false;
	for (size_t i = 0; i < 2 * modes.size(); i++) {
		const OrderCouplingMode mode = modes[i % modes.size()];
		if (mode == OrderCouplingMode::Couple) {
			if (coupled && i >= modes.size()) return false;
			coupled = true;
		} else if (mode == OrderCouplingMode::Split) {
			coupled = false;
		}
	}
	return true;
}

/**
 * Get the order of a train for the station it is heading to or loading at, if it is a coupling or splitting order.
 * @param v The train.
 * @return The order, or nullptr if the current order is not a coupling or splitting order.
 */
const Order *GetCurrentCouplingOrder(const Train *v)
{
	StationID station;
	if (v->current_order.IsType(OT_GOTO_STATION)) {
		station = v->current_order.GetDestination().ToStationID();
	} else if (v->current_order.IsAnyLoadingType()) {
		station = v->last_station_visited;
	} else {
		return nullptr;
	}
	const Order *order = v->GetOrder(v->cur_real_order_index);
	if (order == nullptr || !order->IsType(OT_GOTO_STATION) || order->GetDestination() != station) return nullptr;
	if (order->GetCouplingMode() == OrderCouplingMode::None) return nullptr;
	return order;
}

/**
 * Validate the coupling state against the vehicles, used after loading a game.
 * Invalid entries are removed.
 */
void ValidateTrainCouplings()
{
	for (auto it = _train_couplings.begin(); it != _train_couplings.end();) {
		const TrainCoupling &tc = it->second;
		Train *head = Train::GetIfValid(tc.head);
		Train *passive = Train::GetIfValid(tc.passive_head);
		if (head == nullptr || passive == nullptr || !head->IsFrontEngine() || passive->First() != head ||
				!head->flags.Test(VehicleRailFlag::CoupledCombination) || !passive->flags.Test(VehicleRailFlag::CoupledPassiveHead)) {
			Debug(sl, 0, "Removing invalid train coupling: head {}, passive head {}", tc.head, tc.passive_head);
			if (head != nullptr) head->flags.Reset(VehicleRailFlag::CoupledCombination);
			if (passive != nullptr) passive->flags.Reset(VehicleRailFlag::CoupledPassiveHead);
			it = _train_couplings.erase(it);
		} else {
			++it;
		}
	}
	for (Train *t : Train::Iterate()) {
		if (t->flags.Test(VehicleRailFlag::CoupledCombination) && _train_couplings.find(t->index) == _train_couplings.end()) {
			t->flags.Reset(VehicleRailFlag::CoupledCombination);
		}
	}
}

/**
 * Get the train to which a train is driving to couple, if the coupling is (still) possible.
 * @param v The approaching train.
 * @return The target train, or nullptr if there is no valid target.
 */
Train *GetCouplingApproachTarget(const Train *v)
{
	if (v->coupling_target == VehicleID::Invalid()) return nullptr;
	Train *target = Train::GetIfValid(v->coupling_target);
	if (target == nullptr || target == v || !target->IsFrontEngine() || target->owner != v->owner) return nullptr;
	if (target->vehstatus.Test(VehState::Crashed) || v->vehstatus.Test(VehState::Crashed)) return nullptr;
	if (IsTrainCoupled(target) || IsTrainCoupled(v)) return nullptr;
	if (target->cur_speed != 0 || !IsRailStationTile(target->tile)) return nullptr;
	if (!IsTrainCompletelyInStation(target, GetStationIndex(target->tile))) return nullptr;

	/* The approaching train must be heading for the station of the target. */
	const StationID station = GetStationIndex(target->tile);
	if (v->current_order.IsType(OT_GOTO_STATION)) {
		if (v->current_order.GetDestination() != station) return nullptr;
	} else if (!v->current_order.IsAnyLoadingType() || v->last_station_visited != station) {
		return nullptr;
	}
	return target;
}

/**
 * Is this tile part of the platform on which the coupling target of a train is waiting?
 * @param v The approaching train.
 * @param tile The tile to check.
 * @return True if the tile is part of the platform of the coupling target.
 */
bool IsCouplingApproachPlatformTile(const Train *v, TileIndex tile)
{
	const Train *target = GetCouplingApproachTarget(v);
	if (target == nullptr || !IsRailStationTile(tile) || !IsCompatibleTrainStationTile(tile, target->tile)) return false;

	/* Walk along the platform from the tile to find the target. */
	const Axis axis = GetRailStationAxis(tile);
	for (DiagDirection dir : { AxisToDiagDir(axis), ReverseDiagDir(AxisToDiagDir(axis)) }) {
		const TileIndexDiff diff = TileOffsByDiagDir(dir);
		for (TileIndex t = tile; IsCompatibleTrainStationTile(t, tile); t = TileAdd(t, diff)) {
			if (t == target->tile || t == target->Last()->tile) return true;
		}
	}
	return false;
}

/**
 * Get the position of a point along a direction of travel.
 * @param x X coordinate.
 * @param y Y coordinate.
 * @param dir Direction of travel.
 * @return Position, increasing in the direction of travel.
 */
static int GetCoordinateAlongDirection(int x, int y, DiagDirection dir)
{
	switch (dir) {
		case DiagDirection::NE: return -x;
		case DiagDirection::SE: return y;
		case DiagDirection::SW: return x;
		case DiagDirection::NW: return -y;
		default: NOT_REACHED();
	}
}

/**
 * Get the position of a vehicle in a platform, measured from the begin of the platform in the direction of travel.
 * This uses the same units as GetTrainStopLocation.
 * @param v The vehicle, which must be on a platform tile.
 * @param dir The direction of travel.
 * @return The position of the centre of the vehicle.
 */
static int GetPositionFromPlatformStart(const Train *v, DiagDirection dir)
{
	const TileIndexDiff diff = TileOffsByDiagDir(dir);
	TileIndex start = v->tile;
	while (IsCompatibleTrainStationTile(TileAdd(start, -diff), v->tile)) start = TileAdd(start, -diff);

	int start_x = TileX(start) * TILE_SIZE;
	int start_y = TileY(start) * TILE_SIZE;
	if (dir == DiagDirection::NE) start_x += TILE_SIZE - 1;
	if (dir == DiagDirection::NW) start_y += TILE_SIZE - 1;

	return GetCoordinateAlongDirection(v->x_pos, v->y_pos, dir) - GetCoordinateAlongDirection(start_x, start_y, dir);
}

/**
 * Get the location where the moving front of an approaching train should stop to couple to its target.
 * The location is measured from the begin of the platform in the direction of travel, in the same way as GetTrainStopLocation.
 * @param v The approaching train.
 * @param dir The direction in which the train drives into the platform.
 * @return The location of the centre of the moving front vehicle, or std::nullopt if the train can not stop to couple.
 */
std::optional<int> GetCouplingStopLocation(const Train *v, DiagDirection dir)
{
	const Train *target = GetCouplingApproachTarget(v);
	if (target == nullptr || !IsValidDiagDirection(dir) || DiagDirToAxis(dir) != GetRailStationAxis(target->tile)) return std::nullopt;

	/* The end of the target nearest to the approaching train. */
	const Train *first = target;
	const Train *last = target->Last();
	const Train *near = GetCoordinateAlongDirection(first->x_pos, first->y_pos, dir) <= GetCoordinateAlongDirection(last->x_pos, last->y_pos, dir) ? first : last;

	const int near_position = GetPositionFromPlatformStart(near, dir);

	/* Stop such that the moving front is positioned as the next vehicle in a chain after the near end of the target. */
	const Train *front = v->GetMovingFront();
	const int stop = near_position - (near->gcache.cached_veh_length / 2 + (front->gcache.cached_veh_length + 1) / 2);
	if (stop < 0) return std::nullopt;
	return stop;
}

/**
 * Get the remaining distance until the moving front of an approaching train reaches its coupling stop location.
 * @param v The approaching train, of which the moving front must be on the platform of the coupling target.
 * @return The remaining distance, or std::nullopt if the train can not stop to couple.
 */
std::optional<int> GetCouplingStopDistance(const Train *v)
{
	const Train *moving_front = v->GetMovingFront();
	const Direction direction = moving_front->GetMovingDirection();
	if (!IsDiagonalDirection(direction) || !IsRailStationTile(moving_front->tile)) return std::nullopt;
	const DiagDirection dir = DirToDiagDir(direction);
	std::optional<int> stop = GetCouplingStopLocation(v, dir);
	if (!stop.has_value()) return std::nullopt;
	return *stop - GetPositionFromPlatformStart(moving_front, dir);
}

/**
 * Is a train approaching this train to couple?
 * @param target The train which is waiting.
 * @return True if a train is approaching to couple.
 */
bool IsTrainBeingApproachedForCoupling(const Train *target)
{
	for (const Train *t : Train::IterateFrontOnly()) {
		/* Only hold the waiting train once the approaching train reserved its path into the platform. */
		if (t->coupling_target == target->index && t->IsFrontEngine() && IsValidDiagDirection(t->coupling_dir) && GetCouplingApproachTarget(t) == target) return true;
	}
	return false;
}

/**
 * Stop approaching a train to couple.
 * @param v The approaching train.
 */
void ClearCouplingApproach(Train *v)
{
	v->coupling_target = VehicleID::Invalid();
	v->coupling_dir = DiagDirection::Invalid;
	v->flags.Reset({VehicleRailFlag::CouplingArrivingLeads, VehicleRailFlag::CouplingSpeedLimit, VehicleRailFlag::CouplingFromOrders});
	SetWindowDirty(WindowClass::VehicleView, v->index);
}

/**
 * A waiting train became the target of a train which drives to it to couple.
 * Free the path reservation in front of the waiting train, which may block the platform for the approaching train.
 * @param target The waiting train.
 */
static void ReleaseCouplingTargetReservation(Train *target)
{
	if (target->cur_speed != 0 || !target->current_order.IsAnyLoadingType()) return;
	FreeTrainTrackReservation(target);
	target->lookahead.reset();
	target->ReserveTrackUnderConsist();
}

/**
 * Does the group or order list of the coupling filter of an order still exist?
 * @param order The couple order.
 * @return False if the group or order list was deleted.
 */
bool IsCouplingFilterDefined(const Order *order)
{
	switch (order->GetCouplingFilter()) {
		case OrderCouplingFilter::Group:
			return Group::IsValidID(GroupID(order->GetCouplingFilterID()));
		case OrderCouplingFilter::OrderList:
			return OrderList::IsValidID(OrderListID(order->GetCouplingFilterID()));
		default:
			return true;
	}
}

/**
 * Does a train match the coupling filter of a couple order?
 * @param order The couple order.
 * @param t The train.
 * @return True if the train may be coupled to.
 */
static bool DoesTrainMatchCouplingFilter(const Order *order, const Train *t)
{
	/* An undefined filter allows any train, CheckOrders warns about it. */
	if (!IsCouplingFilterDefined(order)) return true;

	switch (order->GetCouplingFilter()) {
		case OrderCouplingFilter::Group:
			return GroupIsInGroup(t->group_id, GroupID(order->GetCouplingFilterID()));
		case OrderCouplingFilter::OrderList:
			return t->orders != nullptr && t->orders->index == OrderListID(order->GetCouplingFilterID());
		default:
			return true;
	}
}

/**
 * Find a partner to couple to for a train with a couple order, and drive to it.
 * The partner must be waiting at the station of the order, with a couple order for that station itself, and both trains must match each other's coupling filter.
 * @param v The train.
 */
void UpdateCouplingApproachFromOrders(Train *v)
{
	const Order *order = v->current_order.IsType(OT_GOTO_STATION) ? GetCurrentCouplingOrder(v) : nullptr;
	const bool couple = order != nullptr && order->GetCouplingMode() == OrderCouplingMode::Couple && !IsTrainCoupled(v);

	if (v->flags.Test(VehicleRailFlag::CouplingFromOrders)) {
		/* Stop approaching when the order changed, or the partner is no longer available. */
		if (!couple || GetCouplingApproachTarget(v) == nullptr) ClearCouplingApproach(v);
	}
	if (!couple || v->coupling_target != VehicleID::Invalid()) return;

	/* Only search for a partner every few ticks. */
	if ((v->tick_counter & 7) != 0) return;

	const Station *st = Station::GetIfValid(order->GetDestination().ToStationID());
	if (st == nullptr) return;
	for (const Vehicle *u : st->loading_vehicles) {
		if (u->type != VehicleType::Train || u == v || !u->IsPrimaryVehicle() || u->owner != v->owner) continue;
		const Train *partner = Train::From(u);
		if (IsTrainCoupled(partner)) continue;
		const Order *partner_order = GetCurrentCouplingOrder(partner);
		if (partner_order == nullptr || partner_order->GetCouplingMode() != OrderCouplingMode::Couple) continue;
		if (!DoesTrainMatchCouplingFilter(order, partner) || !DoesTrainMatchCouplingFilter(partner_order, v)) continue;
		if (IsTrainBeingApproachedForCoupling(partner)) continue;

		v->coupling_target = partner->index;
		v->flags.Set(VehicleRailFlag::CouplingArrivingLeads, order->IsCouplingArrivingLeads());
		v->flags.Set(VehicleRailFlag::CouplingFromOrders);
		if (GetCouplingApproachTarget(v) == partner) {
			ReleaseCouplingTargetReservation(Train::Get(partner->index));
			SetWindowDirty(WindowClass::VehicleView, v->index);
			return;
		}
		ClearCouplingApproach(v);
	}
}

/** Combinations which reached a split order during the current tick. */
static std::vector<VehicleID> _pending_train_splits;

/**
 * A combination stopped at a station: split if the order of the leading or the passive train says so,
 * otherwise the passive train visited this station.
 * The split is done after all vehicles have been ticked, as the train might currently be moving.
 * @param head The head of the combination.
 * @param station The station.
 */
void OnCombinationEnteredStation(Train *head, StationID station)
{
	Train *passive = GetCoupledPassiveHead(head);
	if (passive == nullptr) return;

	bool split = false;
	const Order *leader_order = GetCurrentCouplingOrder(head);
	if (leader_order != nullptr && leader_order->GetCouplingMode() == OrderCouplingMode::Split) split = true;

	/* While coupled, the passive train skips its orders which are not station orders, such as depot orders. */
	for (uint i = 0; i < passive->GetNumOrders(); i++) {
		const Order *o = passive->GetOrder(passive->cur_real_order_index);
		if (o == nullptr || o->IsType(OT_GOTO_STATION)) break;
		passive->IncrementRealOrderIndex();
	}

	const Order *passive_order = passive->GetOrder(passive->cur_real_order_index);
	if (passive_order != nullptr && passive_order->IsType(OT_GOTO_STATION) && passive_order->GetDestination() == station) {
		if (passive_order->GetCouplingMode() == OrderCouplingMode::Split) {
			split = true;
		} else if (!split) {
			/* The passive train visits this station as part of the combination. */
			passive->last_station_visited = station;
			passive->IncrementImplicitOrderIndex();
		}
	}

	if (split) _pending_train_splits.push_back(head->index);
}

/** Split the combinations which reached a split order during this tick. */
static void ProcessPendingTrainSplits()
{
	if (_pending_train_splits.empty()) return;

	std::vector<VehicleID> pending;
	pending.swap(_pending_train_splits);
	for (VehicleID id : pending) {
		Train *head = Train::GetIfValid(id);
		if (head == nullptr || !IsTrainCoupled(head)) continue;
		head->cur_speed = 0;
		head->subspeed = 0;
		CommandCost ret = CheckCanSplitCombination(head);
		if (ret.Failed()) {
			Debug(misc, 1, "Train split of {} failed: {}", head->index, ret.GetErrorMessage());
			continue;
		}
		SplitCombination(head);
	}
}

/**
 * Is a train part of a coupled combination, either as head or as passive train?
 * @param t The head of the train, or the head of the passive train.
 * @return True if the train is coupled.
 */
bool IsTrainInCombination(const Train *t)
{
	return t->flags.Test(VehicleRailFlag::CoupledPassiveHead) || IsTrainCoupled(t);
}

/**
 * Is a train which uses the same order list as a vehicle currently part of a coupled combination?
 * @param v The vehicle.
 * @return True if a train sharing the orders of \a v is coupled.
 */
bool IsOrderListUsedByCombination(const Vehicle *v)
{
	if (v->type != VehicleType::Train) return false;
	for (const Vehicle *u = v->FirstShared(); u != nullptr; u = u->NextShared()) {
		if (IsTrainInCombination(Train::From(u))) return true;
	}
	return false;
}

/**
 * Should the player be warned that a depot order makes a coupled combination stop in a depot?
 * @param v A vehicle using the order.
 * @param order The order.
 * @return True if the order stops in the depot without allowing coupled combinations, and a train with this order list is coupled.
 */
bool ShouldWarnCoupledDepotStop(const Vehicle *v, const Order *order)
{
	if (order == nullptr || !order->IsType(OT_GOTO_DEPOT)) return false;
	if ((order->GetDepotActionType() & ODATFB_HALT) == 0) return false;
	if (order->GetDepotExtraFlags().Test(OrderDepotExtraFlag::CoupledAllowed)) return false;
	return IsOrderListUsedByCombination(v);
}

/**
 * Does a train have a coupling role, see OrderConditionVariable::CouplingRole?
 * @param t The train.
 * @param role The coupling role.
 * @return True if the train has the role.
 */
bool HasTrainCouplingRole(const Train *t, OrderCouplingRole role)
{
	switch (role) {
		case OrderCouplingRole::Coupled:        return IsTrainInCombination(t);
		case OrderCouplingRole::Leading:        return IsTrainCoupled(t);
		case OrderCouplingRole::SplitOffLast:   return t->flags.Test(VehicleRailFlag::SplitOffLast);
		case OrderCouplingRole::CouplingFailed: return t->flags.Test(VehicleRailFlag::CouplingFailedLast);
		default: return false;
	}
}

/**
 * A train leaves a station: remember whether it left a couple order without coupling.
 * @param t The train.
 */
void OnTrainLeavingCouplingStation(Train *t)
{
	const Order *order = GetCurrentCouplingOrder(t);
	if (order == nullptr || order->GetCouplingMode() != OrderCouplingMode::Couple) return;
	t->flags.Set(VehicleRailFlag::CouplingFailedLast, !IsTrainInCombination(t));
}

/** Trains which reached their coupling stop location during the current tick. */
static std::vector<VehicleID> _pending_train_couplings;

/**
 * A train which is approaching another train to couple reached its stop location in the station.
 * The coupling is done after all vehicles have been ticked, as the train might currently be moving.
 * @param consist The approaching train.
 */
void OnCouplingTrainEnteredStation(Train *consist)
{
	if (GetCouplingApproachTarget(consist) == nullptr) return;
	_pending_train_couplings.push_back(consist->index);
}

/** Couple the trains which reached their coupling target during this tick, and split the combinations which reached a split order. */
void ProcessPendingTrainCouplings()
{
	ProcessPendingTrainSplits();
	if (_pending_train_couplings.empty()) return;

	std::vector<VehicleID> pending;
	pending.swap(_pending_train_couplings);
	for (VehicleID id : pending) {
		Train *arriving = Train::GetIfValid(id);
		if (arriving == nullptr || !arriving->IsFrontEngine()) continue;
		Train *target = GetCouplingApproachTarget(arriving);
		if (target == nullptr) continue;

		const bool arriving_leads = arriving->flags.Test(VehicleRailFlag::CouplingArrivingLeads);
		arriving->cur_speed = 0;
		arriving->subspeed = 0;
		CommandCost ret = CheckCanCoupleTrains(target, arriving);
		if (ret.Failed() && ret.GetErrorMessage() == STR_ERROR_TRAIN_COUPLING_NOT_ADJACENT) {
			/* The trains touch head to head or tail to tail, turn the arriving train around. */
			FlipTrainForCoupling(arriving);
			ret = CheckCanCoupleTrains(target, arriving);
			if (ret.Failed()) FlipTrainForCoupling(arriving);
		}
		ClearCouplingApproach(arriving);
		if (ret.Failed()) {
			Debug(misc, 1, "Train coupling of {} to {} failed: {}", arriving->index, target->index, ret.GetErrorMessage());
			for (const Train *t : { target, arriving }) {
				for (const Train *u : { t, t->Last() }) {
					Debug(misc, 1, "  train {} {}: pos {}x{} z {} dir {} len {} backwards {} track 0x{:X}", t->unitnumber, u == t ? "head" : "last",
							u->x_pos, u->y_pos, u->z_pos, u->direction, u->gcache.cached_veh_length, u->IsDrivingBackwards(), u->track);
				}
			}
			continue;
		}
		CoupleTrains(target, arriving, arriving_leads);
	}
}

/**
 * Do two trains touch because one is approaching the other to couple? Such a contact must not lead to a crash.
 * @param a The first train.
 * @param b The second train.
 * @return True if one train is approaching the other to couple.
 */
bool IsCouplingApproachCollision(const Train *a, const Train *b)
{
	return (a->coupling_target == b->index && GetCouplingApproachTarget(a) == b) ||
			(b->coupling_target == a->index && GetCouplingApproachTarget(b) == a);
}

/**
 * Get the distance from the moving front of a train to the nearest end of its split partner, if the partner is directly ahead on the same line.
 * @param v The train.
 * @return The distance between the centres of the vehicles, or std::nullopt if the partner is not directly ahead.
 */
static std::optional<int> GetSplitPartnerDistanceAhead(const Train *v)
{
	const Train *partner = Train::GetIfValid(v->split_partner);
	if (partner == nullptr || !partner->IsFrontEngine() || partner == v) return std::nullopt;

	const Train *moving_front = v->GetMovingFront();
	const Direction direction = moving_front->GetMovingDirection();
	if (!IsDiagonalDirection(direction)) return std::nullopt;
	const DiagDirection dir = DirToDiagDir(direction);
	const int front_position = GetCoordinateAlongDirection(moving_front->x_pos, moving_front->y_pos, dir);

	std::optional<int> result;
	for (const Train *end : { partner, partner->Last() }) {
		/* The partner must be on the same line. */
		if (DiagDirToAxis(dir) == Axis::X ? end->y_pos != moving_front->y_pos : end->x_pos != moving_front->x_pos) continue;
		if (end->z_pos != moving_front->z_pos) continue;
		const int distance = GetCoordinateAlongDirection(end->x_pos, end->y_pos, dir) - front_position;
		if (distance > 0 && (!result.has_value() || distance < *result)) result = distance;
	}
	return result;
}

/**
 * Is a train blocked by the train from which it was just split, because that train is directly ahead of it?
 * @param v The train.
 * @return True if the train may not move.
 */
bool IsTrainBlockedBySplitPartner(const Train *v)
{
	if (v->split_partner == VehicleID::Invalid()) return false;
	std::optional<int> distance = GetSplitPartnerDistanceAhead(v);
	if (!distance.has_value()) return false;

	/* Blocked while the partner is (nearly) touching. */
	return *distance <= 2 * (int)TILE_SIZE;
}

/**
 * Stop guarding against the split partner once the trains have moved apart.
 * @param v The train.
 */
void UpdateSplitPartner(Train *v)
{
	if (v->split_partner == VehicleID::Invalid()) return;
	const Train *partner = Train::GetIfValid(v->split_partner);
	if (partner == nullptr || !partner->IsFrontEngine()) {
		v->split_partner = VehicleID::Invalid();
		return;
	}

	/* Keep guarding while any vehicles of both trains are still on the same tile or adjacent tiles. */
	for (const Train *u = v; u != nullptr; u = u->Next()) {
		for (const Train *w = partner; w != nullptr; w = w->Next()) {
			if (DistanceManhattan(u->tile, w->tile) <= 1) return;
		}
	}
	v->split_partner = VehicleID::Invalid();
}

/**
 * Set or clear the train to which a train should drive to couple.
 * @param flags Type of operation.
 * @param veh_id The approaching train.
 * @param target_id The train to couple to, or VehicleID::Invalid() to stop approaching.
 * @param arriving_leads Whether the approaching train leads the combination.
 * @return The cost of this operation or an error.
 */
CommandCost CmdSetTrainCouplingTarget(DoCommandFlags flags, VehicleID veh_id, VehicleID target_id, bool arriving_leads)
{
	Train *v = Train::GetIfValid(veh_id);
	if (v == nullptr || !v->IsFrontEngine()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	if (target_id != VehicleID::Invalid()) {
		Train *target = Train::GetIfValid(target_id);
		if (target == nullptr || target == v || !target->IsFrontEngine() || target->owner != v->owner) return CMD_ERROR;
		if (IsTrainCoupled(target) || IsTrainCoupled(v)) return CommandCost(STR_ERROR_TRAIN_COUPLING_ALREADY_COUPLED);
	}

	if (flags.Test(DoCommandFlag::Execute)) {
		ClearCouplingApproach(v);
		if (target_id != VehicleID::Invalid()) {
			v->coupling_target = target_id;
			v->flags.Set(VehicleRailFlag::CouplingArrivingLeads, arriving_leads);
			Train *target = GetCouplingApproachTarget(v);
			if (target != nullptr) ReleaseCouplingTargetReservation(target);
		}
	}

	return CommandCost();
}

/**
 * Debug command: cut a train which is not in a depot into two trains, for testing coupling.
 * The rear part becomes a new train which shares the orders of the original train.
 * @param flags Type of operation.
 * @param veh_id The first vehicle of the rear part.
 * @return The cost of this operation or an error.
 */
CommandCost CmdCutTrain(DoCommandFlags flags, VehicleID veh_id)
{
	Train *v = Train::GetIfValid(veh_id);
	if (v == nullptr || v->IsVirtual()) return CMD_ERROR;

	CommandCost ret = CheckOwnership(v->owner);
	if (ret.Failed()) return ret;

	Train *head = v->First();
	if (head == v || !head->IsFrontEngine()) return CMD_ERROR;
	if (!v->IsEngine() || v->IsArticulatedPart() || v->IsRearDualheaded()) return CMD_ERROR;
	if (IsTrainCoupled(head)) return CommandCost(STR_ERROR_TRAIN_COUPLING_COMBINATION_LOCKED);
	if (head->vehstatus.Test(VehState::Crashed)) return CMD_ERROR;
	if (head->cur_speed != 0) return CommandCost(STR_ERROR_TRAIN_COUPLING_MUST_BE_STOPPED_IN_STATION);

	/* Both parts of multiheaded engines must end up in the same train. */
	for (Train *u = head; u != nullptr; u = u->Next()) {
		if (u->track & (TRACK_BIT_WORMHOLE | TRACK_BIT_DEPOT)) return CommandCost(STR_ERROR_TRAIN_COUPLING_MUST_BE_STOPPED_IN_STATION);
		if (u->IsMultiheaded()) {
			bool u_rear = false;
			bool other_rear = false;
			for (Train *w = v; w != nullptr; w = w->Next()) {
				if (w == u) u_rear = true;
				if (w == u->other_multiheaded_part) other_rear = true;
			}
			if (u_rear != other_rear) return CMD_ERROR;
		}
	}

	if (GetFreeUnitNumber(VehicleType::Train) > _settings_game.vehicle.max_trains) return CommandCost(STR_ERROR_TOO_MANY_VEHICLES_IN_GAME);

	if (flags.Test(DoCommandFlag::Execute)) {
		FreeTrainTrackReservation(head);
		head->lookahead.reset();
		SeparateLastLoadingStation(head);

		v->Previous()->SetNext(nullptr);
		NormaliseSubtypes(v);

		v->unitnumber = Company::Get(v->owner)->freeunits[v->type].UseID(GetFreeUnitNumber(VehicleType::Train));
		v->CopyConsistPropertiesFrom(head);
		v->vehicle_flags.Set(VehicleFlag::LastLoadStationSeparate);
		v->vehstatus.Set(VehState::Stopped, head->vehstatus.Test(VehState::Stopped));
		v->orders = head->orders;
		v->AddToShared(head);
		v->group_id = head->group_id;
		v->last_station_visited = head->last_station_visited;
		SetTrainGroupID(v, head->group_id);

		for (Train *t : { head, v }) {
			t->ConsistChanged(CCF_TRACK);
			CheckBreakdownFlags(t);
			t->flags.Set(VehicleRailFlag::ConsistSpeedReduction);
		}
		GroupStatistics::CountVehicle(v, 1);
		InvalidateVehicleTickCaches();
		PrepareDepartureAfterSplit(v);
		head->split_partner = v->index;
		v->split_partner = head->index;

		head->ReserveTrackUnderConsist();
		v->ReserveTrackUnderConsist();

		for (Train *t : { head, v }) {
			t->MarkDirty();
			SetWindowDirty(WindowClass::VehicleView, t->index);
			SetWindowDirty(WindowClass::VehicleDetails, t->index);
		}
		InvalidateWindowData(WindowClass::VehicleOrders, head->index, VIWD_MODIFY_ORDERS);
		SetWindowDirty(WindowClass::Company, head->owner);
		InvalidateVehicleListWindows(VehicleType::Train);
	}

	return CommandCost();
}

/**
 * Couple two trains which are stopped next to each other in a station.
 * @param flags Type of operation.
 * @param waiting_id The train which is waiting in the platform.
 * @param arriving_id The train which arrived to couple.
 * @param arriving_leads Whether the arriving train leads the combination.
 * @return The cost of this operation or an error.
 */
CommandCost CmdCoupleTrains(DoCommandFlags flags, VehicleID waiting_id, VehicleID arriving_id, bool arriving_leads)
{
	Train *waiting = Train::GetIfValid(waiting_id);
	Train *arriving = Train::GetIfValid(arriving_id);
	if (waiting == nullptr || arriving == nullptr) return CMD_ERROR;

	CommandCost ret = CheckOwnership(waiting->owner);
	if (ret.Failed()) return ret;
	ret = CheckOwnership(arriving->owner);
	if (ret.Failed()) return ret;

	ret = CheckCanCoupleTrains(waiting, arriving);
	if (ret.Failed()) return ret;

	if (flags.Test(DoCommandFlag::Execute)) CoupleTrains(waiting, arriving, arriving_leads);

	return CommandCost();
}

/**
 * Split a combination of coupled trains.
 * @param flags Type of operation.
 * @param veh_id The head of the combination.
 * @return The cost of this operation or an error.
 */
CommandCost CmdSplitCoupledTrain(DoCommandFlags flags, VehicleID veh_id)
{
	Train *head = Train::GetIfValid(veh_id);
	if (head == nullptr) return CMD_ERROR;

	CommandCost ret = CheckOwnership(head->owner);
	if (ret.Failed()) return ret;

	ret = CheckCanSplitCombination(head);
	if (ret.Failed()) return ret;

	if (flags.Test(DoCommandFlag::Execute)) SplitCombination(head);

	return CommandCost();
}
