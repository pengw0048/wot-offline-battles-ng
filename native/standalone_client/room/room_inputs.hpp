#pragma once
#include "room_state.hpp"
#include <deque>
#include <functional>

namespace wot::room {
class Inputs {
public:
  using Advanced =
      std::function<void(std::int64_t, const Value &, const Value &)>;
  explicit Inputs(State &state, Advanced advanced = {})
      : state_(state), advanced_(std::move(advanced)) {}
  void reset();
  void advanceTicks(std::int64_t count = 1);
  Outcome dispatch(Sender, const Value &);
  // Source revisions are immutable after first admission, including HP/death
  // changes that do not publish a new physical pose revision.
  void rememberBots(std::int64_t revision, std::int64_t stamp,
                    const Value &bots);
  void rememberBots(std::int64_t revision, std::int64_t stamp,
                    const std::map<std::int64_t, Value> &bots);

private:
  friend struct InputsArchiveProbe;
  struct Decision {
    std::string fingerprint, outcome, reason, field;
    bool active = true;
  };
  struct Player {
    std::map<std::int64_t, Decision> decisions;
    std::map<std::int64_t, std::string> fingerprints;
  };
  struct Sample {
    std::int64_t stamp = 0;
    std::map<std::int64_t, Value> bots;
  };
  State &state_;
  Advanced advanced_;
  std::map<std::int64_t, Player> players_;
  std::map<std::int64_t, Sample> archive_;
  static void sampleBot(Sample &, const Value &);
  void storeSample(std::int64_t revision, Sample);
  Outcome reject(Value &, std::optional<std::int64_t>, const std::string &,
                 const std::string &field = {}, bool consumed = false,
                 bool active = true);
  void decision(Value &, std::int64_t, const std::string &, const std::string &,
                const std::string &reason = {}, const std::string &field = {},
                bool active = true);
  void pose(Value &, const Value &);
  std::pair<Value, std::string> ramContact(const Value &, const Value &,
                                           bool current) const;
  Value destructibleContact(const Value &, const Value &) const;
  void contacts(Value &, const Value &, Outcome &);
};
} // namespace wot::room
