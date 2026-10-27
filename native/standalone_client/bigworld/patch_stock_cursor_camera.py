#!/usr/bin/env python3
"""Restore the #1513 CursorCamera extensions and update owner in public BigWorld."""
import argparse
from pathlib import Path


HEADER = 'lib/camera/cursor_camera.hpp'
SOURCE = 'lib/camera/cursor_camera.cpp'
WATER = 'lib/romp/water.hpp'
SCRIPT_APP = 'client/script_app.cpp'

UPDATE_START = '/**\n *\tUpdates the camera position and viewing direction based on time.'
UPDATE_END = ('\n// -----------------------------------------------------------------------------\n'
              '// Protected Helper Methods.')
# Every public update stage the replacement owns, in source order.
UPDATE_STAGES = (
    'void CursorCamera::update( float deltaTime )\n{',
    '\t\tpTarget_->matrix( tmatrix );',
    '\t\tpSource_->matrix( smatrix );',
    '\t\tif (this->reverseView()) targetPitch += DEG_TO_RAD(180.0f);',
    '\t\tif ( pivotPosition_ != targetPivotPosition_ )',
    '\t\tif (!MF_FLOAT_EQUAL(maxDistanceFromPivot_, targetMaxDistanceFromPivot_))',
    '\t\tif ( !inPosition_ && lastDesiredKnown_ )',
    '\t\tthis->roll( Angle::decay( this->roll(),',
    '\t\tcameraDirInWS_.setPitchYaw( this->pitch(), this->yaw() );',
    '\t\tif (shakeLeft_ > 0.f)',
    '\t\tif (inaccuracyProvider_)',
    '\t\t\tif ( inPosition_ && !limitVelocity_ )',
    '\t\t\t\tBaseCamera::sceneCheck( idealPosInWS, positionOnEntity, cameraDirInWS_, uprightDirection_ );',
    '\t\t\tif ( !limitVelocity_ )',
    '\t\tfor(uint i = 0; i < Waters::instance().size(); i++)',
    '\t\tif ( !inPosition_ )',
    '\tview_.lookAt( finalPosInWS, useCDIWS, this->uprightDirection() );',
    '\tinvView_.invert( view_ );',
)

UPDATE = r'''/**
 *	Updates the camera position and viewing direction based on time.
 *
 *	#1513 0079e110: a shaking frame renders the shaken pass and keeps a stable
 *	pass for the camera's matrix providers.
 *
 *	@param	deltaTime	The time passed in seconds since last update.
 */
void CursorCamera::update( float deltaTime )
{
	BW_GUARD;
	if (shakeLeft_ > 0.f)
	{
		this->updateStock( deltaTime, false );
		this->updateStock( deltaTime, true );
		return;
	}

	this->updateStock( deltaTime, false );
	stableView_ = view_;
	stableInvView_ = invView_;
}


/**
 *	This method performs one #1513 update pass (0079bb30). The stable pass
 *	skips smoothing and shake, snaps to the ideal position and writes the
 *	stable matrices instead of the rendered ones.
 */
void CursorCamera::updateStock( float deltaTime, bool stablePass )
{
	BW_GUARD;
	bool collided = false;

	Vector3 finalPosInWS = cameraPosInWS_;
	if ( pTarget_ && pSource_ )
	{
		Matrix tmatrix( Matrix::identity );
		pTarget_->matrix( tmatrix );

		// Calculate target camera yaw and pitch in world space.
		Matrix smatrix( Matrix::identity );
		pSource_->matrix( smatrix );
		Angle targetYaw = smatrix.yaw();
		Angle targetPitch = smatrix.pitch();
		Angle targetRoll = smatrix.roll();
		if (this->reverseView()) targetPitch += DEG_TO_RAD(180.0f);

		if (!stablePass)
		{
			// Move camera pivot position towards target position.
			if ( pivotPosition_ != targetPivotPosition_ )
			{
				pivotPosition_ = Vector3(
					Math::decay( pivotPosition_.x, targetPivotPosition_.x,
						this->movementHalfLife() * 2.0f, deltaTime ),
					Math::decay( pivotPosition_.y, targetPivotPosition_.y,
						this->movementHalfLife() * 2.0f, deltaTime ),
					Math::decay( pivotPosition_.z, targetPivotPosition_.z,
						this->movementHalfLife() * 2.0f, deltaTime ) );
			}

			// Move camera distance towards the target camera distance.
			if (!MF_FLOAT_EQUAL(maxDistanceFromPivot_, targetMaxDistanceFromPivot_))
			{
				maxDistanceFromPivot_ = Math::decay(
					maxDistanceFromPivot_,
					targetMaxDistanceFromPivot_,
					maxDistHalfLife_, deltaTime );
			}

			// Now that target angles are determined, see if they have changed.
			if ( !inPosition_ && lastDesiredKnown_ )
			{
				if ( ( targetYaw != lastDesiredYaw_ ) ||
					( targetPitch != lastDesiredPitch_ ) ||
					( targetRoll != lastDesiredRoll_ ) )
				{
					inPosition_ = true;
				}
			}

			// Rotate the camera angles to the target angles.
			this->yaw( Angle::decay( this->yaw(),
				targetYaw,
				this->turningHalfLife(),
				deltaTime ) );

			this->pitch( Angle::decay( this->pitch(),
				targetPitch,
				this->turningHalfLife(),
				deltaTime ) );

			this->roll( Angle::decay( this->roll(),
				targetRoll,
				this->turningHalfLife(),
				deltaTime ) );
		}

		// #1513 0079c0cc: CursorCamera pitch is positive upwards.
		cameraDirInWS_.setPitchYaw( -this->pitch(), this->yaw() );

		Vector3 positionOnEntity =
			tmatrix.applyPoint( Vector3( 0.f, this->pivotPosition().y, 0.f ) );

		// The pivot x and z offsets are relative to the camera yaw.
		const float sinYaw = sinf( this->yaw() );
		const float cosYaw = cosf( this->yaw() );
		const Vector3 & pivot = this->pivotPosition();
		Vector3 pivotPosInWS(
			pivot.z * sinYaw + (pivot.x * cosYaw + positionOnEntity.x),
			positionOnEntity.y,
			pivot.z * cosYaw + (positionOnEntity.z - pivot.x * sinYaw) );

		// #1513 0079c214: only the rendered pass shakes, by the square of the
		// remaining fraction.
		if (shakeLeft_ > 0.f && !stablePass)
		{
			const float remaining = shakeLeft_ / shakeTime_;
			const Vector3 amt = shakeAmount_ * (remaining * remaining);
			const float randomScale = 1.f / RAND_MAX;

			const float yaw = (float(rand()) * randomScale - 0.5f) * amt.x;
			const float pitch = (float(rand()) * randomScale - 0.5f) * amt.y;
			const float roll = (float(rand()) * randomScale - 0.5f) * amt.z;

			Matrix m;
			m.setRotate( yaw, pitch, roll );
			cameraDirInWS_ = m.applyVector(cameraDirInWS_);

			shakeLeft_ -= deltaTime;
		}

		// Make the camera shift due to unskillful camera person
		// (e.g. for sniper rifle)
		if (inaccuracyProvider_)
		{
			Vector4 amt;
			inaccuracyProvider_->output(amt);

			float yaw = amt.x;
			float pitch = amt.y;
			float roll = 0.f;

			Matrix m;
			m.setRotate( yaw, pitch, roll );
			cameraDirInWS_ = m.applyVector(cameraDirInWS_);
		}

		// Slide it to its distance and make sure it doesn't hit anything
		Vector3 idealPosInWS = pivotPosInWS;
		if (!this->firstPersonPerspective())
		{
			// Calculate the new position of the camera in world space.
			float cameraDist = this->maxDistanceFromPivot();
			idealPosInWS = pivotPosInWS - cameraDist * cameraDirInWS_;

			// Calculate the position on the sphere by which the camera is
			// restricted.
			Vector3 positionOnSphere = pivotPosInWS -
				( cameraPosInWS_ - pivotPosInWS ).length() * cameraDirInWS_;
			if ( inPosition_ && !limitVelocity_ )
			{
				// Snap camera to the correct position on the sphere.
				cameraPosInWS_ = positionOnSphere;
			}
			else
			{
				float distSquared = (cameraPosInWS_ -
					positionOnSphere).lengthSquared();
				if ( distSquared < 0.01f && !stablePass )
				{
					// Glide camera to the correct position on the sphere.
					cameraPosInWS_.x = Math::decay( cameraPosInWS_.x,
						positionOnSphere.x,
						this->movementHalfLife(), deltaTime );
					cameraPosInWS_.y = Math::decay( cameraPosInWS_.y,
						positionOnSphere.y,
						this->movementHalfLife(), deltaTime );
					cameraPosInWS_.z = Math::decay( cameraPosInWS_.z,
						positionOnSphere.z,
						this->movementHalfLife(), deltaTime );

					// If the distance is really close, consider the camera
					// to be locked and in position.
					if ( distSquared < 0.0001f )
					{
						inPosition_ = true;
					}
				}
			}

			// #1513 0079c683: model hulls first, then the space and its bounds.
			collided = collideStockCursorCameraModels( stockModels_,
				stockModelTrees_, idealPosInWS, cameraDirInWS_, uprightDirection_ );
			if (BaseCamera::sceneCheck( idealPosInWS, positionOnEntity,
					cameraDirInWS_, uprightDirection_ ))
			{
				collided = true;
			}

			// #1513 0079c6d6: lift the ideal position until the bottom edge of
			// the near plane leaves the first water above it.
			const Moo::Camera & camera = Moo::rc().camera();
			const Vector3 & zAxis = cameraDirInWS_;
			Vector3 xAxis = uprightDirection_.crossProduct( zAxis );
			xAxis.normalise();
			const Vector3 yAxis = zAxis.crossProduct( xAxis );
			const Vector3 planeSize = camera.nearPlanePoint( 1.f, 1.f );
			const float snapHeight = fabsf( yAxis.y * planeSize.y );
			const Vector3 waterStart =
				idealPosInWS + zAxis * camera.nearPlane() - yAxis * snapHeight;
			const Vector3 waterEnd( waterStart.x, waterStart.y + 20.f,
				waterStart.z );
			for (uint i = 0; i < Waters::instance().size(); i++)
			{
				const float depth = Waters::instance()[i]->
					stockGroundRayDistance( waterStart, waterEnd );
				if (depth > 0.f)
				{
					idealPosInWS.y = depth + snapHeight + idealPosInWS.y;
					break;
				}
			}
		}

		float oldDist = (cameraPosInWS_ - positionOnEntity).length();
		float newDist = (idealPosInWS - positionOnEntity).length();

		// #1513 0079c9a4: the stable pass always snaps to the ideal position.
		if (!stablePass && ((!collided && !inPosition_) ||
			oldDist < newDist ||
				limitVelocity_))
		{
			// Glide camera to its position in world space if we're moving
			// backwards.
			if ( !limitVelocity_ )
			{
				// #1513 0079c9f7: tiny ranges or divisors keep the plain
				// half-life.
				float halfLife = this->movementHalfLife();
				const float range = newDist - this->minDistanceFromPivot();
				if (double( fabsf( range ) ) >= 0.0001)
				{
					const float ratio =
						(oldDist - this->minDistanceFromPivot()) / range;
					float div = 1.0f - ratio;
					div = div + div + 2.0f;
					if (double( fabsf( div ) ) >= 0.0001)
					{
						halfLife = this->movementHalfLife() / div;
					}
				}
				const float dist =
					Math::decay( oldDist, newDist, halfLife, deltaTime );

				if (newDist != 0.f)
				{
					cameraPosInWS_ = positionOnEntity +
								 (dist / newDist) * (idealPosInWS - positionOnEntity);
				}
				else
				{
					cameraPosInWS_ = positionOnEntity;
				}
			}
			else
			{
				//Move camera to the desired position, given a fixed velocity.
				Vector3 move = idealPosInWS - cameraPosInWS_;
				float distSq = move.lengthSquared();
				float maxMove = maxVelocity_ * deltaTime;
				if ( distSq > maxMove )
				{
					move.normalise();
					move *= maxMove;
					cameraPosInWS_ += move;
				}
				else
				{
					cameraPosInWS_.x = Math::decay( cameraPosInWS_.x, idealPosInWS.x,
						this->movementHalfLife(), deltaTime );
					cameraPosInWS_.y = Math::decay( cameraPosInWS_.y, idealPosInWS.y,
						this->movementHalfLife(), deltaTime );
					cameraPosInWS_.z = Math::decay( cameraPosInWS_.z, idealPosInWS.z,
						this->movementHalfLife(), deltaTime );
					limitVelocity_ = false;
				}
			}
		}
		else
		{
			// Else teleport camera directly there to avoid crashing through
			// the collision scene.
			cameraPosInWS_ = idealPosInWS;
			inPosition_ = true;
		}

		finalPosInWS = cameraPosInWS_;

		// Store previous desired direction.
		if ( !inPosition_ )
		{
			lastDesiredYaw_ = targetYaw;
			lastDesiredPitch_ = targetPitch;
			lastDesiredRoll_ = targetRoll;
			lastDesiredKnown_ = true;
		}
	}

	Matrix & view = stablePass ? stableView_ : view_;
	Matrix & invView = stablePass ? stableInvView_ : invView_;

	// Apply changes to the camera.
	view.lookAt( finalPosInWS, cameraDirInWS_, this->uprightDirection() );

	// Roll the camera.
	Matrix rollAdjust;
	rollAdjust.setRotateZ( roll() );
	view.postMultiply( rollAdjust );

	// And calculate the inverse too.
	invView.invert( view );

	// #1513 0079cf65: every pass republishes this camera's renderer targets.
	publishStockCursorCameraHiding( pTarget_.get(), sptHiding_ );
}

'''

EXTENSIONS = r'''

/**
 *	#1513 0079d3e0: while a shake runs, the camera's matrix providers read
 *	the stable pass; otherwise they read the rendered matrices.
 */
namespace
{
class StockCursorCameraMatrixProvider : public MatrixProvider
{
public:
	StockCursorCameraMatrixProvider( CursorCamera * pCamera, bool inverse ) :
		MatrixProvider( false, &s_type_ ),
		pCamera_( pCamera ),
		inverse_( inverse )
	{
	}

private:
	virtual void matrix( Matrix & m ) const
	{
		if (!pCamera_)
		{
			m.setIdentity();
		}
		else
		{
			pCamera_->stockViewMatrix( m, inverse_ );
		}
	}

	WeakPyPtr< CursorCamera > pCamera_;
	bool inverse_;
};
}


/**
 *	This method returns the matrix the camera's matrix providers read.
 */
void CursorCamera::stockViewMatrix( Matrix & m, bool inverse ) const
{
	const bool stable = !(shakeLeft_ <= 0.f);
	if (inverse)
	{
		m = stable ? stableInvView_ : invView_;
	}
	else
	{
		m = stable ? stableView_ : view_;
	}
}


/**
 *	#1513 0079e1c0/0079d2d0 create a provider per read. The provider holds
 *	the camera weakly; the original strong reference formed a cycle.
 */
MatrixProviderPtr CursorCamera::stockViewProvider( bool inverse ) const
{
	return MatrixProviderPtr( new StockCursorCameraMatrixProvider(
			const_cast< CursorCamera * >( this ), inverse ),
		MatrixProviderPtr::STEAL_REFERENCE );
}


/*~ function CursorCamera.wg_applyParams
 *
 *	This function applies the current parameters as one update of a second.
 */
/**
 *	#1513 0079b8f0 calls the update slot with a second.
 */
void CursorCamera::wg_applyParams()
{
	BW_GUARD;
	this->update( 1.f );
}


/*~ function CursorCamera.wg_setModelsToCollideWith
 *
 *	This function replaces the PyModels whose collision hulls the camera
 *	avoids.
 *
 *	@param	models	a list of PyModel instances.
 */
/**
 *	#1513 0079b950 returned None with its RuntimeError pending. The error is
 *	raised here instead of leaking into an unrelated later call.
 */
bool CursorCamera::wg_setModelsToCollideWith( PyObjectPtr models )
{
	BW_GUARD;
	return setStockCursorCameraModels( models.get(), stockModels_,
		stockModelTrees_ );
}


/**
 *	#1513 0079df40: disabling tree hiding clears the renderer targets at once.
 */
void CursorCamera::sptHiding( bool value )
{
	sptHiding_ = value;
	if (!value)
	{
		publishStockCursorCameraHiding( NULL, true );
	}
}

'''


def patch(root):
    root = Path(root)
    pending = {}

    def text(relative):
        if relative not in pending:
            pending[relative] = (root / relative).read_text()
        return pending[relative]

    def edit(relative, before, after, present=None):
        source = text(relative)
        if (present or after) in source:
            return
        if source.count(before) != 1:
            raise ValueError('CursorCamera owner anchor changed: ' + relative + ': ' + repr(before))
        pending[relative] = source.replace(before, after, 1)

    if 'float stockGroundRayDistance(const Vector3& start, const Vector3& end) const;' not in text(WATER):
        raise ValueError('CursorCamera water lift requires the stock ground-effects Water query')

    # Release retained camera targets while Python and its engine owners live.
    edit(SCRIPT_APP, '#include "stock_ground_effects.hpp"',
         '#include "stock_ground_effects.hpp"\n#include "stock_cursor_camera.hpp"')
    edit(SCRIPT_APP, '\tfiniStockGroundEffects();',
         '\tfiniStockCursorCameraHiding();\n\tfiniStockGroundEffects();')
    # A closed target owner reopens only when the script layer starts again.
    edit(SCRIPT_APP, '\tif (!BigWorldClientScript::init( AppConfig::instance().pRoot() ))',
         '\tinitStockCursorCameraHiding();\n\tif (!BigWorldClientScript::init( AppConfig::instance().pRoot() ))')

    edit(HEADER, 'class WorldTriangle;\n', 'class WorldTriangle;\nclass BSPTree;\n', 'class BSPTree;\n')
    edit(HEADER, '\tvirtual void update( float deltaTime );\n\t//@}',
         '\tvirtual void update( float deltaTime );\n\t//@}\n\n'
         '\t///	@name #1513 extensions.\n'
         '\t//@{\n'
         '\tbool sptHiding() const { return sptHiding_; }\n'
         '\tvoid sptHiding( bool value );\n'
         '\tvoid wg_applyParams();\n'
         '\tbool wg_setModelsToCollideWith( PyObjectPtr models );\n'
         '\tvoid stockViewMatrix( Matrix & m, bool inverse ) const;\n'
         '\tMatrixProviderPtr stockViewProvider( bool inverse ) const;\n'
         '\t//@}', '\tbool wg_setModelsToCollideWith( PyObjectPtr models );')
    edit(HEADER, '\tPY_RW_ATTRIBUTE_DECLARE( inaccuracyProvider_, inaccuracyProvider )\n',
         '\tPY_RW_ATTRIBUTE_DECLARE( inaccuracyProvider_, inaccuracyProvider )\n\n'
         '\tPY_AUTO_METHOD_DECLARE( RETVOID, wg_applyParams, END )\n'
         '\tPY_AUTO_METHOD_DECLARE( RETOK, wg_setModelsToCollideWith,\n'
         '\t\tARG( PyObjectPtr, END ) )\n'
         '\tPY_RW_ACCESSOR_ATTRIBUTE_DECLARE( bool, sptHiding, sptHiding )\n'
         '\tPY_RO_ATTRIBUTE_DECLARE( this->stockViewProvider( false ), matrix )\n'
         '\tPY_RO_ATTRIBUTE_DECLARE( this->stockViewProvider( true ), invViewMatrix )\n',
         '\tPY_AUTO_METHOD_DECLARE( RETVOID, wg_applyParams, END )\n')
    edit(HEADER, '\t\tconst Vector3 &end );\n\t//@}\n',
         '\t\tconst Vector3 &end );\n\n\tvoid updateStock( float deltaTime, bool stablePass );\n\t//@}\n',
         '\tvoid updateStock( float deltaTime, bool stablePass );\n')
    edit(HEADER, '\tVector3 cameraDirInWS_;\n\t//@}\n',
         '\tVector3 cameraDirInWS_;\n\t//@}\n\n'
         '\t///	@name #1513 extension data.\n'
         '\t//@{\n'
         '\tbool sptHiding_;\n'
         '\tBW::vector< PyObjectPtr > stockModels_;\n'
         '\tBW::vector< const BSPTree * > stockModelTrees_;\n'
         '\tMatrix stableView_;\n'
         '\tMatrix stableInvView_;\n'
         '\t//@}', '\tbool sptHiding_;\n')

    edit(SOURCE, '\tpitch_ = cameraDirInWS_.pitch();\n',
         '\t// #1513 0079dbb6: CursorCamera pitch is positive upwards.\n'
         '\tpitch_ = -cameraDirInWS_.pitch();\n')
    edit(SOURCE, '#include "romp/water.hpp"\n',
         '#include "romp/water.hpp"\n#include "stock_cursor_camera.hpp"\n',
         '#include "stock_cursor_camera.hpp"\n')
    edit(SOURCE, '\tinaccuracyProvider_( NULL )\n{',
         '\tinaccuracyProvider_( NULL ),\n'
         '\tsptHiding_( false ),\n'
         '\tstableView_( Matrix::identity ),\n'
         '\tstableInvView_( Matrix::identity )\n{', '\tsptHiding_( false ),\n')
    edit(SOURCE, 'CursorCamera::~CursorCamera()\n{\n}',
         'CursorCamera::~CursorCamera()\n{\n'
         '\t// #1513 0079b679: every destroyed CursorCamera clears the targets.\n'
         '\tpublishStockCursorCameraHiding( NULL, true );\n}',
         '\tpublishStockCursorCameraHiding( NULL, true );\n}')

    source = text(SOURCE)
    if 'void CursorCamera::updateStock( float deltaTime, bool stablePass )' not in source:
        if source.count(UPDATE_START) != 1 or source.count(UPDATE_END) != 1:
            raise ValueError('CursorCamera update boundaries changed')
        start = source.index(UPDATE_START)
        end = source.index(UPDATE_END)
        region = source[start:end]
        cursor = 0
        for stage in UPDATE_STAGES:
            if region.count(stage) != 1 or region.index(stage) < cursor:
                raise ValueError('CursorCamera update stage changed: ' + repr(stage))
            cursor = region.index(stage)
        pending[SOURCE] = source[:start] + UPDATE + source[end:]

    edit(SOURCE, '\tshakeLeft_ = duration;\n}\n',
         '\tshakeLeft_ = duration;\n}\n' + EXTENSIONS,
         'void CursorCamera::wg_applyParams()')
    edit(SOURCE, 'PY_BEGIN_METHODS( CursorCamera )\n\tPY_METHOD( shake )\n',
         'PY_BEGIN_METHODS( CursorCamera )\n\tPY_METHOD( shake )\n'
         '\tPY_METHOD( wg_setModelsToCollideWith )\n'
         '\tPY_METHOD( wg_applyParams )\n', '\tPY_METHOD( wg_applyParams )\n')
    edit(SOURCE, '\tPY_ATTRIBUTE( inaccuracyProvider )\nPY_END_ATTRIBUTES()',
         '\tPY_ATTRIBUTE( inaccuracyProvider )\n'
         '\t/*~ attribute CursorCamera.sptHiding\n'
         '\t *\n'
         '\t *\tIf true, the renderer hides trees around the line to the target.\n'
         '\t *\n'
         '\t *\t@type\tBool\n'
         '\t */\n'
         '\tPY_ATTRIBUTE( sptHiding )\n'
         '\t/*~ attribute CursorCamera.matrix\n'
         '\t *\n'
         '\t *\tThe view matrix provider. It reads the unshaken view while a\n'
         '\t *\tshake runs.\n'
         '\t *\n'
         '\t *\t@type\tRead-Only MatrixProvider\n'
         '\t */\n'
         '\tPY_ATTRIBUTE( matrix )\n'
         '\t/*~ attribute CursorCamera.invViewMatrix\n'
         '\t *\n'
         '\t *\tThe inverse view matrix provider. It reads the unshaken view while\n'
         '\t *\ta shake runs.\n'
         '\t *\n'
         '\t *\t@type\tRead-Only MatrixProvider\n'
         '\t */\n'
         '\tPY_ATTRIBUTE( invViewMatrix )\nPY_END_ATTRIBUTES()',
         '\tPY_ATTRIBUTE( invViewMatrix )\nPY_END_ATTRIBUTES()')

    # The camera library does not link the client adapters; publish the
    # adapter header beside the public engine headers it is included from.
    header = (Path(__file__).resolve().parent / 'stock_cursor_camera.hpp').read_text()
    # Validate every anchor before publishing any translation unit.
    for relative, value in pending.items():
        path = root / relative
        if path.read_text() != value:
            path.write_text(value)
    destination = root / 'lib/stock_cursor_camera.hpp'
    if not destination.exists() or destination.read_text() != header:
        destination.write_text(header)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bigworld_root', type=Path)
    patch(parser.parse_args().bigworld_root)
