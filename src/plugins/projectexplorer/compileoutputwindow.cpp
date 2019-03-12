/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/

#include "compileoutputwindow.h"
#include "buildmanager.h"
#include "showoutputtaskhandler.h"
#include "task.h"
#include "projectexplorer.h"
#include "projectexplorericons.h"
#include "projectexplorersettings.h"
#include "taskhub.h"

#include <coreplugin/outputwindow.h>
#include <coreplugin/find/basetextfind.h>
#include <coreplugin/icore.h>
#include <coreplugin/coreconstants.h>
#include <extensionsystem/pluginmanager.h>
#include <texteditor/texteditorsettings.h>
#include <texteditor/fontsettings.h>
#include <texteditor/behaviorsettings.h>
#include <utils/outputformatter.h>
#include <utils/proxyaction.h>
#include <utils/theme/theme.h>
#include <utils/utilsicons.h>

#include <QCheckBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QToolButton>
#include <QVBoxLayout>

using namespace ProjectExplorer;
using namespace ProjectExplorer::Internal;

namespace {
const char SETTINGS_KEY[] = "ProjectExplorer/CompileOutput/Zoom";
const char C_COMPILE_OUTPUT[] = "ProjectExplorer.CompileOutput";
const char POP_UP_KEY[] = "ProjectExplorer/Settings/ShowCompilerOutput";
const char WRAP_OUTPUT_KEY[] = "ProjectExplorer/Settings/WrapBuildOutput";
const char MAX_LINES_KEY[] = "ProjectExplorer/Settings/MaxBuildOutputLines";
const char OPTIONS_PAGE_ID[] = "C.ProjectExplorer.CompileOutputOptions";
}

namespace ProjectExplorer {
namespace Internal {

class CompileOutputTextEdit : public Core::OutputWindow
{
    Q_OBJECT
public:
    CompileOutputTextEdit(const Core::Context &context) : Core::OutputWindow(context)
    {
        setWheelZoomEnabled(true);

        QSettings *settings = Core::ICore::settings();
        float zoom = settings->value(QLatin1String(SETTINGS_KEY), 0).toFloat();
        setFontZoom(zoom);

        fontSettingsChanged();

        connect(TextEditor::TextEditorSettings::instance(), &TextEditor::TextEditorSettings::fontSettingsChanged,
                this, &CompileOutputTextEdit::fontSettingsChanged);

        connect(Core::ICore::instance(), &Core::ICore::saveSettingsRequested,
                this, &CompileOutputTextEdit::saveSettings);

        setMouseTracking(true);
    }

    void saveSettings()
    {
        QSettings *settings = Core::ICore::settings();
        settings->setValue(QLatin1String(SETTINGS_KEY), fontZoom());
    }

    void addTask(const Task &task, int blocknumber)
    {
        m_taskids.insert(blocknumber, task.taskId);
    }

    void clearTasks()
    {
        m_taskids.clear();
    }
private:
    void fontSettingsChanged()
    {
        setBaseFont(TextEditor::TextEditorSettings::fontSettings().font());
    }

protected:
    void mouseMoveEvent(QMouseEvent *ev) override
    {
        const int line = cursorForPosition(ev->pos()).block().blockNumber();
        if (m_taskids.contains(line) && m_mousePressButton == Qt::NoButton)
            viewport()->setCursor(Qt::PointingHandCursor);
        else
            viewport()->setCursor(Qt::IBeamCursor);
        QPlainTextEdit::mouseMoveEvent(ev);
    }

    void mousePressEvent(QMouseEvent *ev) override
    {
        m_mousePressPosition = ev->pos();
        m_mousePressButton = ev->button();
        QPlainTextEdit::mousePressEvent(ev);
    }

    void mouseReleaseEvent(QMouseEvent *ev) override
    {
        if ((m_mousePressPosition - ev->pos()).manhattanLength() < 4
                && m_mousePressButton == Qt::LeftButton) {
            int line = cursorForPosition(ev->pos()).block().blockNumber();
            if (unsigned taskid = m_taskids.value(line, 0))
                TaskHub::showTaskInEditor(taskid);
        }

        m_mousePressButton = Qt::NoButton;
        QPlainTextEdit::mouseReleaseEvent(ev);
    }

private:
    QHash<int, unsigned int> m_taskids;   //Map blocknumber to taskId
    QPoint m_mousePressPosition;
    Qt::MouseButton m_mousePressButton = Qt::NoButton;
};

} // namespace Internal
} // namespace ProjectExplorer

CompileOutputWindow::CompileOutputWindow(QAction *cancelBuildAction) :
    m_cancelBuildButton(new QToolButton),
    m_zoomInButton(new QToolButton),
    m_zoomOutButton(new QToolButton),
    m_settingsButton(new QToolButton),
    m_formatter(new Utils::OutputFormatter)
{
    Core::Context context(C_COMPILE_OUTPUT);
    m_outputWindow = new CompileOutputTextEdit(context);
    m_outputWindow->setWindowTitle(displayName());
    m_outputWindow->setWindowIcon(Icons::WINDOW.icon());
    m_outputWindow->setReadOnly(true);
    m_outputWindow->setUndoRedoEnabled(false);
    m_outputWindow->setMaxCharCount(Core::Constants::DEFAULT_MAX_CHAR_COUNT);
    m_outputWindow->setFormatter(m_formatter);

    // Let selected text be colored as if the text edit was editable,
    // otherwise the highlight for searching is too light
    QPalette p = m_outputWindow->palette();
    QColor activeHighlight = p.color(QPalette::Active, QPalette::Highlight);
    p.setColor(QPalette::Highlight, activeHighlight);
    QColor activeHighlightedText = p.color(QPalette::Active, QPalette::HighlightedText);
    p.setColor(QPalette::HighlightedText, activeHighlightedText);
    m_outputWindow->setPalette(p);

    Utils::ProxyAction *cancelBuildProxyButton =
            Utils::ProxyAction::proxyActionWithIcon(cancelBuildAction,
                                                    Utils::Icons::STOP_SMALL_TOOLBAR.icon());
    m_cancelBuildButton->setDefaultAction(cancelBuildProxyButton);
    m_zoomInButton->setToolTip(tr("Increase Font Size"));
    m_zoomInButton->setIcon(Utils::Icons::PLUS_TOOLBAR.icon());
    m_zoomOutButton->setToolTip(tr("Decrease Font Size"));
    m_zoomOutButton->setIcon(Utils::Icons::MINUS.icon());
    m_settingsButton->setToolTip(tr("Open Settings Page"));
    m_settingsButton->setIcon(Utils::Icons::SETTINGS_TOOLBAR.icon());

    updateZoomEnabled();

    connect(TextEditor::TextEditorSettings::instance(),
            &TextEditor::TextEditorSettings::behaviorSettingsChanged,
            this, &CompileOutputWindow::updateZoomEnabled);

    connect(m_zoomInButton, &QToolButton::clicked,
            this, [this]() { m_outputWindow->zoomIn(1); });
    connect(m_zoomOutButton, &QToolButton::clicked,
            this, [this]() { m_outputWindow->zoomOut(1); });
    connect(m_settingsButton, &QToolButton::clicked, this, [] {
        Core::ICore::showOptionsDialog(OPTIONS_PAGE_ID);
    });

    auto agg = new Aggregation::Aggregate;
    agg->add(m_outputWindow);
    agg->add(new Core::BaseTextFind(m_outputWindow));

    qRegisterMetaType<QTextCharFormat>("QTextCharFormat");

    m_handler = new ShowOutputTaskHandler(this);
    ExtensionSystem::PluginManager::addObject(m_handler);
    loadSettings();
    updateFromSettings();
}

CompileOutputWindow::~CompileOutputWindow()
{
    ExtensionSystem::PluginManager::removeObject(m_handler);
    delete m_handler;
    delete m_cancelBuildButton;
    delete m_zoomInButton;
    delete m_zoomOutButton;
    delete m_settingsButton;
    delete m_formatter;
}

void CompileOutputWindow::updateZoomEnabled()
{
    const TextEditor::BehaviorSettings &settings
            = TextEditor::TextEditorSettings::behaviorSettings();
    bool zoomEnabled  = settings.m_scrollWheelZooming;
    m_zoomInButton->setEnabled(zoomEnabled);
    m_zoomOutButton->setEnabled(zoomEnabled);
    m_outputWindow->setWheelZoomEnabled(zoomEnabled);
}

void CompileOutputWindow::updateFromSettings()
{
    m_outputWindow->setWordWrapEnabled(m_settings.wrapOutput);
    m_outputWindow->setMaxCharCount(m_settings.maxCharCount);
}

bool CompileOutputWindow::hasFocus() const
{
    return m_outputWindow->window()->focusWidget() == m_outputWindow;
}

bool CompileOutputWindow::canFocus() const
{
    return true;
}

void CompileOutputWindow::setFocus()
{
    m_outputWindow->setFocus();
}

QWidget *CompileOutputWindow::outputWidget(QWidget *)
{
    return m_outputWindow;
}

QList<QWidget *> CompileOutputWindow::toolBarWidgets() const
{
     return {m_cancelBuildButton, m_zoomInButton, m_zoomOutButton, m_settingsButton};
}

void CompileOutputWindow::appendText(const QString &text, BuildStep::OutputFormat format)
{
    Utils::OutputFormat fmt = Utils::NormalMessageFormat;
    switch (format) {
    case BuildStep::OutputFormat::Stdout:
        fmt = Utils::StdOutFormat;
        break;
    case BuildStep::OutputFormat::Stderr:
        fmt = Utils::StdErrFormat;
        break;
    case BuildStep::OutputFormat::NormalMessage:
        fmt = Utils::NormalMessageFormat;
        break;
    case BuildStep::OutputFormat::ErrorMessage:
        fmt = Utils::ErrorMessageFormat;
        break;

    }

    m_outputWindow->appendMessage(text, fmt);
}

void CompileOutputWindow::clearContents()
{
    m_outputWindow->clear();
    m_outputWindow->clearTasks();
    m_taskPositions.clear();
}

void CompileOutputWindow::visibilityChanged(bool)
{ }

int CompileOutputWindow::priorityInStatusBar() const
{
    return 50;
}

bool CompileOutputWindow::canNext() const
{
    return false;
}

bool CompileOutputWindow::canPrevious() const
{
    return false;
}

void CompileOutputWindow::goToNext()
{ }

void CompileOutputWindow::goToPrev()
{ }

bool CompileOutputWindow::canNavigate() const
{
    return false;
}

void CompileOutputWindow::registerPositionOf(const Task &task, int linkedOutputLines, int skipLines)
{
    if (linkedOutputLines <= 0)
        return;
    const int charNumber = m_outputWindow->document()->characterCount();
    if (charNumber > m_outputWindow->maxCharCount())
        return;

    const int blocknumber = m_outputWindow->document()->blockCount();
    const int startLine = blocknumber - linkedOutputLines + 1 - skipLines;
    const int endLine = blocknumber - skipLines;

    m_taskPositions.insert(task.taskId, qMakePair(startLine, endLine));

    for (int i = startLine; i <= endLine; ++i)
        m_outputWindow->addTask(task, i);
}

bool CompileOutputWindow::knowsPositionOf(const Task &task)
{
    return (m_taskPositions.contains(task.taskId));
}

void CompileOutputWindow::showPositionOf(const Task &task)
{
    QPair<int, int> position = m_taskPositions.value(task.taskId);
    QTextCursor newCursor(m_outputWindow->document()->findBlockByNumber(position.second));

    // Move cursor to end of last line of interest:
    newCursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::MoveAnchor);
    m_outputWindow->setTextCursor(newCursor);

    // Move cursor and select lines:
    newCursor.setPosition(m_outputWindow->document()->findBlockByNumber(position.first).position(),
                          QTextCursor::KeepAnchor);
    m_outputWindow->setTextCursor(newCursor);

    // Center cursor now:
    m_outputWindow->centerCursor();
}

void CompileOutputWindow::flush()
{
    m_formatter->flush();
}

void CompileOutputWindow::setSettings(const CompileOutputSettings &settings)
{
    m_settings = settings;
    storeSettings();
    updateFromSettings();
}

void CompileOutputWindow::loadSettings()
{
    QSettings * const s = Core::ICore::settings();
    m_settings.popUp = s->value(POP_UP_KEY, false).toBool();
    m_settings.wrapOutput = s->value(WRAP_OUTPUT_KEY, true).toBool();
    m_settings.maxCharCount = s->value(MAX_LINES_KEY,
                                       Core::Constants::DEFAULT_MAX_CHAR_COUNT).toInt() * 100;
}

void CompileOutputWindow::storeSettings() const
{
    QSettings * const s = Core::ICore::settings();
    s->setValue(POP_UP_KEY, m_settings.popUp);
    s->setValue(WRAP_OUTPUT_KEY, m_settings.wrapOutput);
    s->setValue(MAX_LINES_KEY, m_settings.maxCharCount / 100);
}

class CompileOutputSettingsPage::SettingsWidget : public QWidget
{
    Q_DECLARE_TR_FUNCTIONS(ProjectExplorer::Internal::CompileOutputSettingsPage)
public:
    SettingsWidget()
    {
        const CompileOutputSettings &settings = BuildManager::compileOutputSettings();
        m_wrapOutputCheckBox.setText(tr("Word-wrap output"));
        m_wrapOutputCheckBox.setChecked(settings.wrapOutput);
        m_popUpCheckBox.setText(tr("Open pane when building"));
        m_popUpCheckBox.setChecked(settings.popUp);
        m_maxCharsBox.setMaximum(100000000);
        m_maxCharsBox.setValue(settings.maxCharCount);
        const auto layout = new QVBoxLayout(this);
        layout->addWidget(&m_wrapOutputCheckBox);
        layout->addWidget(&m_popUpCheckBox);
        const auto maxCharsLayout = new QHBoxLayout;
        maxCharsLayout->addWidget(new QLabel(tr("Limit output to"))); // TODO: This looks problematic i18n-wise
        maxCharsLayout->addWidget(&m_maxCharsBox);
        maxCharsLayout->addWidget(new QLabel(tr("characters")));
        maxCharsLayout->addStretch(1);
        layout->addLayout(maxCharsLayout);
        layout->addStretch(1);
    }

    CompileOutputSettings settings() const
    {
        CompileOutputSettings s;
        s.wrapOutput = m_wrapOutputCheckBox.isChecked();
        s.popUp = m_popUpCheckBox.isChecked();
        s.maxCharCount = m_maxCharsBox.value();
        return s;
    }

private:
    QCheckBox m_wrapOutputCheckBox;
    QCheckBox m_popUpCheckBox;
    QSpinBox m_maxCharsBox;
};

CompileOutputSettingsPage::CompileOutputSettingsPage()
{
    setId(OPTIONS_PAGE_ID);
    setDisplayName(tr("Compile Output"));
    setCategory(Constants::BUILD_AND_RUN_SETTINGS_CATEGORY);
}

QWidget *CompileOutputSettingsPage::widget()
{
    if (!m_widget)
        m_widget = new SettingsWidget;
    return m_widget;
}

void CompileOutputSettingsPage::apply()
{
    if (m_widget)
        BuildManager::setCompileOutputSettings(m_widget->settings());
}

void CompileOutputSettingsPage::finish()
{
    delete m_widget;
}

#include "compileoutputwindow.moc"
