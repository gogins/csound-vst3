#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "CsoundTokeniser.h"
#include "csd_ids.h"

/// juce::StringArray csoundKeywords;
std::set<std::string> csound_keywords;
/// const juce::StringArray csoundOpcodes = { "oscil", "adsr", "reverb" };

CsoundTokeniser::CsoundTokeniser() {
    csd_color_scheme.set("Keyword", juce::Colours::white);
    /// csd_color_scheme.set("Opcode", juce::Colours::purple);
    csd_color_scheme.set("Number", juce::Colours::lightcoral);
    csd_color_scheme.set("String", juce::Colours::mediumaquamarine);
    csd_color_scheme.set("Comment", juce::Colours::lightgreen);
    csd_color_scheme.set("Identifier", juce::Colours::powderblue);
    csd_color_scheme.set("Other", juce::Colours::wheat);
    for (int i = 0; ; ++i)
    {
        const char *keyword = csd_ids[i];
        if (keyword == nullptr)
        {
            break;
        }
        csound_keywords.insert(keyword);
    }
}

int CsoundTokeniser::readNextToken(juce::CodeDocument::Iterator& source)
{
    source.skipWhitespace();
    auto startChar = source.peekNextChar();

    // Handle comments (e.g., ; for single-line comments)
    if (startChar == ';')
    {
        while (!source.isEOF() && source.peekNextChar() != '\n')
            source.skip();
        return CsoundComment;
    }
    if (startChar == '/')
    {
        auto result = CsoundOther;
        source.skip();
        if (source.peekNextChar() == '=')
        {
            source.skip();
        }
        else if (source.peekNextChar() == '/')
        {
            result = CsoundComment;
            source.skipToEndOfLine();
        }
        else if (source.peekNextChar() == '*')
        {
            source.skip();
            result = CsoundComment;
            bool lastWasStar = false;
            
            for (;;)
            {
                const juce::juce_wchar c = source.nextChar();
                
                if (c == 0 || (c == '/' && lastWasStar))
                    break;
                
                lastWasStar = (c == '*');
            }
        }
        if (result == CsoundComment)
        {
            return result;
        }
    }
    // Handle strings
    if (startChar == '"')
    {
        source.skip();
        while (!source.isEOF() && source.peekNextChar() != '"')
            source.skip();
        source.skip(); // Skip closing quote
        return CsoundString;
    }
    // Handle numbers
    if (juce::CharacterFunctions::isDigit(startChar) || startChar == '.')
    {
        while (juce::CharacterFunctions::isDigit(source.peekNextChar()) || source.peekNextChar() == '.')
            source.skip();
        return CsoundNumber;
    }
    // Handle identifiers and keywords
    if (juce::CharacterFunctions::isLetter(startChar))
    {
        juce::String token;
        while (juce::CharacterFunctions::isLetterOrDigit(source.peekNextChar()) || source.peekNextChar() == '_')
            token += source.nextChar();
        if (csound_keywords.contains(token.toRawUTF8()))
            return CsoundKeyword;
        ///if (csoundOpcodes.contains(token))
        ///    return CsoundOpcode;
        return CsoundIdentifier;
    }
    // Skip unrecognized characters
    source.skip();
    return CsoundOther;
}

juce::CodeEditorComponent::ColourScheme CsoundTokeniser::getDefaultColourScheme()
{
    return csd_color_scheme;
}

juce::StringArray CsoundTokeniser::getTokenTypes()
{
    return { "Keyword", /* "Opcode", */ "Number", "String", "Comment", "Identifier", "Other" };
}
