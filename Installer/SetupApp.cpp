// GKEQ Setup: a self-contained installer / uninstaller.
//
//   GKEQ Setup.exe                 interactive installation
//   GKEQ Setup.exe /S              silent installation with defaults
//   Uninstall GKEQ.exe             interactive removal (also /uninstall)
//   ... /uninstall /S                   silent removal
//
// Advanced (testing / deployment): /vst3dir=  /appdir=  /menudir=  /novst3  /noapp
// /noshortcuts  /noregistry  /log=<file>  /snapshot=<png> (renders the window)
#include <juce_gui_basics/juce_gui_basics.h>
#include <thread>
#include "PayloadData.h"
#include "Win32.h"

namespace setup
{
using namespace juce;

static const String kProduct ("GKEQ");
static const String kPublisher ("Modern Audio");
static const String kVersion (MEQ_VERSION);
static const String kRegKey ("HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\GKEQ");
static const String kLegacyRegKey ("HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\ModernEQ");
static const String kLegacyProduct ("Modern" " EQ");   // previous product name
static const String kUninstallerName ("Uninstall GKEQ.exe");

namespace col
{
    static const Colour bg0 { 0xff0b0e13 }, bg1 { 0xff141922 }, bg2 { 0xff1b212b }, edge { 0xff2b3344 },
                        text { 0xffd9dfea }, dim { 0xff8a95a8 }, accent { 0xff4fc3ff }, warn { 0xffffc04d },
                        good { 0xff35d07f }, bad { 0xffff5d6c };
}

static Font uiFont (float h, bool bold = false) { return Font ("Segoe UI", h, bold ? Font::bold : Font::plain); }

static File envDir (const char* var, const char* fallback)
{
    return File (SystemStats::getEnvironmentVariable (var, fallback));
}

static File defaultVst3Dir() { return envDir ("CommonProgramFiles", "C:\\Program Files\\Common Files").getChildFile ("VST3"); }
static File defaultAppDir()  { return envDir ("ProgramFiles", "C:\\Program Files").getChildFile (kProduct); }
static File defaultMenuDir()
{
    const auto s = win32::commonProgramsFolder();
    const File base = s.empty() ? envDir ("ProgramData", "C:\\ProgramData").getChildFile ("Microsoft\\Windows\\Start Menu\\Programs")
                                : File (String (s.c_str()));
    return base.getChildFile (kProduct);
}

struct Options
{
    bool vst3 = true, app = true, shortcuts = true, registry = true;
    File vst3Dir = defaultVst3Dir(), appDir = defaultAppDir(), menuDir = defaultMenuDir();

    File bundle() const { return vst3Dir.getChildFile (kProduct + ".vst3"); }
};

struct Result
{
    bool ok = true;
    bool needsHostRestart = false;
    String message;
};

using Log = std::function<void (const String&)>;

// ---------------------------------------------------------------------------
// File operations
// ---------------------------------------------------------------------------
static String stamp() { return Time::getCurrentTime().formatted ("%Y%m%d%H%M%S"); }

/** Deletes a file. If Windows refuses because it is loaded (e.g. the plug-in
    inside a running Ableton Live), the file is renamed out of the way and
    removed at the next reboot; the host keeps using it until it restarts. */
static bool removeFile (const File& f, Result& r, const Log& log)
{
    if (! f.existsAsFile()) return true;
    if (f.deleteFile()) return true;

    const auto moved = f.getSiblingFile (f.getFileName() + ".old-" + stamp());
    if (f.moveFileTo (moved))
    {
        win32::deleteOnReboot (moved.getFullPathName().toWideCharPointer());
        r.needsHostRestart = true;
        log ("  (in use - previous version moved aside: " + f.getFileName() + ")");
        return true;
    }
    log ("  ERROR: cannot remove " + f.getFullPathName());
    return false;
}

static void removeLeftovers (const File& dir, bool recursive)
{
    if (dir.isDirectory())
        for (auto& f : dir.findChildFiles (File::findFiles, recursive, "*.old-*"))
            f.deleteFile();
}

static bool writeFile (const File& target, InputStream& in, Result& r, const Log& log)
{
    if (! target.getParentDirectory().createDirectory())
    {
        log ("  ERROR: cannot create folder " + target.getParentDirectory().getFullPathName());
        return false;
    }
    if (! removeFile (target, r, log))
        return false;

    FileOutputStream out (target);
    if (! out.openedOk())
    {
        log ("  ERROR: cannot write " + target.getFullPathName());
        return false;
    }
    out.writeFromInputStream (in, -1);
    out.flush();
    if (out.getStatus().failed())
    {
        log ("  ERROR: " + out.getStatus().getErrorMessage());
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Install
// ---------------------------------------------------------------------------
static Result install (const Options& opt, const Log& log)
{
    Result r;
    ZipFile zip (new MemoryInputStream (Payload::payload_zip, (size_t) Payload::payload_zipSize, false), true);
    if (zip.getNumEntries() == 0)
    {
        r.ok = false;
        r.message = "The installer is damaged (no payload).";
        return r;
    }

    removeLeftovers (opt.bundle(), true);
    removeLeftovers (opt.appDir, false);

    // The product used to be called "Modern EQ" and has the same plug-in ID:
    // remove that installation so hosts do not see the plug-in twice.
    {
        const File oldBundle = opt.vst3Dir.getChildFile (kLegacyProduct + ".vst3");
        if (oldBundle.isDirectory())
        {
            for (auto& f : oldBundle.findChildFiles (File::findFiles, true))
                removeFile (f, r, log);
            if (! oldBundle.deleteRecursively())
                win32::deleteOnReboot (oldBundle.getFullPathName().toWideCharPointer());
            log ("Removed previous version: " + oldBundle.getFullPathName());
        }
        const File oldApp = opt.appDir.getSiblingFile (kLegacyProduct);
        for (auto name : { kLegacyProduct + ".exe", "Uninstall " + kLegacyProduct + ".exe", String ("Third-Party Notices.txt") })
            removeFile (oldApp.getChildFile (name), r, log);
        removeLeftovers (oldApp, false);
        oldApp.deleteFile();   // only if empty
        const File oldMenu = opt.menuDir.getSiblingFile (kLegacyProduct);
        oldMenu.getChildFile (kLegacyProduct + ".lnk").deleteFile();
        oldMenu.getChildFile ("Uninstall " + kLegacyProduct + ".lnk").deleteFile();
        oldMenu.deleteFile();
        if (opt.registry)
            WindowsRegistry::deleteKey (kLegacyRegKey, WindowsRegistry::WoW64_64bit);
    }

    int64 bytes = 0;
    log ("Installing " + kProduct + " " + kVersion);
    for (int i = 0; i < zip.getNumEntries(); ++i)
    {
        const auto* entry = zip.getEntry (i);
        const String name = entry->filename.replaceCharacter ('\\', '/');
        if (name.endsWithChar ('/')) continue;

        File target;
        if (name.startsWith ("vst3/"))
        {
            if (! opt.vst3) continue;
            target = opt.vst3Dir.getChildFile (name.substring (5));
        }
        else if (name.startsWith ("app/"))
        {
            if (! opt.app && ! name.endsWithIgnoreCase (".txt")) continue;
            target = opt.appDir.getChildFile (name.substring (4));
        }
        else
        {
            continue;
        }

        std::unique_ptr<InputStream> in (zip.createStreamForEntry (i));
        if (in == nullptr || ! writeFile (target, *in, r, log))
        {
            r.ok = false;
            r.message = "Could not write " + target.getFullPathName()
                      + (win32::isElevated() ? String() : String ("\n(The installer needs administrator rights.)"));
            return r;
        }
        bytes += entry->uncompressedSize;
        log ("  " + target.getFullPathName());
    }

    // The uninstaller is a copy of this program.
    const File uninst = opt.appDir.getChildFile (kUninstallerName);
    const File self = File::getSpecialLocation (File::currentExecutableFile);
    if (self != uninst)
    {
        FileInputStream fin (self);
        if (! fin.openedOk() || ! writeFile (uninst, fin, r, log))
        {
            r.ok = false;
            r.message = "Could not create the uninstaller.";
            return r;
        }
        bytes += self.getSize();
        log ("  " + uninst.getFullPathName());
    }

    if (opt.shortcuts)
    {
        opt.menuDir.createDirectory();
        if (opt.app)
            opt.appDir.getChildFile (kProduct + ".exe").createShortcut ("GKEQ - standalone equaliser", opt.menuDir.getChildFile (kProduct + ".lnk"));
        uninst.createShortcut ("Remove GKEQ", opt.menuDir.getChildFile ("Uninstall " + kProduct + ".lnk"));
        log ("Start Menu shortcuts: " + opt.menuDir.getFullPathName());
    }

    if (opt.registry)
    {
        using WR = WindowsRegistry;
        const auto mode = WR::WoW64_64bit;
        auto set = [&] (const String& name, const String& value) { return WR::setValue (kRegKey + "\\" + name, value, mode); };
        bool ok = set ("DisplayName", kProduct)
               && set ("DisplayVersion", kVersion)
               && set ("Publisher", kPublisher)
               && set ("InstallLocation", opt.appDir.getFullPathName())
               && set ("UninstallString", "\"" + uninst.getFullPathName() + "\" /uninstall")
               && set ("QuietUninstallString", "\"" + uninst.getFullPathName() + "\" /uninstall /S")
               && set ("DisplayIcon", uninst.getFullPathName())
               && set ("VST3Path", opt.vst3 ? opt.bundle().getFullPathName() : String())
               && set ("StartMenuPath", opt.shortcuts ? opt.menuDir.getFullPathName() : String());
        ok = WR::setValue (kRegKey + "\\NoModify", (uint32) 1, mode) && ok;
        ok = WR::setValue (kRegKey + "\\NoRepair", (uint32) 1, mode) && ok;
        ok = WR::setValue (kRegKey + "\\EstimatedSize", (uint32) (bytes / 1024), mode) && ok;
        log (ok ? "Registered in Settings > Apps > Installed apps."
                : "WARNING: could not register the uninstaller (administrator rights missing?).");
    }

    r.message = "GKEQ " + kVersion + " has been installed.";
    return r;
}

// ---------------------------------------------------------------------------
// Uninstall
// ---------------------------------------------------------------------------
static void scheduleSelfDelete (const File& exe, const File& dir)
{
    // cmd waits until this process has exited, then removes the uninstaller
    // and the (then empty) program folder. rmdir without /s never deletes
    // anything that is not ours.
    String cmd = "cmd.exe /c cd /d \"" + File::getSpecialLocation (File::tempDirectory).getFullPathName()
               + "\" & ping 127.0.0.1 -n 3 > nul & del /f /q \"" + exe.getFullPathName() + "\"";
    if (dir.getFileName().equalsIgnoreCase (kProduct))
        cmd << " & rmdir \"" << dir.getFullPathName() << "\"";
    ChildProcess p;
    p.start (cmd, 0);
}

static Result uninstall (const Options& opt, bool removeUserData, const Log& log)
{
    Result r;
    using WR = WindowsRegistry;
    const auto mode = WR::WoW64_64bit;
    auto reg = [&] (const String& name, const File& fallback)
    {
        if (! opt.registry) return fallback;
        const auto v = WR::getValue (kRegKey + "\\" + name, String(), mode);
        return v.isNotEmpty() ? File (v) : fallback;
    };

    const File appDir  = reg ("InstallLocation", opt.appDir);
    const File bundle  = reg ("VST3Path", opt.bundle());
    const File menuDir = reg ("StartMenuPath", opt.menuDir);

    log ("Removing " + kProduct);

    // VST3 bundle (only ever a folder called "GKEQ.vst3")
    if (bundle.getFileName().equalsIgnoreCase (kProduct + ".vst3") && bundle.isDirectory())
    {
        for (auto& f : bundle.findChildFiles (File::findFiles, true))
            removeFile (f, r, log);
        if (! bundle.deleteRecursively())
        {
            // Something is still loaded: remove the rest at the next reboot.
            for (auto& f : bundle.findChildFiles (File::findFiles, true))
                win32::deleteOnReboot (f.getFullPathName().toWideCharPointer());
            auto dirs = bundle.findChildFiles (File::findDirectories, true);
            std::sort (dirs.begin(), dirs.end(), [] (const File& a, const File& b) { return a.getFullPathName().length() > b.getFullPathName().length(); });
            for (auto& d : dirs) win32::deleteOnReboot (d.getFullPathName().toWideCharPointer());
            win32::deleteOnReboot (bundle.getFullPathName().toWideCharPointer());
            r.needsHostRestart = true;
        }
        log ("  " + bundle.getFullPathName());
    }

    // Program folder: only our own files
    for (auto name : { kProduct + ".exe", String ("Third-Party Notices.txt") })
    {
        const File f = appDir.getChildFile (name);
        if (f.existsAsFile() && removeFile (f, r, log))
            log ("  " + f.getFullPathName());
    }
    removeLeftovers (appDir, false);

    // Start Menu
    if (menuDir.getFileName().equalsIgnoreCase (kProduct))
    {
        menuDir.getChildFile (kProduct + ".lnk").deleteFile();
        menuDir.getChildFile ("Uninstall " + kProduct + ".lnk").deleteFile();
        menuDir.deleteFile();   // only succeeds when empty
        log ("  Start Menu shortcuts");
    }

    if (opt.registry)
        WR::deleteKey (kRegKey, mode);

    if (removeUserData)
    {
        const File docs = File::getSpecialLocation (File::userDocumentsDirectory).getChildFile (kProduct);
        if (docs.isDirectory() && docs.deleteRecursively())
            log ("  " + docs.getFullPathName() + " (presets)");
    }

    const File uninst = appDir.getChildFile (kUninstallerName);
    const File self = File::getSpecialLocation (File::currentExecutableFile);
    if (self == uninst)
    {
        scheduleSelfDelete (uninst, appDir);
    }
    else
    {
        uninst.deleteFile();
        if (appDir.getFileName().equalsIgnoreCase (kProduct)) appDir.deleteFile();
    }

    r.message = "GKEQ has been removed.";
    return r;
}

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------
class SetupLookAndFeel : public LookAndFeel_V4
{
public:
    SetupLookAndFeel()
    {
        setColour (ResizableWindow::backgroundColourId, col::bg0);
        setColour (TextButton::buttonColourId, col::bg2);
        setColour (TextButton::buttonOnColourId, col::accent.withAlpha (0.4f));
        setColour (TextButton::textColourOffId, col::text);
        setColour (TextButton::textColourOnId, Colours::white);
        setColour (ToggleButton::textColourId, col::text);
        setColour (ToggleButton::tickColourId, col::accent);
        setColour (ToggleButton::tickDisabledColourId, col::dim);
        setColour (Label::textColourId, col::text);
        setColour (TextEditor::backgroundColourId, col::bg1);
        setColour (TextEditor::textColourId, col::dim);
        setColour (TextEditor::outlineColourId, col::edge);
        setColour (TextEditor::focusedOutlineColourId, col::edge);
        setColour (ScrollBar::thumbColourId, col::edge);
    }

    void drawButtonBackground (Graphics& g, Button& b, const Colour&, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool primary = b.getProperties()["primary"];
        Colour c = primary ? col::accent.withAlpha (0.85f) : col::bg2;
        if (! b.isEnabled()) c = c.withMultipliedAlpha (0.4f);
        if (over) c = c.brighter (0.12f);
        if (down) c = c.brighter (0.2f);
        g.setColour (c);
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (primary ? col::accent : col::edge);
        g.drawRoundedRectangle (r, 5.0f, 1.0f);
    }

    void drawButtonText (Graphics& g, TextButton& b, bool, bool) override
    {
        const bool primary = b.getProperties()["primary"];
        g.setFont (uiFont (15.0f, primary));
        g.setColour ((primary ? Colour (0xff06121b) : col::text).withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.5f));
        g.drawFittedText (b.getButtonText(), b.getLocalBounds(), Justification::centred, 1);
    }

    void drawTickBox (Graphics& g, Component& c, float x, float y, float w, float h, bool ticked, bool enabled, bool, bool) override
    {
        const Rectangle<float> r (x + 1.0f, y + 1.0f, w - 2.0f, h - 2.0f);
        g.setColour (ticked ? col::accent : col::bg2);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (ticked ? col::accent : col::edge);
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
        if (ticked)
        {
            Path tick;
            tick.startNewSubPath (r.getX() + r.getWidth() * 0.22f, r.getCentreY());
            tick.lineTo (r.getX() + r.getWidth() * 0.43f, r.getBottom() - r.getHeight() * 0.25f);
            tick.lineTo (r.getRight() - r.getWidth() * 0.2f, r.getY() + r.getHeight() * 0.25f);
            g.setColour (Colour (0xff06121b).withMultipliedAlpha (enabled ? 1.0f : 0.5f));
            g.strokePath (tick, PathStrokeType (2.2f, PathStrokeType::curved, PathStrokeType::rounded));
        }
        ignoreUnused (c);
    }

    Font getLabelFont (Label& l) override { return l.getFont(); }
};

class SetupComponent : public Component
{
public:
    SetupComponent (bool uninstallMode, Options options) : uninstalling (uninstallMode), opt (std::move (options))
    {
        setLookAndFeel (&lnf);
        for (auto* c : std::initializer_list<Component*> { &body, &warning, &vst3Path, &appPath, &status,
                                                            &vst3Toggle, &appToggle, &shortcutToggle, &removeData,
                                                            &changeVst3, &primary, &secondary, &log })
            addChildComponent (c);

        body.setFont (uiFont (15.0f));
        body.setJustificationType (Justification::topLeft);
        warning.setFont (uiFont (13.5f));
        warning.setColour (Label::textColourId, col::warn);
        warning.setJustificationType (Justification::topLeft);
        for (auto* l : { &vst3Path, &appPath })
        {
            l->setFont (uiFont (12.5f));
            l->setColour (Label::textColourId, col::dim);
        }
        status.setFont (uiFont (16.0f, true));
        // Wrap long text instead of squeezing it onto one line.
        for (auto* l : { &body, &warning, &status, &vst3Path, &appPath })
            l->setMinimumHorizontalScale (1.0f);

        vst3Toggle.setButtonText ("VST3 plug-in  (Ableton Live and other hosts)");
        appToggle.setButtonText ("Standalone application");
        shortcutToggle.setButtonText ("Start Menu shortcuts");
        removeData.setButtonText ("Also delete my presets and settings (Documents\\GKEQ)");
        for (auto* t : { &vst3Toggle, &appToggle, &shortcutToggle, &removeData })
        {
            t->setColour (ToggleButton::textColourId, col::text);
            t->onClick = [this] { refresh(); };
        }
        vst3Toggle.setToggleState (opt.vst3, dontSendNotification);
        appToggle.setToggleState (opt.app, dontSendNotification);
        shortcutToggle.setToggleState (opt.shortcuts, dontSendNotification);

        changeVst3.setButtonText ("Change...");
        changeVst3.onClick = [this]
        {
            chooser = std::make_unique<FileChooser> ("VST3 folder", opt.vst3Dir);
            chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories,
                                  [this] (const FileChooser& fc)
                                  {
                                      if (fc.getResult() != File()) { opt.vst3Dir = fc.getResult(); refresh(); }
                                  });
        };

        primary.getProperties().set ("primary", true);
        primary.onClick = [this] { onPrimary(); };
        secondary.onClick = [] { JUCEApplication::getInstance()->systemRequestedQuit(); };

        log.setMultiLine (true);
        log.setReadOnly (true);
        log.setScrollbarsShown (true);
        log.setFont (Font (Font::getDefaultMonospacedFontName(), 12.5f, Font::plain));

        showStart();
        setSize (660, 470);
    }

    ~SetupComponent() override
    {
        if (worker.joinable()) worker.join();
        setLookAndFeel (nullptr);
    }

    bool isBusy() const noexcept { return busy.load(); }

    /** For the screenshot test: show the finished page with sample text. */
    void showDemoDone()
    {
        Result r;
        r.message = "GKEQ " + kVersion + " has been installed.";
        appendLog ("Installing GKEQ " + kVersion);
        appendLog ("  " + opt.bundle().getFullPathName() + "\\Contents\\x86_64-win\\GKEQ.vst3");
        appendLog ("  " + opt.appDir.getFullPathName() + "\\GKEQ.exe");
        appendLog ("Registered in Settings > Apps > Installed apps.");
        showWorking();
        finished (r);
    }

    void paint (Graphics& g) override
    {
        g.fillAll (col::bg0);
        auto header = getLocalBounds().removeFromTop (96).toFloat();
        ColourGradient grad (col::bg2, 0.0f, 0.0f, col::bg0, 0.0f, header.getBottom(), false);
        g.setGradientFill (grad);
        g.fillRect (header);
        g.setColour (col::edge);
        g.drawHorizontalLine ((int) header.getBottom(), 0.0f, (float) getWidth());

        // logo mark
        const Rectangle<float> mark (28.0f, 22.0f, 52.0f, 52.0f);
        g.setColour (col::bg1);
        g.fillRoundedRectangle (mark, 12.0f);
        g.setColour (col::edge);
        g.drawRoundedRectangle (mark, 12.0f, 1.0f);
        Path curve;
        for (int i = 0; i <= 40; ++i)
        {
            const float t = i / 40.0f;
            const float x = mark.getX() + 7.0f + t * (mark.getWidth() - 14.0f);
            const float y = mark.getCentreY() - 13.0f * std::exp (-std::pow ((t - 0.3f) / 0.13f, 2.0f))
                                              + 10.0f * std::exp (-std::pow ((t - 0.67f) / 0.08f, 2.0f));
            if (i == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
        }
        g.setColour (Colours::white);
        g.strokePath (curve, PathStrokeType (2.6f, PathStrokeType::curved, PathStrokeType::rounded));

        g.setFont (uiFont (26.0f, true));
        g.setColour (col::text);
        g.drawText ("GK", 96, 22, 60, 32, Justification::centredLeft);
        g.setColour (col::accent);
        g.drawText ("EQ", 96 + g.getCurrentFont().getStringWidth ("GK") + 1, 22, 60, 32, Justification::centredLeft);
        g.setFont (uiFont (14.0f));
        g.setColour (col::dim);
        g.drawText ((uninstalling ? "Uninstall  -  version " : "Setup  -  version ") + kVersion + "   |   24-band dynamic EQ, VST3",
                    96, 52, 460, 22, Justification::centredLeft);

        // footer
        g.setColour (col::edge);
        g.drawHorizontalLine (getHeight() - 64, 0.0f, (float) getWidth());
    }

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromTop (110);
        auto footer = r.removeFromBottom (64).reduced (24, 14);
        primary.setBounds (footer.removeFromRight (130));
        footer.removeFromRight (10);
        secondary.setBounds (footer.removeFromRight (110));

        auto content = r.reduced (28, 4);
        if (page == Page::Start && ! uninstalling)
        {
            body.setBounds (content.removeFromTop (48));
            content.removeFromTop (6);
            auto row = content.removeFromTop (26);
            changeVst3.setBounds (row.removeFromRight (90).reduced (0, 1));
            vst3Toggle.setBounds (row);
            vst3Path.setBounds (content.removeFromTop (20).withTrimmedLeft (28));
            content.removeFromTop (8);
            appToggle.setBounds (content.removeFromTop (26));
            appPath.setBounds (content.removeFromTop (20).withTrimmedLeft (28));
            content.removeFromTop (8);
            shortcutToggle.setBounds (content.removeFromTop (26));
            content.removeFromTop (12);
            warning.setBounds (content.removeFromTop (44));
        }
        else if (page == Page::Start)
        {
            body.setBounds (content.removeFromTop (110));
            content.removeFromTop (10);
            removeData.setBounds (content.removeFromTop (26));
            content.removeFromTop (12);
            warning.setBounds (content.removeFromTop (44));
        }
        else
        {
            status.setBounds (content.removeFromTop (28));
            content.removeFromTop (4);
            body.setBounds (content.removeFromTop (body.getText().isEmpty() ? 0 : 66));
            content.removeFromTop (4);
            log.setBounds (content);
        }
    }

private:
    enum class Page { Start, Working, Done };

    void refresh()
    {
        opt.vst3 = vst3Toggle.getToggleState();
        opt.app = appToggle.getToggleState();
        opt.shortcuts = shortcutToggle.getToggleState();
        vst3Path.setText (opt.bundle().getFullPathName(), dontSendNotification);
        appPath.setText (opt.appDir.getFullPathName(), dontSendNotification);
        changeVst3.setEnabled (opt.vst3);
        primary.setEnabled (uninstalling || opt.vst3 || opt.app);
    }

    void showStart()
    {
        page = Page::Start;
        for (auto* c : getChildren()) c->setVisible (false);

        const bool liveRunning = win32::isProcessRunning (L"ableton live");
        if (uninstalling)
        {
            body.setText ("This removes the GKEQ VST3 plug-in, the standalone application, the Start Menu "
                          "shortcuts and the entry in Installed apps.\n\nLive sets that use GKEQ keep their "
                          "settings, but the plug-in will be missing until it is installed again.",
                          dontSendNotification);
            removeData.setVisible (true);
            primary.setButtonText ("Uninstall");
        }
        else
        {
            body.setText ("Choose what to install. The VST3 plug-in goes into the standard Windows VST3 folder "
                          "that Ableton Live scans.", dontSendNotification);
            for (auto* c : std::initializer_list<Component*> { &vst3Toggle, &appToggle, &shortcutToggle, &vst3Path, &appPath, &changeVst3 })
                c->setVisible (true);
            primary.setButtonText ("Install");
        }
        warning.setText (liveRunning ? String ("Ableton Live is running. You can continue - the new version is loaded "
                                               "the next time Live starts - or close Live first.")
                                     : String(), dontSendNotification);
        for (auto* c : std::initializer_list<Component*> { &body, &warning, &primary, &secondary })
            c->setVisible (true);
        secondary.setButtonText ("Cancel");
        refresh();
        resized();
    }

    void showWorking()
    {
        page = Page::Working;
        for (auto* c : getChildren()) c->setVisible (false);
        status.setText (uninstalling ? "Removing GKEQ..." : "Installing GKEQ...", dontSendNotification);
        status.setColour (Label::textColourId, col::text);
        body.setText ({}, dontSendNotification);
        for (auto* c : std::initializer_list<Component*> { &status, &log, &primary })
            c->setVisible (true);
        primary.setButtonText ("Finish");
        primary.setEnabled (false);
        resized();
    }

    void appendLog (const String& line)
    {
        log.moveCaretToEnd();
        log.insertTextAtCaret (line + "\n");
    }

    void onPrimary()
    {
        if (page == Page::Done)
        {
            JUCEApplication::getInstance()->systemRequestedQuit();
            return;
        }
        if (page != Page::Start) return;

        refresh();
        showWorking();
        busy = true;
        const bool removeUser = removeData.getToggleState();
        Component::SafePointer<SetupComponent> safe (this);
        auto logFn = [safe] (const String& line)
        {
            MessageManager::callAsync ([safe, line] { if (safe != nullptr) safe->appendLog (line); });
        };
        worker = std::thread ([this, safe, logFn, removeUser]
        {
            const auto res = uninstalling ? uninstall (opt, removeUser, logFn) : install (opt, logFn);
            MessageManager::callAsync ([safe, res] { if (safe != nullptr) safe->finished (res); });
        });
    }

    void finished (const Result& r)
    {
        busy = false;
        page = Page::Done;
        status.setText (r.ok ? r.message : "Setup could not finish", dontSendNotification);
        status.setColour (Label::textColourId, r.ok ? col::good : col::bad);

        String text;
        if (! r.ok)
            text = r.message;
        else if (! uninstalling)
        {
            text = "In Ableton Live: Options > Preferences > Plug-Ins - switch on \"Use VST3 Plug-in System "
                   "Folders\" and press Rescan. GKEQ appears under Plug-Ins > Modern Audio.";
            if (r.needsHostRestart)
                text << "\nA running host still uses the previous version: restart Ableton Live to load the new one.";
        }
        else if (r.needsHostRestart)
            text = "Some files were in use and will be removed after you restart Windows.";
        body.setText (text, dontSendNotification);
        body.setColour (Label::textColourId, r.ok ? col::text : col::bad);
        body.setVisible (text.isNotEmpty());
        primary.setEnabled (true);
        resized();
    }

    SetupLookAndFeel lnf;
    bool uninstalling;
    Options opt;
    Page page = Page::Start;

    Label body, warning, vst3Path, appPath, status;
    ToggleButton vst3Toggle, appToggle, shortcutToggle, removeData;
    TextButton changeVst3, primary, secondary;
    TextEditor log;
    std::unique_ptr<FileChooser> chooser;
    std::thread worker;
    std::atomic<bool> busy { false };
};

class SetupWindow : public DocumentWindow
{
public:
    SetupWindow (bool uninstallMode, const Options& opt)
        : DocumentWindow (uninstallMode ? "Uninstall GKEQ" : "GKEQ Setup", col::bg0, DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar (true);
        content = new SetupComponent (uninstallMode, opt);
        setContentOwned (content, true);
        setResizable (false, false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        JUCEApplication::getInstance()->systemRequestedQuit();
    }

    bool isBusy() const { return content != nullptr && content->isBusy(); }

private:
    SetupComponent* content = nullptr;
};

// ---------------------------------------------------------------------------
class SetupApplication : public JUCEApplication
{
public:
    const String getApplicationName() override { return "GKEQ Setup"; }
    const String getApplicationVersion() override { return kVersion; }

    // Only one interactive window at a time. Scripted runs (/S, /silent,
    // /snapshot) must never be swallowed by the single-instance check, which
    // can still see a just-exited previous run.
    bool moreThanOneInstanceAllowed() override
    {
        return getCommandLineParameters().containsIgnoreCase ("/s");
    }

    void initialise (const String& commandLine) override
    {
        // Arguments may arrive quoted as a whole ("/appdir=C:\some path").
        StringArray args;
        for (auto& a : StringArray::fromTokens (commandLine, true))
            args.add (a.unquoted());
        auto has = [&] (const char* flag) { return args.contains (flag, true); };
        auto value = [&] (const char* key) -> String
        {
            for (auto& a : args)
                if (a.startsWithIgnoreCase (String (key) + "="))
                    return a.fromFirstOccurrenceOf ("=", false, false).unquoted();
            return {};
        };

        const auto exeName = File::getSpecialLocation (File::currentExecutableFile).getFileName();
        const bool uninstallMode = has ("/uninstall") || exeName.startsWithIgnoreCase ("Uninstall");
        const bool silent = has ("/S") || has ("/silent");

        Options opt;
        if (value ("/vst3dir").isNotEmpty()) opt.vst3Dir = File (value ("/vst3dir"));
        if (value ("/appdir").isNotEmpty())  opt.appDir  = File (value ("/appdir"));
        if (value ("/menudir").isNotEmpty()) opt.menuDir = File (value ("/menudir"));
        if (has ("/novst3"))      opt.vst3 = false;
        if (has ("/noapp"))       opt.app = false;
        if (has ("/noshortcuts")) opt.shortcuts = false;
        if (has ("/noregistry"))  opt.registry = false;

        const String snapshot = value ("/snapshot");
        if (snapshot.isNotEmpty())
        {
            SetupComponent c (uninstallMode, opt);
            if (has ("/done")) c.showDemoDone();
            const auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
            File out (snapshot);
            out.deleteFile();
            FileOutputStream os (out);
            PNGImageFormat().writeImageToStream (img, os);
            quit();
            return;
        }

        if (silent)
        {
            String text;
            auto logFn = [&text] (const String& line) { text << line << "\n"; };
            const auto res = uninstallMode ? uninstall (opt, false, logFn) : install (opt, logFn);
            text << (res.ok ? "OK: " : "FAILED: ") << res.message << "\n";
            if (value ("/log").isNotEmpty())
                File (value ("/log")).replaceWithText (text);
            setApplicationReturnValue (res.ok ? 0 : 1);
            quit();
            return;
        }

        window = std::make_unique<SetupWindow> (uninstallMode, opt);
    }

    void shutdown() override { window = nullptr; }

    void systemRequestedQuit() override
    {
        if (window != nullptr && window->isBusy()) return;   // never abort half-way
        quit();
    }

    void anotherInstanceStarted (const String&) override {}

private:
    std::unique_ptr<SetupWindow> window;
};

} // namespace setup

START_JUCE_APPLICATION (setup::SetupApplication)
