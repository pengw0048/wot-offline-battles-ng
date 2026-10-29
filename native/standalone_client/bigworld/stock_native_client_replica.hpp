#pragma once
struct _object;
namespace stock_native_client_state {struct Value;}
namespace stock_native_client_replica {
// A visible-client presentation owner. Snapshot ingress copies accepted state;
// render advancement never calls the Python gameplay interpolator or adapters.
_object* create(_object*, _object*);
_object* ingest(_object*, _object*);
_object* advance(_object*, _object*);
_object* manages(_object*, _object*);
_object* close(_object*, _object*);
_object* stateHandle(_object*, _object*);
_object* refresh(_object*, _object*);
_object* refreshPending(_object*, _object*);
_object* flushPending(_object*, _object*);
_object* snapshot(_object*, _object*);
// Native transport handoff. Only discrete lifecycle events become Python.
_object* receiveValue(_object* owner,_object* sync,
    const stock_native_client_state::Value&,double now,bool manifest=false);
_object* manifest(_object*, _object*);
_object* inspect(_object*, _object*);
_object* statistics(_object*, _object*);
_object* probes(_object*, _object*);
}
