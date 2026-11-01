#include "pch.hpp"
#include "stock_color_grading.hpp"

#include "moo/graphics_settings.hpp"
#include "pyscript/script.hpp"
#include "resmgr/auto_config.hpp"
#include "resmgr/bwresource.hpp"

BW_BEGIN_NAMESPACE
namespace
{
const AutoConfigString colorGradingResource( "system/colorGradingXML" );

/**
 *	#1513 00941129 registers COLOR_GRADING_TECHNIQUE after LIGHTING_QUALITY
 *	with the SHADOWS_QUALITY construction flags, and 00a1fe90 fills it: a basic
 *	NONE option, then one supported advanced option per <item> name/desc of
 *	system/colorGradingXML. The settings view requires its option list.
 *
 *	The 0093f9b0 callback forwards selections to the pipeline's ColorGrading
 *	owner, which this client's deferred pipeline does not have, so a
 *	selection is only recorded (as getColorGradingStrength reports 0.0).
 */
class ColorGradingSetting : public Moo::GraphicsSetting
{
public:
    ColorGradingSetting() : Moo::GraphicsSetting( "COLOR_GRADING_TECHNIQUE",
        "Color Grading", -1, false, false )
    {
        this->addOption( "NONE", "None", true, false );
        DataSectionPtr root = BWResource::openSection( colorGradingResource.value() );
        if (!root) return;
        BW::vector<DataSectionPtr> items;
        root->openSections( "item", items );
        for (const DataSectionPtr & item : items)
        {
            this->addOption( item->readString( "name" ), item->readString( "desc" ), true, true );
        }
    }
    virtual void onOptionSelected( int ) {}
};
}

bool initialiseStockColorGradingSetting()
{
    auto previous = Moo::GraphicsSetting::getFromLabel( "COLOR_GRADING_TECHNIQUE" );
    if (previous)
    {
        if (dynamic_cast<ColorGradingSetting*>( previous.getObject() )) return true;
        PyErr_SetString( PyExc_RuntimeError, "Color grading setting already has another owner" );
        return false;
    }
    Moo::GraphicsSetting::add( new ColorGradingSetting() );
    return true;
}
BW_END_NAMESPACE
