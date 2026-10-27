/**
 * @file ModuleBrowser.cpp
 * @brief Implementation of ModuleInfoView and ModuleBrowser.
 */
#include "ModuleBrowser.h"

#include "Theme.h"

namespace midiforge
{

namespace
{
/** The automation letter of a slot parameter: A ... H. */
juce::String letterOfParameter (int parameterIndex)
{
    return juce::String::charToString ((juce::juce_wchar) ('A' + parameterIndex));
}

/** The name shown for a module in the browser and the info ("Empty slot" instead of "Empty"). */
juce::String displayNameOf (int moduleType)
{
    return moduleType == 0 ? juce::String ("Empty slot") : juce::String (getModuleDescription (moduleType).name);
}

const juce::String enDash = juce::String::fromUTF8 (" \xe2\x80\x93 "); // " – "
} // namespace

//==============================================================================
ModuleInfoView::ModuleInfoView()
{
    setInterceptsMouseClicks (false, false);
}

void ModuleInfoView::showModule (int moduleType)
{
    shownModuleType = (moduleType >= 0 && moduleType < numModuleTypes) ? moduleType : 0;
    repaint();
}

juce::AttributedString ModuleInfoView::createText() const
{
    using namespace Theme::Colours;
    const auto& description = getModuleDescription (shownModuleType);
    const auto accentColour = Theme::accentColourFor (description.category);

    juce::AttributedString text;
    text.setWordWrap (juce::AttributedString::byWord);
    text.setLineSpacing (2.0f);

    text.append (displayNameOf (shownModuleType), Theme::font (16.0f, true),
                 shownModuleType == 0 ? primaryText : accentColour);
    if (shownModuleType != 0)
        text.append (juce::String ("   ") + getCategoryName (description.category), Theme::font (11.5f), secondaryText);
    text.append ("\n\n", Theme::font (6.0f), primaryText);
    text.append (description.details, Theme::font (13.0f), primaryText);

    const int numUsedParameters = getNumberOfUsedParameters (description);
    if (numUsedParameters > 0)
    {
        text.append ("\n\n", Theme::font (6.0f), primaryText);
        text.append ("KNOBS", Theme::font (10.5f, true), secondaryText);
        for (int parameterIndex = 0; parameterIndex < numUsedParameters; ++parameterIndex)
        {
            const auto& parameter = description.parameters[parameterIndex];
            if (! parameter.isUsed())
                continue;
            text.append ("\n", Theme::font (13.0f), primaryText);
            text.append (letterOfParameter (parameterIndex) + "  ", Theme::font (12.5f, true), accentColour);
            text.append (parameter.name, Theme::font (12.5f, true), primaryText);
            if (parameterIndex >= numParametersPerPage)
                text.append (" (page 2)", Theme::font (11.5f), secondaryText);
            text.append (enDash + parameter.helpText, Theme::font (12.5f), secondaryText);
        }
    }
    return text;
}

int ModuleInfoView::getHeightForWidth (int width) const
{
    juce::TextLayout layout;
    layout.createLayout (createText(), (float) juce::jmax (1, width - 2 * margin));
    return (int) std::ceil (layout.getHeight()) + 2 * margin;
}

juce::String ModuleInfoView::getPlainText() const
{
    return createText().getText();
}

void ModuleInfoView::paint (juce::Graphics& graphics)
{
    const auto area = getLocalBounds().reduced (margin).toFloat();
    juce::TextLayout layout;
    layout.createLayout (createText(), area.getWidth());
    layout.draw (graphics, area);
}

//==============================================================================
bool ModuleBrowser::SearchField::keyPressed (const juce::KeyPress& key)
{
    const int keyCode = key.getKeyCode();
    const int direction = keyCode == juce::KeyPress::upKey         ? -1
                          : keyCode == juce::KeyPress::downKey     ? +1
                          : keyCode == juce::KeyPress::pageUpKey   ? -8
                          : keyCode == juce::KeyPress::pageDownKey ? +8
                                                                   : 0;
    if (direction != 0 && onArrowKey != nullptr)
    {
        onArrowKey (direction);
        return true;
    }
    return juce::TextEditor::keyPressed (key);
}

//==============================================================================
int ModuleBrowser::ModuleList::rowAt (int y) const
{
    const int rowIndex = y / rowHeight;
    return (y >= 0 && rowIndex < (int) browser.rows.size()) ? rowIndex : -1;
}

void ModuleBrowser::ModuleList::paint (juce::Graphics& graphics)
{
    using namespace Theme::Colours;
    if (browser.rows.empty())
    {
        graphics.setColour (secondaryText);
        graphics.setFont (Theme::font (13.0f));
        graphics.drawText ("No module found", getLocalBounds().removeFromTop (rowHeight * 2),
                           juce::Justification::centred);
        return;
    }

    for (int rowIndex = 0; rowIndex < (int) browser.rows.size(); ++rowIndex)
    {
        const auto& row = browser.rows[(size_t) rowIndex];
        auto rowArea = juce::Rectangle<int> (0, rowIndex * rowHeight, getWidth(), rowHeight);

        if (row.isHeading)
        {
            graphics.setColour (secondaryText);
            graphics.setFont (Theme::font (10.5f, true));
            graphics.drawText (row.text.toUpperCase(), rowArea.withTrimmedLeft (8).withTrimmedTop (4),
                               juce::Justification::centredLeft);
            continue;
        }

        if (rowIndex == browser.highlightedRow)
        {
            graphics.setColour (popupHighlight);
            graphics.fillRoundedRectangle (rowArea.toFloat().reduced (2.0f, 1.0f), 4.0f);
        }

        const auto category = getModuleDescription (row.moduleType).category;
        graphics.setColour (Theme::accentColourFor (category));
        graphics.fillEllipse (rowArea.removeFromLeft (24).toFloat().withSizeKeepingCentre (7.0f, 7.0f));

        const bool isCurrentModule = row.moduleType == browser.currentModuleType;
        graphics.setColour (rowIndex == browser.highlightedRow ? juce::Colours::white : primaryText);
        graphics.setFont (Theme::font (13.5f, isCurrentModule));
        graphics.drawText (row.text, rowArea.withTrimmedRight (6), juce::Justification::centredLeft);
    }
}

void ModuleBrowser::ModuleList::mouseMove (const juce::MouseEvent& event)
{
    const int rowIndex = rowAt (event.y);
    if (rowIndex >= 0 && ! browser.rows[(size_t) rowIndex].isHeading && rowIndex != browser.highlightedRow)
        browser.highlightRow (rowIndex);
}

void ModuleBrowser::ModuleList::mouseUp (const juce::MouseEvent& event)
{
    const int rowIndex = rowAt (event.y);
    if (rowIndex >= 0 && ! browser.rows[(size_t) rowIndex].isHeading && event.mouseWasClicked())
        browser.chooseRow (rowIndex);
}

//==============================================================================
ModuleBrowser::ModuleBrowser (int currentModuleTypeToShow, std::vector<int> recentModules,
                              std::function<void (int)> onModuleChosen)
    : currentModuleType (currentModuleTypeToShow), recentModuleTypes (std::move (recentModules)),
      moduleChosenCallback (std::move (onModuleChosen))
{
    using namespace Theme::Colours;
    searchField.setFont (Theme::font (14.0f));
    searchField.setTextToShowWhenEmpty (juce::String::fromUTF8 ("Search modules\xe2\x80\xa6"), secondaryText);
    searchField.setColour (juce::TextEditor::backgroundColourId, controlTrack.darker (0.35f));
    searchField.setColour (juce::TextEditor::outlineColourId, slotOutline);
    searchField.setColour (juce::TextEditor::focusedOutlineColourId, secondaryText);
    searchField.setColour (juce::TextEditor::textColourId, primaryText);
    searchField.setColour (juce::CaretComponent::caretColourId, primaryText);
    searchField.setIndents (8, 6);
    searchField.setEscapeAndReturnKeysConsumed (true);
    searchField.onTextChange = [this] { rebuildList(); };
    searchField.onReturnKey = [this] { chooseHighlightedModule(); };
    searchField.onEscapeKey = [this] { closeEnclosingCallOutBox(); };
    searchField.onArrowKey = [this] (int direction) { moveHighlight (direction); };
    addAndMakeVisible (searchField);

    listViewport.setViewedComponent (&moduleList, false);
    listViewport.setScrollBarsShown (true, false);
    listViewport.setScrollBarThickness (8);
    addAndMakeVisible (listViewport);
    addAndMakeVisible (infoView);

    setSize (preferredWidth, preferredHeight);
    rebuildList();
}

ModuleBrowser::~ModuleBrowser() = default;

void ModuleBrowser::rebuildList()
{
    const auto searchText = searchField.getText().trim().toStdString();
    const bool isSearching = ! searchText.empty();
    rows.clear();

    // "Empty slot" first (as in the old drop-down), then the recently used modules, then the categories.
    if (moduleMatchesSearch (getModuleDescription (ModuleType::Empty), searchText))
        rows.push_back ({ false, displayNameOf (0), 0 });

    if (! isSearching && ! recentModuleTypes.empty())
    {
        rows.push_back ({ true, "Recently used", 0 });
        for (int type : recentModuleTypes)
            if (type > 0 && type < numModuleTypes)
                rows.push_back ({ false, displayNameOf (type), type });
    }

    // Categories in menu order; within a category the modules keep their ModuleType order.
    for (auto category :
         { ModuleCategory::Filter, ModuleCategory::Transform, ModuleCategory::Generator, ModuleCategory::Randomizer })
    {
        bool hasHeading = false;
        for (int type = 1; type < numModuleTypes; ++type)
        {
            const auto& description = getModuleDescription (type);
            if (description.category != category || ! moduleMatchesSearch (description, searchText))
                continue;
            if (! hasHeading)
                rows.push_back ({ true, getCategoryName (category), 0 });
            hasHeading = true;
            rows.push_back ({ false, displayNameOf (type), type });
        }
    }

    // What gets highlighted: without a search the slot's current module (in its category, not in the
    // recently used list); with a search the first of the best matches (name before summary before details).
    int rowToHighlight = -1;
    auto bestRelevance = SearchRelevance::noMatch;
    for (int rowIndex = 0; rowIndex < (int) rows.size(); ++rowIndex)
    {
        const auto& row = rows[(size_t) rowIndex];
        if (row.isHeading)
            continue;
        if (isSearching)
        {
            const auto relevance = getSearchRelevance (getModuleDescription (row.moduleType), searchText);
            if (relevance > bestRelevance)
            {
                bestRelevance = relevance;
                rowToHighlight = rowIndex;
            }
        }
        else if (row.moduleType == currentModuleType)
        {
            rowToHighlight = rowIndex; // the last occurrence wins
        }
    }
    if (rowToHighlight < 0)
        for (int rowIndex = 0; rowIndex < (int) rows.size() && rowToHighlight < 0; ++rowIndex)
            if (! rows[(size_t) rowIndex].isHeading)
                rowToHighlight = rowIndex;

    moduleList.setSize (juce::jmax (1, listViewport.getMaximumVisibleWidth()),
                        juce::jmax (rowHeight * 2, (int) rows.size() * rowHeight));
    highlightRow (rowToHighlight);
    if (! isSearching)
        listViewport.setViewPosition (0, 0);
    scrollToHighlightedRow();
    moduleList.repaint();
}

void ModuleBrowser::highlightRow (int rowIndex)
{
    highlightedRow = rowIndex;
    infoView.setVisible (rowIndex >= 0);
    if (rowIndex >= 0)
        infoView.showModule (rows[(size_t) rowIndex].moduleType);
    moduleList.repaint();
}

void ModuleBrowser::scrollToHighlightedRow()
{
    if (highlightedRow < 0)
        return;
    const int rowTop = highlightedRow * rowHeight;
    const int visibleTop = listViewport.getViewPositionY();
    const int visibleHeight = listViewport.getMaximumVisibleHeight();
    if (visibleHeight <= 0)
        return;
    if (rowTop < visibleTop)
        listViewport.setViewPosition (0, juce::jmax (0, rowTop - rowHeight)); // keep the heading above visible
    else if (rowTop + rowHeight > visibleTop + visibleHeight)
        listViewport.setViewPosition (0, rowTop + rowHeight - visibleHeight);
}

void ModuleBrowser::moveHighlight (int numberOfModules)
{
    if (rows.empty() || numberOfModules == 0)
        return;
    const int step = numberOfModules > 0 ? 1 : -1;
    int rowIndex = highlightedRow;
    for (int movesLeft = std::abs (numberOfModules); movesLeft > 0; --movesLeft)
    {
        int candidate = rowIndex + step;
        while (candidate >= 0 && candidate < (int) rows.size() && rows[(size_t) candidate].isHeading)
            candidate += step;
        if (candidate < 0 || candidate >= (int) rows.size())
            break; // stop at the first / last module
        rowIndex = candidate;
    }
    highlightRow (rowIndex);
    scrollToHighlightedRow();
}

std::vector<int> ModuleBrowser::getListedModuleTypes() const
{
    std::vector<int> moduleTypes;
    for (const auto& row : rows)
        if (! row.isHeading)
            moduleTypes.push_back (row.moduleType);
    return moduleTypes;
}

int ModuleBrowser::getHighlightedModuleType() const
{
    return highlightedRow >= 0 ? rows[(size_t) highlightedRow].moduleType : -1;
}

void ModuleBrowser::setSearchText (const juce::String& text)
{
    searchField.setText (text, juce::dontSendNotification);
    rebuildList();
}

void ModuleBrowser::chooseHighlightedModule()
{
    if (highlightedRow >= 0)
        chooseRow (highlightedRow);
}

void ModuleBrowser::chooseRow (int rowIndex)
{
    const int moduleType = rows[(size_t) rowIndex].moduleType;
    // Closing is asynchronous, so the callback can safely run first.
    closeEnclosingCallOutBox();
    if (moduleChosenCallback != nullptr)
        moduleChosenCallback (moduleType);
}

void ModuleBrowser::closeEnclosingCallOutBox()
{
    if (auto* callOutBox = findParentComponentOfClass<juce::CallOutBox>())
        callOutBox->dismiss();
}

void ModuleBrowser::focusSearchFieldWhenShown()
{
    if (! isShowing())
        return;
    // The box becomes visible before it is modal, so grab the focus once the current event is handled.
    juce::MessageManager::callAsync (
        [safeThis = juce::Component::SafePointer<ModuleBrowser> (this)]
        {
            if (safeThis != nullptr && safeThis->isShowing())
                safeThis->searchField.grabKeyboardFocus();
        });
}

void ModuleBrowser::visibilityChanged()
{
    focusSearchFieldWhenShown();
}

void ModuleBrowser::parentHierarchyChanged()
{
    focusSearchFieldWhenShown();
}

void ModuleBrowser::paint (juce::Graphics& graphics)
{
    graphics.fillAll (Theme::Colours::popupBackground);
    graphics.setColour (Theme::Colours::slotOutline);
    graphics.fillRect (infoView.getX() - 6, infoView.getY(), 1, infoView.getHeight());
}

void ModuleBrowser::resized()
{
    auto area = getLocalBounds().reduced (6);
    searchField.setBounds (area.removeFromTop (30));
    area.removeFromTop (6);
    listViewport.setBounds (area.removeFromLeft (220));
    area.removeFromLeft (12);
    infoView.setBounds (area);
    moduleList.setSize (juce::jmax (1, listViewport.getMaximumVisibleWidth()), moduleList.getHeight());
    scrollToHighlightedRow();
}

} // namespace midiforge
