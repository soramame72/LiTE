#include <QtGui/QApplication>
#include <QtGui/QWidget>
#include <QtGui/QVBoxLayout>
#include <QtGui/QHBoxLayout>
#include <QtGui/QPushButton>
#include <QtGui/QLineEdit>
#include <QtGui/QStackedWidget>
#include <QtGui/QProgressBar>
#include <QtWebKit/QWebView>
#include <QtWebKit/QWebPage>
#include <QtWebKit/QWebFrame>
#include <QtWebKit/QWebHistory>
#include <QtCore/QSettings>
#include <QtCore/QFile>
#include <QtCore/QTextStream>
#include <QtCore/QMap>
#include <QtCore/QDir>
#include <QtCore/QTextCodec>
#include <QtCore/QDateTime>
#include <QtCore/QDebug>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QSslConfiguration>
#include <QtNetwork/QSslError>
#include <QtNetwork/QSslSocket>
#include <QtGui/QMessageBox>
#include <QtGui/QFileDialog>
#include <QtGui/QMenu>
#include <QtGui/QAction>
#include <QtWebKit/QWebHitTestResult>

QMap<QString, QString> g_langMap;
QFile* g_logFile = NULL;
QTextStream* g_logStream = NULL;

void logMessage(const QString& msg) {
    if (g_logStream) {
        *g_logStream << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") << " " << msg << "\n";
        g_logStream->flush();
    }
    qDebug() << msg;
}

struct Theme {
    QString background;
    QString text;
    QString button;
    QString tabActive;
    QString tabInactive;
    QString progressLoad;
    QString progressDownload;
};

Theme g_theme = {"#f0f0f0", "#000000", "#e0e0e0", "#ffffff", "#d0d0d0", "#00ff00", "#0000ff"};

bool loadLanguage(const QString& lang) {
    QString fileName = lang + ".txt";
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    
    QTextStream in(&file);
    in.setCodec("UTF-8");
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith("#")) continue;
        
        int pos = line.indexOf('=');
        if (pos > 0) {
            QString key = line.left(pos).trimmed();
            QString value = line.mid(pos + 1).trimmed();
            g_langMap[key] = value;
        }
    }
    file.close();
    return true;
}

void loadTheme() {
    QFile file("theme.tme");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }
    
    QTextStream in(&file);
    in.setCodec("UTF-8");
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith("#")) continue;
        
        int pos = line.indexOf(':');
        if (pos > 0) {
            QString key = line.left(pos).trimmed();
            QString value = line.mid(pos + 1).trimmed();
            value = value.remove(';').trimmed();
            
            if (key == "background") g_theme.background = value;
            else if (key == "text") g_theme.text = value;
            else if (key == "button") g_theme.button = value;
            else if (key == "tab_active") g_theme.tabActive = value;
            else if (key == "tab_inactive") g_theme.tabInactive = value;
            else if (key == "progress_load") g_theme.progressLoad = value;
            else if (key == "progress_download") g_theme.progressDownload = value;
        }
    }
    file.close();
}

QString detectEncoding(const QByteArray& data) {
    if (data.size() >= 3 && 
        (unsigned char)data[0] == 0xEF && 
        (unsigned char)data[1] == 0xBB && 
        (unsigned char)data[2] == 0xBF) {
        return "UTF-8";
    }
    
    QString preview = QString::fromLatin1(data.left(2048));
    QRegExp charsetRx("charset\\s*=\\s*[\"']?([^\"';\\s>]+)", Qt::CaseInsensitive);
    if (charsetRx.indexIn(preview) >= 0) {
        QString charset = charsetRx.cap(1).toUpper();
        if (charset == "SHIFT_JIS" || charset == "SHIFT-JIS") return "Shift_JIS";
        if (charset == "EUC-JP") return "EUC-JP";
        if (charset == "ISO-2022-JP") return "ISO-2022-JP";
        if (charset.contains("UTF-8") || charset.contains("UTF8")) return "UTF-8";
    }
    
    bool hasHighBytes = false;
    for (int i = 0; i < data.size(); i++) {
        if ((unsigned char)data[i] > 0x7F) {
            hasHighBytes = true;
            break;
        }
    }
    
    if (!hasHighBytes) return "UTF-8";
    
    int sjisScore = 0;
    for (int i = 0; i < data.size() - 1; i++) {
        unsigned char c1 = (unsigned char)data[i];
        unsigned char c2 = (unsigned char)data[i+1];
        if ((c1 >= 0x81 && c1 <= 0x9F) || (c1 >= 0xE0 && c1 <= 0xFC)) {
            if ((c2 >= 0x40 && c2 <= 0x7E) || (c2 >= 0x80 && c2 <= 0xFC)) {
                sjisScore++;
            }
        }
    }
    
    int eucScore = 0;
    for (int i = 0; i < data.size() - 1; i++) {
        unsigned char c1 = (unsigned char)data[i];
        unsigned char c2 = (unsigned char)data[i+1];
        if (c1 >= 0xA1 && c1 <= 0xFE && c2 >= 0xA1 && c2 <= 0xFE) {
            eucScore++;
        }
    }
    
    if (sjisScore > eucScore) return "Shift_JIS";
    if (eucScore > 0) return "EUC-JP";
    
    return "UTF-8";
}

QString generateErrorPage(const QString& title, const QString& message, const QString& url) {
    QString asciiArt;
    if (title.contains("404") || message.contains("found")) {
        asciiArt = 
            "    ___   ___  _  _   \n"
            "   | | | / _ \\| || |  \n"
            "   | |_|| | | | || |_ \n"
            "   |___|_| |_|\\____|  \n";
    } else if (message.contains("timeout") || message.contains("タイムアウト")) {
        asciiArt = 
            "   _____ _                \n"
            "  |_   _(_)_ __  ___  ___ \n"
            "    | | | | '_ \\/ _ \\/ __|\n"
            "    | | | | | | | __/\\__ \\\n"
            "    |_| |_|_| |_|\\___||___/\n";
    } else {
        asciiArt = 
            "   _____                    \n"
            "  | ____|_ __ _ __ ___  _ __\n"
            "  |  _| | '__| '__/ _ \\| '__|\n"
            "  | |___| |  | | | (_) | |  \n"
            "  |_____|_|  |_|  \\___/|_|  \n";
    }
    
    return QString(
        "<html><head><meta charset='UTF-8'><title>%1</title>"
        "<style>body{font-family:'Courier New',monospace;margin:50px;background:#fdfdfd;color:#d4d4d4;}"
        "h1{color:#f44336;}pre{color:#4970f0;font-size:14px;line-height:1.2;}p{color:#1e1e1e;}</style></head>"
        "<body><pre>%2</pre><h1>%1</h1><p>%3</p></body></html>"
    ).arg(title).arg(asciiArt).arg(message).arg(url);
}

class LiteBrowser;

class CustomWebPage : public QWebPage {
    Q_OBJECT
    
private:
    LiteBrowser* browser;
    
public:
    CustomWebPage(LiteBrowser* browser, QObject* parent = 0) : QWebPage(parent), browser(browser) {}
    
protected:
    QWebPage* createWindow(QWebPage::WebWindowType type);
};

class LiteBrowser : public QWidget {
    Q_OBJECT
    
private:
    QVBoxLayout* mainLayout;
    QHBoxLayout* tabBarLayout;
    QHBoxLayout* navLayout;
    QStackedWidget* stackedWidget;
    QLineEdit* urlBar;
    QPushButton* backBtn;
    QPushButton* nextBtn;
    QPushButton* reloadBtn;
    QPushButton* goBtn;
    QProgressBar* progressBar;
    
    QList<QWebView*> webViews;
    QList<QPushButton*> tabButtons;
    QList<QWidget*> tabWidgets;
    QMap<QNetworkReply*, QFile*> downloads;
    int currentTabIndex;
    int maxTabs;
    QString defaultUrl;
    QString contextMenuLinkUrl;
    QString contextMenuImageUrl;
    
public:
    LiteBrowser(const QString& defaultUrl, int maxTabs) 
        : currentTabIndex(0), defaultUrl(defaultUrl), maxTabs(maxTabs) {
        
        setWindowTitle("LiTE 0.0.2x208");
        resize(1024, 768);
        
        mainLayout = new QVBoxLayout(this);
        mainLayout->setSpacing(5);
        mainLayout->setMargin(5);
        
        QWidget* tabBarWidget = new QWidget;
        tabBarLayout = new QHBoxLayout(tabBarWidget);
        tabBarLayout->setSpacing(2);
        tabBarLayout->setMargin(0);
        tabBarLayout->addStretch();
        
        QPushButton* newTabBtn = new QPushButton("+");
        newTabBtn->setFixedSize(30, 30);
        connect(newTabBtn, SIGNAL(clicked()), this, SLOT(addNewTab()));
        tabBarLayout->addWidget(newTabBtn);
        
        mainLayout->addWidget(tabBarWidget);
        
        QWidget* navWidget = new QWidget;
        navLayout = new QHBoxLayout(navWidget);
        navLayout->setSpacing(5);
        navLayout->setMargin(0);
        
        backBtn = new QPushButton(g_langMap["back"]);
        nextBtn = new QPushButton(g_langMap["next"]);
        reloadBtn = new QPushButton(g_langMap["reload"]);
        urlBar = new QLineEdit;
        goBtn = new QPushButton(g_langMap["go"]);
        
        backBtn->setFixedWidth(80);
        nextBtn->setFixedWidth(80);
        reloadBtn->setFixedWidth(100);
        goBtn->setFixedWidth(80);
        
        connect(backBtn, SIGNAL(clicked()), this, SLOT(goBack()));
        connect(nextBtn, SIGNAL(clicked()), this, SLOT(goNext()));
        connect(reloadBtn, SIGNAL(clicked()), this, SLOT(reloadPage()));
        connect(goBtn, SIGNAL(clicked()), this, SLOT(navigateToUrl()));
        connect(urlBar, SIGNAL(returnPressed()), this, SLOT(navigateToUrl()));
        
        navLayout->addWidget(backBtn);
        navLayout->addWidget(nextBtn);
        navLayout->addWidget(reloadBtn);
        navLayout->addWidget(urlBar);
        navLayout->addWidget(goBtn);
        
        mainLayout->addWidget(navWidget);
        
        progressBar = new QProgressBar;
        progressBar->setMaximum(100);
        progressBar->setValue(0);
        progressBar->setTextVisible(false);
        progressBar->setFixedHeight(3);
        progressBar->setVisible(false);
        progressBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        
        QString defaultStyle = QString("QProgressBar { border: none; background-color: transparent; }"
                                      "QProgressBar::chunk { background-color: %1; }")
                                      .arg(g_theme.progressLoad);
        progressBar->setStyleSheet(defaultStyle);
        
        mainLayout->addWidget(progressBar);
        
        stackedWidget = new QStackedWidget;
        mainLayout->addWidget(stackedWidget);
        
        applyTheme();
        
        addNewTab();
    }
    
    ~LiteBrowser() {
        QMapIterator<QNetworkReply*, QFile*> i(downloads);
        while (i.hasNext()) {
            i.next();
            if (i.value()) {
                i.value()->close();
                delete i.value();
            }
        }
    }
    
    void applyTheme() {
        QString bgColor = g_theme.background;
        QString textColor = g_theme.text;
        QString btnColor = g_theme.button;
        
        QString style = QString(
            "QWidget { background-color: %1; color: %2; }"
            "QPushButton { background-color: %3; color: %2; border: 1px solid #888; padding: 5px; }"
            "QLineEdit { background-color: white; color: black; border: 1px solid #888; padding: 3px; }"
        ).arg(bgColor).arg(textColor).arg(btnColor);
        
        setStyleSheet(style);
        
        for (int i = 0; i < tabButtons.size(); i++) {
            updateTabStyle(i);
        }
    }
    
    void updateTabStyle(int index) {
        if (index < 0 || index >= tabButtons.size()) return;
        
        QString style;
        if (index == currentTabIndex) {
            style = QString("background-color: %1; color: %2; border: 1px solid #888; padding: 5px;")
                .arg(g_theme.tabActive).arg(g_theme.text);
        } else {
            style = QString("background-color: %1; color: %2; border: 1px solid #888; padding: 5px;")
                .arg(g_theme.tabInactive).arg(g_theme.text);
        }
        tabButtons[index]->setStyleSheet(style);
    }
    
    void addNewTabWithUrl(const QString& url) {
        if (webViews.size() >= maxTabs) {
            logMessage(QString("[TAB] Cannot create new tab, max tabs reached: %1").arg(maxTabs));
            return;
        }
        
        logMessage(QString("[TAB] Creating new tab with URL: %1").arg(url));
        
        QWebView* webView = new QWebView;
        CustomWebPage* page = new CustomWebPage(this, webView);
        webView->setPage(page);
        
        page->setForwardUnsupportedContent(true);
        page->setLinkDelegationPolicy(QWebPage::DelegateExternalLinks);
        
        QWebSettings* settings = page->settings();
        settings->setAttribute(QWebSettings::PluginsEnabled, true);
        settings->setAttribute(QWebSettings::JavascriptEnabled, true);
        settings->setAttribute(QWebSettings::JavascriptCanOpenWindows, true);
        
        webView->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(webView, SIGNAL(customContextMenuRequested(const QPoint&)), 
                this, SLOT(showContextMenu(const QPoint&)));
        
        connect(page->networkAccessManager(),
                SIGNAL(sslErrors(QNetworkReply*, const QList<QSslError>&)),
                this, SLOT(handleSslErrors(QNetworkReply*, const QList<QSslError>&)));
        
        connect(page->mainFrame(), SIGNAL(javaScriptConsoleMessage(const QString&, int, const QString&)),
                this, SLOT(handleConsoleMessage(const QString&, int, const QString&)));
        connect(page->mainFrame(), SIGNAL(loadFinished(bool)),
                this, SLOT(handleFrameLoadFinished(bool)));
        
        webView->load(QUrl(url));
        
        connect(webView, SIGNAL(urlChanged(const QUrl&)), this, SLOT(updateUrlBar(const QUrl&)));
        connect(webView, SIGNAL(titleChanged(const QString&)), this, SLOT(updateTabTitle(const QString&)));
        connect(webView, SIGNAL(loadFinished(bool)), this, SLOT(onLoadFinished(bool)));
        connect(webView, SIGNAL(loadStarted()), this, SLOT(onLoadStarted()));
        connect(webView, SIGNAL(loadProgress(int)), this, SLOT(onLoadProgress(int)));
        connect(page, SIGNAL(downloadRequested(const QNetworkRequest&)), 
                this, SLOT(handleDownload(const QNetworkRequest&)));
        connect(page, SIGNAL(unsupportedContent(QNetworkReply*)), 
                this, SLOT(handleUnsupportedContent(QNetworkReply*)));
        connect(page, SIGNAL(linkClicked(const QUrl&)), 
                this, SLOT(handleLinkClicked(const QUrl&)));
        
        webViews.append(webView);
        stackedWidget->addWidget(webView);
        
        QWidget* tabWidget = new QWidget;
        QHBoxLayout* tabLayout = new QHBoxLayout(tabWidget);
        tabLayout->setSpacing(2);
        tabLayout->setMargin(2);
        
        QPushButton* tabBtn = new QPushButton(QString("LiTE (0.0.2x208)"));
        tabBtn->setProperty("tabIndex", webViews.size() - 1);
        connect(tabBtn, SIGNAL(clicked()), this, SLOT(switchTab()));
        
        QPushButton* closeBtn = new QPushButton("x");
        closeBtn->setFixedSize(20, 20);
        closeBtn->setProperty("tabIndex", webViews.size() - 1);
        connect(closeBtn, SIGNAL(clicked()), this, SLOT(closeTab()));
        
        tabLayout->addWidget(tabBtn);
        tabLayout->addWidget(closeBtn);
        
        tabButtons.append(tabBtn);
        tabWidgets.append(tabWidget);
        tabBarLayout->insertWidget(tabBarLayout->count() - 2, tabWidget);
        
        switchToTab(webViews.size() - 1);
        
        logMessage(QString("[TAB] New tab created, total tabs: %1").arg(webViews.size()));
    }
    
private slots:
    void handleSslErrors(QNetworkReply* reply, const QList<QSslError>& errors) {
        foreach (const QSslError& error, errors) {
            logMessage(QString("[SSL_WARNING] %1").arg(error.errorString()));
        }
        reply->ignoreSslErrors();
    }
    
    void handleConsoleMessage(const QString& message, int lineNumber, const QString& sourceID) {
        logMessage(QString("[WEB_CONSOLE] %1:%2 - %3").arg(sourceID).arg(lineNumber).arg(message));
    }
    
    void handleFrameLoadFinished(bool ok) {
        QWebFrame* frame = qobject_cast<QWebFrame*>(sender());
        if (frame) {
            logMessage(QString("[FRAME_LOAD] Finished: %1 - Success: %2").arg(frame->url().toString()).arg(ok ? "YES" : "NO"));
        }
    }
    
    void addNewTab() {
        addNewTabWithUrl(defaultUrl);
    }
    
    void handleLinkClicked(const QUrl& url) {
        logMessage(QString("[LINK] Link clicked: %1").arg(url.toString()));
        if (currentTabIndex >= 0 && currentTabIndex < webViews.size()) {
            webViews[currentTabIndex]->load(url);
        }
    }
    
    void showContextMenu(const QPoint& pos) {
        QWebView* webView = qobject_cast<QWebView*>(sender());
        if (!webView) return;
        
        QWebHitTestResult hit = webView->page()->mainFrame()->hitTestContent(pos);
        QMenu menu(this);
        
        contextMenuLinkUrl = hit.linkUrl().toString();
        contextMenuImageUrl = hit.imageUrl().toString();
        
        logMessage(QString("[CONTEXT_MENU] Link: %1, Image: %2").arg(contextMenuLinkUrl).arg(contextMenuImageUrl));
        
        if (!contextMenuLinkUrl.isEmpty()) {
            QAction* openLinkAction = menu.addAction(g_langMap.value("open_link", "Open Link"));
            connect(openLinkAction, SIGNAL(triggered()), this, SLOT(openContextLink()));
            
            QAction* openLinkNewTabAction = menu.addAction(g_langMap.value("open_link_new_tab", "Open Link in New Tab"));
            connect(openLinkNewTabAction, SIGNAL(triggered()), this, SLOT(openContextLinkNewTab()));
            
            QAction* downloadLinkAction = menu.addAction(g_langMap.value("download_link", "Download Link"));
            connect(downloadLinkAction, SIGNAL(triggered()), this, SLOT(downloadContextLink()));
            
            menu.addSeparator();
        }
        
        if (!contextMenuImageUrl.isEmpty()) {
            QAction* downloadImageAction = menu.addAction(g_langMap.value("download_image", "Download Image"));
            connect(downloadImageAction, SIGNAL(triggered()), this, SLOT(downloadContextImage()));
            
            menu.addSeparator();
        }
        
        QAction* backAction = menu.addAction(g_langMap.value("back", "Back"));
        connect(backAction, SIGNAL(triggered()), this, SLOT(goBack()));
        backAction->setEnabled(webView->page()->history()->canGoBack());
        
        QAction* forwardAction = menu.addAction(g_langMap.value("next", "Forward"));
        connect(forwardAction, SIGNAL(triggered()), this, SLOT(goNext()));
        forwardAction->setEnabled(webView->page()->history()->canGoForward());
        
        QAction* reloadAction = menu.addAction(g_langMap.value("reload", "Reload"));
        connect(reloadAction, SIGNAL(triggered()), this, SLOT(reloadPage()));
        
        menu.exec(webView->mapToGlobal(pos));
    }
    
    void openContextLink() {
        if (!contextMenuLinkUrl.isEmpty() && currentTabIndex >= 0 && currentTabIndex < webViews.size()) {
            logMessage(QString("[CONTEXT_ACTION] Opening link: %1").arg(contextMenuLinkUrl));
            webViews[currentTabIndex]->load(QUrl(contextMenuLinkUrl));
        }
    }
    
    void openContextLinkNewTab() {
        if (!contextMenuLinkUrl.isEmpty()) {
            logMessage(QString("[CONTEXT_ACTION] Opening link in new tab: %1").arg(contextMenuLinkUrl));
            addNewTabWithUrl(contextMenuLinkUrl);
        }
    }
    
    void downloadContextLink() {
        if (!contextMenuLinkUrl.isEmpty()) {
            logMessage(QString("[CONTEXT_ACTION] Downloading link: %1").arg(contextMenuLinkUrl));
            startDownload(QUrl(contextMenuLinkUrl));
        }
    }
    
    void downloadContextImage() {
        if (!contextMenuImageUrl.isEmpty()) {
            logMessage(QString("[CONTEXT_ACTION] Downloading image: %1").arg(contextMenuImageUrl));
            startDownload(QUrl(contextMenuImageUrl));
        }
    }
    
    void startDownload(const QUrl& url) {
        QNetworkAccessManager* manager = new QNetworkAccessManager(this);
        connect(manager, SIGNAL(sslErrors(QNetworkReply*, const QList<QSslError>&)),
                this, SLOT(handleSslErrors(QNetworkReply*, const QList<QSslError>&)));
        
        QNetworkRequest request(url);
        QNetworkReply* reply = manager->get(request);
        
        QString urlPath = url.path();
        QString fileName = QFileInfo(urlPath).fileName();
        if (fileName.isEmpty()) {
            fileName = "download";
        }
        
        QString savePath = QFileDialog::getSaveFileName(this, 
            g_langMap.value("save_file", "Save File"), fileName);
        
        if (!savePath.isEmpty()) {
            QFile* file = new QFile(savePath);
            if (file->open(QIODevice::WriteOnly)) {
                downloads.insert(reply, file);
                
                connect(reply, SIGNAL(finished()), this, SLOT(downloadFinished()));
                connect(reply, SIGNAL(readyRead()), this, SLOT(downloadReadyRead()));
                connect(reply, SIGNAL(downloadProgress(qint64, qint64)), 
                        this, SLOT(downloadProgress(qint64, qint64)));
                
                progressBar->setValue(0);
                progressBar->setVisible(true);
                QString style = QString("QProgressBar { border: none; background-color: transparent; }"
                                      "QProgressBar::chunk { background-color: %1; }")
                                      .arg(g_theme.progressDownload);
                progressBar->setStyleSheet(style);
                
                logMessage(QString("[DOWNLOAD] Started download to: %1").arg(savePath));
            } else {
                logMessage(QString("[ERROR] Failed to open file for writing: %1").arg(savePath));
                delete file;
                reply->abort();
                reply->deleteLater();
            }
        } else {
            logMessage("[DOWNLOAD] Download cancelled by user");
            reply->abort();
            reply->deleteLater();
        }
    }
    
    void handleDownload(const QNetworkRequest& request) {
        logMessage(QString("[DOWNLOAD] Download requested: %1").arg(request.url().toString()));
        startDownload(request.url());
    }
    
    void handleUnsupportedContent(QNetworkReply* reply) {
        if (reply->error() != QNetworkReply::NoError) {
            logMessage(QString("[ERROR] Network error in unsupportedContent: %1").arg(reply->errorString()));
            reply->deleteLater();
            return;
        }
        
        QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
        logMessage(QString("[DOWNLOAD] Unsupported content: %1, Type: %2").arg(reply->url().toString()).arg(contentType));
        
        QUrl url = reply->url();
        QString urlPath = url.path();
        QString fileName = QFileInfo(urlPath).fileName();
        
        if (fileName.isEmpty()) {
            fileName = "download";
        }
        
        QString savePath = QFileDialog::getSaveFileName(this, 
            g_langMap.value("save_file", "Save File"), fileName);
        
        if (!savePath.isEmpty()) {
            QFile* file = new QFile(savePath);
            if (file->open(QIODevice::WriteOnly)) {
                QByteArray data = reply->readAll();
                file->write(data);
                file->close();
                logMessage(QString("[DOWNLOAD] Completed immediately: %1 (%2 bytes)").arg(savePath).arg(data.size()));
                delete file;
            } else {
                logMessage(QString("[ERROR] Failed to open file for writing: %1").arg(savePath));
                delete file;
            }
        } else {
            logMessage("[DOWNLOAD] Download cancelled by user");
        }
        reply->deleteLater();
    }
    
    void downloadReadyRead() {
        QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
        if (!reply) return;
        
        if (downloads.contains(reply)) {
            QFile* file = downloads[reply];
            if (file) {
                qint64 bytes = reply->bytesAvailable();
                file->write(reply->readAll());
                logMessage(QString("[DOWNLOAD] Written %1 bytes").arg(bytes));
            }
        }
    }
    
    void downloadFinished() {
        QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
        if (!reply) return;
        
        if (downloads.contains(reply)) {
            QFile* file = downloads[reply];
            if (file) {
                file->write(reply->readAll());
                file->close();
                logMessage(QString("[DOWNLOAD] Completed: %1").arg(file->fileName()));
                delete file;
            }
            downloads.remove(reply);
        }
        
        if (downloads.isEmpty()) {
            progressBar->setVisible(false);
            progressBar->setValue(0);
            logMessage("[DOWNLOAD] All downloads completed");
        }
        
        reply->deleteLater();
    }
    
    void downloadProgress(qint64 received, qint64 total) {
        if (total > 0) {
            int percent = (received * 100) / total;
            progressBar->setValue(percent);
            progressBar->setVisible(true);
            
            if (percent % 10 == 0) {
                logMessage(QString("[DOWNLOAD] Progress: %1% (%2/%3 bytes)").arg(percent).arg(received).arg(total));
            }
            
            QString style = QString("QProgressBar { border: none; background-color: transparent; }"
                                  "QProgressBar::chunk { background-color: %1; }")
                                  .arg(g_theme.progressDownload);
            progressBar->setStyleSheet(style);
        }
    }
    
    void closeTab() {
        QPushButton* btn = qobject_cast<QPushButton*>(sender());
        if (!btn) return;
        
        int index = btn->property("tabIndex").toInt();
        
        if (webViews.size() <= 1) {
            logMessage("[TAB] Cannot close last tab");
            return;
        }
        
        logMessage(QString("[TAB] Closing tab %1").arg(index));
        
        QWidget* tabWidget = tabWidgets.takeAt(index);
        tabBarLayout->removeWidget(tabWidget);
        tabWidget->deleteLater();
        
        QWebView* webView = webViews.takeAt(index);
        stackedWidget->removeWidget(webView);
        webView->deleteLater();
        
        tabButtons.removeAt(index);
        
        for (int i = index; i < webViews.size(); i++) {
            QWidget* tw = tabWidgets[i];
            QList<QPushButton*> buttons = tw->findChildren<QPushButton*>();
            foreach (QPushButton* b, buttons) {
                b->setProperty("tabIndex", i);
            }
        }
        
        if (currentTabIndex >= webViews.size()) {
            currentTabIndex = webViews.size() - 1;
        }
        if (currentTabIndex == index && currentTabIndex > 0) {
            currentTabIndex--;
        }
        
        switchToTab(currentTabIndex);
        
        logMessage(QString("[TAB] Tab closed, total tabs: %1").arg(webViews.size()));
    }
    
    void switchTab() {
        QPushButton* btn = qobject_cast<QPushButton*>(sender());
        if (btn) {
            int index = btn->property("tabIndex").toInt();
            logMessage(QString("[TAB] Switching to tab %1").arg(index));
            switchToTab(index);
        }
    }
    
    void switchToTab(int index) {
        if (index < 0 || index >= webViews.size()) return;
        
        currentTabIndex = index;
        stackedWidget->setCurrentIndex(index);
        
        for (int i = 0; i < tabButtons.size(); i++) {
            updateTabStyle(i);
        }
        
        updateUrlBar(webViews[index]->url());
        logMessage(QString("[TAB] Active tab: %1, URL: %2").arg(index).arg(webViews[index]->url().toString()));
    }
    
    void updateUrlBar(const QUrl& url) {
        QWebView* view = qobject_cast<QWebView*>(sender());
        if (view == webViews[currentTabIndex] || view == NULL) {
            urlBar->setText(url.toString());
        }
    }
    
    void updateTabTitle(const QString& title) {
        QWebView* view = qobject_cast<QWebView*>(sender());
        if (!view) return;
        
        int index = webViews.indexOf(view);
        if (index >= 0 && index < tabButtons.size()) {
            QString displayTitle = title;
            if (displayTitle.isEmpty()) {
                displayTitle = "LiTE (0.0.2x208)";
            } else if (displayTitle.length() > 20) {
                displayTitle = displayTitle.left(17) + "...";
            }
            tabButtons[index]->setText(displayTitle);
            logMessage(QString("[TAB] Tab %1 title updated: %2").arg(index).arg(title));
        }
    }
    
    void onLoadStarted() {
        QWebView* view = qobject_cast<QWebView*>(sender());
        if (!view) return;
        
        int index = webViews.indexOf(view);
        logMessage(QString("[LOAD] Started (Tab %1): %2").arg(index).arg(view->url().toString()));
        
        if (index == currentTabIndex) {
            progressBar->setValue(0);
            progressBar->setVisible(true);
            logMessage("[PROGRESS] Progress bar shown (green)");
            
            QString style = QString("QProgressBar { border: none; background-color: transparent; }"
                                  "QProgressBar::chunk { background-color: %1; }")
                                  .arg(g_theme.progressLoad);
            progressBar->setStyleSheet(style);
        }
    }
    
    void onLoadProgress(int progress) {
        QWebView* view = qobject_cast<QWebView*>(sender());
        if (!view) return;
        
        int index = webViews.indexOf(view);
        if (index == currentTabIndex) {
            progressBar->setValue(progress);
            progressBar->setVisible(true);
            
            if (progress % 20 == 0) {
                logMessage(QString("[PROGRESS] Loading page: %1%").arg(progress));
            }
            
            QString style = QString("QProgressBar { border: none; background-color: transparent; }"
                                  "QProgressBar::chunk { background-color: %1; }")
                                  .arg(g_theme.progressLoad);
            progressBar->setStyleSheet(style);
        }
    }
    
    void onLoadFinished(bool ok) {
        QWebView* view = qobject_cast<QWebView*>(sender());
        if (!view) return;
        
        int index = webViews.indexOf(view);
        logMessage(QString("[LOAD] Finished (Tab %1): %2 - Success: %3").arg(index).arg(view->url().toString()).arg(ok ? "YES" : "NO"));
        
        if (index == currentTabIndex) {
            updateUrlBar(view->url());
            progressBar->setValue(100);
            progressBar->setVisible(false);
            logMessage("[PROGRESS] Progress bar hidden");
        }
        
        if (!ok) {
            logMessage(QString("[ERROR] Failed to load page: %1").arg(view->url().toString()));
            QString errorTitle = g_langMap.value("error_page_title", "Page Load Error");
            QString errorMsg = g_langMap.value("error_page_message", "Failed to load the page. Please check the URL and try again.");
            QString errorPage = generateErrorPage(errorTitle, errorMsg, view->url().toString());
            view->setHtml(errorPage);
        }
    }
    
    void goBack() {
        if (currentTabIndex >= 0 && currentTabIndex < webViews.size()) {
            logMessage(QString("[NAV] Back on tab %1").arg(currentTabIndex));
            webViews[currentTabIndex]->back();
        }
    }
    
    void goNext() {
        if (currentTabIndex >= 0 && currentTabIndex < webViews.size()) {
            logMessage(QString("[NAV] Forward on tab %1").arg(currentTabIndex));
            webViews[currentTabIndex]->forward();
        }
    }
    
    void reloadPage() {
        if (currentTabIndex >= 0 && currentTabIndex < webViews.size()) {
            logMessage(QString("[NAV] Reload on tab %1").arg(currentTabIndex));
            webViews[currentTabIndex]->reload();
        }
    }
    
    void navigateToUrl() {
        if (currentTabIndex >= 0 && currentTabIndex < webViews.size()) {
            QString url = urlBar->text();
            if (!url.startsWith("http://") && !url.startsWith("https://")) {
                url = "http://" + url;
            }
            logMessage(QString("[NAV] Navigate to: %1").arg(url));
            webViews[currentTabIndex]->load(QUrl(url));
        }
    }
};

QWebPage* CustomWebPage::createWindow(QWebPage::WebWindowType type) {
    Q_UNUSED(type);
    QWebView* view = qobject_cast<QWebView*>(this->view());
    if (view) {
        QString url = mainFrame()->requestedUrl().toString();
        if (!url.isEmpty() && browser) {
            browser->addNewTabWithUrl(url);
        }
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    app.setWindowIcon(QIcon("favicon.ico"));
    
    QTextCodec::setCodecForTr(QTextCodec::codecForName("UTF-8"));
    QTextCodec::setCodecForCStrings(QTextCodec::codecForName("UTF-8"));
    
    QSettings settings("settings.ini", QSettings::IniFormat);
    settings.setIniCodec("UTF-8");
    
    QString lang = settings.value("browser/language", "ja").toString();
    QString defaultUrl = settings.value("browser/defaultUrl", 
        "http://mamechosu.cloudfree.jp/software/lite/welcome.html").toString();
    int maxTabs = settings.value("browser/maxTabs", 8).toInt();
    int customTheme = settings.value("browser/custom_theme", 0).toInt();
    int saveLog = settings.value("browser/savelog", 0).toInt();
    
    if (saveLog == 1) {
        g_logFile = new QFile("logfile.log");
        if (g_logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            g_logStream = new QTextStream(g_logFile);
            g_logStream->setCodec("UTF-8");
            logMessage("=== LiTE Browser Started ===");
            logMessage(QString("Default URL: %1").arg(defaultUrl));
            logMessage(QString("Max Tabs: %1").arg(maxTabs));
            logMessage(QString("Language: %1").arg(lang));
            logMessage(QString("Custom Theme: %1").arg(customTheme));
        }
    }
    
    if (!QSslSocket::supportsSsl()) {
        logMessage("[ERROR] SSL support not available!");
        QMessageBox::warning(NULL, "SSL Error", 
            QString::fromUtf8("OpenSSL libraries not found.\nPlease install libeay32.dll and ssleay32.dll"));
    } else {
        logMessage("[SSL] OpenSSL support enabled");
    }
    
    if (!loadLanguage(lang)) {
        logMessage(QString("[ERROR] Language file not found: %1.txt").arg(lang));
        QMessageBox::critical(NULL, "Error", QString::fromUtf8("Language file not found. \nPlease place the language file in the same directory."));
        if (g_logStream) {
            delete g_logStream;
            g_logFile->close();
            delete g_logFile;
        }
        return 1;
    }
    
    if (customTheme == 1) {
        loadTheme();
        logMessage("[THEME] Custom theme loaded");
    }
    
    LiteBrowser browser(defaultUrl, maxTabs);
    browser.show();
    
    int result = app.exec();
    
    if (g_logStream) {
        logMessage("=== LiTE Browser Closed ===");
        delete g_logStream;
        g_logFile->close();
        delete g_logFile;
    }
    
    return result;
}

#include "main.moc"
