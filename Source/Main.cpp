#include <JuceHeader.h>
#include "MainComponent.h"

class LiteDAWApplication : public juce::JUCEApplication
{
public:
    LiteDAWApplication() = default;

    const juce::String getApplicationName() override    { return "LiteDAW"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override           { return false; }

    void initialise(const juce::String&) override
    {
        // Ventana única, sin decoraciones extra: menos overhead de render.
        mainWindow.reset(new MainWindow(getApplicationName()));
    }

    void shutdown() override
    {
        mainWindow = nullptr;
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow(const juce::String& name)
            : DocumentWindow(name,
                              juce::Desktop::getInstance().getDefaultLookAndFeel()
                                  .findColour(juce::ResizableWindow::backgroundColourId),
                              DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(), true);

            // Tamaño moderado: nada de fullscreen pesado por defecto.
            centreWithSize(1100, 650);
            setResizable(true, true);
            setVisible(true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(LiteDAWApplication)
