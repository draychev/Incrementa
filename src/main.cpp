#include <windows.h>
#include <chrono>
#include <algorithm>
#include <codecvt>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <locale>
#include <string>
#include <vector>

struct Card {
    std::wstring question;
    std::wstring answer;
};

enum class Rating { Bad, Meh, Good };

struct RatedCard {
    Card card;
    Rating rating;
    std::chrono::system_clock::time_point timestamp;
};

struct AppControls {
    HWND hTopEdit{};
    HWND hBottomEdit{};
    HWND hBtnShowAnswer{};
    HWND hBtnGood{};
    HWND hBtnMeh{};
    HWND hBtnBad{};
};

struct AppState {
    std::vector<Card> cards{};
    size_t currentCardIndex{0};
    bool answerVisible{false};
    std::ofstream ratingLog{};
    HFONT hFont{nullptr};
    AppControls controls{};
};

namespace {
constexpr int ID_TOP_EDIT = 1001;
constexpr int ID_BOTTOM_EDIT = 1002;
constexpr int ID_BTN_SHOWANSWER = 1003;
constexpr int ID_BTN_GOOD = 1004;
constexpr int ID_BTN_MEH = 1005;
constexpr int ID_BTN_BAD = 1006;

constexpr int BTN_BAR_HEIGHT = 40;
constexpr int REVEAL_HEIGHT = 40;
constexpr int MARGIN = 10;
constexpr int MIN_WIDTH = 640;
constexpr int MIN_HEIGHT = 480;

AppState g_state;
}

void AppendRatingToLog(size_t cardIndex, Rating rating) {
    if (!g_state.ratingLog.is_open()) {
        return;
    }

    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
    localtime_s(&localTime, &time);

    char ratingChar = '?';
    switch (rating) {
    case Rating::Good:
        ratingChar = 'G';
        break;
    case Rating::Meh:
        ratingChar = 'M';
        break;
    case Rating::Bad:
        ratingChar = 'B';
        break;
    }

    g_state.ratingLog << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S")
                      << '|' << cardIndex << '|' << ratingChar << "\n";
    g_state.ratingLog.flush();
}

std::vector<Card> LoadDefaultCards() {
    return {
        {L"What is the capital of France?", L"Paris"},
        {L"What is 2 + 2?", L"4"},
        {L"Name the largest planet in our solar system.", L"Jupiter"},
    };
}

std::wstring ToWide(const std::string& text) {
    static std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    return converter.from_bytes(text);
}

std::string Trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }

    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::wstring ExtractValue(const std::string& line, const std::string& key) {
    const auto prefix = key + ":";
    if (line.rfind(prefix, 0) != 0) {
        return L"";
    }

    const auto value = Trim(line.substr(prefix.size()));
    return ToWide(value);
}

std::vector<Card> LoadCardsFromYaml(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return {};
    }

    std::vector<Card> cards;
    Card currentCard{};
    bool inCard = false;
    std::string line;
    while (std::getline(file, line)) {
        const std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }

        if (trimmed == "cards:") {
            continue;
        }

        if (trimmed.rfind("- ", 0) == 0) {
            if (inCard && !currentCard.question.empty() && !currentCard.answer.empty()) {
                cards.push_back(currentCard);
            }

            currentCard = Card{};
            inCard = true;

            const std::string entry = Trim(trimmed.substr(2));
            if (entry.rfind("question:", 0) == 0) {
                currentCard.question = ExtractValue(entry, "question");
            } else if (entry.rfind("answer:", 0) == 0) {
                currentCard.answer = ExtractValue(entry, "answer");
            }
            continue;
        }

        if (!inCard) {
            continue;
        }

        if (trimmed.rfind("question:", 0) == 0) {
            currentCard.question = ExtractValue(trimmed, "question");
        } else if (trimmed.rfind("answer:", 0) == 0) {
            currentCard.answer = ExtractValue(trimmed, "answer");
        }
    }

    if (inCard && !currentCard.question.empty() && !currentCard.answer.empty()) {
        cards.push_back(currentCard);
    }

    return cards;
}

std::vector<Card> LoadCards() {
    const std::vector<Card> loadedCards = LoadCardsFromYaml("cards.yaml");
    if (!loadedCards.empty()) {
        return loadedCards;
    }

    return LoadDefaultCards();
}

HFONT CreateDefaultFont(HWND hwnd) {
    HDC hdc = GetDC(hwnd);
    const int pixelHeight = -MulDiv(16, GetDeviceCaps(hdc, LOGPIXELSY), 72);
    ReleaseDC(hwnd, hdc);

    return CreateFontW(pixelHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}

void ApplyFontToControls() {
    const HWND controls[] = {
        g_state.controls.hTopEdit,    g_state.controls.hBottomEdit,
        g_state.controls.hBtnShowAnswer, g_state.controls.hBtnGood,
        g_state.controls.hBtnMeh,     g_state.controls.hBtnBad};

    for (HWND hwnd : controls) {
        SendMessageW(hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(g_state.hFont), TRUE);
    }
}

void LoadCurrentCard(HWND hwnd) {
    if (g_state.cards.empty()) {
        SetWindowTextW(g_state.controls.hTopEdit, L"No cards available.");
        return;
    }

    const Card& card = g_state.cards[g_state.currentCardIndex % g_state.cards.size()];
    SetWindowTextW(g_state.controls.hTopEdit, card.question.c_str());
    SetWindowTextW(g_state.controls.hBottomEdit, L"");
    g_state.answerVisible = false;

    EnableWindow(g_state.controls.hBtnGood, FALSE);
    EnableWindow(g_state.controls.hBtnMeh, FALSE);
    EnableWindow(g_state.controls.hBtnBad, FALSE);
    SetFocus(g_state.controls.hBtnShowAnswer);
}

void ShowAnswer() {
    if (g_state.cards.empty() || g_state.answerVisible) {
        return;
    }

    const Card& card = g_state.cards[g_state.currentCardIndex % g_state.cards.size()];
    SetWindowTextW(g_state.controls.hBottomEdit, card.answer.c_str());
    g_state.answerVisible = true;

    EnableWindow(g_state.controls.hBtnGood, TRUE);
    EnableWindow(g_state.controls.hBtnMeh, TRUE);
    EnableWindow(g_state.controls.hBtnBad, TRUE);
    SetFocus(g_state.controls.hBtnGood);
}

void AdvanceToNextCard(HWND hwnd) {
    if (g_state.cards.empty()) {
        return;
    }

    g_state.currentCardIndex = (g_state.currentCardIndex + 1) % g_state.cards.size();
    if (g_state.currentCardIndex == 0) {
        MessageBoxW(hwnd, L"Reached the end of the deck. Restarting from the beginning.",
                    L"Q/A Trainer", MB_OK | MB_ICONINFORMATION);
    }
    LoadCurrentCard(hwnd);
}

void HandleRating(HWND hwnd, Rating rating) {
    if (!g_state.answerVisible) {
        return;
    }

    AppendRatingToLog(g_state.currentCardIndex, rating);
    AdvanceToNextCard(hwnd);
}

void LayoutControls(HWND hwnd, int width, int height) {
    const int clientWidth = width;
    const int clientHeight = height;

    int availableHeight = clientHeight - BTN_BAR_HEIGHT - REVEAL_HEIGHT - (4 * MARGIN);
    if (availableHeight < 0) {
        availableHeight = 0;
    }

    const int topHeight = static_cast<int>(availableHeight * 0.7);
    int bottomHeight = availableHeight - topHeight;
    if (bottomHeight < 0) {
        bottomHeight = 0;
    }

    const int topY = MARGIN;
    const int topWidth = clientWidth - (2 * MARGIN);
    MoveWindow(g_state.controls.hTopEdit, MARGIN, topY, topWidth, topHeight, TRUE);

    const int revealY = topY + topHeight + MARGIN;
    MoveWindow(g_state.controls.hBtnShowAnswer, MARGIN, revealY, topWidth, REVEAL_HEIGHT, TRUE);

    const int bottomY = revealY + REVEAL_HEIGHT + MARGIN;
    const int bottomWidth = topWidth;
    MoveWindow(g_state.controls.hBottomEdit, MARGIN, bottomY, bottomWidth, bottomHeight, TRUE);

    const int buttonsY = clientHeight - BTN_BAR_HEIGHT - MARGIN;
    const int buttonWidth = (clientWidth - (4 * MARGIN)) / 3;

    MoveWindow(g_state.controls.hBtnBad, MARGIN, buttonsY, buttonWidth, BTN_BAR_HEIGHT, TRUE);
    MoveWindow(g_state.controls.hBtnMeh, MARGIN + buttonWidth + MARGIN, buttonsY, buttonWidth,
               BTN_BAR_HEIGHT, TRUE);
    MoveWindow(g_state.controls.hBtnGood, MARGIN + 2 * (buttonWidth + MARGIN), buttonsY,
               buttonWidth, BTN_BAR_HEIGHT, TRUE);
}

bool HandleKeyDown(HWND hwnd, WPARAM key) {
    switch (key) {
    case VK_ESCAPE:
        PostQuitMessage(0);
        return true;
    case VK_SPACE:
    case VK_RETURN:
        if (!g_state.answerVisible) {
            ShowAnswer();
            return true;
        }
        break;
    case '1':
    case VK_NUMPAD1:
        HandleRating(hwnd, Rating::Bad);
        return true;
    case '2':
    case VK_NUMPAD2:
        HandleRating(hwnd, Rating::Meh);
        return true;
    case '3':
    case VK_NUMPAD3:
        HandleRating(hwnd, Rating::Good);
        return true;
    default:
        break;
    }
    return false;
}

void InitializeDpiAwareness() {
    HMODULE user32 = LoadLibraryW(L"user32.dll");
    if (user32) {
        using SetDpiAwarenessContextFunc = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
        auto setDpiAwarenessContext =
            reinterpret_cast<SetDpiAwarenessContextFunc>(
                GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (setDpiAwarenessContext) {
            setDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        } else {
            SetProcessDPIAware();
        }
        FreeLibrary(user32);
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_state.cards = LoadCards();
        g_state.ratingLog.open("ratings.log", std::ios::out | std::ios::app);

        g_state.controls.hTopEdit = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", nullptr,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL |
                ES_WANTRETURN,
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(ID_TOP_EDIT), GetModuleHandleW(nullptr),
            nullptr);

        g_state.controls.hBottomEdit = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", nullptr,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL |
                ES_WANTRETURN,
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(ID_BOTTOM_EDIT),
            GetModuleHandleW(nullptr), nullptr);

        g_state.controls.hBtnShowAnswer = CreateWindowExW(
            0, L"BUTTON", L"Show Answer", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(ID_BTN_SHOWANSWER),
            GetModuleHandleW(nullptr), nullptr);

        g_state.controls.hBtnGood = CreateWindowExW(
            0, L"BUTTON", L"Good", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0,
            0, hwnd, reinterpret_cast<HMENU>(ID_BTN_GOOD), GetModuleHandleW(nullptr), nullptr);

        g_state.controls.hBtnMeh = CreateWindowExW(
            0, L"BUTTON", L"Meh", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0,
            0, hwnd, reinterpret_cast<HMENU>(ID_BTN_MEH), GetModuleHandleW(nullptr), nullptr);

        g_state.controls.hBtnBad = CreateWindowExW(
            0, L"BUTTON", L"Bad", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0,
            0, hwnd, reinterpret_cast<HMENU>(ID_BTN_BAD), GetModuleHandleW(nullptr), nullptr);

        g_state.hFont = CreateDefaultFont(hwnd);
        ApplyFontToControls();

        LoadCurrentCard(hwnd);
        return 0;
    }
    case WM_SIZE: {
        const int width = LOWORD(lParam);
        const int height = HIWORD(lParam);
        LayoutControls(hwnd, width, height);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        LPMINMAXINFO info = reinterpret_cast<LPMINMAXINFO>(lParam);
        info->ptMinTrackSize.x = MIN_WIDTH;
        info->ptMinTrackSize.y = MIN_HEIGHT;
        return 0;
    }
    case WM_COMMAND: {
        switch (LOWORD(wParam)) {
        case ID_BTN_SHOWANSWER:
            ShowAnswer();
            break;
        case ID_BTN_GOOD:
            HandleRating(hwnd, Rating::Good);
            break;
        case ID_BTN_MEH:
            HandleRating(hwnd, Rating::Meh);
            break;
        case ID_BTN_BAD:
            HandleRating(hwnd, Rating::Bad);
            break;
        default:
            break;
        }
        return 0;
    }
    case WM_DESTROY:
        if (g_state.hFont) {
            DeleteObject(g_state.hFont);
            g_state.hFont = nullptr;
        }
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    InitializeDpiAwareness();

    const wchar_t CLASS_NAME[] = L"QATrainerMainWindow";

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = CLASS_NAME;
    wc.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);

    if (!RegisterClassExW(&wc)) {
        return 0;
    }

    HWND hwnd = CreateWindowExW(0, CLASS_NAME, L"Q/A Trainer", WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, 800, 600, nullptr, nullptr,
                                hInstance, nullptr);

    if (!hwnd) {
        return 0;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN) {
            if (HandleKeyDown(hwnd, msg.wParam)) {
                continue;
            }
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return static_cast<int>(msg.wParam);
}
