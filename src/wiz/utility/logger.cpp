#include <cstdint>
#include <utility>

#define WIZ_TTY_INCLUDE_LOWLEVEL
#include <wiz/utility/tty.h>
#include <wiz/utility/logger.h>

namespace wiz {
    namespace {
        enum class ColorAttributeFlag {
            BackgroundRed,
            BackgroundGreen,
            BackgroundBlue,
            BackgroundIntensity,
            ForegroundRed,
            ForegroundGreen,
            ForegroundBlue,
            ForegroundIntensity,
            DefaultBackgroundColor,
            DefaultForegroundColor,

            Count
        };
        using ColorAttributeFlags = BitFlags<ColorAttributeFlag, ColorAttributeFlag::Count>;

        ColorAttributeFlags ColorAttributeFlagsBackgroundChannels = ColorAttributeFlags::of<
            ColorAttributeFlag::BackgroundRed,
            ColorAttributeFlag::BackgroundGreen,
            ColorAttributeFlag::BackgroundBlue,
            ColorAttributeFlag::BackgroundIntensity   
        >();

        ColorAttributeFlags ColorAttributeFlagsForegroundChannels = ColorAttributeFlags::of<
            ColorAttributeFlag::ForegroundRed,
            ColorAttributeFlag::ForegroundGreen,
            ColorAttributeFlag::ForegroundBlue,
            ColorAttributeFlag::ForegroundIntensity
        >();

#ifdef _WIN32
        std::int16_t toWindowsAttribute(ColorAttributeFlags flags) {
            std::uint16_t result = 0;

            if (flags.has<ColorAttributeFlag::BackgroundRed>()) { result |= BACKGROUND_RED; }
            if (flags.has<ColorAttributeFlag::BackgroundGreen>()) { result |= BACKGROUND_GREEN; }
            if (flags.has<ColorAttributeFlag::BackgroundBlue>()) { result |= BACKGROUND_BLUE; }
            if (flags.has<ColorAttributeFlag::BackgroundIntensity>()) { result |= BACKGROUND_INTENSITY; }
            if (flags.has<ColorAttributeFlag::ForegroundRed>()) { result |= FOREGROUND_RED; }
            if (flags.has<ColorAttributeFlag::ForegroundGreen>()) { result |= FOREGROUND_GREEN; }
            if (flags.has<ColorAttributeFlag::ForegroundBlue>()) { result |= FOREGROUND_BLUE; }
            if (flags.has<ColorAttributeFlag::ForegroundIntensity>()) { result |= FOREGROUND_INTENSITY; }

            return static_cast<std::int16_t>(result);
        }

        ColorAttributeFlags fromWindowsAttributes(int16_t signedMask) {
            ColorAttributeFlags result;
            const auto flags = static_cast<std::uint16_t>(signedMask);

            if (flags & BACKGROUND_RED) { result |= ColorAttributeFlags::of<ColorAttributeFlag::BackgroundRed>(); }
            if (flags & BACKGROUND_GREEN) { result |= ColorAttributeFlags::of<ColorAttributeFlag::BackgroundGreen>(); }
            if (flags & BACKGROUND_BLUE) { result |= ColorAttributeFlags::of<ColorAttributeFlag::BackgroundBlue>(); }
            if (flags & BACKGROUND_INTENSITY) { result |= ColorAttributeFlags::of<ColorAttributeFlag::BackgroundIntensity>(); }
            if (flags & FOREGROUND_RED) { result |= ColorAttributeFlags::of<ColorAttributeFlag::ForegroundRed>(); }
            if (flags & FOREGROUND_GREEN) { result |= ColorAttributeFlags::of<ColorAttributeFlag::ForegroundGreen>(); }
            if (flags & FOREGROUND_BLUE) { result |= ColorAttributeFlags::of<ColorAttributeFlag::ForegroundBlue>(); }
            if (flags & FOREGROUND_INTENSITY) { result |= ColorAttributeFlags::of<ColorAttributeFlag::ForegroundIntensity>(); }

            return result;
        }
#endif

        FileLogger::ColorMethod getColorMethod(Logger::ColorSetting colorSetting, std::FILE* file) {
            if (colorSetting == Logger::ColorSetting::None) {
                return FileLogger::ColorMethod::None;
            }
            if (colorSetting == Logger::ColorSetting::ForceAnsi
            || isAnsiTTY(file)) {
                return FileLogger::ColorMethod::Ansi;
            }

#ifdef _WIN32
            if (const auto handle = getStdHandleForFile(file)) {
                if (handle != nullptr)  {
                    return FileLogger::ColorMethod::Windows;
                }
            }
#endif

            return FileLogger::ColorMethod::None;
        }


        struct ConsoleColorContext {
            public:
                ConsoleColorContext(std::FILE* file, FileLogger::ColorMethod colorMethod)
                : file(file),
                needsReset(false),
                colorMethod(colorMethod)
#ifdef _WIN32
                , originalAttributes(0),
                handle(getStdHandleForFile(file))
#endif
                {
#ifdef _WIN32
                    if (handle != nullptr) {
                        CONSOLE_SCREEN_BUFFER_INFO info;
                        GetConsoleScreenBufferInfo(handle, &info);
                        originalAttributes = info.wAttributes;
                    }
#endif
                }

                ~ConsoleColorContext() {
                    if (needsReset) {
                        resetColor();
                    }
                }

                void resetColor() {
                    if (colorMethod == FileLogger::ColorMethod::None) {
                        return;
                    }
                    if (colorMethod == FileLogger::ColorMethod::Ansi) {
                        std::fputs("\033[0m", file);
                    }
#ifdef _WIN32
                    if (colorMethod == FileLogger::ColorMethod::Windows && handle != nullptr) {
                        SetConsoleTextAttribute(handle, originalAttributes);
                    }
#endif

                    needsReset = false;
                }

                void setColor(ColorAttributeFlags colorFlags) {
                    if (colorMethod == FileLogger::ColorMethod::None) {
                        return;
                    }

                    if (colorMethod == FileLogger::ColorMethod::Ansi) {
                        std::fprintf(file, "\033[%d", colorFlags.has<ColorAttributeFlag::ForegroundIntensity>() ? 1 : 0);
                        if (!colorFlags.has<ColorAttributeFlag::DefaultForegroundColor>()) {
                            std::uint8_t mask = 0;
                            if (colorFlags.has<ColorAttributeFlag::ForegroundRed>()) { mask |= 0x01; }
                            if (colorFlags.has<ColorAttributeFlag::ForegroundGreen>()) { mask |= 0x02; }
                            if (colorFlags.has<ColorAttributeFlag::ForegroundBlue>()) { mask |= 0x04; }
                            std::fprintf(file, ";%u", mask + 30);
                        }
                        if (!colorFlags.has<ColorAttributeFlag::DefaultBackgroundColor>()) {
                            std::uint8_t mask = 0;
                            if (colorFlags.has<ColorAttributeFlag::BackgroundRed>()) { mask |= 0x01; }
                            if (colorFlags.has<ColorAttributeFlag::BackgroundGreen>()) { mask |= 0x02; }
                            if (colorFlags.has<ColorAttributeFlag::BackgroundBlue>()) { mask |= 0x04; }
                            std::fprintf(file, ";%u", mask + 40);
                        }
                        std::fputs("m", file);
                    }

#ifdef _WIN32
                    if (colorMethod == FileLogger::ColorMethod::Windows && handle != nullptr) {
                        if (colorFlags.has<ColorAttributeFlag::DefaultBackgroundColor>()) {
                            colorFlags |= (fromWindowsAttributes(originalAttributes) & ColorAttributeFlagsBackgroundChannels);
                        }
                        if (colorFlags.has<ColorAttributeFlag::DefaultForegroundColor>()) {
                            colorFlags |= (fromWindowsAttributes(originalAttributes) & ColorAttributeFlagsForegroundChannels);
                        }
                        SetConsoleTextAttribute(handle, toWindowsAttribute(colorFlags));
                    }
#endif

                    needsReset = true;
                }

                std::pair<std::size_t, std::size_t> getWindowSize() const {
#ifdef _WIN32
                    {
                        CONSOLE_SCREEN_BUFFER_INFO info;
                        GetConsoleScreenBufferInfo(handle, &info);
                        return {static_cast<std::size_t>(info.dwSize.X), static_cast<std::size_t>(info.dwSize.Y)};
                    }
#elif defined(__unix__) && !defined(__EMSCRIPTEN__) && defined(_POSIX_SOURCE)
                    {
                        winsize ws;
                        ioctl(WIZ_FILENO(file), TIOCGWINSZ, &ws);
                        return {static_cast<std::size_t>(ws.ws_col), static_cast<std::size_t>(ws.ws_row)};
                    }
#else
                    return {0, 0};
#endif
                }

            private:
                std::FILE* file;
                bool needsReset;
                FileLogger::ColorMethod colorMethod;
#ifdef _WIN32
                std::int16_t originalAttributes;
                HANDLE handle;
#endif
        };

        ColorAttributeFlags getColorForError(ReportErrorSeverity severity) {
            switch (severity) {
                case ReportErrorSeverity::Note: return ColorAttributeFlags::of<
                    ColorAttributeFlag::DefaultBackgroundColor,
                    ColorAttributeFlag::ForegroundRed,
                    ColorAttributeFlag::ForegroundBlue,
                    ColorAttributeFlag::ForegroundIntensity
                >();
                default: return ColorAttributeFlags::of<
                    ColorAttributeFlag::DefaultBackgroundColor,
                    ColorAttributeFlag::ForegroundRed,
                    ColorAttributeFlag::ForegroundIntensity
                >();
            }
        }

        void printRichString(std::size_t& column, std::size_t windowWidth, ConsoleColorContext& context, std::FILE* file, StringView message) {
            static_cast<void>(column);
            static_cast<void>(windowWidth);
            char quote = 0;
            bool escape = false;

            // TODO: implement word-wrapping that is punctuation/spacing aware, and nicely word-wraps quoted entities.

            std::size_t lastIndex = 0;
            for (std::size_t i = 0; i != message.getLength(); ++i) {
                char c = message[i];

                if (escape) {
                    escape = false;
                } else {
                    if (c == '\\') {
                        escape = true;
                    } else if ((c == '`' || c == '"') && (quote == 0 || quote == c)) {
                        std::fwrite(&message[lastIndex], i - lastIndex + (quote != 0 ? 1 : 0), 1, file);

                        if (quote != 0) {
                            quote = 0;
                            context.resetColor();
                        } else {
                            quote = c;
                            context.setColor(ColorAttributeFlags::of<
                                ColorAttributeFlag::DefaultBackgroundColor,
                                ColorAttributeFlag::DefaultForegroundColor,
                                ColorAttributeFlag::ForegroundIntensity
                            >());
                        }
                        lastIndex = quote != 0 ? i : i + 1;
                    }
                }
            }

            if (lastIndex < message.getLength()) {
                std::fwrite(&message[lastIndex], message.getLength() - lastIndex, 1, file);
            }
            context.resetColor();
        }

        void printWordWrapped(std::size_t& column, std::size_t windowWidth, std::FILE* file, StringView message) {
            static_cast<void>(column);
            static_cast<void>(windowWidth);

            // TODO: implement word-wrapping that is punctuation/spacing aware, and nicely word-wraps quoted entities.

            std::fwrite(message.getData(), message.getLength(), 1, file);
        }
    }

    FileLogger::FileLogger(FILE* file, ColorSetting colorSetting)
    : file(file),
    colorSetting(colorSetting),
    colorMethod(getColorMethod(colorSetting, file)) {}

    FileLogger::~FileLogger() {}

    // TODO: nice-to-have: word-wrapping support for TTY
    // See GetConsoleScreenBufferInfo on Windows / struct winsize size; ioctl(STDOUT_FILENO, TIOCGWINSZ, &size); on POSIX.
    void FileLogger::log(const std::string& message) {
        ConsoleColorContext context(file, colorMethod);

        std::size_t column = 0;
        auto windowSize = context.getWindowSize();

        if (message.rfind(">>", 0) == 0) {
            context.setColor(ColorAttributeFlags::of<
                ColorAttributeFlag::DefaultBackgroundColor,
                ColorAttributeFlag::DefaultForegroundColor,
                ColorAttributeFlag::ForegroundIntensity
            >());
            printWordWrapped(column, windowSize.first, file, StringView(message));
        } else {
            context.resetColor();            
            printRichString(column, windowSize.first, context, file, StringView(message));
        }
        std::fwrite("\n", 1, 1, file);
    }

    // TODO: nice-to-have: word-wrapping support for TTY
    void FileLogger::error(const SourceLocation& location, ReportErrorSeverity severity, const std::string& message) {
        ConsoleColorContext context(file, colorMethod);
        context.setColor(ColorAttributeFlags::of<
            ColorAttributeFlag::DefaultBackgroundColor,
            ColorAttributeFlag::DefaultForegroundColor,
            ColorAttributeFlag::ForegroundIntensity
        >());

        std::size_t column = 0;
        auto windowSize = context.getWindowSize();

        const auto locationString = location.toString();
        if (locationString.size() != 0) {
            printWordWrapped(column, windowSize.first, file, StringView(locationString));
            printWordWrapped(column, windowSize.first, file, ": "_sv); 
        }

        context.setColor(getColorForError(severity));
        printWordWrapped(column, windowSize.first, file, getReportErrorSeverityName(severity));
        printWordWrapped(column, windowSize.first, file, ": "_sv); 
        context.resetColor();
        printRichString(column, windowSize.first, context, file, StringView(message));
        std::fwrite("\n", 1, 1, file);
    }

    // TODO: nice-to-have: word-wrapping support for TTY
    void FileLogger::notice(const std::string& message) {
        ConsoleColorContext context(file, colorMethod);
        context.setColor(ColorAttributeFlags::of<
            ColorAttributeFlag::DefaultBackgroundColor,
            ColorAttributeFlag::DefaultForegroundColor,
            ColorAttributeFlag::ForegroundIntensity
        >());

        std::size_t column = 0;
        auto windowSize = context.getWindowSize();

        std::fputs("* wiz: ", file);
        context.resetColor();
        printRichString(column, windowSize.first, context, file, StringView(message));
        std::fwrite("\n", 1, 1, file);
    }

    Logger::ColorSetting FileLogger::getColorSetting() const {
        return colorSetting;
    }

    void FileLogger::setColorSetting(ColorSetting value) {
        colorSetting = value;
        colorMethod = getColorMethod(colorSetting, file);
    }



    MemoryLogger::MemoryLogger() {}
    MemoryLogger::~MemoryLogger() {}

    void MemoryLogger::log(const std::string& message) {
        logs.push_back(message);
    }

    void MemoryLogger::error(const SourceLocation& location, ReportErrorSeverity severity, const std::string& message) {
        errors.push_back(ErrorMessage(location, severity, message));
    }

    void MemoryLogger::notice(const std::string& message) {
        notices.push_back(message);
    }



    MemoryLogger::ErrorMessage::ErrorMessage(const SourceLocation& location, ReportErrorSeverity severity, const std::string& message)
    : location(location), severity(severity), message(message) {}

    MemoryLogger::ErrorMessage::~ErrorMessage() {}
}