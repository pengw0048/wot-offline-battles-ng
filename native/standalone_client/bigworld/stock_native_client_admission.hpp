#pragma once
#include <Python.h>
namespace stock_native_client_admission {
// Cold setup captures the fixed module's normalizers and native clock. The
// optional static module supplies immutable value-version ownership.
PyObject* configure(PyObject*,PyObject*);
// (client, accepted-candidate envelope, trusted_player_static=None).
// Returns its accepted canonical envelope or None for a contained/stale packet.
// This does not advance ClientBattleState or dispatch replay/UI notifications.
PyObject* accept(PyObject*,PyObject*);
PyObject* timing(PyObject*,PyObject*);
// Resolve the native-held accepted envelope after Python dict copies. Matching
// uses the full accepted frontier, never Python object identity alone.
PyObject* held(PyObject* client,PyObject* envelope);
}
