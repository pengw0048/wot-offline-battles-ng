#include "pch.hpp"
#include "stock_particle_quality.hpp"
#include "app.hpp"

#include "resmgr/bwresource.hpp"
#include "moo/graphics_settings.hpp"
#include "romp/hdr_support.hpp"

BW_BEGIN_NAMESPACE
namespace
{
class ParticleQualitySetting : public Moo::GraphicsSetting
{
public:
    explicit ParticleQualitySetting( bool sniper ) : Moo::GraphicsSetting(
        sniper ? "SNIPER_MODE_EFFECTS_QUALITY" : "EFFECTS_QUALITY",
        sniper ? "Sniper mode effects quality" : "Effects quality",
        0, false, false ), sniper_( sniper )
    {
        // 00971070 gives the two registries distinct option/profile mappings.
        if (!sniper) addOption( "MAX", "Max", true );
        addOption( "HIGH", "High", true );
        addOption( "MEDIUM", "Medium", true );
        addOption( "LOW", "Low", true );
        addOption( "OFF", "Off", true );
    }
    virtual void onOptionSelected( int option )
    {
        static const int ordinary[] = { 1, 2, 3, 4, 9 };
        static const int sniper[] = { 5, 7, 8, 9 };
        if (option < 0 || option >= (sniper_ ? 4 : 5)) return;
        stockParticleQuality().select_profile(
            sniper_ ? sniper[option] : ordinary[option], sniper_ );
        // 00970ee0's jump-table blocks tail-call the real HDR owner. The
        // sniper callback 00970f90 leaves that renderer-wide flag unchanged.
        if (!sniper_) HDRSupport::setGlobalEnabled( option < 3 );
    }
private:
    bool sniper_;
};

bool registerParticleQualitySetting( bool sniper )
{
    const char * label = sniper ? "SNIPER_MODE_EFFECTS_QUALITY" : "EFFECTS_QUALITY";
    auto previous = Moo::GraphicsSetting::getFromLabel( label );
    if (previous)
    {
        if (dynamic_cast<ParticleQualitySetting *>( previous.getObject() )) return true;
        PyErr_Format( PyExc_RuntimeError,
            "Original particle quality setting '%s' already has another owner", label );
        return false;
    }
    Moo::GraphicsSetting::add( new ParticleQualitySetting( sniper ) );
    return true;
}

wot::StockParticleQualityProfile readProfile( DataSectionPtr section, bool sniper )
{
    wot::StockParticleQualityProfile value;
    // Exact 00624a00 defaults deliberately differ from the record constructor.
    value.lod_offset = section->readFloat( sniper ? "LOD_SNIPER_OFFSET" : "LOD_OFFSET", 0.f );
    value.lod_multiplier = section->readFloat( sniper ? "LOD_SNIPER_MULT" : "LOD_MULT", 1.f );
    value.time_multiplier = section->readFloat( sniper ? "TIME_SNIPER_MULT" : "TIME_MULT", 1.f );
    value.tessellation_scale = section->readFloat( sniper ?
        "TESSELATION_SNIPER_SCALE_FACTOR" : "TESSELATION_SCALE_FACTOR", 1.f );
    value.tessellation_maximum = section->readInt( sniper ?
        "TESSELATION_SNIPER_MAX_LEVEL" : "TESSELATION_MAX_LEVEL", 0 );
    value.render_quality = section->readInt( sniper ? "RENDER_SNIPER_QUALITY" : "RENDER_QUALITY", 0 );
    return value;
}
}

wot::StockParticleQuality & stockParticleQuality()
{
    // f9ade0 owns one manager, shared by both ordinary and sniper settings.
    static wot::StockParticleQuality value;
    return value;
}

bool initialiseStockParticleQualityProfiles()
{
    DataSectionPtr graphics = BWResource::openSection( s_graphicsSettingsXML.value() );
    DataSectionPtr profiles = graphics ? graphics->openSection( "PS_settings" ) : NULL;
    // Original 624a00 accepts a missing section and retains manager defaults.
    if (!profiles)
        return true;
    BW::vector<DataSectionPtr> options;
    profiles->openSections( "option", options );
    for (const auto & option : options)
    {
        const BW::string label = option->readString( "label", "" );
        const std::string name( label.c_str(), label.size() );
        stockParticleQuality().register_profile( name, false, readProfile( option, false ) );
        stockParticleQuality().register_profile( name, true, readProfile( option, true ) );
    }
    return true;
}

bool initialiseStockParticleQualitySettings()
{
    return initialiseStockParticleQualitySetting( true ) && initialiseStockParticleQualitySetting( false );
}
bool initialiseStockParticleQualitySetting( bool sniper ) { return registerParticleQualitySetting( sniper ); }
BW_END_NAMESPACE
