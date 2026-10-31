#pragma once
#include "stock_native_player_environment.hpp"
#include "stock_native_client_state.hpp"
#include <map>
#include <memory>
struct _object;
namespace native_battle { struct Battle; }
namespace stock_native_client_state { struct Actor; }
namespace stock_native_player_environment {
// Cold descriptor snapshot; may call the fixed descriptor metadata producers.
bool readMetadata(_object* descriptor,Metadata&);
// Reads accepted vehicle state and entity-owned critical fields without business calls.
bool readPlayer(const stock_native_client_state::Actor& vehicle,_object* entity,const Metadata&,Player&);
struct MetadataCacheEntry {std::shared_ptr<_object> descriptor;Metadata metadata;};
using MetadataCache=std::map<std::int64_t,MetadataCacheEntry>;
// Reads the battle's player vehicles and native entities, then advances the complete owner.
// A malformed/unready player is contained to that player.
bool readAndAdvance(native_battle::Battle& battle,double dt,bool authority,Owner&,World&,
                    MetadataCache&,std::optional<Batch>&);
// The wire observations of one batch.
stock_native_client_state::Value observationsValue(const Batch&);
}
