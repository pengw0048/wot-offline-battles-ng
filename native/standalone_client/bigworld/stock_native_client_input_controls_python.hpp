#pragma once
#include <Python.h>
namespace native_battle {struct Battle;}
namespace stock_native_client_input {
// #1513 getAutorotation returns this handler-owned private boolean directly.
// The stock control-mode/settings edges remain its sole producer.
bool autorotationEnabled(native_battle::Battle&);
// Echoes the rotator's current gun angle as the accepted packed server angle.
void echoGun(native_battle::Battle&,double turret,double pitch);
}
