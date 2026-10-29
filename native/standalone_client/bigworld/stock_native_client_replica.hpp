#pragma once
struct _object;
namespace stock_native_client_replica {
// A visible-client presentation owner. Snapshot ingress copies accepted state;
// render advancement never calls the Python gameplay interpolator or adapters.
_object* create(_object*, _object*);
_object* ingest(_object*, _object*);
_object* advance(_object*, _object*);
_object* manages(_object*, _object*);
_object* close(_object*, _object*);
}
