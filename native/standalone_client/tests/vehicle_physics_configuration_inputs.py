"""Explicit authored inputs for the physics configuration Reader and configure.

Values are test inputs, not vehicle dimensions or replacement tunings.
"""
import copy


def numbers(names, start=1):
    return dict((name, index + start + 0.125) for index, name in enumerate(names.split()))


def make_mode(index):
    body = numbers('gravity fullMass hullCOMShiftY hullBoxOffsetZ turretTopWidth smplFwMaxSpeed smplBkMaxSpeed '
                   'smplRotSpeed angVelocityFactor allowedRPMExcess overspeedResistFactor centerRotationFwdSpeed '
                   'movementRevertSpeed comFrictionYOffs comSideFriction')
    body.update(mode_index=index, hullInertiaFactors=(1.5, 2.5, 3.5), hullSize=(4.5, 5.5, 6.5),
                turretTopPos=(7.5, 8.5, 9.5), isRotationAroundCenter=True, enableSabilization=False,
                siegeModeAvailable=True)
    body['shape'] = numbers('auxClearance tankBoardAngle tankAftChamferFraction tankFrontChamferFraction '
                            'terrBoardAngle terrFrontChamferFraction terrAftChamferFraction', 31)
    body['shape']['useComplexForm'] = True
    chassis = numbers('chassisMass chassisMassFraction clearance trackGaugeFactor trackToBeLockedDelay '
                      'wheelRadius wheelInertiaFactor wheelFwdInertiaFactor stiffness0 stiffness1 damping '
                      'wheelRestitution wheelPenetration brake rotationBrake wheelSinkageResistFactor '
                      'wheelDryFrictionFactor sideFrictionConstantRatio', 51)
    chassis.update(wheelDetachOnRoll=False, wheelUsePseudoContacts=True, flatSideFriction=False,
                   roadWheelPositions=[1, 2, 3, 4, 5], stiffnessFactors=(6, 7, 8, 9, 10))
    for name in ('slopeResistTerrain', 'slopeResistStaticObject', 'slopeResistDynamicObject'):
        chassis[name] = (11, 12, 13)
    for name in ('slopeGripLngTerrain', 'slopeGripSdwTerrain', 'slopeGripLngStaticObject',
                 'slopeGripSdwStaticObject', 'slopeGripLngDynamicObject', 'slopeGripSdwDynamicObject'):
        chassis[name] = [14, 15, 16, 17]
    surface = numbers('dirtCumulationRate dirtReleaseRate dirtSideVelocity maxDirt hbComSideFriction '
                      'sideFriction fwdFriction rollingFriction hbSideFrictionAddition rotationFactor', 71)
    chassis['grounds'] = {0: dict((name, copy.deepcopy(surface)) for name in ('firm', 'medium', 'soft')),
                          3: dict((name, copy.deepcopy(surface)) for name in ('firm', 'medium', 'soft'))}
    chassis['hullAiming'] = {
        'pitch': numbers('correctionCenterZ correctionSpeed pitchMin pitchMax correctionStiffness '
                         'correctionDamping correctionScale', 91),
        'yaw': numbers('gimletForce stiffness damping preciseRestitution dampingYawDist preciseYawDist', 111)}
    body['chassis'] = chassis
    engine = numbers('smplEnginePower engineInertia smplEngJoinRatio idleChoker rotationChoker '
                     'rotationByLockChoker smplMinRPM idleRPM startRPM engVelMax engVelBkMax engVelRot', 131)
    engine['engineLoses'] = (151, 152)
    body['engine'] = engine
    body['anchor'] = numbers('anchorMaxReactionFactor anchorConstFraction anchorVelFactor', 161)
    body['rail'] = {'railFactorInContact': 181.25}
    body['gimlet'] = numbers('wPushedDiag wPushedRot wPushedHB pushInContact pushHB pushStop '
                             'pushRotOnSpotFixedPeriod pushRotOnSpotGrowPeriod gimletPushOnSpotInit '
                             'gimletPushOnSpotFinal pushRotOnMoveFixedPeriod pushRotOnMoveGrowPeriod '
                             'gimletPushOnMoveInit gimletPushOnMoveFinal gimletVelScaleMin gimletVelScaleMax', 201)
    return body


def make_configuration():
    gears = {'switchSpeed': [1, 2, 3], 'switchHysteresis': (4, 5, 6),
             'lowRpm': [7, 8, 9], 'highRpm': (10, 11, 12)}
    modes = {'normal': make_mode(0), 'siegeMode': make_mode(1)}
    for mode in modes.values():
        mode['fakegearbox'] = {'fwdgears': copy.deepcopy(gears), 'bkwdgears': copy.deepcopy(gears)}
    return {'vehicleType': 0, 'modes': modes}
