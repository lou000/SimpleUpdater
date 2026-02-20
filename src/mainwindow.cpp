#include "mainwindow.h"
#include "updatecontroller.h"
#include <QApplication>
#include <QFileDialog>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPalette>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStyle>
#include <QStyleFactory>
#include <QTextEdit>
#include <QThreadPool>
#include <QTimer>
#include <QVBoxLayout>

QString MainWindow::exeDisplayName(const QString& appExe)
{
    if(appExe.isEmpty())
        return QString();
    QString name = QFileInfo(appExe).completeBaseName();
    if(name.isEmpty())
        name = appExe;
    return name;
}

MainWindow::MainWindow(const CliResult& config, QWidget* parent)
    : QMainWindow(parent)
    , m_controller(new UpdateController(this))
{
    bool isInstall = (config.mode == AppMode::Install);

    if(isInstall)
    {
        auto& inst = config.install.value();
        m_controller->setSourceDir(inst.sourceDir.value_or(QDir()));
        if(inst.targetDir)
            m_controller->setTargetDir(inst.targetDir.value());
        m_controller->setInstallMode(true);
    }
    else
    {
        auto& upd = config.update.value();
        if(isUrl(upd.source))
            m_controller->setSourceUrl(upd.source);
        else
            m_controller->setSourceDir(QDir(upd.source));
        m_controller->setTargetDir(upd.targetDir);
        m_controller->setForceUpdate(upd.forceUpdate);
        m_controller->setContinueUpdate(upd.continueUpdate);
    }

    m_controller->prepare();

    bool forceUpdate = m_controller->isMandatory();
    const auto& srcManifest = m_controller->sourceManifest();
    auto tgtVersion = m_controller->targetVersion();

    QString newVersion = !srcManifest.version.isNull()
                             ? srcManifest.version.toString() : "?.?.?";
    QString appName = exeDisplayName(srcManifest.appExe);
    if(appName.isEmpty())
        appName = tr("Application");

    setWindowTitle(appName + (isInstall ? tr(" Install") : tr(" Update")));

    QPalette pal = palette();
    QString colBase     = pal.color(QPalette::Base).name();
    QString colMid      = pal.color(QPalette::Mid).name();
    QString colMidlight = pal.color(QPalette::Midlight).name();
    QString colText     = pal.color(QPalette::WindowText).name();
    QString colDimText  = pal.color(QPalette::Disabled, QPalette::WindowText).name();

    QColor accent = pal.color(QPalette::Active, QPalette::Accent);
    if(!accent.isValid())
        accent = pal.color(QPalette::Highlight);
    QString colAccent      = accent.name();
    QColor accentHover     = accent.lighter(115);
    QColor accentPress     = accent.darker(115);
    QString colAccentHover = accentHover.name();
    QString colAccentPress = accentPress.name();

    auto contrastText = [](const QColor& bg) -> QString {
        double lum = 0.2126 * bg.redF() + 0.7152 * bg.greenF() + 0.0722 * bg.blueF();
        return lum > 0.4 ? QStringLiteral("#000000") : QStringLiteral("#ffffff");
    };
    QString colOnAccent      = contrastText(accent);
    QString colOnAccentHover = contrastText(accentHover);

    QPixmap appIconPixmap;
    QString exePath = m_controller->sourceDir().absoluteFilePath(srcManifest.appExe);
    if(!exePath.isEmpty() && QFileInfo::exists(exePath))
    {
        QFileIconProvider iconProvider;
        QIcon ico = iconProvider.icon(QFileInfo(exePath));
        appIconPixmap = ico.pixmap(48, 48);
        if(!appIconPixmap.isNull())
            setWindowIcon(ico);
    }

    resize(600, 440);
    setWindowFlags(windowFlags() & ~Qt::WindowMaximizeButtonHint);
    setFixedSize(size());

    auto* central = new QWidget(this);
    auto* mainLayout = new QVBoxLayout(central);
    mainLayout->setSpacing(0);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // Header
    auto* headerFrame = new QFrame();
    headerFrame->setObjectName("headerPanel");
    headerFrame->setStyleSheet(QString(
        "QFrame#headerPanel { background-color: %1; border-bottom: 1px solid %2; }")
        .arg(colBase, colMid));

    auto* headerOuter = new QHBoxLayout(headerFrame);
    headerOuter->setContentsMargins(24, 20, 24, 20);
    headerOuter->setSpacing(16);

    headerIcon = new QLabel();
    headerIcon->setFixedSize(48, 48);
    headerIcon->setStyleSheet("background: transparent; border: none;");
    if(!appIconPixmap.isNull())
        headerIcon->setPixmap(appIconPixmap);
    else
        headerIcon->setVisible(false);
    headerOuter->addWidget(headerIcon);

    auto* headerTextLayout = new QVBoxLayout();
    headerTextLayout->setSpacing(2);

    headerTitle = new QLabel();
    headerTitle->setStyleSheet("background: transparent; border: none;");
    QFont titleFont = headerTitle->font();
    titleFont.setPointSize(titleFont.pointSize() + 5);
    titleFont.setBold(true);
    headerTitle->setFont(titleFont);

    headerSubtitle = new QLabel();
    headerSubtitle->setStyleSheet(QString(
        "color: %1; background: transparent; border: none;").arg(colDimText));

    headerTextLayout->addWidget(headerTitle);
    headerTextLayout->addWidget(headerSubtitle);
    headerOuter->addLayout(headerTextLayout, 1);

    mainLayout->addWidget(headerFrame);

    stackedWidget = new QStackedWidget();

    // Install screen
    installScreen = new QWidget();
    auto* installLayout = new QVBoxLayout(installScreen);
    installLayout->setContentsMargins(28, 24, 28, 12);
    installLayout->setSpacing(16);

    auto* installInfo = new QLabel(tr("Select the directory where %1 will be installed.").arg(appName));
    installInfo->setWordWrap(true);
    installLayout->addWidget(installInfo);

    auto* pathPanel = new QFrame();
    pathPanel->setObjectName("pathPanel");
    pathPanel->setStyleSheet(QString(
        "QFrame#pathPanel { background-color: %1; border: 1px solid %2; border-radius: 8px; }")
        .arg(colBase, colMid));
    auto* pathPanelLayout = new QVBoxLayout(pathPanel);
    pathPanelLayout->setContentsMargins(14, 12, 14, 12);
    pathPanelLayout->setSpacing(8);

    auto* pathLabel = new QLabel(tr("Destination folder:"));
    pathLabel->setStyleSheet("border: none; background: transparent;");
    QFont pathLabelFont = pathLabel->font();
    pathLabelFont.setBold(true);
    pathLabel->setFont(pathLabelFont);
    pathPanelLayout->addWidget(pathLabel);

    auto* pathRow = new QHBoxLayout();
    pathEdit = new QLineEdit();
    pathEdit->setStyleSheet(QString(
        "QLineEdit { border: 1px solid %1; border-radius: 4px; padding: 5px 8px; }").arg(colMid));
    auto path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir dir(path);
    dir.cdUp();
    pathEdit->setText(dir.filePath(QDir(QApplication::applicationDirPath()).dirName()));
    browseButton = new QPushButton(tr("Browse..."));
    pathRow->addWidget(pathEdit, 1);
    pathRow->addWidget(browseButton);
    pathPanelLayout->addLayout(pathRow);

    installLayout->addWidget(pathPanel);
    installLayout->addStretch();
    stackedWidget->addWidget(installScreen);

    // Update screen
    updateScreen = new QWidget();
    auto* updateLayout = new QVBoxLayout(updateScreen);
    updateLayout->setContentsMargins(28, 24, 28, 12);
    updateLayout->setSpacing(14);

    QString oldVersion = !tgtVersion.isNull() ? tgtVersion.toString() : "?.?.?";
    bool hasChangelog = !srcManifest.changelog.isEmpty();

    if(!hasChangelog)
        updateLayout->addStretch(2);

    auto* versionRow = new QHBoxLayout();
    versionRow->setSpacing(0);
    versionRow->addStretch();

    auto* fromBadge = new QLabel(oldVersion);
    fromBadge->setAlignment(Qt::AlignCenter);
    fromBadge->setStyleSheet(QString(
        "background-color: %1; color: %2; border: 1px solid %3; "
        "border-radius: 12px; padding: 5px 16px; font-weight: bold;")
        .arg(colMidlight, colText, colMid));

    auto* arrowLabel = new QLabel(QStringLiteral("  \u2192  "));
    QFont arrowFont = arrowLabel->font();
    arrowFont.setPointSize(arrowFont.pointSize() + 3);
    arrowLabel->setFont(arrowFont);

    auto* toBadge = new QLabel(newVersion);
    toBadge->setAlignment(Qt::AlignCenter);
    toBadge->setStyleSheet(QString(
        "background-color: %1; color: %2; border: 1px solid %1; "
        "border-radius: 12px; padding: 5px 16px; font-weight: bold;")
        .arg(colAccent, colOnAccent));

    versionRow->addWidget(fromBadge);
    versionRow->addWidget(arrowLabel);
    versionRow->addWidget(toBadge);
    versionRow->addStretch();

    updateLayout->addLayout(versionRow);

    QString forceUpdateText;
    if(forceUpdate)
        forceUpdateText = tr("This update is mandatory and cannot be skipped.");
    else
        forceUpdateText = tr("This update can be skipped. Press \"%1\" to launch without updating.")
                              .arg(tr("Update Later"));

    auto* updateNote = new QLabel(forceUpdateText);
    updateNote->setWordWrap(true);
    updateNote->setAlignment(Qt::AlignCenter);
    updateNote->setStyleSheet(QString("color: %1;").arg(colDimText));
    updateLayout->addWidget(updateNote);

    if(!hasChangelog)
    {
        updateLayout->addStretch(3);
    }
    else
    {
        auto* changelogLabel = new QLabel(tr("What's new:"));
        QFont clFont = changelogLabel->font();
        clFont.setBold(true);
        changelogLabel->setFont(clFont);
        updateLayout->addWidget(changelogLabel);

        auto* changelogBox = new QTextEdit();
        changelogBox->setReadOnly(true);
        changelogBox->setPlainText(srcManifest.changelog);
        changelogBox->setFrameShape(QFrame::NoFrame);
        changelogBox->setStyleSheet(QString(
            "QTextEdit { background-color: %1; border: 1px solid %2; "
            "border-radius: 6px; padding: 8px; }").arg(colBase, colMid));
        updateLayout->addWidget(changelogBox, 1);
    }

    stackedWidget->addWidget(updateScreen);

    // Progress screen
    progressScreen = new QWidget();
    auto* progressLayout = new QVBoxLayout(progressScreen);
    progressLayout->setContentsMargins(28, 24, 28, 12);
    progressLayout->setSpacing(12);

    progressBar = new QProgressBar();
    QStyle* fusionStyle = QStyleFactory::create("Fusion");
    if(fusionStyle)
        progressBar->setStyle(fusionStyle);
    QPalette progPal = progressBar->palette();
    progPal.setColor(QPalette::Highlight, accent);
    progressBar->setPalette(progPal);
    progressBar->setTextVisible(true);
    progressBar->setRange(0, 100);
    progressLayout->addWidget(progressBar);

    logBox = new QTextEdit();
    logBox->setReadOnly(true);
    logBox->setFrameShape(QFrame::NoFrame);
    logBox->setStyleSheet(QString(
        "QTextEdit { background-color: %1; border: 1px solid %2; border-radius: 6px; padding: 4px; }")
        .arg(colBase, colMid));
    QFont monoFont("Courier");
    monoFont.setStyleHint(QFont::Monospace);
    logBox->setFont(monoFont);
    progressLayout->addWidget(logBox, 1);

    stackedWidget->addWidget(progressScreen);
    mainLayout->addWidget(stackedWidget, 1);

    // Button bar
    auto* buttonSep = new QFrame();
    buttonSep->setFrameShape(QFrame::HLine);
    buttonSep->setFrameShadow(QFrame::Sunken);
    mainLayout->addWidget(buttonSep);

    auto* buttonBar = new QWidget();
    auto* buttonLayout = new QHBoxLayout(buttonBar);
    buttonLayout->setContentsMargins(28, 12, 28, 14);
    buttonLayout->addStretch();

    updateLaterButton = new QPushButton(tr("Update Later"));
    cancelButton = new QPushButton(tr("Cancel"));
    continueButton = new QPushButton(tr("Continue"));
    quitButton = new QPushButton(tr("Quit"));

    continueButton->setDefault(true);
    continueButton->setStyleSheet(QString(
        "QPushButton { background-color: %1; color: %2; border: 1px solid %3; "
        "border-radius: 4px; padding: 6px 24px; font-weight: bold; }"
        "QPushButton:hover { background-color: %4; color: %5; }"
        "QPushButton:pressed { background-color: %6; }")
        .arg(colAccent, colOnAccent, accent.darker(120).name(),
             colAccentHover, colOnAccentHover, colAccentPress));

    buttonLayout->addWidget(updateLaterButton);
    buttonLayout->addWidget(quitButton);
    buttonLayout->addWidget(cancelButton);
    buttonLayout->addWidget(continueButton);

    updateLaterButton->setVisible(!forceUpdate);
    cancelButton->setVisible(false);

    mainLayout->addWidget(buttonBar);
    central->setLayout(mainLayout);
    setCentralWidget(central);

    if(isInstall)
    {
        headerTitle->setText(tr("Install %1").arg(appName));
        QString sourceDesc;
        if(config.install->sourceDir)
            sourceDesc = config.install->sourceDir->absolutePath();
        headerSubtitle->setText(tr("Version %1  \u2014  %2").arg(newVersion, sourceDesc));

        stackedWidget->setCurrentIndex(0);
        updateLaterButton->setVisible(false);
    }
    else
    {
        headerTitle->setText(tr("Update Available"));
        headerSubtitle->setText(tr("%1 will be updated to version %2").arg(appName, newVersion));
        stackedWidget->setCurrentIndex(1);
    }

    connect(quitButton, &QPushButton::clicked, [](){QApplication::quit();});

    connect(updateLaterButton, &QPushButton::clicked, this, [this](){
        auto& srcManifest = m_controller->sourceManifest();
        if(!srcManifest.appExe.isEmpty())
        {
            QDir tgtDir = m_controller->targetDir();
            QString absPath = tgtDir.absoluteFilePath(srcManifest.appExe);
            if(QFileInfo::exists(absPath))
                QProcess::startDetached(absPath, {"--update_skipped"}, tgtDir.absolutePath());
        }
        QApplication::quit();
    });

    connect(browseButton, &QPushButton::clicked, this, [this](){
        QString dir = QFileDialog::getExistingDirectory(this, "Select Directory");
        if(!dir.isEmpty())
            pathEdit->setText(dir);
    });

    connect(continueButton, &QPushButton::clicked, this, [this, isInstall](){
        if(isInstall)
        {
            auto installationDir = QDir(pathEdit->text());
            if(!installationDir.mkpath(pathEdit->text()))
            {
                QMessageBox::critical(this, "Invalid directory",
                                      "The provided directory cannot be created or is inaccessible, check permissions.");
                return;
            }
            m_controller->setTargetDir(installationDir);
            m_controller->prepare();
        }

        headerSubtitle->setText(tr("Please wait..."));
        if(isInstall)
            headerTitle->setText(tr("Installing..."));
        else
            headerTitle->setText(tr("Updating..."));

        stackedWidget->setCurrentIndex(2);
        quitButton->setVisible(false);
        continueButton->setVisible(false);
        updateLaterButton->setVisible(false);
        cancelButton->setVisible(true);

        QThreadPool::globalInstance()->start([this](){
            m_controller->execute();
        });
    });

    connect(cancelButton, &QPushButton::clicked, this, [this](){
        auto answer = QMessageBox::question(this, tr("Interrupt Operation?"),
                                            tr("Are you sure you want to cancel the current operation?\n\n"
                                               "Interrupting it at this stage may leave the application in an unusable or inconsistent state."),
                                            QMessageBox::Yes | QMessageBox::No,
                                            QMessageBox::No);
        if(answer == QMessageBox::Yes)
            m_controller->cancel();
    });

    connect(m_controller, &UpdateController::progressUpdated, this, [this](const QString& description, bool success){
        if(progressBar->value() < progressBar->maximum())
            progressBar->setValue(progressBar->value()+1);
        auto msg = description + (success ? "    OK" : "    ERROR");
        auto clr = success ? Qt::white : Qt::red;
        logMessage(msg, clr);
    });

    connect(m_controller, &UpdateController::progressRangeChanged, this, [this](int min, int max){
        progressBar->setRange(min, max);
    });

    connect(m_controller, &UpdateController::downloadProgress, this, [this](qint64 received, qint64 total){
        if(total > 0)
        {
            progressBar->setRange(0, 100);
            progressBar->setValue(static_cast<int>(received * 100 / total));
        }
        else
        {
            progressBar->setRange(0, 0);
        }
    });

    connect(m_controller, &UpdateController::statusMessage, this, &MainWindow::logMessage);

    connect(m_controller, &UpdateController::processLockDetected, this, [this](const QStringList& processes){
        QString msg = tr("The following processes are locking files that need to be updated:\n\n");
        for(const auto& p : processes)
            msg += "  " + p + "\n";
        msg += tr("\nClose these processes and click Retry, or click Kill All to terminate them.");

        QMessageBox box(this);
        box.setWindowTitle(tr("Files Locked"));
        box.setText(msg);
        box.setIcon(QMessageBox::Warning);
        auto* retryBtn = box.addButton(tr("Retry"), QMessageBox::AcceptRole);
        auto* killBtn = box.addButton(tr("Kill All"), QMessageBox::DestructiveRole);
        box.addButton(tr("Cancel"), QMessageBox::RejectRole);
        box.setDefaultButton(retryBtn);
        box.exec();

        if(box.clickedButton() == killBtn)
            m_controller->respondToLockPrompt(LockAction::KillAll);
        else if(box.clickedButton() == retryBtn)
            m_controller->respondToLockPrompt(LockAction::Retry);
        else
            m_controller->respondToLockPrompt(LockAction::Cancel);
    });

    connect(m_controller, &UpdateController::error, this, [this](const QString& message){
        logMessage(message, Qt::red);
    });

    connect(m_controller, &UpdateController::selfUpdateRelaunch, this, [](){
        QApplication::quit();
    });

    connect(m_controller, &UpdateController::updateFinished, this, [this, isInstall](bool success){
        cancelButton->setVisible(false);
        QString operation = isInstall ? "INSTALLATION" : "UPDATE";

        if(!success)
        {
            headerTitle->setText(tr("Operation Failed"));
            headerSubtitle->setText(tr("See the log below for details."));
            logMessage(operation + " FAILED", Qt::red);
            if(!m_controller->isCancelled())
            {
                QMessageBox::critical(this, tr("Operation failed"),
                                      tr("Installation/update process failed, "
                                         "please refer to the log to see which files could not be copied successfully.\n"
                                         "A backup was created before the operation and can be used for recovery."));
            }
            quitButton->setVisible(true);
        }
        else
        {
            headerTitle->setText(tr("Complete"));
            headerSubtitle->setText(isInstall ? tr("Installation finished successfully.")
                                              : tr("Update finished successfully."));
            logMessage(operation + " COMPLETE", Qt::green);
            progressBar->setValue(progressBar->maximum());
            QTimer::singleShot(300, this, [this](){
                QApplication::quit();
            });
        }
    });

    if(!isInstall && config.update->continueUpdate)
    {
        headerTitle->setText(tr("Updating..."));
        headerSubtitle->setText(tr("Please wait..."));
        stackedWidget->setCurrentIndex(2);
        quitButton->setVisible(false);
        continueButton->setVisible(false);
        updateLaterButton->setVisible(false);
        cancelButton->setVisible(true);

        QThreadPool::globalInstance()->start([this](){
            m_controller->execute();
        });
    }
}

MainWindow::~MainWindow()
{
    m_controller->cancel();
    QThreadPool::globalInstance()->waitForDone();
}

void MainWindow::logMessage(const QString& msg, const QColor& color)
{
    QScrollBar* vScroll = logBox->verticalScrollBar();
    bool atBottom = (vScroll->value() == vScroll->maximum());

    QTextCharFormat format;
    format.setForeground(color);

    QTextCursor cursor = logBox->textCursor();
    cursor.movePosition(QTextCursor::End);

    QFontMetrics metrics(logBox->font());
    int maxWidth = logBox->viewport()->width();
    QString elidedText = metrics.elidedText(msg, Qt::ElideLeft, maxWidth);

    cursor.insertText("\n"+elidedText, format);

    if(atBottom)
        vScroll->setValue(vScroll->maximum());
}
