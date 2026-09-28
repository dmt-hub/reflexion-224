#pragma once
// Part:    none (simulation machinery): the host's clock of record.
// Inputs:  at() (a board schedules an action), set_cpu_time() (when the
//          CPU's next state runs), step_one() (the backplane runs the next thing).
// Outputs: now(); row phases and CPU states, passed to the backplane.
//
// Things happen in time order:
// - Row phases: every DSP row runs the same phases at the same offsets from
//   its marker. The execute phase falls in the next row's window, so it acts
//   on the previous row's microinstruction.
// - Bus effects: one-shot actions the boards schedule with at() -- strobe
//   edges, latch settlings, WCS commits.
// - The CPU's states: a second clock, one state every 488.28 ns.
// On an exact tie, whatever was scheduled first runs first: a row's phases
// count as scheduled at the previous row's marker. Late actions, and then
// the CPU's state, run after everything else at their time: at a tie, the
// DSP side goes first.
#include "timing.hpp"
#include <algorithm>
#include <functional>
#include <stdexcept>
#include <vector>

namespace lexicon224x::cpu {

enum class RowPhase { Begin, ExecutePrevious, Fetch, ResetDecode, Converter };

class Scheduler {
public:
    RowTiming timing = timing_224x;     // the model's row timing (timing.hpp); set before the first step

    Tick now() const {
        return time;
    }

    // Schedule `action` at `when` (not in the past). A late action runs after
    // the row phases and the other actions at the same time.
    void at(Tick when, std::function<void()> action, bool late = false) {
        if (when < time) {
            throw std::logic_error("event scheduled in the past");
        }
        events.push_back({when, serial++, time, late, std::move(action)});
        std::push_heap(events.begin(), events.end(), fires_after);
    }

    // The CPU's next state runs at `when`.
    void set_cpu_time(Tick when) {
        cpu_time = when;
    }

    // Run the next row phase, bus effect or CPU state if it happens at or
    // before `deadline`, and return whether one ran. A row phase brings the
    // phases after it that come before anything else. Phases go to
    // run_phase(RowPhase, row_number), CPU states to run_cpu().
    template <class RunPhase, class RunCpu>
    bool step_one(Tick deadline, RunPhase &&run_phase, RunCpu &&run_cpu) {
        RowPhase kind = phase_kinds[phase];
        Tick phase_time = timing.marker(row_index) + offset_of(kind);
        uint64_t acting_row = row_index;
        if (kind == RowPhase::ExecutePrevious) {
            acting_row = row_index - 1;
        }

        bool event_first = false;
        if (!events.empty()) {
            const Event &e = events.front();
            event_first = e.time < phase_time ||
                          (e.time == phase_time && !e.late && e.created < phase_created(acting_row));
        }
        Tick first = phase_time;
        if (event_first) {
            first = events.front().time;
        }
        // The CPU's state runs only when nothing else is due at or before it.
        if (cpu_time < first) {
            if (cpu_time > deadline) {
                return false;
            }
            time = cpu_time;
            run_cpu();
            return true;
        }

        if (event_first) {
            if (events.front().time > deadline) {
                return false;
            }
            std::pop_heap(events.begin(), events.end(), fires_after);
            Event next = std::move(events.back());
            events.pop_back();
            time = next.time;
            next.action();
            return true;
        }

        if (phase_time > deadline) {
            return false;
        }
        run_next_phase(run_phase);
        // Fast path: while no bus effect is waiting, the row's next phases
        // run back to back, until the CPU's state is due first. (At a tie the
        // phase goes first, as above.) Any effect a phase schedules sends the
        // choice back through the general rule.
        while (events.empty()) {
            Tick next = timing.marker(row_index) + offset_of(phase_kinds[phase]);
            if (next > deadline || cpu_time < next) {
                break;
            }
            run_next_phase(run_phase);
        }
        return true;
    }

    // The row phase that runs next (a lens asks where in the row the DSP stands).
    RowPhase next_phase() const {
        return phase_kinds[phase];
    }

    // After everything up to `deadline` has run, the clock stands there.
    void stand_at(Tick deadline) {
        time = std::max(time, deadline);
    }

private:
    // A row's phases in order, and their offsets from its marker.
    static constexpr RowPhase phase_kinds[] = {
        RowPhase::Begin, RowPhase::ExecutePrevious, RowPhase::Fetch, RowPhase::ResetDecode, RowPhase::Converter,
    };
    static constexpr unsigned phase_count = sizeof phase_kinds / sizeof phase_kinds[0];

    Tick offset_of(RowPhase kind) const {
        switch (kind) {
            case RowPhase::Begin:
                return 0;
            case RowPhase::ExecutePrevious:
                return timing.execute_offset;
            case RowPhase::Fetch:
                return timing.fetch_offset;
            case RowPhase::ResetDecode:
                return timing.reset_decode_offset;
            case RowPhase::Converter:
                return timing.converter_offset;
        }
        return 0;
    }

    struct Event {
        Tick time;
        uint64_t serial;        // creation order
        Tick created;
        bool late;
        std::function<void()> action;
    };

    // Heap order: earlier time first, then late ones last, then creation order.
    static bool fires_after(const Event &a, const Event &b) {
        if (a.time != b.time) {
            return a.time > b.time;
        }
        if (a.late != b.late) {
            return a.late;
        }
        return a.serial > b.serial;
    }

    // Run the phase that is due next, in the window it belongs to.
    template <class RunPhase>
    void run_next_phase(RunPhase &run_phase) {
        RowPhase kind = phase_kinds[phase];
        uint64_t acting_row = row_index;
        if (kind == RowPhase::ExecutePrevious) {
            acting_row = row_index - 1;
        }
        time = timing.marker(row_index) + offset_of(kind);
        phase++;
        if (phase == phase_count) {
            phase = 0;
            row_index++;
        }
        run_phase(kind, acting_row);
    }

    // When a row's phases were (conceptually) scheduled: at the previous row's marker.
    Tick phase_created(uint64_t row_number) const {
        if (row_number == 0) {
            return 0;
        }
        return timing.marker(row_number - 1);
    }

    Tick time = 0;
    Tick cpu_time = ~Tick(0);   // the CPU's next state (none until set)
    uint64_t row_index = 0;     // the row whose window the next phase is in
    unsigned phase = 0;
    std::vector<Event> events;  // a min-heap by (time, creation order)
    uint64_t serial = 0;
};

}  // namespace lexicon224x::cpu
