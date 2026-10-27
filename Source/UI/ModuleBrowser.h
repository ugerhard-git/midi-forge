/**
 * @file ModuleBrowser.h
 * @brief The pop-ups of the module menu: the module browser with its search field, and the module info
 *        (full description plus one line per knob) shown by the ⓘ button and inside the browser.
 */
#pragma once

#include "Engine/ModuleDescriptions.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace midiforge
{

/**
 * Shows everything there is to know about one module: name, category, the full description and one line
 * per knob (with its automation letter A ... H and, for page 2, a page hint).
 */
class ModuleInfoView final : public juce::Component
{
public:
    ModuleInfoView();

    /** Shows a module (a ModuleType number). Out-of-range numbers show the Empty module. */
    void showModule (int moduleType);
    int getShownModule() const noexcept { return shownModuleType; }

    /** @return The height needed to show the whole text at the given width. */
    int getHeightForWidth (int width) const;

    void paint (juce::Graphics& graphics) override;

    /** @return The text that is drawn, as plain text (for tests and screen readers). */
    juce::String getPlainText() const;

    static constexpr int margin = 12;

private:
    juce::AttributedString createText() const;

    int shownModuleType = 0;
};

/**
 * The module browser that opens instead of a plain drop-down menu: a search field at the top, the modules
 * grouped by category (the recently used ones first) below it, and the info of the highlighted module on
 * the right.
 *
 * Typing filters the list (see moduleMatchesSearch()); the arrow keys move the highlight, Enter or a click
 * picks a module, Esc closes the browser. When it is shown in a juce::CallOutBox, picking a module or Esc
 * closes the box.
 */
class ModuleBrowser final : public juce::Component
{
public:
    /**
     * @param currentModuleType The module loaded in the slot; it is highlighted when the browser opens.
     * @param recentModuleTypes The recently used modules, newest first.
     * @param onModuleChosen    Called with the picked ModuleType number.
     */
    ModuleBrowser (int currentModuleType, std::vector<int> recentModuleTypes, std::function<void (int)> onModuleChosen);
    ~ModuleBrowser() override;

    void resized() override;
    void paint (juce::Graphics& graphics) override;
    void visibilityChanged() override;
    void parentHierarchyChanged() override;

    /** Replaces the search text and filters the list, as if the user had typed it. */
    void setSearchText (const juce::String& text);
    /** @return The module numbers in the list, top to bottom (a module may appear twice: recent and category). */
    std::vector<int> getListedModuleTypes() const;
    /** @return The module number of the highlighted row, or -1 if the list is empty. */
    int getHighlightedModuleType() const;
    /** Moves the highlight by this many modules (headings are skipped). */
    void moveHighlight (int numberOfModules);
    /** Picks the highlighted module (does nothing if the list is empty). */
    void chooseHighlightedModule();

    /** The size the browser asks for. */
    static constexpr int preferredWidth = 580;
    static constexpr int preferredHeight = 380;
    static constexpr int rowHeight = 22;

private:
    /** One line of the list: a heading ("Generators") or a module. */
    struct ListRow
    {
        bool isHeading = false;
        juce::String text;
        int moduleType = 0;
    };

    /** The search field: a text editor that passes the up and down keys to the list. */
    class SearchField final : public juce::TextEditor
    {
    public:
        std::function<void (int direction)> onArrowKey;
        bool keyPressed (const juce::KeyPress& key) override;
    };

    /** Draws the rows and turns mouse movement and clicks into highlighting and picking. */
    class ModuleList final : public juce::Component
    {
    public:
        explicit ModuleList (ModuleBrowser& owner) : browser (owner) {}
        void paint (juce::Graphics& graphics) override;
        void mouseMove (const juce::MouseEvent& event) override;
        void mouseUp (const juce::MouseEvent& event) override;

    private:
        int rowAt (int y) const;
        ModuleBrowser& browser;
    };

    void rebuildList();
    void highlightRow (int rowIndex);
    void scrollToHighlightedRow();
    void chooseRow (int rowIndex);
    void closeEnclosingCallOutBox();
    void focusSearchFieldWhenShown();

    const int currentModuleType;
    const std::vector<int> recentModuleTypes;
    std::function<void (int)> moduleChosenCallback;

    std::vector<ListRow> rows;
    int highlightedRow = -1;

    SearchField searchField;
    juce::Viewport listViewport;
    ModuleList moduleList { *this };
    ModuleInfoView infoView;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModuleBrowser)
};

} // namespace midiforge
