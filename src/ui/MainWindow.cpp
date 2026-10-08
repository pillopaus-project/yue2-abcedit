#include "ui/MainWindow.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QCloseEvent>
#include <QSplitter>
#include <QVBoxLayout>
#include <QFile>
#include <QTextStream>
#include <QHeaderView>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QUrl>

#include "convert/Exporter.h"
#include "convert/Quantizer.h"
#include "audio/PreviewSound.h"
#include "core/AbcChecker.h"
#include "core/AbcDialect.h"
#include "import/MidiImport.h"
#include "import/StandardAbcReader.h"

namespace yue2_abcedit {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) { buildUi(); }

void MainWindow::buildUi() {
    setWindowTitle("yue2-abcedit — MIDI/ABC to native ABC");
    resize(1100, 750);
    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);
    stepLabel_ = new QLabel("Step 1/5: Import", this);
    QFont stepFont = stepLabel_->font();
    stepFont.setBold(true);
    stepLabel_->setFont(stepFont);
    root->addWidget(stepLabel_);

    auto* split = new QSplitter(Qt::Horizontal, this);
    root->addWidget(split, 1);

    auto* bottomBar = new QHBoxLayout;
    bottomBar->addStretch(1);
    auto* fontLabel = new QLabel("Font size:", this);
    QFont fontLabelFont = fontLabel->font();
    fontLabelFont.setBold(true);
    fontLabel->setFont(fontLabelFont);
    bottomBar->addWidget(fontLabel);
    fontSizeBox_ = new QComboBox(this);
    fontSizeBox_->addItems(
        {"8", "9", "10", "11", "12", "13", "14", "16", "18", "20", "24"});
    fontSizeBox_->setCurrentText(QString::number(kDefaultEditorFontSize));
    bottomBar->addWidget(fontSizeBox_);
    root->addLayout(bottomBar);

    stack_ = new QStackedWidget(this);
    split->addWidget(stack_);
    split->setStretchFactor(0, 3);

    // ---- Page 0: Import ----
    {
        auto* w = new QWidget;
        auto* l = new QVBoxLayout(w);
        auto* browse = new QPushButton("Choose file…", w);
        l->addWidget(browse);
        importPathLabel_ = new QLabel("(no file chosen)", w);
        l->addWidget(importPathLabel_);
        divLabel_ = new QLabel("", w);
        l->addWidget(divLabel_);
        auto* orow = new QHBoxLayout;
        overrideCheck_ = new QCheckBox(
            "Override file resolution (wrong header division)", w);
        overrideSpin_ = new QSpinBox(w);
        overrideSpin_->setRange(24, 960);
        overrideSpin_->setValue(480);
        overrideSpin_->setEnabled(false);
        orow->addWidget(overrideCheck_);
        orow->addWidget(overrideSpin_);
        orow->addWidget(new QLabel("ticks per quarter note", w));
        l->addLayout(orow);
        connect(overrideCheck_, &QCheckBox::toggled, overrideSpin_,
                &QSpinBox::setEnabled);
        auto* next = new QPushButton("Next: track map →", w);
        l->addWidget(next);
        l->addStretch(1);
        connect(browse, &QPushButton::clicked, this,
                &MainWindow::onBrowseImport);
        connect(next, &QPushButton::clicked, this, &MainWindow::onImportNext);
        stack_->addWidget(w);
    }
    // ---- Page 1: Mapper ----
    {
        auto* w = new QWidget;
        auto* l = new QVBoxLayout(w);
        l->addWidget(new QLabel(
            "Map each source track/voice to Vocal, Ins, or Ignore.\n"
            "Drums are ignored by default but stay visible."));
        mapTable_ = new QTableWidget(w);
        mapTable_->setColumnCount(6);
        mapTable_->setHorizontalHeaderLabels(
            {"Source", "Program", "Notes", "Drums", "Lyrics", "Assign"});
        mapTable_->horizontalHeader()->setStretchLastSection(true);
        l->addWidget(mapTable_, 1);
        auto* row = new QHBoxLayout;
        auto* back = new QPushButton("← Back", w);
        auto* next = new QPushButton("Next: quantize →", w);
        row->addWidget(back);
        row->addWidget(next);
        l->addLayout(row);
        connect(back, &QPushButton::clicked, this,
                [this] { gotoPage(0); });
        connect(next, &QPushButton::clicked, this, &MainWindow::onMapperNext);
        stack_->addWidget(w);
    }
    // ---- Page 2: Quantize preview ----
    {
        auto* w = new QWidget;
        auto* l = new QVBoxLayout(w);
        quantLabel_ = new QLabel("L: not chosen yet", w);
        l->addWidget(quantLabel_);
        snapCheck_ = new QCheckBox(
            "Allow timing snaps (closest writable position; "
            "triplet feels flagged per bar)", w);
        snapCheck_->setChecked(false);
        l->addWidget(snapCheck_);
        quantLog_ = new QPlainTextEdit(w);
        quantLog_->setReadOnly(true);
        l->addWidget(quantLog_, 1);
        auto* row = new QHBoxLayout;
        auto* back = new QPushButton("← Back", w);
        auto* run = new QPushButton("Run quantize", w);
        auto* next = new QPushButton("Next: validate →", w);
        row->addWidget(back);
        row->addWidget(run);
        row->addWidget(next);
        l->addLayout(row);
        connect(back, &QPushButton::clicked, this,
                [this] { gotoPage(1); });
        connect(run, &QPushButton::clicked, this, &MainWindow::onQuantizeRun);
        connect(next, &QPushButton::clicked, this,
                [this] { gotoPage(3); });
        stack_->addWidget(w);
    }
    // ---- Page 3: Validate ----
    {
        auto* w = new QWidget;
        auto* l = new QVBoxLayout(w);
        validateStatus_ = new QLabel("Not validated yet", w);
        l->addWidget(validateStatus_);
        validateView_ = new QPlainTextEdit(w);
        validateView_->setReadOnly(true);
        validateView_->setPlaceholderText("Exported ABC will appear here…");
        l->addWidget(validateView_, 1);
        auto* row = new QHBoxLayout;
        auto* back = new QPushButton("← Back", w);
        auto* run = new QPushButton("Validate now", w);
        auto* next = new QPushButton("Next: export →", w);
        row->addWidget(back);
        row->addWidget(run);
        row->addWidget(next);
        l->addLayout(row);
        connect(back, &QPushButton::clicked, this,
                [this] { gotoPage(2); });
        connect(run, &QPushButton::clicked, this, &MainWindow::onValidateRun);
        connect(next, &QPushButton::clicked, this,
                [this] { gotoPage(4); });
        stack_->addWidget(w);
    }
    // ---- Page 4: Export ----
    {
        auto* w = new QWidget;
        auto* l = new QVBoxLayout(w);
        auto* browse = new QPushButton("Choose output path…", w);
        l->addWidget(browse);
        exportPathLabel_ = new QLabel("(no output chosen)", w);
        l->addWidget(exportPathLabel_);
        auto* run = new QPushButton("Export (refuses overwrite w/o confirm)",
                                    w);
        l->addWidget(run);
        l->addStretch(1);
        auto* back = new QPushButton("← Back", w);
        l->addWidget(back);
        connect(browse, &QPushButton::clicked, this,
                &MainWindow::onExportBrowse);
        connect(run, &QPushButton::clicked, this, &MainWindow::onExportRun);
        connect(back, &QPushButton::clicked, this,
                [this] { gotoPage(3); });
        stack_->addWidget(w);
    }

    // ---- Editor pane with live validation ----
    {
        auto* side = new QWidget(this);
        auto* l = new QVBoxLayout(side);
        auto* editorTitle = new QLabel("Editor (live checker):");
        QFont titleFont = editorTitle->font();
        titleFont.setBold(true);
        editorTitle->setFont(titleFont);
        l->addWidget(editorTitle);
        editorFileLabel_ = new QLabel("(untitled)", side);
        l->addWidget(editorFileLabel_);
        editor_ = new QPlainTextEdit(side);
        editor_->setPlaceholderText(
            "Converted output lands here — edit freely, the live checker "
            "watches…");
        l->addWidget(editor_, 1);
        connect(fontSizeBox_, &QComboBox::currentTextChanged, this,
                [this](const QString& sizeText) {
                    bool ok = false;
                    int size = sizeText.toInt(&ok);
                    if (!ok || size < 6 || size > 48) return;
                    applyFontSize(size);
                });
        liveStatus_ = new QLabel("live: —", side);
        l->addWidget(liveStatus_);
        auto* previewBtn = new QPushButton("Preview sound → /tmp/temp.wav", side);
        l->addWidget(previewBtn);
        auto* playRow = new QHBoxLayout;
        auto* playBtn = new QPushButton("▶ Play in app", side);
        auto* stopBtn = new QPushButton("■ Stop", side);
        playRow->addWidget(playBtn);
        playRow->addWidget(stopBtn);
        l->addLayout(playRow);
        chordsCheck_ = new QCheckBox("Sound chords", side);
        chordsCheck_->setChecked(true);
        l->addWidget(chordsCheck_);
        previewStatus_ = new QLabel("preview: —", side);
        l->addWidget(previewStatus_);
        connect(previewBtn, &QPushButton::clicked, this,
                &MainWindow::onPreview);
        connect(playBtn, &QPushButton::clicked, this,
                &MainWindow::onPlayInApp);
        connect(stopBtn, &QPushButton::clicked, this,
                &MainWindow::onStopInApp);
        split->addWidget(side);
        split->setStretchFactor(1, 2);
        liveTimer_ = new QTimer(this);
        liveTimer_->setSingleShot(true);
        liveTimer_->setInterval(400);
        connect(editor_, &QPlainTextEdit::textChanged, this, [this] {
            liveTimer_->start();
            // openEditorFile resets the flag right after loading.
            editorDirty_ = true;
            updateEditorTitle();
        });
        connect(liveTimer_, &QTimer::timeout, this,
                &MainWindow::onEditorChanged);
    }
    for (auto* b : findChildren<QPushButton*>()) {
        QFont f = b->font();
        f.setBold(true);
        b->setFont(f);
    }
    applyFontSize(kDefaultEditorFontSize);
    setCentralWidget(central);
}

void MainWindow::gotoPage(int idx) {
    static const char* names[5] = {"Import", "Mapper", "Quantize preview",
                                   "Validate", "Export"};
    stack_->setCurrentIndex(idx);
    stepLabel_->setText(QString("Step %1/5: %2").arg(idx + 1).arg(names[idx]));
}

void MainWindow::onBrowseImport() {
    // File type comes from the extension, never from a selector: MIDI for
    // .mid/.midi (any case), standard ABC for anything else.
    QString f = QFileDialog::getOpenFileName(
        this, "Choose source", "",
        "All supported (*.mid *.MID *.midi *.MIDI *.abc *.ABC *.txt *.TXT);;"
        "MIDI (*.mid *.MID *.midi *.MIDI);;"
        "ABC (*.abc *.ABC *.txt *.TXT)");
    if (f.isEmpty()) return;
    QString ext;
    int dot = f.lastIndexOf('.');
    if (dot >= 0) ext = f.mid(dot + 1).toLower();
    midiMode_ = (ext == "mid" || ext == "midi");
    importPath_ = f.toStdString();
    importPathLabel_->setText(f);
    overrideCheck_->setChecked(false);
    if (midiMode_) {
        int div = 0;
        std::string err;
        if (MidiImport::readDivision(importPath_, div, err)) {
            divLabel_->setText(
                QString("File declares %1 ticks per quarter note.").arg(div));
            overrideSpin_->setValue(div);
        } else {
            divLabel_->setText("Could not read file header.");
        }
        divLabel_->setVisible(true);
        overrideCheck_->setVisible(true);
        overrideSpin_->setVisible(true);
    } else {
        divLabel_->setText("");
    }
}

void MainWindow::onImportNext() {
    if (importPath_.empty()) {
        QMessageBox::warning(this, "Import", "Choose a source file first.");
        return;
    }
    sources_.clear();
    std::string err;
    bool ok = midiMode_
                  ? MidiImport::probeTracks(importPath_, sources_, err)
                  : StandardAbcReader::probeVoices(importPath_, sources_, err);
    if (!ok) {
        QMessageBox::critical(this, "Import failed",
                              QString::fromStdString(err));
        return;
    }
    refreshMapper();
    gotoPage(1);
}

void MainWindow::refreshMapper() {
    mapTable_->setRowCount(static_cast<int>(sources_.size()));
    for (int i = 0; i < static_cast<int>(sources_.size()); ++i) {
        const auto& s = sources_[i];
        auto* c0 = new QTableWidgetItem(QString::fromStdString(s.name));
        c0->setFlags(c0->flags() & ~Qt::ItemIsEditable);
        auto* c1 = new QTableWidgetItem(
            s.program < 0 ? QString("-") : QString::number(s.program));
        c1->setFlags(c1->flags() & ~Qt::ItemIsEditable);
        auto* c2 = new QTableWidgetItem(QString::number(s.noteCount));
        c2->setFlags(c2->flags() & ~Qt::ItemIsEditable);
        auto* c3 = new QTableWidgetItem(s.isDrum ? "yes" : "no");
        c3->setFlags(c3->flags() & ~Qt::ItemIsEditable);
        auto* c4 = new QTableWidgetItem(s.hasLyrics ? "yes" : "no");
        c4->setFlags(c4->flags() & ~Qt::ItemIsEditable);
        auto* box = new QComboBox(mapTable_);
        box->addItems({"Vocal", "Ins", "Ignore"});
        int idx = s.assign == "Vocal" ? 0 : (s.assign == "Ins" ? 1 : 2);
        box->setCurrentIndex(idx);
        mapTable_->setItem(i, 0, c0);
        mapTable_->setItem(i, 1, c1);
        mapTable_->setItem(i, 2, c2);
        mapTable_->setItem(i, 3, c3);
        mapTable_->setItem(i, 4, c4);
        mapTable_->setCellWidget(i, 5, box);
    }
}

void MainWindow::onMapperNext() {
    // Read assignments back.
    for (int i = 0; i < static_cast<int>(sources_.size()); ++i) {
        auto* box =
            qobject_cast<QComboBox*>(mapTable_->cellWidget(i, 5));
        if (box) sources_[i].assign = box->currentText().toStdString();
    }
    std::string err;
    bool ok = false;
    if (midiMode_) {
        MidiImport::Options opt;
        for (const auto& s : sources_) opt.trackAssign.push_back(s.assign);
        if (overrideCheck_->isChecked())
            opt.divisionOverride = overrideSpin_->value();
        ok = MidiImport::importFile(importPath_, opt, song_, err);
    } else {
        StandardAbcReader::Options opt;
        for (const auto& s : sources_) opt.voiceAssign.push_back(s.assign);
        ok = StandardAbcReader::importFile(importPath_, opt, song_, err);
    }
    if (!ok) {
        QMessageBox::critical(this, "Import failed",
                              QString::fromStdString(err));
        return;
    }
    onQuantizeRun();
    gotoPage(2);
}

bool MainWindow::convertWithDefaults(const std::string& path, bool midi) {
    midiMode_ = midi;
    importPath_ = path;
    sources_.clear();
    std::string err;
    bool probed = midi ? MidiImport::probeTracks(path, sources_, err)
                       : StandardAbcReader::probeVoices(path, sources_, err);
    if (!probed) return false;
    refreshMapper();
    Song song;
    bool ok = false;
    if (midi) {
        MidiImport::Options opt;  // empty: dialect defaults, header trust
        ok = MidiImport::importFile(path, opt, song, err);
    } else {
        StandardAbcReader::Options opt;
        ok = StandardAbcReader::importFile(path, opt, song, err);
    }
    if (!ok) return false;
    song_ = song;
    onQuantizeRun();
    return true;
}

void MainWindow::onQuantizeRun() {
    // Exact grid first; with the snap box checked, explicit closest-slot
    // snapping follows (every move logged per bar, triplet feels flagged).
    // Unchecked means exact only: any off-grid bar refuses. Nothing silent.
    Quantizer::Result qr = snapCheck_->isChecked()
                               ? Quantizer::prepare(song_)
                               : Quantizer::pickL(song_);
    if (!qr.ok) {
        quantLabel_->setText("Quantize REFUSED — no writable grid");
        quantLog_->setPlainText(QString::fromStdString(qr.error) + "\n" +
                                QString::fromStdString(song_.log.renderText()));
        validateStatus_->setText("Quantize refused; fix source or mapping.");
        return;
    }
    unitDenom_ = qr.unitDenom;
    if (qr.snapped)
        quantLabel_->setText(
            QString("Timing snapped into place (%1 timing snaps%2; review "
                    "the log, then approve by continuing)")
                .arg(qr.snapCount)
                .arg(qr.tripletApprox
                         ? QString("; incl. straightened triplet feel in %1 bar(s)")
                               .arg(qr.tripletBars)
                         : QString()));
    else
        quantLabel_->setText(QString("Timing fits exactly")); 
    // Per-bar fit preview.
    QString preview =
        QString("bpm=%1 meter=%2/%3 key=%4 vocal=%5 notes ins=%6 notes\n")
            .arg(song_.bpm)
            .arg(song_.meterN)
            .arg(song_.meterD)
            .arg(QString::fromStdString(song_.key))
            .arg(song_.vocal.size())
            .arg(song_.ins.size());
    preview += QString::fromStdString(song_.log.renderText());
    quantLog_->setPlainText(preview);
    // The conversion result belongs to the editor pane: one file, one
    // flow -- import, convert, optionally edit, validate, export.
    Song copy = song_;
    Exporter::Result er = Exporter::exportSong(copy, unitDenom_);
    song_.log = copy.log;
    if (!er.ok) {
        validateStatus_->setText("Export refused: " +
                                 QString::fromStdString(er.error));
        validateView_->setPlainText("REFUSED:\n" +
                                    QString::fromStdString(er.error) + "\n" +
                                    QString::fromStdString(
                                        song_.log.renderText()));
    } else {
        if (!confirmEditorDiscard()) {
            validateStatus_->setText(
                "Conversion ready but editor kept; re-run Quantize to load "
                "it.");
            validateView_->setPlainText(
                "Conversion withheld: editor has unsaved content.");
            return;
        }
        editor_->setPlainText(QString::fromStdString(er.text));
        editorPath_.clear();  // converted output, not yet saved anywhere
        editorDirty_ = false;
        updateEditorTitle();
        onEditorChanged();  // live-check the fresh conversion immediately
        validateView_->setPlainText(
            "Conversion is in the editor pane — edit it freely (live "
            "checker watches), then run validation.");
        validateStatus_->setText("Converted — review in the editor.");
    }
}

void MainWindow::onValidateRun() {
    // The editor pane is the single source of truth: it holds either the
    // fresh conversion or the user's edited version of it.
    std::string text = editor_->toPlainText().toStdString();
    if (text.empty()) {
        validateStatus_->setText("Nothing to validate yet.");
        return;
    }
    validateView_->setPlainText(QString::fromStdString(text));
    CheckerResult cr = AbcChecker::check(text);
    if (!cr.ok) {
        validateStatus_->setText("FAIL: " +
                                 QString::fromStdString(cr.error));
    } else {
        validateStatus_->setText(
            QString("PASS: %1 bars, %2 vocal notes, %3 ins notes, %4 qn @ %5bpm")
                .arg(cr.vocal.bars.size())
                .arg(cr.vocal.notes.size())
                .arg(cr.ins.notes.size())
                .arg(QString::fromStdString(cr.totalTime.str()))
                .arg(cr.bpm));
    }
}

void MainWindow::onExportBrowse() {
    QString f =
        QFileDialog::getSaveFileName(this, "Output ABC", "", "ABC (*.abc)");
    if (f.isEmpty()) return;
    if (!f.endsWith(".abc", Qt::CaseInsensitive)) f += ".abc";
    exportPath_ = f.toStdString();
    exportPathLabel_->setText(f);
}

void MainWindow::onExportRun() {
    // Exports whatever the editor holds: the fresh conversion or the
    // user's edited version of it. Same overwrite rule as Save as.
    QString text = editor_->toPlainText();
    if (exportPath_.empty()) {
        QMessageBox::warning(this, "Export", "Choose an output path first.");
        return;
    }
    if (text.isEmpty()) {
        QMessageBox::warning(this, "Export", "Nothing to export yet.");
        return;
    }
    QFile existing(QString::fromStdString(exportPath_));
    if (existing.exists()) {
        auto ans = QMessageBox::question(
            this, "Overwrite?",
            "Output file exists. Overwrite requires explicit confirmation.\n"
            "Overwrite?");
        if (ans != QMessageBox::Yes) return;
        existing.remove();
    }
    if (!writeFileText(exportPath_, text)) {
        QMessageBox::critical(this, "Export", "Cannot write output file.");
        return;
    }
    QMessageBox::information(this, "Export", "Wrote output file.");
}

void MainWindow::onEditorChanged() {
    std::string text = editor_->toPlainText().toStdString();
    if (text.empty()) {
        liveStatus_->setText("live: —");
        return;
    }
    CheckerResult cr = AbcChecker::check(text);
    lastResult_ = cr;  // single verdict: the preview renders exactly this
    lastCheckedText_ = text;
    hasLiveResult_ = true;
    if (!cr.ok)
        liveStatus_->setText("live FAIL: " +
                             QString::fromStdString(cr.error));
    else
        liveStatus_->setText(
            QString("live PASS: %1 bars @ %2bpm")
                .arg(cr.vocal.bars.size())
                .arg(cr.bpm));
}

void MainWindow::onPreview() {
    renderPreview();
}

bool MainWindow::renderPreview() {
    std::string text = editor_->toPlainText().toStdString();
    if (text.empty()) {
        previewStatus_->setText("preview: nothing to render yet");
        return false;
    }
    if (!hasLiveResult_ || text != lastCheckedText_) onEditorChanged();
    if (!hasLiveResult_ || !lastResult_.ok) {
        previewStatus_->setText("preview: refused — fix the live message first");
        return false;
    }
    PreviewSound::Result pr = PreviewSound::render(
        lastResult_, "/tmp/temp.wav", chordsCheck_->isChecked());
    if (!pr.ok) {
        previewStatus_->setText("preview REFUSED: " +
                                QString::fromStdString(pr.error));
        return false;
    }
    previewStatus_->setText(
        QString("preview: wrote /tmp/temp.wav (%1s)")
            .arg(pr.seconds, 0, 'f', 1));
    return true;
}

void MainWindow::onPlayInApp() {
    if (!renderPreview()) return;  // refused: status already explains
    if (!player_) {
        player_ = new QMediaPlayer(this);
        audioOut_ = new QAudioOutput(this);
        player_->setAudioOutput(audioOut_);
        connect(player_, &QMediaPlayer::errorOccurred, this, [this] {
            previewStatus_->setText("play error: " +
                                    player_->errorString());
        });
    } else {
        player_->stop();
    }
    player_->setSource(QUrl::fromLocalFile("/tmp/temp.wav"));
    player_->play();
    previewStatus_->setText(previewStatus_->text() + " — playing…");
}

void MainWindow::onStopInApp() {
    if (player_) player_->stop();
}

void MainWindow::applyFontSize(int size) {
    for (auto* w : findChildren<QWidget*>()) {
        QFont f = w->font();
        f.setPointSize(size);
        w->setFont(f);
    }
}

void MainWindow::updateEditorTitle() {
    QString name = editorPath_.empty()
                       ? "(untitled)"
                       : QString::fromStdString(editorPath_);
    if (editorDirty_) name += " *";
    editorFileLabel_->setText(name);
}

bool MainWindow::confirmEditorDiscard() {
    if (!editorDirty_) return true;
    auto ans = QMessageBox::question(
        this, "Unsaved changes",
        "The editor has unsaved changes. Discard them?");
    return ans == QMessageBox::Yes;
}

bool MainWindow::readFileText(const std::string& path, QString& out) {
    QFile f(QString::fromStdString(path));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    QTextStream ts(&f);
    out = ts.readAll();
    return true;
}

bool MainWindow::writeFileText(const std::string& path, const QString& text) {
    QFile f(QString::fromStdString(path));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return false;
    QTextStream ts(&f);
    ts << text;
    f.close();
    return true;
}

bool MainWindow::openEditorFile(const std::string& path) {
    QString text;
    if (!readFileText(path, text)) return false;
    editor_->setPlainText(text);
    editorPath_ = path;
    editorDirty_ = false;
    updateEditorTitle();
    onEditorChanged();  // validate immediately, don't wait for the timer
    return true;
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!confirmEditorDiscard()) {
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
}

}  // namespace yue2_abcedit
