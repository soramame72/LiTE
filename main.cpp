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
#include <QtGui/QMessageBox>
#include <QtGui/QFileDialog>

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
    
public:
    LiteBrowser(const QString& defaultUrl, int maxTabs) 
        : currentTabIndex(0), defaultUrl(defaultUrl), maxTabs(maxTabs) {
        
        setWindowTitle("LiTE 0.0.2x192");
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
        progressBar->setMaximumHeight(3);
        progressBar->setMinimumHeight(3);
        progressBar->setVisible(false);
        
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
    
private slots:
    void addNewTab() {
        if (webViews.size() >= maxTabs) {
            return;
        }
        
        QWebView* webView = new QWebView;
        webView->page()->setForwardUnsupportedContent(true);
        
        QWebSettings* settings = webView->settings();
        settings->setAttribute(QWebSettings::PluginsEnabled, true);
        settings->setAttribute(QWebSettings::JavascriptEnabled, true);
        settings->setAttribute(QWebSettings::JavascriptCanOpenWindows, false);
        
        webView->load(QUrl(defaultUrl));
        
        connect(webView, SIGNAL(urlChanged(const QUrl&)), this, SLOT(updateUrlBar(const QUrl&)));
        connect(webView, SIGNAL(titleChanged(const QString&)), this, SLOT(updateTabTitle(const QString&)));
        connect(webView, SIGNAL(loadFinished(bool)), this, SLOT(onLoadFinished(bool)));
        connect(webView, SIGNAL(loadStarted()), this, SLOT(onLoadStarted()));
        connect(webView, SIGNAL(loadProgress(int)), this, SLOT(onLoadProgress(int)));
        connect(webView->page(), SIGNAL(loadStarted()), this, SLOT(onPageLoadStarted()));
        connect(webView->page(), SIGNAL(loadProgress(int)), this, SLOT(onPageLoadProgress(int)));
        connect(webView->page(), SIGNAL(downloadRequested(const QNetworkRequest&)), 
                this, SLOT(handleDownload(const QNetworkRequest&)));
        connect(webView->page(), SIGNAL(unsupportedContent(QNetworkReply*)), 
                this, SLOT(handleUnsupportedContent(QNetworkReply*)));
        connect(webView->page(), SIGNAL(linkClicked(const QUrl&)),
                this, SLOT(handleLinkClicked(const QUrl&)));
        
        webViews.append(webView);
        stackedWidget->addWidget(webView);
        
        QWidget* tabWidget = new QWidget;
        QHBoxLayout* tabLayout = new QHBoxLayout(tabWidget);
        tabLayout->setSpacing(2);
        tabLayout->setMargin(2);
        
        QPushButton* tabBtn = new QPushButton(QString("LiTE (0.0.2x192)"));
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
    }
    
    void handleDownload(const QNetworkRequest& request) {
        logMessage(QString("[DOWNLOAD] Download requested: %1").arg(request.url().toString()));
        
        QNetworkAccessManager* manager = new QNetworkAccessManager(this);
        QNetworkReply* reply = manager->get(request);
        
        QString urlPath = request.url().path();
        QString fileName = QFileInfo(urlPath).fileName();
        if (fileName.isEmpty()) {
            fileName = "download";
        }
        
        QString savePath = QFileDialog::getSaveFileName(this, 
            QString::fromUtf8("ファイルを保存"), fileName);
        
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
            QString::fromUtf8("ファイルを保存"), fileName);
        
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
                reply->deleteLater();
            }
        } else {
            logMessage("[DOWNLOAD] Download cancelled by user");
            reply->deleteLater();
        }
    }
    
    void downloadReadyRead() {
        QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
        if (!reply) return;
        
        if (downloads.contains(reply)) {
            QFile* file = downloads[reply];
            if (file) {
                file->write(reply->readAll());
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
        }
        
        reply->deleteLater();
    }
    
    void downloadProgress(qint64 received, qint64 total) {
        QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
        if (!reply) return;
        
        if (downloads.contains(reply) && total > 0) {
            int percent = (received * 100) / total;
            logMessage(QString("[DOWNLOAD] Progress: %1% (%2/%3 bytes)").arg(percent).arg(received).arg(total));
            progressBar->setValue(percent);
            progressBar->setVisible(true);
            
            QString style = QString("QProgressBar { border: none; background-color: transparent; }"
                                  "QProgressBar::chunk { background-color: %1; }")
                                  .arg(g_theme.progressDownload);
            progressBar->setStyleSheet(style);
        }
    }
    
    void onPageLoadStarted() {
        QWebPage* page = qobject_cast<QWebPage*>(sender());
        if (!page) return;
        
        QWebView* webView = NULL;
        for (int i = 0; i < webViews.size(); i++) {
            if (webViews[i]->page() == page) {
                webView = webViews[i];
                break;
            }
        }
        
        if (!webView) return;
        
        int index = webViews.indexOf(webView);
        logMessage(QString("[PAGE_LOAD] Started (Tab %1): %2").arg(index).arg(webView->url().toString()));
        
        if (index == currentTabIndex) {
            logMessage("[PROGRESS] Showing progress bar (GREEN)");
            progressBar->setValue(0);
            progressBar->setVisible(true);
            
            QString style = QString("QProgressBar { border: none; background-color: transparent; }"
                                  "QProgressBar::chunk { background-color: %1; }")
                                  .arg(g_theme.progressLoad);
            progressBar->setStyleSheet(style);
        }
    }
    
    void onPageLoadProgress(int progress) {
        QWebPage* page = qobject_cast<QWebPage*>(sender());
        if (!page) return;
        
        QWebView* webView = NULL;
        for (int i = 0; i < webViews.size(); i++) {
            if (webViews[i]->page() == page) {
                webView = webViews[i];
                break;
            }
        }
        
        if (!webView) return;
        
        int index = webViews.indexOf(webView);
        if (index == currentTabIndex) {
            if (progress % 20 == 0) {
                logMessage(QString("[PAGE_PROGRESS] Loading: %1%").arg(progress));
            }
            progressBar->setValue(progress);
            progressBar->setVisible(true);
            
            QString style = QString("QProgressBar { border: none; background-color: transparent; }"
                                  "QProgressBar::chunk { background-color: %1; }")
                                  .arg(g_theme.progressLoad);
            progressBar->setStyleSheet(style);
        }
    }
    
    void handleLinkClicked(const QUrl& url) {
        logMessage(QString("[LINK_CLICKED] %1").arg(url.toString()));
        
        QString urlStr = url.toString();
        
        if (urlStr.startsWith("download://")) {
            QString realUrl = urlStr.mid(11);
            logMessage(QString("[DOWNLOAD_LINK] Detected: %1").arg(realUrl));
            
            QNetworkAccessManager* manager = new QNetworkAccessManager(this);
            QUrl downloadUrl(realUrl);
            QNetworkRequest req;
            req.setUrl(downloadUrl);
            QNetworkReply* reply = manager->get(req);
            
            QString fileName = QFileInfo(downloadUrl.path()).fileName();
            if (fileName.isEmpty()) {
                fileName = "download";
            }
            
            QString savePath = QFileDialog::getSaveFileName(this, 
                QString::fromUtf8("ファイルを保存"), fileName);
            
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
    }
    
    void closeTab() {
        QPushButton* btn = qobject_cast<QPushButton*>(sender());
        if (!btn) return;
        
        int index = btn->property("tabIndex").toInt();
        
        if (webViews.size() <= 1) {
            return;
        }
        
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
    }
    
    void switchTab() {
        QPushButton* btn = qobject_cast<QPushButton*>(sender());
        if (btn) {
            int index = btn->property("tabIndex").toInt();
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
    }
    
    void updateUrlBar(const QUrl& url) {
        QWebView* sender_view = qobject_cast<QWebView*>(sender());
        if (sender_view == webViews[currentTabIndex] || sender_view == NULL) {
            urlBar->setText(url.toString());
        }
    }
    
    void updateTabTitle(const QString& title) {
        QWebView* sender_view = qobject_cast<QWebView*>(sender());
        if (!sender_view) return;
        
        int index = webViews.indexOf(sender_view);
        if (index >= 0 && index < tabButtons.size()) {
            QString displayTitle = title;
            if (displayTitle.isEmpty()) {
                displayTitle = "LiTE (0.0.2x192)";
            } else if (displayTitle.length() > 20) {
                displayTitle = displayTitle.left(17) + "...";
            }
            tabButtons[index]->setText(displayTitle);
        }
    }
    
    void onLoadStarted() {
        QWebView* sender_view = qobject_cast<QWebView*>(sender());
        if (!sender_view) {
            logMessage("[ERROR] onLoadStarted: sender is not QWebView");
            return;
        }
        
        int index = webViews.indexOf(sender_view);
        logMessage(QString("[LOAD] Started (Tab %1): %2").arg(index).arg(sender_view->url().toString()));
        
        if (index == currentTabIndex) {
            logMessage("[PROGRESS] Showing progress bar (GREEN)");
            progressBar->setValue(0);
            progressBar->setVisible(true);
            
            QString style = QString("QProgressBar { border: none; background-color: transparent; }"
                                  "QProgressBar::chunk { background-color: %1; }")
                                  .arg(g_theme.progressLoad);
            progressBar->setStyleSheet(style);
        }
    }
    
    void onLoadProgress(int progress) {
        QWebView* sender_view = qobject_cast<QWebView*>(sender());
        if (!sender_view) {
            logMessage("[ERROR] onLoadProgress: sender is not QWebView");
            return;
        }
        
        int index = webViews.indexOf(sender_view);
        if (index == currentTabIndex) {
            if (progress % 10 == 0) {
                logMessage(QString("[PROGRESS] Loading: %1%").arg(progress));
            }
            progressBar->setValue(progress);
            progressBar->setVisible(true);
            
            QString style = QString("QProgressBar { border: none; background-color: transparent; }"
                                  "QProgressBar::chunk { background-color: %1; }")
                                  .arg(g_theme.progressLoad);
            progressBar->setStyleSheet(style);
        }
    }
    
    void onLoadFinished(bool ok) {
        QWebView* sender_view = qobject_cast<QWebView*>(sender());
        if (!sender_view) return;
        
        int index = webViews.indexOf(sender_view);
        logMessage(QString("[LOAD] Finished (Tab %1): %2 - Success: %3").arg(index).arg(sender_view->url().toString()).arg(ok ? "YES" : "NO"));
        
        if (index == currentTabIndex) {
            updateUrlBar(sender_view->url());
            logMessage("[PROGRESS] Hiding progress bar");
            progressBar->setValue(100);
            progressBar->setVisible(false);
        }
    }
    
    void goBack() {
        if (currentTabIndex >= 0 && currentTabIndex < webViews.size()) {
            webViews[currentTabIndex]->back();
        }
    }
    
    void goNext() {
        if (currentTabIndex >= 0 && currentTabIndex < webViews.size()) {
            webViews[currentTabIndex]->forward();
        }
    }
    
    void reloadPage() {
        if (currentTabIndex >= 0 && currentTabIndex < webViews.size()) {
            webViews[currentTabIndex]->reload();
        }
    }
    
    void navigateToUrl() {
        if (currentTabIndex >= 0 && currentTabIndex < webViews.size()) {
            QString url = urlBar->text();
            if (!url.startsWith("http://") && !url.startsWith("https://")) {
                url = "http://" + url;
            }
            webViews[currentTabIndex]->load(QUrl(url));
        }
    }
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

	app.setWindowIcon(QIcon("favicon.ico"));
    
    QTextCodec::setCodecForTr(QTextCodec::codecForName("UTF-8"));
    QTextCodec::setCodecForCStrings(QTextCodec::codecForName("UTF-8"));
    
    QSettings settings("settings.ini", QSettings::IniFormat);
    settings.setIniCodec("UTF-8");
    
    QString lang = settings.value("browser/language", "ja").toString();
    QString defaultUrl = settings.value("browser/defaultUrl", 
        "http://mamechosu.s323.xrea.com/software/lite/welcome.html").toString();
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