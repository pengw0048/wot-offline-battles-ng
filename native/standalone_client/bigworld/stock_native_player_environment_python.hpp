#pragma once
#include "stock_native_player_environment.hpp"
#include <map>
#include <memory>
struct _object;
namespace stock_native_player_environment {
// Cold descriptor snapshot; may call the fixed descriptor metadata producers.
bool readMetadata(_object* descriptor,Metadata&);
// Reads plain record state and entity-owned critical fields without business calls.
bool readPlayer(_object* record,_object* entity,const Metadata&,Player&,_object* battle=nullptr);
struct MetadataCacheEntry {std::shared_ptr<_object> descriptor;Metadata metadata;};
using MetadataCache=std::map<std::int64_t,MetadataCacheEntry>;
// Reads battle records and native entities, then advances the complete owner.
// A malformed/unready player is contained to that player.
bool readAndAdvance(_object* battle,double dt,bool authority,Owner&,World&,
                    MetadataCache&,std::optional<Batch>&);
// Returns a new Python list of wire observations.
_object* observationsValue(const Batch&);
}
