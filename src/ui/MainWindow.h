#pragma once

#include <QMainWindow>
#include <QStackedWidget>
#include <QTableWidget>
#include <QPlainTextEdit>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QTimer>
#include <string>
#include <vector>

#include "core/SongModel.h"
#include "core/AbcChecker.h"

class QCloseEvent;
class QMediaPlayer;
class QAudioOutput;

namespace yue2_abcedit {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    static constexpr int kDefaultEditorFontSize = 13;

    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void buildUi();
    void gotoPage(int idx);
    void onBrowseImport();
    void onImportNext();
    void refreshMapper();
    void onMapperNext();
    void onQuantizeRun();
    void onValidateRun();
    void onExportBrowse();
    void onExportRun();
    void onExportJsonBrowse();
    void onExportJsonRun();
    void onEditorChanged();
    void onPreview();
    void onPlayInApp();
    void onStopInApp();

    QStackedWidget* stack_ = nullptr;
    QLabel* stepLabel_ = nullptr;

    // Import page
    QLabel* importPathLabel_ = nullptr;
    QLabel* divLabel_ = nullptr;
    QCheckBox* overrideCheck_ = nullptr;
    QSpinBox* overrideSpin_ = nullptr;
    std::string importPath_;

    // Mapper page
    QTableWidget* mapTable_ = nullptr;
    std::vector<SourceEntry> sources_;
    bool midiMode_ = true;
    // Yue2 JSON mode: "abc" extracted from a .json file, fed through the
    // same StandardAbcReader text pipeline as .abc content.
    bool jsonMode_ = false;
    QString jsonAbcText_;
    std::string jsonOriginPath_;

    // Quantize page
    QLabel* quantLabel_ = nullptr;
    QPlainTextEdit* quantLog_ = nullptr;
    QCheckBox* snapCheck_ = nullptr;

    // Validate page
    QPlainTextEdit* validateView_ = nullptr;
    QLabel* validateStatus_ = nullptr;

    // Export page
    QLabel* exportPathLabel_ = nullptr;
    std::string exportPath_;
    QLabel* exportJsonPathLabel_ = nullptr;
    QPushButton* exportJsonButton_ = nullptr;
    std::string exportJsonPath_;

    // Shared state
    Song song_;
    int unitDenom_ = 32;

    // Editor pane (live validation)
    QPlainTextEdit* editor_ = nullptr;
    QLabel* liveStatus_ = nullptr;
    QLabel* previewStatus_ = nullptr;
    QLabel* editorFileLabel_ = nullptr;
    QComboBox* fontSizeBox_ = nullptr;
    QCheckBox* chordsCheck_ = nullptr;
    QTimer* liveTimer_ = nullptr;
    std::string editorPath_;
    bool editorDirty_ = false;

    // Last live verdict (the single verdict the preview renders).
    CheckerResult lastResult_;
    std::string lastCheckedText_;
    bool hasLiveResult_ = false;

    // In-app playback of the rendered preview (created on first Play).
    QMediaPlayer* player_ = nullptr;
    QAudioOutput* audioOut_ = nullptr;

    // Renders the cached verdict to /tmp/temp.wav. False when refused.
    bool renderPreview();

    // Applies the point size to every widget (editor, buttons, menus).
    void applyFontSize(int size);

public:
    // Loads a file into the editor pane (also used for argv[1] on launch).
    // Returns false when the file cannot be read.
    bool openEditorFile(const std::string& path);

    // Loads a Yue2 JSON file's "abc" field into the editor pane unchanged
    // (stored ABC is already native dialect: no mapper/quantize rebuild).
    // Remembers the origin for Export JSON. False when JSON is invalid.
    bool openJsonAbc(const std::string& path);

    // The one-file flow without dialogs: probe, import with dialect
    // defaults, quantize, load the conversion into the editor. Returns
    // false when probing or importing fails (quantize refusals land in
    // the Quantize page log instead). midi=false means standard ABC.
    bool convertWithDefaults(const std::string& path, bool midi);

    // Plain file helpers behind Open/Save (unit-tested, no widgets).
    static bool readFileText(const std::string& path, QString& out);
    static bool writeFileText(const std::string& path, const QString& text);

private:
    void updateEditorTitle();
    bool confirmEditorDiscard();
};

}  // namespace yue2_abcedit
