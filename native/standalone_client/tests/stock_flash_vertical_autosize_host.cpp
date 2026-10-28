// Native TextFieldEx methods and SDK DocView autosize layout, with AS3 values
// and font measurement as explicit boundaries.
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#define SF_UNUSED1(value) (void)(value)
#define SF_DEBUG_ARG(value) ,value
using ASString=std::string;
namespace Scaleform { namespace GFx {
struct CharacterDef {enum {TextField=2,Sprite=1};};
namespace Text {
const float GFX_TEXT_GUTTER=40;
struct RectF
{
    float x1=0,y1=200,x2=1000,y2=2200;
    float Width()const{return x2-x1;} float Height()const{return y2-y1;}
    void SetWidth(float v){x2=x1+v;} void SetHeight(float v){y2=y1+v;}
    bool operator!=(const RectF& o)const{return x1!=o.x1||y1!=o.y1||x2!=o.x2||y2!=o.y2;}
};
struct DocView
{
    enum ViewVAlignment {VAlign_None=0,VAlign_Top=1,VAlign_Bottom=2,VAlign_Center=3};
    enum {VAlign_Mask=3,VAlign_Shift=2,Flags_AutoSizeY=2,Flags_AutoSizeX=1,
          Align_Center=1,Align_Right=2,UseInternally=1};
    unsigned Flags=0,AlignProps=0;
    RectF ViewRect;
    float TextHeight=400,TextWidth=600;
    bool reformat=false;
    void SetCompleteReformatReq(){reformat=true;}
    void SetReformatReq(){reformat=true;}
@DOC_API@
@SET_Y@
    bool IsAutoSizeX()const{return Flags&1;}
    int GetAlignment()const{return 0;}
    void SetViewRect(RectF v,int){ViewRect=v;}
    void layout()
    {
        struct {bool ForceVerticalCenterAutoSize=false;} formatter;
@LAYOUT@
        (void)valignApplied;
    }
};
}
struct DisplayObject {int type=CharacterDef::TextField;int GetType()const{return type;}};
struct TextField:DisplayObject
{
    Text::DocView document;
    Text::DocView* pDocument=&document;
    unsigned dirty=0;
    void SetDirtyFlag(){++dirty;}
@FIELD_API@
};
namespace AS3 {
struct Object {bool display=true;};
bool AreDisplayObjectTraits(Object* value){return value->display;}
namespace Instances {
namespace fl_display {struct DisplayObject:Object {GFx::DisplayObject* pDispObj=NULL;};}
namespace fl_text {struct TextField:fl_display::DisplayObject {GFx::TextField* GetTextField(){return static_cast<GFx::TextField*>(pDispObj);}};}
}
struct Value
{
    enum Kind {Undefined,Null,Number,ObjectKind};
    Kind kind=Undefined;Object* object=NULL;
    Value(Kind value=Undefined,Object* obj=NULL):kind(value),object(obj){}
    bool IsNullOrUndefined()const{return kind==Null||kind==Undefined;}
    bool IsObject()const{return kind==ObjectKind;}
    Object* GetObject()const{return object;}
};
struct VM
{
    enum {eNullArgumentError=1507,eCheckTypeFailedError=1034};
    struct Error {int code;Error(int value,VM&,const char*):code(value){} Error(int value,VM&,const Value&,const char*):code(value){}};
    int error=0;
    bool IsException()const{return error!=0;}
    void ThrowArgumentError(Error value){error=value.code;}
    void ThrowTypeError(Error value){error=value.code;}
};
struct ASVM:VM {bool ExtensionsEnabled=true;};
namespace Classes {namespace fl_gfx {
struct TextFieldEx
{
    ASVM vm;
    VM& GetVM(){return vm;}
    void setVerticalAutoSize(const Value&,const Value&,const ASString&);
    void getVerticalAutoSize(ASString&,const Value&);
};
@METHODS@
}}
}}}
using namespace Scaleform::GFx;
int main(int argc,char** argv)
{
    assert(argc==6);
    AS3::Classes::fl_gfx::TextFieldEx extension;
    extension.vm.ExtensionsEnabled=std::atoi(argv[4])!=0;
    TextField field;
    field.document.Flags=1|(std::atoi(argv[2])?2:0);
    field.document.AlignProps=unsigned(std::atoi(argv[3]))<<2;
    AS3::Instances::fl_text::TextField shell;shell.pDispObj=&field;
    AS3::Object plain;plain.display=false;
    const int kind=std::atoi(argv[5]);
    AS3::Value value(kind==1?AS3::Value::Null:kind==2?AS3::Value::Number:AS3::Value::ObjectKind,
                     kind==3?&plain:&shell);
    extension.setVerticalAutoSize(AS3::Value(),value,argv[1]);
    ASString result="";
    extension.getVerticalAutoSize(result,value);
    assert(field.document.IsAutoSizeX());
    const float oldBottom=field.document.ViewRect.y2;
    const float oldCenter=(field.document.ViewRect.y1+oldBottom)/2;
    if(!extension.vm.error && field.document.IsAutoSizeY())
    {
        field.document.layout();
        assert(field.document.ViewRect.Height()==480);
        if(field.GetVAlignment()==Text::DocView::VAlign_Bottom)assert(field.document.ViewRect.y2==oldBottom);
        if(field.GetVAlignment()==Text::DocView::VAlign_Center)
            assert((field.document.ViewRect.y1+field.document.ViewRect.y2)/2==oldCenter);
    }
    std::printf("{\"y\":%s,\"align\":%u,\"dirty\":%u,\"result\":%s,\"error\":%s}",
        field.document.IsAutoSizeY()?"true":"false",unsigned(field.GetVAlignment()),field.dirty,
        extension.vm.error?"null":("\""+result+"\"").c_str(),
        extension.vm.error?std::to_string(extension.vm.error).c_str():"null");
}
