#pragma once
#include "room_state.hpp"
#include <random>
namespace wot::room::lobby {
struct Assignment {
  std::int64_t id;
  int team, slot;
};
struct Context {
  int automatic = 0, human_spgs = 0;
  std::vector<std::string> pinned;
};
std::string string(const Value &, const std::string &,
                   const std::string &fallback = {});
std::int64_t strictInt(const Value &, std::int64_t low, std::int64_t high);
std::string pythonString(const Value &);
std::string safe(const Value &, const std::string &, const Value &resources,
                 bool vehicle = false);
std::string lower(const std::string &, const Value &resources);
std::string truncate(const std::string &, std::size_t);
std::string alnum(const std::string &, const Value &resources,
                  std::size_t limit);
int requestedTeam(const Value &, const Value &resources);
bool locked(const State &);
void normalizeConfiguration(Value &, const Value &resources);
std::vector<int> slots(const State &, int team);
std::optional<std::vector<Assignment>> assignment(const State &,
                                                  std::mt19937 &);
void spawn(Value &, int team, int slot);
Context context(const State &, int team,
                const std::vector<Assignment> *plan = nullptr);
std::vector<std::string> required(const Value &,
                                  const std::vector<std::string> &pinned = {});
Value trim(State &, int team, const std::string &keep = {},
           const std::vector<Assignment> *plan = nullptr);
int artilleryLimit(int size, int human, int pinned, const Value &);
std::string validate(const Value &, const Context &,
                     const std::set<std::string> &candidates, int limit);
std::vector<int> matchTiers(int host, const std::string &mode,
                            std::set<int> available = {},
                            std::set<int> humans = {});
bool inTier(int host, int candidate, const std::string &mode);
std::optional<std::vector<int>>
matchingTiers(const std::vector<std::pair<int, std::string>> &,
              const std::set<std::string> &needed, const std::set<int> &humans,
              std::vector<int> proposed, bool random,
              const std::vector<std::set<std::string>> &groups,
              const std::vector<std::set<int>> &slots);
std::string vehicleClass(const State &, const Value &, bool fallback = true);
std::set<std::string> allowedNames(const State &, const Value &catalog);
bool matches(const State &, const Value &filter, const Value &candidate);
Value outfits(const Value &, const Value &resources = Value());
Value marks(const Value &);
Value descriptor(const Value &);
Value catalog(const Value &, const Value &resources);
} // namespace wot::room::lobby
