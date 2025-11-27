#include <windows.h>
#include <chrono>
#include <algorithm>
#include <codecvt>
#include <cwctype>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <locale>
#include <string>
#include <vector>

struct Card {
    std::wstring id;
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
    std::ofstream answerLog{};
    HFONT hFont{nullptr};
    AppControls controls{};
    HWND hMainWnd{nullptr};
};

namespace {
constexpr int ID_TOP_EDIT = 1001;
constexpr int ID_BOTTOM_EDIT = 1002;
constexpr int ID_BTN_SHOWANSWER = 1003;
constexpr int ID_BTN_GOOD = 1004;
constexpr int ID_BTN_MEH = 1005;
constexpr int ID_BTN_BAD = 1006;
constexpr int ID_MENU_FILE_NEW_CARD = 2001;
constexpr int ID_NEW_CARD_QUESTION = 3001;
constexpr int ID_NEW_CARD_ANSWER = 3002;
constexpr int ID_NEW_CARD_SAVE = 3003;

constexpr int BTN_BAR_HEIGHT = 40;
constexpr int REVEAL_HEIGHT = 40;
constexpr int MARGIN = 10;
constexpr int MIN_WIDTH = 640;
constexpr int MIN_HEIGHT = 480;

AppState g_state;
constexpr wchar_t MAIN_WINDOW_CLASS_NAME[] = L"QATrainerMainWindow";
constexpr wchar_t NEW_CARD_WINDOW_CLASS_NAME[] = L"QATrainerNewCardWindow";
}

std::wstring ToWide(const std::string& text) {
    static std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    return converter.from_bytes(text);
}

std::string ToUtf8(const std::wstring& text) {
    static std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    return converter.to_bytes(text);
}

std::wstring TrimWide(const std::wstring& text) {
    const auto first = text.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) {
        return L"";
    }

    const auto last = text.find_last_not_of(L" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::string RatingToText(Rating rating) {
    switch (rating) {
    case Rating::Good:
        return "good";
    case Rating::Meh:
        return "meh";
    case Rating::Bad:
        return "bad";
    default:
        return "unknown";
    }
}

bool IsCardComplete(const Card& card) {
    return !card.id.empty() && !card.question.empty() && !card.answer.empty();
}

void AppendRatingToLog(const Card& card, Rating rating) {
    if (!g_state.answerLog.is_open() || card.id.empty()) {
        return;
    }

    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
    localtime_s(&localTime, &time);

    g_state.answerLog << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S")
                      << '|' << ToUtf8(card.id) << '|' << RatingToText(rating) << "\n";
    g_state.answerLog.flush();
}

std::vector<Card> LoadDefaultCards() {
    return {
        {L"capital-france", L"What is the capital of France?", L"Paris"},
        {L"math-basic-2-plus-2", L"What is 2 + 2?", L"4"},
        {L"largest-planet", L"Name the largest planet in our solar system.", L"Jupiter"},
    };
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
            if (inCard && IsCardComplete(currentCard)) {
                cards.push_back(currentCard);
            }

            currentCard = Card{};
            inCard = true;

            const std::string entry = Trim(trimmed.substr(2));
            if (entry.rfind("id:", 0) == 0) {
                currentCard.id = ExtractValue(entry, "id");
            } else if (entry.rfind("question:", 0) == 0) {
                currentCard.question = ExtractValue(entry, "question");
            } else if (entry.rfind("answer:", 0) == 0) {
                currentCard.answer = ExtractValue(entry, "answer");
            }
            continue;
        }

        if (!inCard) {
            continue;
        }

        if (trimmed.rfind("id:", 0) == 0) {
            currentCard.id = ExtractValue(trimmed, "id");
        } else if (trimmed.rfind("question:", 0) == 0) {
            currentCard.question = ExtractValue(trimmed, "question");
        } else if (trimmed.rfind("answer:", 0) == 0) {
            currentCard.answer = ExtractValue(trimmed, "answer");
        }
    }

    if (inCard && IsCardComplete(currentCard)) {
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

    const Card& card = g_state.cards[g_state.currentCardIndex % g_state.cards.size()];
    AppendRatingToLog(card, rating);
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

bool IdExists(const std::wstring& id) {
    return std::any_of(g_state.cards.begin(), g_state.cards.end(),
                       [&id](const Card& card) { return card.id == id; });
}

std::wstring GenerateUniqueId() {
    int counter = 1;
    while (true) {
        const std::wstring candidate = L"card-" + std::to_wstring(counter);
        if (!IdExists(candidate)) {
            return candidate;
        }
        ++counter;
    }
}

std::wstring GetWindowTextWString(HWND hwnd) {
    const int length = GetWindowTextLengthW(hwnd);
    if (length <= 0) {
        return L"";
    }

    std::wstring buffer(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(hwnd, buffer.data(), length + 1);
    buffer.resize(length);
    return buffer;
}

struct NewCardWindowState {
    HWND hQuestionEdit{};
    HWND hAnswerEdit{};
    HWND hSaveButton{};
};

void LayoutNewCardControls(const NewCardWindowState& state, int width, int height) {
    const int clientWidth = width;
    const int clientHeight = height;
    int availableHeight = clientHeight - BTN_BAR_HEIGHT - (3 * MARGIN);
    if (availableHeight < 0) {
        availableHeight = 0;
    }

    const int topHeight = static_cast<int>(availableHeight * 0.7);
    const int bottomHeight = availableHeight - topHeight;

    MoveWindow(state.hQuestionEdit, MARGIN, MARGIN, clientWidth - 2 * MARGIN, topHeight, TRUE);
    MoveWindow(state.hAnswerEdit, MARGIN, MARGIN + topHeight + MARGIN,
               clientWidth - 2 * MARGIN, bottomHeight, TRUE);
    MoveWindow(state.hSaveButton, clientWidth - 100 - MARGIN,
               clientHeight - BTN_BAR_HEIGHT, 100, BTN_BAR_HEIGHT - MARGIN, TRUE);
}

void ApplyFontToNewCardControls(const NewCardWindowState& state) {
    const HWND controls[] = {state.hQuestionEdit, state.hAnswerEdit, state.hSaveButton};
    for (HWND control : controls) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g_state.hFont), TRUE);
    }
}

void HandleSaveNewCard(HWND hwnd, const NewCardWindowState& state) {
    std::wstring question = TrimWide(GetWindowTextWString(state.hQuestionEdit));
    std::wstring answer = TrimWide(GetWindowTextWString(state.hAnswerEdit));

    if (question.empty() || answer.empty()) {
        MessageBoxW(hwnd, L"Please enter both a question and an answer before saving.",
                    L"New Card", MB_OK | MB_ICONWARNING);
        return;
    }

    const std::wstring id = GenerateUniqueId();
    g_state.cards.push_back({id, question, answer});
    g_state.currentCardIndex = g_state.cards.size() - 1;
    LoadCurrentCard(g_state.hMainWnd);

    MessageBoxW(hwnd, (L"New card saved with ID: " + id).c_str(), L"New Card",
                MB_OK | MB_ICONINFORMATION);
    DestroyWindow(hwnd);
}

LRESULT CALLBACK NewCardWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<NewCardWindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_CREATE: {
        state = new NewCardWindowState{};
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));

        state->hQuestionEdit = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", nullptr,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(ID_NEW_CARD_QUESTION),
            GetModuleHandleW(nullptr), nullptr);

        state->hAnswerEdit = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", nullptr,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
            0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(ID_NEW_CARD_ANSWER),
            GetModuleHandleW(nullptr), nullptr);

        state->hSaveButton = CreateWindowExW(
            0, L"BUTTON", L"Save", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 0, 0,
            0, 0, hwnd, reinterpret_cast<HMENU>(ID_NEW_CARD_SAVE), GetModuleHandleW(nullptr),
            nullptr);

        ApplyFontToNewCardControls(*state);
        return 0;
    }
    case WM_SIZE: {
        if (state) {
            const int width = LOWORD(lParam);
            const int height = HIWORD(lParam);
            LayoutNewCardControls(*state, width, height);
        }
        return 0;
    }
    case WM_COMMAND: {
        if (LOWORD(wParam) == ID_NEW_CARD_SAVE && state) {
            HandleSaveNewCard(hwnd, *state);
            return 0;
        }
        break;
    }
    case WM_DESTROY: {
        delete state;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return 0;
    }
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void CreateNewCardWindow(HINSTANCE hInstance) {
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, NEW_CARD_WINDOW_CLASS_NAME, L"New Card",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT,
                                CW_USEDEFAULT, 480, 360, g_state.hMainWnd, nullptr, hInstance,
                                nullptr);
    if (hwnd) {
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
    }
}

void InitializeMenu(HWND hwnd) {
    HMENU hMenuBar = CreateMenu();
    HMENU hFileMenu = CreateMenu();
    HMENU hEditMenu = CreateMenu();
    HMENU hViewMenu = CreateMenu();

    AppendMenuW(hFileMenu, MF_STRING, ID_MENU_FILE_NEW_CARD, L"&New Card");

    AppendMenuW(hMenuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(hFileMenu), L"&File");
    AppendMenuW(hMenuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(hEditMenu), L"&Edit");
    AppendMenuW(hMenuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(hViewMenu), L"&View");

    SetMenu(hwnd, hMenuBar);
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
        g_state.answerLog.open("answers.log", std::ios::out | std::ios::app);
        g_state.hMainWnd = hwnd;

        InitializeMenu(hwnd);

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
        case ID_MENU_FILE_NEW_CARD:
            CreateNewCardWindow(GetModuleHandleW(nullptr));
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

    WNDCLASSEXW mainWc{};
    mainWc.cbSize = sizeof(WNDCLASSEXW);
    mainWc.style = CS_HREDRAW | CS_VREDRAW;
    mainWc.lpfnWndProc = WndProc;
    mainWc.hInstance = hInstance;
    mainWc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    mainWc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    mainWc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    mainWc.lpszClassName = MAIN_WINDOW_CLASS_NAME;
    mainWc.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);

    if (!RegisterClassExW(&mainWc)) {
        return 0;
    }

    WNDCLASSEXW newCardWc{};
    newCardWc.cbSize = sizeof(WNDCLASSEXW);
    newCardWc.style = CS_HREDRAW | CS_VREDRAW;
    newCardWc.lpfnWndProc = NewCardWndProc;
    newCardWc.hInstance = hInstance;
    newCardWc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    newCardWc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    newCardWc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    newCardWc.lpszClassName = NEW_CARD_WINDOW_CLASS_NAME;
    newCardWc.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);

    if (!RegisterClassExW(&newCardWc)) {
        return 0;
    }

    HWND hwnd = CreateWindowExW(0, MAIN_WINDOW_CLASS_NAME, L"Q/A Trainer", WS_OVERLAPPEDWINDOW,
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
