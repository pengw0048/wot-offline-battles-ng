"""Restore #1513 Flock's cached BoidsFilter properties and actual steering consumers."""
import argparse
from pathlib import Path

FIELDS = (('Vector3', 'yprSpeed'), ('Vector3', 'deadZonePosition'), ('float', 'deadZoneRadius'))


OUTPUT = r'''void BoidsFilter::output( double time, MovementFilterTarget & target )
{
	BW_GUARD;
	static DogWatch dwBoidsFilterOutput("BoidsFilter");
	dwBoidsFilterOutput.start();
	Entity & entity = static_cast< Entity & >( target );
	this->AvatarFilter::output( time, target );
	const Vector3 goal = entity.position();
	if (prevTime_ == -1.f) initialHeight_ = goal.y;
	float dTime = prevTime_ == -1.f ? 1.f : float(time - prevTime_);
	prevTime_ = time;
	EntityEmbodiments & models = entity.auxiliaryEmbodiments();
	const int size = static_cast<int>(models.size());
	const int oldSize = static_cast<int>(boidData_.size());
	if (oldSize != size)
	{
		boidData_.resize(size);
		for (int i = oldSize; i < size; ++i)
		{
			// 65f360..65f4db reads the actual embodiment transform. The
			// original scene snapshot supplies this same matrix as the SDK
			// embodiment worldTransform accessor, before filter placement.
			const Matrix & transform = models[i]->worldTransform();
			Vector3 z = transform.applyToUnitAxisVector(2);
			Vector3 x = transform.applyToUnitAxisVector(0);
			Vector3 y = transform.applyToUnitAxisVector(1);
			D3DXVec3Normalize(&z, &z);
			BoidData & boid = boidData_[i];
			boid.yaw_ = -atan2f(z.x, z.z);
			boid.pitch_ = asinf((std::max)(-1.f, (std::min)(z.y, 1.f)));
			D3DXVec3Normalize(&x, &x);
			D3DXVec3Normalize(&y, &y);
			boid.roll_ = atan2f(x.y, y.y);
			boid.pos_ = goal + transform.applyToOrigin();
		}
	}
	bool boidsLanded = false;
	// 65f503..65f6c4 ignores nonpositive/20-second gaps, and advances
	// every admitted frame through float 0.1-second substeps.
	wot::stockBoidSubsteps(dTime, [&](float step)
	{
		for (int i = 0; i < size; ++i)
		{
			IEntityEmbodimentPtr model = models[i];
			boidData_[i].updateModel(goal, &*model, *this, step, 30.f, state_, radius_);
			if (state_ == STATE_LANDING && boidData_[i].pos_ == goal) boidsLanded = true;
		}
	});
	if (boidsLanded)
	{
		MF_ASSERT(!entity.isDestroyed());
		entity.pPyEntity().callMethod("boidsLanded", ScriptErrorPrint(), true);
	}
	dwBoidsFilterOutput.stop();
}'''

CONSTRUCTOR = r'''BoidsFilter::BoidData::BoidData()
{
	// 65e9e0 uses a dedicated native BWRandom stream and explicit draw
	// order. Position is subsequently replaced by the model transform.
	const float x0 = stockBoidUnitRandom(), x1 = stockBoidUnitRandom();
	pos_.x = (x1 - x0) * 20.f;
	pos_.y = stockBoidUnitRandom() * 2.f;
	const float z0 = stockBoidUnitRandom(), z1 = stockBoidUnitRandom();
	pos_.z = (z1 - z0) * 20.f;
	dir_.x = stockBoidUnitRandom();
	dir_.y = stockBoidUnitRandom();
	dir_.z = stockBoidUnitRandom();
	dir_.x += .0001f;
	D3DXVec3Normalize(&dir_, &dir_);
	yaw_ = pitch_ = roll_ = dYaw_ = 0.f;
	speed_ = .1f;
}'''


def replace_body(source, signature, replacement):
    start=source.index(signature)
    end=source.index('{',start)
    depth=1
    while depth:
        end+=1
        depth+=(source[end]=='{')-(source[end]=='}')
    return source[:start]+replacement+source[end+1:]

def once(source, before, after):
    if source.count(after) == 1: return source
    if after in source or source.count(before) != 1:
        raise ValueError('BoidsFilter SDK seam changed: ' + repr(before))
    return source.replace(before, after, 1)


def patch(root):
    root = Path(root)
    pending = {}
    for name in ('py_boids_filter.hpp', 'py_boids_filter.cpp', 'boids_filter.hpp', 'boids_filter.cpp'):
        path = root / 'client' / name
        source = path.read_text(encoding='utf-8-sig')
        if name.endswith('.hpp'):
            declarations = ''.join('\t%s %s() const;\n\tvoid %s( %s value );\n' % (kind, field, field, kind)
                                   if name.startswith('py_') else
                                   '\t%s %s() const { return %s_; }\n\tvoid %s( %s value ) { %s_ = value; }\n' %
                                   (kind, field, field, field, kind, field) for kind, field in FIELDS)
            source = once(source, '\tfloat radius() const', declarations + '\n\tfloat radius() const')
            members = ''.join('\t%s %s_;\n' % (kind, field) for kind, field in FIELDS)
            source = once(source, '\tfloat radius_;', '\tfloat radius_;\n' + members)
            if name.startswith('py_'):
                attributes = ''.join('\tPY_RW_ACCESSOR_ATTRIBUTE_DECLARE( %s, %s, %s );\n' %
                                     (kind, field, field) for kind, field in FIELDS)
                source = once(source, '\tPY_RW_ACCESSOR_ATTRIBUTE_DECLARE( float, radius, radius );', '\tPY_RW_ACCESSOR_ATTRIBUTE_DECLARE( float, radius, radius );\n' + attributes)
        elif name.startswith('py_'):
            attributes = ''.join('\tPY_ATTRIBUTE( %s )\n' % field for unused, field in FIELDS)
            source = once(source, 'PY_END_ATTRIBUTES()', attributes + 'PY_END_ATTRIBUTES()')
            source = once(source, '\tradius_( 1000.0f )', '\tradius_( 1000.0f ),\n'
                '\typrSpeed_( 1.f, .02f, .05f ),\n\tdeadZonePosition_( 0.f, 0.f, 0.f ),\n\tdeadZoneRadius_( 0.f )')
            accessors = ''.join('GET_SET_CACHED( %s, %s )\n' % (kind, field) for kind, field in FIELDS)
            source = once(source, 'GET_SET_CACHED( float, radius )', 'GET_SET_CACHED( float, radius )\n' + accessors)
            assignments = ''.join('\tpNewFilter->%s( %s_ );\n' % (field, field) for unused, field in FIELDS)
            source = once(source, '\tpNewFilter->radius( radius_ );', '\tpNewFilter->radius( radius_ );\n' + assignments)
            assignments = ''.join('\t%s_ = pFilter->%s();\n' % (field, field) for unused, field in FIELDS)
            source = once(source, '\tradius_ = pFilter->radius();', '\tradius_ = pFilter->radius();\n' + assignments)
        else:
            source = once(source, '#include "pch.hpp"', '#include "pch.hpp"\n#include "stock_boids_filter.hpp"\n#include "cstdmf/bwrandom.hpp"')
            random = '\nstatic float stockBoidUnitRandom()\n{\n\tstatic BWRandom generator;\n\treturn float(generator()) * 2.3283064365386963e-10f;\n}\n'
            source = once(source, 'const int BoidsFilter::STATE_LANDING = 1;', 'const int BoidsFilter::STATE_LANDING = 1;\n' + random)
            source = replace_body(source, 'void BoidsFilter::output(', OUTPUT)
            source = replace_body(source, 'BoidsFilter::BoidData::BoidData()', CONSTRUCTOR)
            source = once(source, '\ttimeMultiplier_( 30.0f )', '\ttimeMultiplier_( 30.0f ),\n'
                '\typrSpeed_( 1.f, .02f, .05f ),\n\tdeadZonePosition_( 0.f, 0.f, 0.f ),\n\tdeadZoneRadius_( 0.f )')
            assignments = ''.join('\t%s_ = pOtherBoidsFilter->%s_;\n' % (field, field) for unused, field in FIELDS)
            source = once(source, '\tboidData_ = pOtherBoidsFilter->boidData_;', assignments + '\tboidData_ = pOtherBoidsFilter->boidData_;')
            # The rest of the public owner still advances the real auxiliary
            # embodiments, native transforms and boidsLanded callback.
            marker = 'wot::stockBoidAvoidDeadZone('
            start = source.index('\t// First deal with pitch changes') if marker not in source else 0
            end = source.index('\t// Set matrix for model', start) if marker not in source else 0
            replacement = '''\t// Original 65ec90/65f980 use authored yaw/pitch/roll rates and
\t// horizontal dead-zone avoidance before the native model update.
\tauto stockNormalize = []( wot::StockBoidVector& value )
\t{
\t\tVector3 native( value.x, value.y, value.z );
\t\tD3DXVec3Normalize( &native, &native );
\t\tvalue = { native.x, native.y, native.z };
\t};
\twot::StockBoidVector stockDelta = { delta.x, delta.y, delta.z };
\tconst wot::StockBoidVector stockPosition = { pos_.x, pos_.y, pos_.z };
\tconst wot::StockBoidVector stockDirection = { dir_.x, dir_.y, dir_.z };
\tconst wot::StockBoidVector stockCentre = { filter.deadZonePosition_.x,
\t\tfilter.deadZonePosition_.y, filter.deadZonePosition_.z };
\twot::stockBoidAvoidDeadZone( stockDelta, stockPosition, stockDirection,
\t\tstockCentre, filter.deadZoneRadius_, stockNormalize );
\twot::StockBoidAngles stockAngles = { yaw_, pitch_, roll_, dYaw_, speed_ };
\tconst wot::StockBoidVector stockRates = { filter.yprSpeed_.x,
\t\tfilter.yprSpeed_.y, filter.yprSpeed_.z };
\twot::stockBoidSteer( stockAngles, stockDelta, stockDirection, stockRates,
\t\tfilter.normalSpeed_, filter.pitchToSpeedRatio_, DT,
\t\tstate == STATE_LANDING, stockNormalize );
\tyaw_ = stockAngles.yaw; pitch_ = stockAngles.pitch;
\troll_ = stockAngles.roll; dYaw_ = stockAngles.yawDelta;
\tspeed_ = stockAngles.speed;

'''
            if 'wot::stockBoidAvoidDeadZone(' not in source:
                source = source[:start] + replacement + source[end:]
            source = once(source, '\t\t\tdir_ = goalDir;','\t\t\tdir_ = goal - pos_;')
            # The original has neither the public SDK radius clamp/teleport nor
            # its invented maximum flight-distance fallback.
            start = source.find('\t// Limit how far a boid can fly')
            if start != -1:
                end = source.index('\tpCA->worldTransform( m );',start)
                source = source[:start] + '\t// 65fe56: actual directional displacement, then embodiment update.\n\tpos_ += (dir_ * speed_) * DT;\n\n' + source[end:]
            source = once(source, 'const float radius )', 'const float /* radius */ )')
            source = once(source, '\t\t\t\tdiff.normalise();', '\t\t\t\tD3DXVec3Normalize(&diff, &diff);')
            source = once(source, '\tgoalDir.normalise();', '\tD3DXVec3Normalize(&goalDir, &goalDir);')
            matrix_before = '\tm.setTranslate( pos_ );\n\tm.preRotateY( -yaw_ );\n\tm.preRotateX( -pitch_ );\n\tm.preRotateZ( -roll_ );'
            matrix_after = '\tMatrix rotation;\n\tD3DXMatrixTranslation(&m, pos_.x, pos_.y, pos_.z);\n\tD3DXMatrixRotationY(&rotation, -yaw_);\n\tD3DXMatrixMultiply(&m, &rotation, &m);\n\tD3DXMatrixRotationX(&rotation, -pitch_);\n\tD3DXMatrixMultiply(&m, &rotation, &m);\n\tD3DXMatrixRotationZ(&rotation, roll_);\n\tD3DXMatrixMultiply(&m, &rotation, &m);'
            source = once(source, matrix_before, matrix_after)


        pending[path] = source
    for path, source in pending.items(): path.write_text(source, encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine_root', type=Path)
    patch(parser.parse_args().engine_root)
