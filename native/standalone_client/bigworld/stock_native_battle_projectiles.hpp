#pragma once
// Projectiles and the local gun: in-flight shells, launches and terminals, reload, ammunition, fire intents.
#include "stock_native_battle.hpp"
#include <array>
#include <deque>
#include <set>

namespace stock_native_client_weapon_runtime { struct RuntimeOwner; }

namespace native_battle {
class ReloadSound;

// The visible player's native shot owner: freezes fire transactions, advances every collision chord, resolves
// damage and queues reliable launch/progress/terminal messages. Presentation stays in the subsystem.
class PlayerProjectiles {
public:
    struct Fired {
        std::string rejection;          // empty when the shot was launched
        Value presentation;             // the launch meta to present
    };
    struct Edge {
        enum class Kind { Pending, Ricochet } kind = Kind::Pending;
        std::string id;
        bool hit_vehicle = false;
        std::optional<Point> terminal_velocity;
        Value impact, segment;          // a ricochet's impact and its new segment fields
    };
    struct Advanced {
        std::vector<Edge> edges;
        bool burst_due = false, active = false;
    };
    virtual ~PlayerProjectiles() = default;
    virtual bool current(const Battle& b) const = 0;
    virtual bool observe(Battle& b, const Value& message, double now) = 0;
    // Snapshot ledger rows not presented yet, each with its maxDistance mirror.
    virtual std::vector<Value> ingress(Battle& b, const Value& message, double now) = 0;
    virtual bool event(Battle& b, const Value& raw, double now) = 0;
    virtual Fired trigger(Battle& b, double now, bool continuing) = 0;
    virtual bool burstDue(double now) const = 0;
    virtual bool cancel(Battle& b, double now, bool apply_settings) = 0;
    virtual Advanced advance(Battle& b, double now, bool force) = 0;
    virtual bool frameSample(Battle& b, double now) = 0;
    virtual void visualResult(const std::string& id, std::int64_t segment, bool accepted) = 0;
    virtual bool resources(Battle& b, double now) = 0;
    virtual bool discard(Battle& b, const std::string& id) = 0;
    virtual void close() = 0;
};
std::unique_ptr<PlayerProjectiles> createPlayerProjectiles(Battle& b, double now);

// A bounded, insertion-ordered set of projectile ids (the pinned _RecentIdSet).
class RecentIds {
    std::set<std::string> ids_;
    std::deque<std::string> order_;
    std::size_t limit_;
public:
    explicit RecentIds(std::size_t limit = 4096) : limit_(limit) {}
    bool contains(const std::string& id) const { return ids_.count(id) != 0; }
    bool add(const std::string& id);
    void clear() { ids_.clear(); order_.clear(); }
};

// One presented projectile: its normalized canonical launch and what presentation has done with it.
struct ProjectileMeta {
    Value launch;                       // the normalized wire meta; segment fields advance on a ricochet
    std::string id, shooter_kind;
    std::int64_t shooter_id = 0;
    std::optional<bool> hit_vehicle;
    std::optional<Point> terminal_velocity;
    std::int64_t presented_ricochet_count = 0;
    std::optional<double> visual_segment_distance, bot_visual_launch_time;
    bool local_launch = false;          // own shot frozen by the native player owner
    bool pending_resolution = false;    // the owner queued its terminal; the tracer stops on the echo
    Object source_descriptor;           // the shooter's frozen descriptor when its entity is gone
};
struct ProjectileVisual {
    Point origin{}, velocity{};
    double gravity = 0.;
    std::int64_t segment_start_ms = 0, ricochet_count = 0, attacker_id = 0;
    bool active = false, admitted = true;
};

// One canonical projectile handed to the stock ProjectileMover.
struct ProjectileTracer {
    std::int64_t visual_id = 0, attacker_id = 0;
    Object effects;                     // the shot's effectsDescr
    bool own_shot = false, input_hit_notified = false, flock_hit_notified = false, missed_notified = false;
    bool started = false, terminal = false, terminal_failed = false;
};
// The cosmetic tracer presenter: one stock ProjectileMover, bounded admission, terminal feedback once per shot.
struct TracerPresenter {
    Object mover;
    Object flock_manager, triggers_manager;           // stock modules
    std::int64_t next_shot_id = 1000000;
    std::map<std::string, ProjectileTracer> shots;
    std::map<std::int64_t, std::pair<double, double>> budgets;        // attacker -> (tokens, updated)
    std::map<std::string, std::pair<std::int64_t, bool>> admissions;  // projectile -> (attacker, admitted)
    std::set<std::string> failure_stages;
    bool launches_enabled = true, explosions_enabled = true, closed = false;
};

struct Projectiles {
    Projectiles();
    ~Projectiles();
    TracerPresenter tracers;
    std::optional<std::int64_t> epoch;
    std::map<std::string, ProjectileMeta> metas;
    std::map<std::string, ProjectileVisual> visuals;
    RecentIds visual_terminals, resolution_acks, local_presented;
    std::map<std::string, std::uint64_t> local_presentations;   // own launches waiting for Avatar.shoot's token
    std::uint64_t presentation_tokens = 0;
    // The round-relative server clock anchored in this process's clock domain.
    std::optional<std::int64_t> server_time_ms, motion_time_us;
    double server_local_time = 0., motion_local_time = 0.;
    std::int64_t revision = -1;
    // The last advance, for frame diagnostics.
    std::size_t active = 0, chords = 0, terminals = 0, scans = 0, candidates = 0;
    double debt = 0., advance_seconds = 0.;
    std::unique_ptr<PlayerProjectiles> player;          // the visible player's native shot owner
};

// One stock HUD edge the native gun decided; the publisher shows it on the Avatar.
struct HudEdge {
    enum class Kind { Ammo, Shell, Equipment, Targeting, Reload } kind;
    std::string id;
    std::int64_t compact = 0, setting = 0;
    int quantity = 0, clip = 0, stage = 0, remaining_seconds = 0;
    std::array<double, 7> targeting{};
    double remaining = 0., duration = 0., now = 0.;
    std::string reason;
    bool live = false;
    // The gun as it stood at a reload edge, for the mechanical reload sound.
    double gun_reload_time = 0., gun_reload_duration = 0.;
    int gun_clip = 0, gun_shot = 0;
    std::vector<int> gun_ammo;
};

// Shell settings the player changed while a launch or burst owned the gun: applied when it ends.
struct DeferredSettings {
    bool present = false;
    std::vector<std::pair<std::int64_t, std::int64_t>> rows;   // (VEHICLE_SETTING code, value)
    std::optional<int> pending_index;                         // the gun's pending shell before the first deferral
};
// A released trigger's locally owned physical burst.
struct PlayerBurst {
    int count = 0, committed = 0, shell_index = 0;
    double interval = 0., started_at = 0.;
    std::int64_t group_seq = 0;
    std::uint64_t generation = 0;
    DeferredSettings deferred;
};
// The launch being sent to the server: change_vehicle_setting defers into it.
struct FireIntent {
    std::int64_t player_id = 0, shot_seq = 0, input_seq = 0;
    double reload_factor = 1.;
    bool burst = false;
    DeferredSettings deferred;
};

struct LocalGun {
    LocalGun();
    ~LocalGun();
    Object gun;                                         // the native player gun object
    std::shared_ptr<stock_native_client_weapon_runtime::RuntimeOwner> runtime;
    std::optional<double> last_tick;
    std::optional<std::pair<double, double>> reload_event;
    std::optional<std::array<double, 7>> targeting_signature;
    std::optional<PlayerBurst> burst;
    std::optional<FireIntent> intent;
    std::int64_t projectile_sequence = 0;
    std::unique_ptr<ReloadSound> reload_sound;
    Object reload_sound_object;                        // its stock reloadEffect stand-in
    Object ammo_callback;
    std::int64_t replay_muzzle_count = 0;
    bool replay_publishing = false;
    bool client_ready = false;                         // the Avatar entered and the HUD accepts edges
    int equipment_revision = -1;
};

namespace projectiles {
// Per frame while live: advance flights and present terminals.
void advance(Battle& b, double now);
// Prebattle sampling of collision poses on the projectile authority.
void prebattleSample(Battle& b, double now);
// Accepts one projectile event (launch/terminal) the event journal admitted.
bool accept(Battle& b, const Value& event);
// A canonical ricochet or terminal (impact) event the journal applies.
bool ricochet(Battle& b, const Value& event);
bool terminal(Battle& b, const Value& event);
// The LAN fire_intent_result message.
void fireIntentResult(Battle& b, const Value& message);
// Anchors the server clock and authority epoch carried by any LAN message.
bool observe(Battle& b, const Value& message);
// Reconciles the snapshot's active projectile ledger.
bool snapshot(Battle& b, const Value& message);
// Avatar.shoot: the player pulled the trigger.
bool shoot(Battle& b, bool continuing);
// The canonical spelling of one launch (canonical event or snapshot row); nullopt when it is malformed.
std::optional<Value> normalize(const Value& raw);
// Installs or advances the presentation meta of one normalized launch.
ProjectileMeta& install(Battle& b, const Value& normalized);
// Starts this segment's native tracer once; visual_start is #1513's on-screen HP_gunFire for a fresh launch.
bool ensureVisual(Battle& b, const std::string& projectile_id, double now, const std::optional<Point>& visual_start = {});
// #1513's on-screen HP_gunFire start of a fresh visual, or nullopt for the canonical launch point.
std::optional<Point> visualStart(Battle& b, PyObject* entity, const Value& normalized);
// Retires one visual at its impact (event carries impact/outcome/hit_vehicle/resolved_time_ms).
bool stopVisual(Battle& b, const std::string& projectile_id, const Value& event);
// Fences one projectile authority epoch; false for an invalid epoch.
bool setEpoch(Battle& b, const Value& epoch, double now);
// Stock shot bookkeeping without starting the shoot extra again (the shooter's muzzle already played).
p::Ref showLocalShotWithoutExtra(Battle& b, PyObject* entity, PyObject* burst_count);
// Server time estimated from the last anchor, for stun, environment and targeting clocks.
std::optional<std::int64_t> estimatedServerTime(const Battle& b, double now);
bool isAuthority(const Battle& b);
// Ends the round's projectile owners.
void retire(Battle& b);
}

// The stock ProjectileMover presenter of canonical tracers (visible client).
namespace tracers {
// #1513 ProjectileMover.add's startPoint/refStartPoint guard.
constexpr double visual_start_max_diff = 20.;
// A world terminal's stock explosion: (effectsDescr, effectMaterial, unit direction).
struct Explosion { Object effects, material, direction; };
// Reserves one bounded cosmetic slot for a projectile; repeated calls return the stored decision.
bool admit(Battle& b, std::int64_t attacker_id, const std::string& projectile_id, double now);
// Starts one stock tracer at the checked reference pose; visual_start is stock's separate on-screen startPoint.
bool play(Battle& b, PyObject* descriptor, std::int64_t shell_index, const Point& reference_origin, const Point& reference_velocity,
          double gravity, double max_distance, std::int64_t attacker_id, const std::string& projectile_id, bool is_ricochet,
          const std::optional<Point>& visual_start);
// Forwards exactly one terminal to the stock tracer, with its hit (and own-miss) feedback.
bool stop(Battle& b, const std::string& projectile_id, const Point& end, const std::optional<Explosion>& explosion, bool missed);
// Destroys the mover and forgets every shot; false when the old mover could not be destroyed.
bool reset(Battle& b);
// Closes the presenter for the round; throws when the stock mover fails to destroy.
void destroy(Battle& b);
}

namespace gun {
// 10 Hz gun clock: reload, clip, ammunition and server marker selection.
void ammoTick(Battle& b);
// Closes the gun timeline before a synchronous state mutation (siege, stun, critical).
void edge(Battle& b);
// Per frame on the visible client: burst continuation and live reload sound.
void frame(Battle& b, double now);
// Installs the native player gun the startup created, and its last tick.
void bind(Battle& b, PyObject* gun_object, std::optional<double> last_tick);
// Replays the native shooting extra for a recorded local shot.
void replayMuzzle(Battle& b, PyObject* entity, PyObject* burst_count);
// Accepted equipment ledger from a snapshot (identical contracts only move their clocks).
bool equipmentSnapshot(Battle& b, const Value& snapshot, bool present);
// Cancels the player burst; apply_settings publishes the deferred shell selection.
bool cancelBurst(Battle& b, bool apply_settings);
// Publishes HUD edges; returns the ids the Avatar accepted.
std::vector<std::string> publish(Battle& b, PyObject* entity, const std::vector<HudEdge>& edges);
// The player gun's current reload modifier (critical, crew, stun, equipment).
double reloadFactor(Battle& b);
// HUD publication requests: "ammo", "reload", "targeting", "equipment".
bool publishKind(Battle& b, const std::string& kind, double now, bool force = false, double remaining = 0., double duration = 0.);
// The live mechanical reload sound bound to the stock AmmoController.
void retireSound(Battle& b);
}

// The live reload sound's cue schedule, for the weapon runtime's audio pump.
std::optional<double> nextReloadCue(const ReloadSound& sound);
void pumpReloadSound(ReloadSound& sound, double now);
void stopReloadSound(ReloadSound& sound);

// Provided by other subsystems at integration.
namespace needs {
int drownLevel(const Battle& b);                       // local
int overturnLevel(const Battle& b);                    // local
void showShot(Battle& b, const Value& event, bool local);   // events: the pinned _show_shot
void reportEffect(Battle& b, const std::string& kind, PyObject* material, PyObject* effects_index, PyObject* where,
                  PyObject* direction, const Value* event);  // events: _report_effect
p::Ref surfaceEffectMaterial(Battle& b, PyObject* impact);    // events: _surface_effect_material
p::Ref impactSoundIdentity(Battle& b, const Actor& target, std::int64_t attacker, PyObject* direction, double damage);
p::Ref resolveDescriptor(Battle& b, const std::string& vehicle);   // vehicles: _resolve_descriptor
// The worker's native Bot projectile authority; nullopt before its owner exists.
std::optional<bool> botProjectileObserve(Battle& b, const Value& message, double now);
std::optional<bool> botProjectileIngress(Battle& b, const Value& message, double now);
bool botProjectileEvent(Battle& b, const Value& event, double now);
std::size_t botProjectileAdvance(Battle& b, double now, bool force);
bool botAuthority(const Battle& b);
// The visible client's scene/query owners the player shot owner needs (native_client_drive.prepare, query bind).
void prepareDrive(Battle& b);
bool bindQuery(Battle& b);
// Sends the current input checkpoint after a published shot (offline_client_transport_native.send_current).
void sendCurrentInput(Battle& b);
// Acknowledges one server-admitted Bot launch on the worker's Bot outbox, in Bot order.
void botConfirmLaunch(Battle& b, const Value& normalized);
}

// Owned by nb-params (stock_native_battle_params.hpp); declared here until integration reconciles the header.
namespace params {
// The cold equipment restore: contract, quantity and activation-result changes, repair-kit acknowledgement.
bool restoreEquipmentCold(Battle& b, const Value& snapshot, bool present);
}
}
