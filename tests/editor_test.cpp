// GUI test (offscreen): editor open + dirty tracking + live validation.
// Dialog-driven Save/Save As stay thin glue over the tested statics.
#include <QApplication>
#include <QEventLoop>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QComboBox>
#include <QTimer>
#include <iostream>
#include <string>

#include "ui/MainWindow.h"

namespace {
int failures = 0;
void check(bool cond, const std::string& what) {
    if (cond) {
        std::cout << "ok " << what << "\n";
    } else {
        std::cout << "FAIL " << what << "\n";
        failures++;
    }
}
void waitMs(int ms) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}
QLabel* findLabel(yue2_abcedit::MainWindow& win, const std::string& contains) {
    for (auto* l : win.findChildren<QLabel*>())
        if (l->text().toStdString().find(contains) != std::string::npos)
            return l;
    return nullptr;
}
}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    // 1. File helpers round-trip (no widgets).
    QString sink;
    check(!yue2_abcedit::MainWindow::readFileText("/tmp/opencode/no-such-file.abc",
                                             sink),
          "read missing file fails");
    QString back;
    check(yue2_abcedit::MainWindow::writeFileText("/tmp/opencode/ed-roundtrip.abc",
                                             "X:1\n"),
          "write temp file");
    check(yue2_abcedit::MainWindow::readFileText("/tmp/opencode/ed-roundtrip.abc",
                                             back) &&
              back == "X:1\n",
          "read back what was written");
    check(!yue2_abcedit::MainWindow::writeFileText("/tmp/opencode/no-dir/x.abc",
                                              "X:1\n"),
          "write into missing dir fails");

    // 2. Open a real score into the pane (working dir = repo root).
    yue2_abcedit::MainWindow win;
    check(win.openEditorFile("example_code/sample1.abc"), "open sample1.abc");
    check(!win.openEditorFile("/tmp/opencode/no-such-file.abc"),
          "open missing file fails");
    auto* editor = win.findChild<QPlainTextEdit*>();
    check(editor != nullptr, "editor pane exists");
    check(editor && editor->toPlainText().startsWith("X:1"),
          "opened text lands in the pane");
    check(findLabel(win, "sample1.abc") != nullptr,
          "file label shows the opened path");

    // 3. Live validation accepts the opened score...
    waitMs(700);  // live timer is 400ms
    check(findLabel(win, "live PASS") != nullptr,
          "live checker passes opened score");

    // 4. ...flags a broken edit, and marks the file dirty.
    editor->setPlainText(editor->toPlainText() + "C10|\n");
    waitMs(700);
    check(findLabel(win, "live FAIL") != nullptr,
          "live checker fails broken edit");
    QLabel* fileLabel = findLabel(win, "sample1.abc");
    check(fileLabel && fileLabel->text().contains("*"),
          "dirty flag shown after edit");

    // 5. Unified flow on a fresh window (clean editor, so no discard
    // prompt): converting a source file lands the result in the same
    // editor pane (no second copy anywhere).
    yue2_abcedit::MainWindow flow;
    check(flow.convertWithDefaults("tests/data/scale.mid", true),
          "convert scale.mid with defaults");
    auto* flowEditor = flow.findChild<QPlainTextEdit*>();
    QString conv = flowEditor->toPlainText();
    check(conv.startsWith("X:1") && conv.contains("C1D1E1G1"),
          "conversion result lands in the editor");
    waitMs(700);
    check(findLabel(flow, "live PASS") != nullptr,
          "live checker passes the conversion");
    check(flow.convertWithDefaults("tests/data/rebuild.abc", false),
          "convert rebuild.abc with defaults");
    check(flowEditor->toPlainText().contains("% A"),
          "second conversion replaces editor content");

    // 6. Preview button renders the cached live verdict to /tmp/temp.wav.
    {
        yue2_abcedit::MainWindow win2;
        check(win2.openEditorFile("example_code/sample1.abc"),
              "open sample1 for preview");
        waitMs(700);
        QPushButton* preview = nullptr;
        for (auto* b : win2.findChildren<QPushButton*>())
            if (b->text().contains("Preview")) preview = b;
        check(preview != nullptr, "preview button exists");
        if (preview) {
            preview->click();
            waitMs(300);
            check(findLabel(win2, "wrote /tmp/temp.wav") != nullptr,
                  "preview renders /tmp/temp.wav");
        }
    }

    // 7. Font size dropdown drives the editor pane; headers are bold.
    {
        yue2_abcedit::MainWindow win3;
        auto* editor3 = win3.findChild<QPlainTextEdit*>();
        QComboBox* fonts = nullptr;
        for (auto* b : win3.findChildren<QComboBox*>())
            if (b->currentText() ==
                QString::number(yue2_abcedit::MainWindow::kDefaultEditorFontSize))
                fonts = b;
        check(fonts != nullptr, "font size dropdown exists");
        if (fonts && editor3) {
            fonts->setCurrentText("16");
            check(editor3->font().pointSize() == 16,
                  "editor follows font size choice");
            bool buttonFollows = false;
            for (auto* b : win3.findChildren<QPushButton*>())
                if (b->font().pointSize() == 16) buttonFollows = true;
            check(buttonFollows, "buttons follow font size choice");
        }
        bool boldHeader = false;
        for (auto* l : win3.findChildren<QLabel*>())
            if (l->text().startsWith("Step") && l->font().bold())
                boldHeader = true;
        check(boldHeader, "step header is bold");
    }

    if (failures) {
        std::cout << failures << " FAILURE(S)\n";
        return 1;
    }
    std::cout << "ALL EDITOR TESTS PASSED\n";
    return 0;
}
