#include "PluginEditor.h"

namespace
{
    juce::String mimeTypeForExtension (const juce::String& ext)
    {
        static const std::unordered_map<std::string, juce::String> map {
            { "html", "text/html" },
            { "htm",  "text/html" },
            { "js",   "text/javascript" },
            { "mjs",  "text/javascript" },
            { "css",  "text/css" },
            { "json", "application/json" },
            { "svg",  "image/svg+xml" },
            { "png",  "image/png" },
            { "jpg",  "image/jpeg" },
            { "jpeg", "image/jpeg" },
            { "gif",  "image/gif" },
            { "webp", "image/webp" },
            { "ico",  "image/x-icon" },
            { "woff", "font/woff" },
            { "woff2","font/woff2" },
            { "ttf",  "font/ttf" },
            { "otf",  "font/otf" },
            { "wasm", "application/wasm" },
            { "map",  "application/json" },
        };
        const auto it = map.find (ext.toLowerCase().toStdString());
        return it != map.end() ? it->second : juce::String ("application/octet-stream");
    }
}

SpectralFreezeEditor::SpectralFreezeEditor (SpectralFreezeProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    addAndMakeVisible (webView);

   #if SPECTRAL_FREEZE_UI_DEV
    webView.goToURL (SPECTRAL_FREEZE_UI_DEV_URL);
   #else
    webView.goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
   #endif

    setResizable (true, true);
    setResizeLimits (480, 320, 1600, 1000);
    setSize (720, 420);
}

SpectralFreezeEditor::~SpectralFreezeEditor() = default;

void SpectralFreezeEditor::resized()
{
    webView.setBounds (getLocalBounds());
}

juce::WebBrowserComponent::Options SpectralFreezeEditor::buildOptions()
{
    auto options = juce::WebBrowserComponent::Options {}
        .withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
        .withWinWebView2Options (juce::WebBrowserComponent::Options::WinWebView2 {}
            .withUserDataFolder (juce::File::getSpecialLocation (juce::File::tempDirectory)))
        .withNativeIntegrationEnabled()
        .withOptionsFrom (filterRelay)
        .withOptionsFrom (freezeRelay)
        .withOptionsFrom (scBoostRelay)
        .withOptionsFrom (scFreqSmoothRelay)
        .withOptionsFrom (organicRelay);

   #if ! SPECTRAL_FREEZE_UI_DEV
    options = options.withResourceProvider (
        [this] (const juce::String& url) { return provideResource (url); },
        juce::URL (juce::WebBrowserComponent::getResourceProviderRoot()).getOrigin());
   #endif

    return options;
}

std::optional<juce::WebBrowserComponent::Resource>
SpectralFreezeEditor::provideResource (const juce::String& urlPath)
{
    // urlPath arrives like "/" or "/assets/main-abcd.js" — strip the leading slash
    // and map "/" → "index.html" (standard static-site behaviour).
    auto relative = urlPath.startsWith ("/") ? urlPath.substring (1) : urlPath;
    if (relative.isEmpty())
        relative = "index.html";

    const juce::File distRoot { SPECTRAL_FREEZE_UI_DIST_DIR };
    const juce::File file = distRoot.getChildFile (relative);

    if (! file.existsAsFile())
        return std::nullopt;

    juce::MemoryBlock bytes;
    if (! file.loadFileAsData (bytes))
        return std::nullopt;

    std::vector<std::byte> data (bytes.getSize());
    std::memcpy (data.data(), bytes.getData(), bytes.getSize());

    return juce::WebBrowserComponent::Resource {
        std::move (data),
        mimeTypeForExtension (file.getFileExtension().trimCharactersAtStart ("."))
    };
}
