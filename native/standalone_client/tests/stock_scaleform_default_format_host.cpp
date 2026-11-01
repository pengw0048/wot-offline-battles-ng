// Run the production HTML setup blocks with an initial definition distinct from
// the mutable default formats. ParseHtml and the renderer remain boundaries.
#include <cassert>
#include <iostream>
#include <map>
#include <string>

namespace Text {
struct Format
{
    std::map<std::string, std::string> values;
    Format Merge(const Format& other) const
    {
        Format result = *this;
        for (const auto& value : other.values)
            result.values[value.first] = value.second;
        return result;
    }
};
using TextFormat = Format;
using ParagraphFormat = Format;
}

struct Document
{
    Text::TextFormat text;
    Text::ParagraphFormat paragraph;
    int reads = 0;
    const Text::TextFormat* GetDefaultTextFormat() { ++reads; return &text; }
    const Text::ParagraphFormat* GetDefaultParagraphFormat() { ++reads; return &paragraph; }
};

struct TextField
{
    Document document;
    Document* pDocument = &document;
    bool css = false;
    Text::TextFormat initialText;
    Text::ParagraphFormat initialParagraph;
    bool HasStyleSheet() const { return css; }
    void GetInitialFormats(Text::TextFormat* text, Text::ParagraphFormat* paragraph)
    {
        *text = initialText;
        *paragraph = initialParagraph;
    }
    void untranslated(Text::TextFormat& txtFmt, Text::ParagraphFormat& paraFmt)
    {
@UNTRANSLATED@
    }
    void translated(Text::TextFormat& txtFmt, Text::ParagraphFormat& paraFmt)
    {
@TRANSLATED@
    }
};

int main()
{
    for (bool translated : {false, true})
        for (bool css : {false, true})
            for (bool dynamic : {false, true})
            {
                TextField field;
                field.css = css;
                field.initialText.values = {{"font", dynamic ? "Times New Roman" : "$TextFont"},
                                            {"size", "12"}, {"bold", "false"}};
                field.initialParagraph.values = {{"align", "left"}, {"leading", "0"}};
                // BattleMessage's constructor and updateTextFieldProperties. Font,
                // size and leading also represent other dynamic HTML consumers.
                field.document.text.values = {{"font", "$FieldFont"}, {"size", "14"},
                                              {"color", "green"}};
                field.document.paragraph.values = {{"leading", "3"}};
                Text::TextFormat text;
                Text::ParagraphFormat paragraph;
                if (translated) field.translated(text, paragraph);
                else field.untranslated(text, paragraph);
                std::cout << translated << css << dynamic << ' ' << text.values["font"]
                          << ' ' << text.values["size"] << ' ' << text.values["bold"]
                          << ' ' << text.values["color"] << ' ' << paragraph.values["align"]
                          << ' ' << paragraph.values["leading"] << ' ' << field.document.reads << '\n';
            }
}
