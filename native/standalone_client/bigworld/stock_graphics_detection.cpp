#include "pch.hpp"
#include "stock_graphics_detection.hpp"
#include "wot/stock_graphics_detection.hpp"
#include "moo/graphics_settings_picker.hpp"
#include "moo/init.hpp"
#include "moo/render_context.hpp"
#include "pyscript/script.hpp"

BW_BEGIN_NAMESPACE
int StockGraphicsDetection_token = 1;
namespace
{
wot::StockGraphicsDetection detection;
bool configured = false;
const char * scoreFields[] = { "gpuMemory", "gpuScore", "ram", "virtualMemory", "cpuFrequency" };
const char * thresholdFields[] = { "GPUMemory", "GPUScore", "RAM", "AvailableVirtualMemory", "CPUFrequency" };

bool ensureConfiguration()
{
    if (configured) return true;
    // init loads groups and bounds only. Neither of the three queries starts
    // a measurement; that producer has a separate startup/button lifecycle.
    Moo::GraphicsSettingsAutoDetector owner;
    if (owner.init() && configured) return true;
    PyErr_SetString( PyExc_RuntimeError, "The graphics detection profiles are unavailable" );
    return false;
}
}

void configureStockGraphicsDetection( DataSectionPtr presets, DataSectionPtr bounds )
{
    const auto retained = detection.scores;
    detection = wot::StockGraphicsDetection();
    detection.scores = retained;
    for (auto & profile : detection.profiles)
    {
        const BW::string expected( profile.name.data(), profile.name.size() );
        profile.name.clear();
        if (presets)
        {
            BW::vector<DataSectionPtr> groups;
            presets->openSections( "graphicsPreferences", groups );
            for (const auto & group : groups)
            {
                const BW::string name = group->asString();
                if (wot::StockGraphicsDetection::lower( std::string( name.data(), name.size() ) ) ==
                    std::string( expected.data(), expected.size() ))
                    profile.name = std::string( expected.data(), expected.size() );
            }
        }
        if (!bounds) continue;
        BW::vector<DataSectionPtr> groups;
        bounds->openSections( "graphicsPreferences", groups );
        for (const auto & group : groups)
        {
            const BW::string name = group->asString();
            if (wot::StockGraphicsDetection::lower( std::string( name.data(), name.size() ) ) !=
                std::string( expected.data(), expected.size() )) continue;
            for (unsigned index = 0; index < 5; ++index)
                profile.minimum[index] = group->readInt( thresholdFields[index], profile.minimum[index] );
            profile.pixelShader = group->readFloat( "PixelShaderVersion", profile.pixelShader );
            profile.vertexShader = group->readFloat( "VertexShaderVersion", profile.vertexShader );
            break;
        }
    }
    configured = !!presets;
}

void readStockGraphicsDetectionPreferences( DataSectionPtr preferences )
{
    DataSectionPtr source = preferences ? preferences->openSection( "detector" ) : DataSectionPtr();
    if (!source) return;
    // 0091db33..0091dbfa imports after version migration/detection. Missing
    // fields default to zero; a missing section leaves the process cache alone.
    for (unsigned index = 0; index < 5; ++index)
        detection.store( index, source->readInt64( scoreFields[index], 0 ) );
}

void writeStockGraphicsDetectionPreferences( DataSectionPtr preferences )
{
    if (!preferences) return;
    DataSectionPtr target = preferences->openSection( "detector", true );
    if (!target) return;
    for (unsigned index = 0; index < 5; ++index)
        target->writeInt64( scoreFields[index], detection.score( index ) );
}

static int64 getAutoDetectGraphicsSettingsScore( int parameter )
{
    return detection.score( parameter );
}
PY_AUTO_MODULE_FUNCTION( RETDATA, getAutoDetectGraphicsSettingsScore, ARG( int, END ), BigWorld )

static PyObject * getSystemPerformancePresetIdFromName( const BW::string & name )
{
    if (!ensureConfiguration()) return NULL;
    return PyInt_FromLong( detection.presetId( std::string( name.data(), name.size() ) ) );
}
PY_AUTO_MODULE_FUNCTION( RETOWN, getSystemPerformancePresetIdFromName, ARG( BW::string, END ), BigWorld )

static PyObject * detectGraphicsPresetFromSystemSettings()
{
    if (!ensureConfiguration()) return NULL;
    if (!Moo::isInitialised() || !Moo::rc().device())
    {
        PyErr_SetString( PyExc_RuntimeError, "The graphics detection device is not initialized" );
        return NULL;
    }
    const unsigned shader = std::min( unsigned( Moo::rc().psVersion() ), unsigned( Moo::rc().vsVersion() ) );
    return PyInt_FromLong( detection.classify( shader ) );
}
PY_AUTO_MODULE_FUNCTION( RETOWN, detectGraphicsPresetFromSystemSettings, END, BigWorld )
BW_END_NAMESPACE
