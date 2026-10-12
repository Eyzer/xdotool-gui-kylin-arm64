/*
 * xdotool-gui - Qt5 graphical front-end built on top of libxdo (xdotool)
 * Target: Kylin OS V10 (arm64), X11
 *
 * libxdo is statically linked (libxdo.a built from upstream xdotool sources),
 * so no runtime dependency on libxdo.so is required.
 */

#include <QApplication>
#include <QWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QTableWidget>
#include <QHeaderView>
#include <QAbstractItemView>
#include <QMessageBox>
#include <QDateTime>
#include <QFont>
#include <QFontDatabase>
#include <QStyleFactory>
#include <QIcon>
#include <QSizePolicy>
#include <QClipboard>
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QCoreApplication>
#include <QEventLoop>
#include <QRegularExpression>
#include <QStringList>
#include <QTableWidgetItem>
#include <QTabBar>
#include <QTimer>

#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "xdo.h"

/* ------------------------------------------------------------------ */

static QString nowStamp()
{
    return QDateTime::currentDateTime().toString("HH:mm:ss");
}

static QString winTitle(xdo_t *x, Window w)
{
    unsigned char *name = NULL;
    int len = 0, type = 0;
    QString out;
    if (xdo_get_window_name(x, w, &name, &len, &type) == XDO_SUCCESS && name) {
        out = QString::fromUtf8((const char *)name, len);
        free(name);
    }
    return out;
}

static QString winClass(xdo_t *x, Window w)
{
    unsigned char *cls = NULL;
    QString out;
    if (xdo_get_window_classname(x, w, &cls) == XDO_SUCCESS && cls) {
        out = QString::fromUtf8((const char *)cls);
        free(cls);
    }
    return out;
}

static QString hexId(unsigned long id)
{
    return QString("0x%1").arg((qulonglong)id, 0, 16);
}

/* ------------------------------------------------------------------ */

class MainWindow : public QWidget
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = 0);
    void setTab(int i);
    QTabBar *tabsBar() const { return m_tabs ? m_tabs->tabBar() : 0; }

private:
    xdo_t *m_xdo;
    QTabWidget *m_tabs;

    /* global target selector */
    QComboBox    *m_targetMode;
    QLineEdit    *m_targetId;

    /* mouse tab */
    QSpinBox     *m_mAbsX, *m_mAbsY, *m_mAbsScreen;
    QSpinBox     *m_mRelX, *m_mRelY;
    QComboBox    *m_mButton;
    QSpinBox     *m_mRepeat, *m_mDelay;
    QLabel       *m_mPosLabel;

    /* keyboard tab */
    QPlainTextEdit *m_kText;
    QSpinBox       *m_kTextDelay;
    QLineEdit      *m_kSeq;
    QSpinBox       *m_kSeqDelay;

    /* window tab */
    QLineEdit    *m_wTitle, *m_wClass;
    QCheckBox    *m_wVisible;
    QSpinBox     *m_wDepth, *m_wLimit;
    QTableWidget *m_wTable;
    QSpinBox     *m_wMoveX, *m_wMoveY, *m_wSizeW, *m_wSizeH;
    QCheckBox    *m_wHint;

    /* misc tab */
    QSpinBox     *m_delayMs;
    QSpinBox     *m_desktopIndex, *m_desktopCount;
    QLabel       *m_desktopInfo;

    /* log */
    QPlainTextEdit *m_log;

    /* helpers */
    Window  targetWindow(bool *ok);
    Window  selectedRowWindow();
    void    log(const QString &s, bool err = false);
    void    report(int rc, const QString &what);
    void    refreshDesktopInfo();
    void    doSearch();
    void    readWindowInfo(Window w);

private slots:
    /* mouse */
    void onMoveAbsolute();
    void onMoveRelative();
    void onMouseClick();
    void onMouseDown();
    void onMouseUp();
    void onReadMousePos();
    /* keyboard */
    void onTypeText();
    void onSendKey();
    void onKeyDown();
    void onKeyUp();
    void onHotkeyButton();
    /* target */
    void onPickActive();
    void onTargetModeChanged(int idx);
    /* window */
    void onWindowSearch();
    void onWindowAction();
    void onWindowInfo();
    void onWindowMove();
    void onWindowResize();
    /* misc */
    void onSleep();
    void onDesktopApply();
    void onDesktopSetCount();
    /* log */
    void onClearLog();
    void onCopyLog();
};

/* ------------------------------------------------------------------ */

MainWindow::MainWindow(QWidget *parent) : QWidget(parent), m_xdo(NULL), m_tabs(NULL)
{
    setWindowTitle(QString::fromUtf8("输入自动化助手  —  xdotool GUI (Qt5)"));
    resize(920, 720);

    QVBoxLayout *root = new QVBoxLayout(this);

    /* ---------- global target bar ---------- */
    QGroupBox *tg = new QGroupBox(QString::fromUtf8("作用目标"));
    QHBoxLayout *tl = new QHBoxLayout(tg);
    tl->addWidget(new QLabel(QString::fromUtf8("鼠标 / 键盘事件发送至：")));
    m_targetMode = new QComboBox();
    m_targetMode->addItem(QString::fromUtf8("当前活动窗口（全局 XTest 注入）"));
    m_targetMode->addItem(QString::fromUtf8("指定窗口 ID"));
    tl->addWidget(m_targetMode, 1);

    m_targetId = new QLineEdit();
    m_targetId->setPlaceholderText(QString::fromUtf8("0x3a0000f 或十进制"));
    tl->addWidget(m_targetId, 1);

    QPushButton *pick = new QPushButton(QString::fromUtf8("取当前活动窗口"));
    connect(pick, SIGNAL(clicked()), this, SLOT(onPickActive()));
    tl->addWidget(pick);

    connect(m_targetMode, SIGNAL(currentIndexChanged(int)), this, SLOT(onTargetModeChanged(int)));

    root->addWidget(tg);

    /* ---------- tabs ---------- */
    QTabWidget *tabs = m_tabs = new QTabWidget();
    root->addWidget(tabs, 1);

    /* ================= mouse ================= */
    QWidget *pw = new QWidget();
    QVBoxLayout *pvl = new QVBoxLayout(pw);

    QGroupBox *g1 = new QGroupBox(QString::fromUtf8("指针移动"));
    QGridLayout *g1l = new QGridLayout(g1);
    g1l->addWidget(new QLabel(QString::fromUtf8("横坐标 X")), 0, 0);
    m_mAbsX = new QSpinBox(); m_mAbsX->setRange(0, 32767); m_mAbsX->setValue(0);
    g1l->addWidget(m_mAbsX, 0, 1);
    g1l->addWidget(new QLabel(QString::fromUtf8("纵坐标 Y")), 0, 2);
    m_mAbsY = new QSpinBox(); m_mAbsY->setRange(0, 32767); m_mAbsY->setValue(0);
    g1l->addWidget(m_mAbsY, 0, 3);
    g1l->addWidget(new QLabel(QString::fromUtf8("屏幕")), 0, 4);
    m_mAbsScreen = new QSpinBox(); m_mAbsScreen->setRange(0, 15); m_mAbsScreen->setValue(0);
    g1l->addWidget(m_mAbsScreen, 0, 5);
    QPushButton *bAbs = new QPushButton(QString::fromUtf8("绝对移动"));
    connect(bAbs, SIGNAL(clicked()), this, SLOT(onMoveAbsolute()));
    g1l->addWidget(bAbs, 0, 6);

    g1l->addWidget(new QLabel(QString::fromUtf8("水平偏移 dX")), 1, 0);
    m_mRelX = new QSpinBox(); m_mRelX->setRange(-10000, 10000);
    g1l->addWidget(m_mRelX, 1, 1);
    g1l->addWidget(new QLabel(QString::fromUtf8("垂直偏移 dY")), 1, 2);
    m_mRelY = new QSpinBox(); m_mRelY->setRange(-10000, 10000);
    g1l->addWidget(m_mRelY, 1, 3);
    QPushButton *bRel = new QPushButton(QString::fromUtf8("相对移动"));
    connect(bRel, SIGNAL(clicked()), this, SLOT(onMoveRelative()));
    g1l->addWidget(bRel, 1, 4, 1, 2);

    QPushButton *bPos = new QPushButton(QString::fromUtf8("读取当前位置"));
    connect(bPos, SIGNAL(clicked()), this, SLOT(onReadMousePos()));
    g1l->addWidget(bPos, 2, 0, 1, 2);
    m_mPosLabel = new QLabel(QString::fromUtf8("—"));
    m_mPosLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    g1l->addWidget(m_mPosLabel, 2, 2, 1, 5);
    pvl->addWidget(g1);

    QGroupBox *g2 = new QGroupBox(QString::fromUtf8("按键动作"));
    QGridLayout *g2l = new QGridLayout(g2);
    g2l->addWidget(new QLabel(QString::fromUtf8("鼠标键")), 0, 0);
    m_mButton = new QComboBox();
    m_mButton->addItem(QString::fromUtf8("左键 (1)"), 1);
    m_mButton->addItem(QString::fromUtf8("中键 (2)"), 2);
    m_mButton->addItem(QString::fromUtf8("右键 (3)"), 3);
    m_mButton->addItem(QString::fromUtf8("滚轮上 (4)"), 4);
    m_mButton->addItem(QString::fromUtf8("滚轮下 (5)"), 5);
    m_mButton->addItem(QString::fromUtf8("后退 (8)"), 8);
    m_mButton->addItem(QString::fromUtf8("前进 (9)"), 9);
    g2l->addWidget(m_mButton, 0, 1);
    g2l->addWidget(new QLabel(QString::fromUtf8("重复次数")), 0, 2);
    m_mRepeat = new QSpinBox(); m_mRepeat->setRange(1, 1000); m_mRepeat->setValue(1);
    g2l->addWidget(m_mRepeat, 0, 3);
    g2l->addWidget(new QLabel(QString::fromUtf8("间隔 (毫秒)")), 0, 4);
    m_mDelay = new QSpinBox(); m_mDelay->setRange(0, 60000); m_mDelay->setValue(20);
    g2l->addWidget(m_mDelay, 0, 5);

    QPushButton *bClick = new QPushButton(QString::fromUtf8("点击"));
    connect(bClick, SIGNAL(clicked()), this, SLOT(onMouseClick()));
    g2l->addWidget(bClick, 1, 0, 1, 2);
    QPushButton *bDown = new QPushButton(QString::fromUtf8("按下"));
    connect(bDown, SIGNAL(clicked()), this, SLOT(onMouseDown()));
    g2l->addWidget(bDown, 1, 2);
    QPushButton *bUp = new QPushButton(QString::fromUtf8("抬起"));
    connect(bUp, SIGNAL(clicked()), this, SLOT(onMouseUp()));
    g2l->addWidget(bUp, 1, 3);
    pvl->addWidget(g2);

    QLabel *hint = new QLabel(QString::fromUtf8(
        "说明：选择“当前活动窗口”时使用 XTest 向 X 服务器注入事件（推荐）。"
        "指定窗口 ID 时使用 XSendEvent，部分应用可能忽略该事件。"));
    hint->setWordWrap(true);
    pvl->addWidget(hint);
    pvl->addStretch(1);
    tabs->addTab(pw, QString::fromUtf8("鼠标"));

    /* ================= keyboard ================= */
    QWidget *kw = new QWidget();
    QVBoxLayout *kvl = new QVBoxLayout(kw);

    QGroupBox *g3 = new QGroupBox(QString::fromUtf8("文本输入"));
    QVBoxLayout *g3l = new QVBoxLayout(g3);
    m_kText = new QPlainTextEdit();
    m_kText->setPlaceholderText(QString::fromUtf8("在此输入要自动键入的文本…"));
    m_kText->setFixedHeight(90);
    g3l->addWidget(m_kText);
    QHBoxLayout *g3h = new QHBoxLayout();
    g3h->addWidget(new QLabel(QString::fromUtf8("字符间隔 (毫秒)")));
    m_kTextDelay = new QSpinBox(); m_kTextDelay->setRange(0, 5000); m_kTextDelay->setValue(12);
    g3h->addWidget(m_kTextDelay);
    QPushButton *bType = new QPushButton(QString::fromUtf8("发送文本"));
    connect(bType, SIGNAL(clicked()), this, SLOT(onTypeText()));
    g3h->addWidget(bType);
    g3h->addStretch(1);
    g3l->addLayout(g3h);
    kvl->addWidget(g3);

    QGroupBox *g4 = new QGroupBox(QString::fromUtf8("按键序列"));
    QGridLayout *g4l = new QGridLayout(g4);
    g4l->addWidget(new QLabel(QString::fromUtf8("按键组合")), 0, 0);
    m_kSeq = new QLineEdit();
    m_kSeq->setText("ctrl+c");
    m_kSeq->setPlaceholderText(QString::fromUtf8("例如 ctrl+c、alt+Tab、super+d、shift+Insert、F5"));
    g4l->addWidget(m_kSeq, 0, 1, 1, 3);
    g4l->addWidget(new QLabel(QString::fromUtf8("按键间隔 (毫秒)")), 1, 0);
    m_kSeqDelay = new QSpinBox(); m_kSeqDelay->setRange(0, 5000); m_kSeqDelay->setValue(12);
    g4l->addWidget(m_kSeqDelay, 1, 1);
    QPushButton *bKey = new QPushButton(QString::fromUtf8("发送"));
    connect(bKey, SIGNAL(clicked()), this, SLOT(onSendKey()));
    g4l->addWidget(bKey, 1, 2);
    QPushButton *bKDown = new QPushButton(QString::fromUtf8("按下"));
    connect(bKDown, SIGNAL(clicked()), this, SLOT(onKeyDown()));
    g4l->addWidget(bKDown, 1, 3);
    QPushButton *bKUp = new QPushButton(QString::fromUtf8("抬起"));
    connect(bKUp, SIGNAL(clicked()), this, SLOT(onKeyUp()));
    g4l->addWidget(bKUp, 1, 4);

    const char *hot[] = {
        "ctrl+c", "ctrl+v", "ctrl+x", "ctrl+z", "ctrl+a", "ctrl+s",
        "alt+Tab", "alt+F4", "super", "super+d", "super+l", "super+e",
        "Return", "Escape", "Tab", "BackSpace", "Delete", "F5",
        "ctrl+alt+t", "ctrl+shift+c", "Print", "ctrl+alt+Delete"
    };
    QGridLayout *hg = new QGridLayout();
    for (unsigned i = 0; i < sizeof(hot) / sizeof(hot[0]); ++i) {
        QPushButton *b = new QPushButton(QString::fromLatin1(hot[i]));
        b->setProperty("keys", QString::fromLatin1(hot[i]));
        connect(b, SIGNAL(clicked()), this, SLOT(onHotkeyButton()));
        hg->addWidget(b, i / 6, i % 6);
    }
    g4l->addLayout(hg, 2, 0, 1, 5);
    kvl->addWidget(g4);
    kvl->addStretch(1);
    tabs->addTab(kw, QString::fromUtf8("键盘"));

    /* ================= window ================= */
    QWidget *ww = new QWidget();
    QVBoxLayout *wvl = new QVBoxLayout(ww);

    QGroupBox *g5 = new QGroupBox(QString::fromUtf8("窗口搜索"));
    QGridLayout *g5l = new QGridLayout(g5);
    g5l->addWidget(new QLabel(QString::fromUtf8("标题（正则）")), 0, 0);
    m_wTitle = new QLineEdit(); m_wTitle->setPlaceholderText(".*");
    g5l->addWidget(m_wTitle, 0, 1);
    g5l->addWidget(new QLabel(QString::fromUtf8("类名（正则）")), 0, 2);
    m_wClass = new QLineEdit();
    g5l->addWidget(m_wClass, 0, 3);
    m_wVisible = new QCheckBox(QString::fromUtf8("仅可见窗口"));
    m_wVisible->setChecked(true);
    g5l->addWidget(m_wVisible, 1, 0);
    g5l->addWidget(new QLabel(QString::fromUtf8("搜索深度")), 1, 1);
    m_wDepth = new QSpinBox(); m_wDepth->setRange(1, 20); m_wDepth->setValue(3);
    g5l->addWidget(m_wDepth, 1, 2);
    g5l->addWidget(new QLabel(QString::fromUtf8("结果上限")), 1, 3);
    m_wLimit = new QSpinBox(); m_wLimit->setRange(0, 2000); m_wLimit->setValue(200);
    g5l->addWidget(m_wLimit, 1, 4);
    QPushButton *bSearch = new QPushButton(QString::fromUtf8("搜索"));
    connect(bSearch, SIGNAL(clicked()), this, SLOT(onWindowSearch()));
    g5l->addWidget(bSearch, 0, 4);
    wvl->addWidget(g5);

    m_wTable = new QTableWidget(0, 4);
    QStringList hdr;
    hdr << QString::fromUtf8("窗口 ID") << QString::fromUtf8("标题")
        << QString::fromUtf8("类名") << QString::fromUtf8("PID");
    m_wTable->setHorizontalHeaderLabels(hdr);
    m_wTable->horizontalHeader()->setStretchLastSection(false);
    m_wTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_wTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_wTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_wTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_wTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_wTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_wTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_wTable->setAlternatingRowColors(true);
    wvl->addWidget(m_wTable, 1);

    QGroupBox *g6 = new QGroupBox(QString::fromUtf8("窗口操作（作用于选中行）"));
    QGridLayout *g6l = new QGridLayout(g6);
    const char *acts[] = {
        "激活窗口", "聚焦窗口", "置顶", "降低", "最小化",
        "关闭窗口", "强制结束", "显示(映射)", "隐藏(取消映射)", "读取信息"
    };
    const char *slotNames[] = {
        "act_activate", "act_focus", "act_raise", "act_lower", "act_minimize",
        "act_close", "act_kill", "act_map", "act_unmap", "act_info"
    };
    for (unsigned i = 0; i < 10; ++i) {
        QPushButton *b = new QPushButton(QString::fromUtf8(acts[i]));
        b->setProperty("act", QString::fromLatin1(slotNames[i]));
        connect(b, SIGNAL(clicked()), this, SLOT(onWindowAction()));
        g6l->addWidget(b, i / 5, i % 5);
    }
    g6l->addWidget(new QLabel(QString::fromUtf8("移动到 X")), 2, 0);
    m_wMoveX = new QSpinBox(); m_wMoveX->setRange(-20000, 20000);
    g6l->addWidget(m_wMoveX, 2, 1);
    g6l->addWidget(new QLabel(QString::fromUtf8("Y")), 2, 2);
    m_wMoveY = new QSpinBox(); m_wMoveY->setRange(-20000, 20000);
    g6l->addWidget(m_wMoveY, 2, 3);
    QPushButton *bMove = new QPushButton(QString::fromUtf8("应用位置"));
    connect(bMove, SIGNAL(clicked()), this, SLOT(onWindowMove()));
    g6l->addWidget(bMove, 2, 4);

    g6l->addWidget(new QLabel(QString::fromUtf8("宽度")), 3, 0);
    m_wSizeW = new QSpinBox(); m_wSizeW->setRange(1, 20000); m_wSizeW->setValue(800);
    g6l->addWidget(m_wSizeW, 3, 1);
    g6l->addWidget(new QLabel(QString::fromUtf8("高度")), 3, 2);
    m_wSizeH = new QSpinBox(); m_wSizeH->setRange(1, 20000); m_wSizeH->setValue(600);
    g6l->addWidget(m_wSizeH, 3, 3);
    QPushButton *bSize = new QPushButton(QString::fromUtf8("应用尺寸"));
    connect(bSize, SIGNAL(clicked()), this, SLOT(onWindowResize()));
    g6l->addWidget(bSize, 3, 4);
    m_wHint = new QCheckBox(QString::fromUtf8("按终端尺寸提示 (SIZE_USEHINTS) 调整"));
    g6l->addWidget(m_wHint, 4, 0, 1, 5);
    wvl->addWidget(g6);
    tabs->addTab(ww, QString::fromUtf8("窗口"));

    /* ================= misc ================= */
    QWidget *mw = new QWidget();
    QVBoxLayout *mvl = new QVBoxLayout(mw);

    QGroupBox *g7 = new QGroupBox(QString::fromUtf8("延时"));
    QHBoxLayout *g7l = new QHBoxLayout(g7);
    g7l->addWidget(new QLabel(QString::fromUtf8("等待时长 (毫秒)")));
    m_delayMs = new QSpinBox(); m_delayMs->setRange(1, 600000); m_delayMs->setValue(1000);
    g7l->addWidget(m_delayMs);
    QPushButton *bSleep = new QPushButton(QString::fromUtf8("开始延时"));
    connect(bSleep, SIGNAL(clicked()), this, SLOT(onSleep()));
    g7l->addWidget(bSleep);
    g7l->addStretch(1);
    mvl->addWidget(g7);

    QGroupBox *g8 = new QGroupBox(QString::fromUtf8("工作区 / 虚拟桌面"));
    QGridLayout *g8l = new QGridLayout(g8);
    m_desktopInfo = new QLabel(QString::fromUtf8("—"));
    m_desktopInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
    g8l->addWidget(m_desktopInfo, 0, 0, 1, 3);
    g8l->addWidget(new QLabel(QString::fromUtf8("切换到桌面序号")), 1, 0);
    m_desktopIndex = new QSpinBox(); m_desktopIndex->setRange(0, 63);
    g8l->addWidget(m_desktopIndex, 1, 1);
    QPushButton *bSw = new QPushButton(QString::fromUtf8("切换"));
    connect(bSw, SIGNAL(clicked()), this, SLOT(onDesktopApply()));
    g8l->addWidget(bSw, 1, 2);
    g8l->addWidget(new QLabel(QString::fromUtf8("设置桌面总数")), 2, 0);
    m_desktopCount = new QSpinBox(); m_desktopCount->setRange(1, 64); m_desktopCount->setValue(4);
    g8l->addWidget(m_desktopCount, 2, 1);
    QPushButton *bCnt = new QPushButton(QString::fromUtf8("应用数量"));
    connect(bCnt, SIGNAL(clicked()), this, SLOT(onDesktopSetCount()));
    g8l->addWidget(bCnt, 2, 2);
    mvl->addWidget(g8);

    QLabel *about = new QLabel(QString::fromUtf8(
        "本程序是 xdotool 的 Qt5 图形前端，基于上游 libxdo 静态链接构建。\n"
        "运行环境要求：X11 会话（Wayland 会话下 XTest 注入不生效）。\n"
        "上游项目：https://github.com/jordansissel/xdotool"));
    about->setWordWrap(true);
    mvl->addWidget(about);
    mvl->addStretch(1);
    tabs->addTab(mw, QString::fromUtf8("其他"));

    /* ---------- log ---------- */
    QGroupBox *gl = new QGroupBox(QString::fromUtf8("运行日志"));
    QVBoxLayout *gll = new QVBoxLayout(gl);
    m_log = new QPlainTextEdit();
    m_log->setReadOnly(true);
    m_log->setFixedHeight(130);
    m_log->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    gll->addWidget(m_log);
    QHBoxLayout *lh = new QHBoxLayout();
    QPushButton *bClear = new QPushButton(QString::fromUtf8("清空"));
    connect(bClear, SIGNAL(clicked()), this, SLOT(onClearLog()));
    lh->addWidget(bClear);
    QPushButton *bCopy = new QPushButton(QString::fromUtf8("复制全部"));
    connect(bCopy, SIGNAL(clicked()), this, SLOT(onCopyLog()));
    lh->addWidget(bCopy);
    lh->addStretch(1);
    gll->addLayout(lh);
    root->addWidget(gl);

    /* ---------- init xdo ---------- */
    const char *dpy = getenv("DISPLAY");
    m_xdo = xdo_new(NULL);
    if (!m_xdo) {
        log(QString::fromUtf8("无法连接 X 服务器：DISPLAY=%1").arg(dpy ? dpy : "(未设置)"), true);
        QMessageBox::critical(this, QString::fromUtf8("连接失败"),
            QString::fromUtf8("无法连接到 X 显示器。\n\nDISPLAY=%1\n\n"
                              "请确认当前为 X11 桌面会话，并在图形界面下启动本程序。")
                .arg(dpy ? dpy : QString::fromUtf8("(未设置)")));
    } else {
        log(QString::fromUtf8("已连接 X 显示器 DISPLAY=%1").arg(dpy ? dpy : "(默认)"));
        /* 注意：上游 xdo_enable_feature() 存在位掩码缺陷（0 << feature），
           导致 xdo_has_feature(XDO_FEATURE_XTEST) 恒为假，故此处直接探测 XTest 扩展。 */
        int xt_ev = 0, xt_err = 0, xt_major = 0, xt_minor = 0;
        if (XTestQueryExtension(m_xdo->xdpy, &xt_ev, &xt_err, &xt_major, &xt_minor))
            log(QString::fromUtf8("XTest 扩展可用（%1.%2），支持全局鼠标/键盘注入。")
                    .arg(xt_major).arg(xt_minor));
        else
            log(QString::fromUtf8("XTest 扩展不可用，鼠标键盘注入将失败。"), true);
    }
    onTargetModeChanged(m_targetMode->currentIndex());
    refreshDesktopInfo();
}

/* ------------------------------------------------------------------ */

void MainWindow::log(const QString &s, bool err)
{
    if (!m_log) return;
    m_log->appendPlainText(QString("[%1] %2%3")
        .arg(nowStamp())
        .arg(err ? QString::fromUtf8("[错误] ") : QString())
        .arg(s));
}

void MainWindow::report(int rc, const QString &what)
{
    if (rc == XDO_SUCCESS)
        log(QString::fromUtf8("%1 — 成功").arg(what));
    else
        log(QString::fromUtf8("%1 — 失败（返回码 %2）").arg(what).arg(rc), true);
}

Window MainWindow::targetWindow(bool *ok)
{
    if (ok) *ok = true;
    if (m_targetMode->currentIndex() == 0)
        return CURRENTWINDOW;

    bool cvt = false;
    QString t = m_targetId->text().trimmed();
    unsigned long v = t.toULongLong(&cvt, 0);
    if (!cvt || v == 0) {
        log(QString::fromUtf8("目标窗口 ID 无效：%1").arg(t), true);
        if (ok) *ok = false;
        return 0;
    }
    return (Window)v;
}

Window MainWindow::selectedRowWindow()
{
    int r = m_wTable->currentRow();
    if (r < 0) {
        log(QString::fromUtf8("请先在搜索结果中选择一个窗口。"), true);
        return 0;
    }
    QTableWidgetItem *it = m_wTable->item(r, 0);
    if (!it) return 0;
    bool cvt = false;
    unsigned long v = it->text().toULongLong(&cvt, 0);
    return cvt ? (Window)v : 0;
}

void MainWindow::onTargetModeChanged(int idx)
{
    m_targetId->setEnabled(idx == 1);
}

void MainWindow::setTab(int i)
{
    if (m_tabs && i >= 0 && i < m_tabs->count())
        m_tabs->setCurrentIndex(i);
}

void MainWindow::onPickActive()
{
    if (!m_xdo) return;
    Window w = 0;
    if (xdo_get_active_window(m_xdo, &w) == XDO_SUCCESS && w) {
        m_targetMode->setCurrentIndex(1);
        m_targetId->setText(hexId(w));
        log(QString::fromUtf8("当前活动窗口：%1  标题：%2").arg(hexId(w)).arg(winTitle(m_xdo, w)));
    } else {
        log(QString::fromUtf8("获取当前活动窗口失败。"), true);
    }
}

/* ---------------- mouse ---------------- */

void MainWindow::onMoveAbsolute()
{
    if (!m_xdo) return;
    report(xdo_move_mouse(m_xdo, m_mAbsX->value(), m_mAbsY->value(), m_mAbsScreen->value()),
           QString::fromUtf8("移动指针到 (%1, %2)").arg(m_mAbsX->value()).arg(m_mAbsY->value()));
}

void MainWindow::onMoveRelative()
{
    if (!m_xdo) return;
    report(xdo_move_mouse_relative(m_xdo, m_mRelX->value(), m_mRelY->value()),
           QString::fromUtf8("指针相对移动 (%1, %2)").arg(m_mRelX->value()).arg(m_mRelY->value()));
}

void MainWindow::onMouseClick()
{
    if (!m_xdo) return;
    bool ok = false;
    Window w = targetWindow(&ok);
    if (!ok) return;
    int btn = m_mButton->currentData().toInt();
    report(xdo_click_window_multiple(m_xdo, w, btn, m_mRepeat->value(),
                                     (useconds_t)(m_mDelay->value() * 1000)),
           QString::fromUtf8("在 %1 上点击鼠标键 %2（%3 次）")
               .arg(w == CURRENTWINDOW ? QString::fromUtf8("当前窗口") : hexId(w))
               .arg(btn).arg(m_mRepeat->value()));
}

void MainWindow::onMouseDown()
{
    if (!m_xdo) return;
    bool ok = false;
    Window w = targetWindow(&ok);
    if (!ok) return;
    report(xdo_mouse_down(m_xdo, w, m_mButton->currentData().toInt()),
           QString::fromUtf8("按下鼠标键 %1").arg(m_mButton->currentData().toInt()));
}

void MainWindow::onMouseUp()
{
    if (!m_xdo) return;
    bool ok = false;
    Window w = targetWindow(&ok);
    if (!ok) return;
    report(xdo_mouse_up(m_xdo, w, m_mButton->currentData().toInt()),
           QString::fromUtf8("抬起鼠标键 %1").arg(m_mButton->currentData().toInt()));
}

void MainWindow::onReadMousePos()
{
    if (!m_xdo) return;
    int x = 0, y = 0, scr = 0;
    if (xdo_get_mouse_location(m_xdo, &x, &y, &scr) == XDO_SUCCESS) {
        m_mPosLabel->setText(QString::fromUtf8("X = %1,  Y = %2,  屏幕 = %3").arg(x).arg(y).arg(scr));
        log(QString::fromUtf8("指针位置：(%1, %2) 屏幕 %3").arg(x).arg(y).arg(scr));
    } else {
        log(QString::fromUtf8("读取指针位置失败。"), true);
    }
}

/* ---------------- keyboard ---------------- */

void MainWindow::onTypeText()
{
    if (!m_xdo) return;
    bool ok = false;
    Window w = targetWindow(&ok);
    if (!ok) return;
    QByteArray s = m_kText->toPlainText().toUtf8();
    if (s.isEmpty()) {
        log(QString::fromUtf8("输入文本为空。"), true);
        return;
    }
    report(xdo_enter_text_window(m_xdo, w, s.constData(),
                                 (useconds_t)(m_kTextDelay->value() * 1000)),
           QString::fromUtf8("发送文本（%1 字节）").arg(s.size()));
}

void MainWindow::onSendKey()
{
    if (!m_xdo) return;
    bool ok = false;
    Window w = targetWindow(&ok);
    if (!ok) return;
    QByteArray s = m_kSeq->text().trimmed().toUtf8();
    if (s.isEmpty()) {
        log(QString::fromUtf8("按键序列为空。"), true);
        return;
    }
    report(xdo_send_keysequence_window(m_xdo, w, s.constData(),
                                       (useconds_t)(m_kSeqDelay->value() * 1000)),
           QString::fromUtf8("发送按键序列 %1").arg(QString::fromUtf8(s)));
}

void MainWindow::onKeyDown()
{
    if (!m_xdo) return;
    bool ok = false;
    Window w = targetWindow(&ok);
    if (!ok) return;
    QByteArray s = m_kSeq->text().trimmed().toUtf8();
    report(xdo_send_keysequence_window_down(m_xdo, w, s.constData(),
                                            (useconds_t)(m_kSeqDelay->value() * 1000)),
           QString::fromUtf8("按下按键 %1").arg(QString::fromUtf8(s)));
}

void MainWindow::onKeyUp()
{
    if (!m_xdo) return;
    bool ok = false;
    Window w = targetWindow(&ok);
    if (!ok) return;
    QByteArray s = m_kSeq->text().trimmed().toUtf8();
    report(xdo_send_keysequence_window_up(m_xdo, w, s.constData(),
                                          (useconds_t)(m_kSeqDelay->value() * 1000)),
           QString::fromUtf8("抬起按键 %1").arg(QString::fromUtf8(s)));
}

void MainWindow::onHotkeyButton()
{
    QObject *o = sender();
    if (!o) return;
    QString keys = o->property("keys").toString();
    if (keys.isEmpty()) return;
    m_kSeq->setText(keys);
    onSendKey();
}

/* ---------------- window ---------------- */

void MainWindow::doSearch()
{
    if (!m_xdo) return;

    xdo_search_t s;
    memset(&s, 0, sizeof(s));
    s.max_depth = m_wDepth->value();
    s.only_visible = m_wVisible->isChecked() ? 1 : 0;
    s.limit = (unsigned int)m_wLimit->value();

    QByteArray title = m_wTitle->text().trimmed().toUtf8();
    QByteArray cls = m_wClass->text().trimmed().toUtf8();
    unsigned int mask = 0;

    if (!title.isEmpty()) {
        s.title = title.constData();
        s.winclass = title.constData();
        s.winname = title.constData();
        mask |= SEARCH_NAME | SEARCH_CLASS;
    }
    if (!cls.isEmpty()) {
        s.winclassname = cls.constData();
        mask |= SEARCH_CLASSNAME;
    }
    s.searchmask = mask;
    /* require 已在 memset 中置 0，即 SEARCH_ANY */

    Window *list = NULL;
    unsigned int n = 0;
    int rc = xdo_search_windows(m_xdo, &s, &list, &n);
    if (rc != XDO_SUCCESS) {
        log(QString::fromUtf8("窗口搜索失败（返回码 %1）").arg(rc), true);
        if (list) free(list);
        return;
    }

    m_wTable->setRowCount(0);
    for (unsigned int i = 0; i < n; ++i) {
        int row = m_wTable->rowCount();
        m_wTable->insertRow(row);
        m_wTable->setItem(row, 0, new QTableWidgetItem(hexId(list[i])));
        m_wTable->setItem(row, 1, new QTableWidgetItem(winTitle(m_xdo, list[i])));
        m_wTable->setItem(row, 2, new QTableWidgetItem(winClass(m_xdo, list[i])));
        int pid = xdo_get_pid_window(m_xdo, list[i]);
        m_wTable->setItem(row, 3, new QTableWidgetItem(pid > 0 ? QString::number(pid) : QString("-")));
    }
    if (list) free(list);
    log(QString::fromUtf8("窗口搜索完成，命中 %1 个窗口。").arg(n));
    if (n == 0)
        log(QString::fromUtf8("提示：可放宽“仅可见窗口”，或增大搜索深度后重试。"), true);
}

void MainWindow::onWindowSearch()
{
    doSearch();
}

void MainWindow::readWindowInfo(Window w)
{
    if (!m_xdo || !w) return;

    QString name = winTitle(m_xdo, w);
    QString cls = winClass(m_xdo, w);
    int wx = 0, wy = 0;
    Screen *scr = NULL;
    unsigned int ww = 0, hh = 0;
    xdo_get_window_location(m_xdo, w, &wx, &wy, &scr);
    xdo_get_window_size(m_xdo, w, &ww, &hh);
    int pid = xdo_get_pid_window(m_xdo, w);

    log(QString::fromUtf8("窗口信息 %1：标题=“%2” 类名=%3 位置=(%4,%5) 尺寸=%6x%7 PID=%8")
            .arg(hexId(w)).arg(name).arg(cls.isEmpty() ? QString("-") : cls)
            .arg(wx).arg(wy).arg(ww).arg(hh).arg(pid > 0 ? QString::number(pid) : QString("-")));

    if (!cls.isEmpty())
        m_wClass->setText(cls);
    if (!name.isEmpty())
        m_wTitle->setText(QRegularExpression::escape(name));
}

void MainWindow::onWindowInfo()
{
    Window w = selectedRowWindow();
    if (!w) return;
    readWindowInfo(w);
}

void MainWindow::onWindowAction()
{
    if (!m_xdo) return;
    Window w = selectedRowWindow();
    if (!w) return;

    QObject *o = sender();
    QString act = o ? o->property("act").toString() : QString();
    int rc = XDO_ERROR;
    QString what;

    if (act == "act_activate")      { rc = xdo_activate_window(m_xdo, w);   what = QString::fromUtf8("激活窗口"); }
    else if (act == "act_focus")    { rc = xdo_focus_window(m_xdo, w);      what = QString::fromUtf8("聚焦窗口"); }
    else if (act == "act_raise")    { rc = xdo_raise_window(m_xdo, w);      what = QString::fromUtf8("置顶窗口"); }
    else if (act == "act_lower")    { rc = xdo_lower_window(m_xdo, w);      what = QString::fromUtf8("降低窗口"); }
    else if (act == "act_minimize") { rc = xdo_minimize_window(m_xdo, w);   what = QString::fromUtf8("最小化窗口"); }
    else if (act == "act_close")    { rc = xdo_close_window(m_xdo, w);      what = QString::fromUtf8("关闭窗口"); }
    else if (act == "act_kill")     { rc = xdo_kill_window(m_xdo, w);       what = QString::fromUtf8("强制结束窗口"); }
    else if (act == "act_map")      { rc = xdo_map_window(m_xdo, w);        what = QString::fromUtf8("映射窗口"); }
    else if (act == "act_unmap")    { rc = xdo_unmap_window(m_xdo, w);      what = QString::fromUtf8("取消映射窗口"); }
    else if (act == "act_info")     { readWindowInfo(w); return; }
    else return;

    report(rc, QString::fromUtf8("%1 %2").arg(what).arg(hexId(w)));
}

void MainWindow::onWindowMove()
{
    if (!m_xdo) return;
    Window w = selectedRowWindow();
    if (!w) return;
    report(xdo_move_window(m_xdo, w, m_wMoveX->value(), m_wMoveY->value()),
           QString::fromUtf8("移动窗口 %1 到 (%2, %3)")
               .arg(hexId(w)).arg(m_wMoveX->value()).arg(m_wMoveY->value()));
}

void MainWindow::onWindowResize()
{
    if (!m_xdo) return;
    Window w = selectedRowWindow();
    if (!w) return;
    int flags = m_wHint->isChecked() ? (int)SIZE_USEHINTS : 0;
    report(xdo_set_window_size(m_xdo, w, m_wSizeW->value(), m_wSizeH->value(), flags),
           QString::fromUtf8("调整窗口 %1 尺寸为 %2x%3")
               .arg(hexId(w)).arg(m_wSizeW->value()).arg(m_wSizeH->value()));
}

/* ---------------- misc ---------------- */

void MainWindow::onSleep()
{
    int ms = m_delayMs->value();
    log(QString::fromUtf8("开始延时 %1 毫秒…").arg(ms));
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (t.elapsed() < ms)
            QThread::msleep(20);
    }
    log(QString::fromUtf8("延时结束。"));
}

void MainWindow::refreshDesktopInfo()
{
    if (!m_xdo) return;
    long n = 0, cur = 0;
    QString s;
    if (xdo_get_number_of_desktops(m_xdo, &n) == XDO_SUCCESS)
        s += QString::fromUtf8("桌面总数：%1    ").arg(n);
    else
        s += QString::fromUtf8("桌面总数：不支持    ");
    if (xdo_get_current_desktop(m_xdo, &cur) == XDO_SUCCESS)
        s += QString::fromUtf8("当前桌面：%1").arg(cur);
    else
        s += QString::fromUtf8("当前桌面：不支持");
    m_desktopInfo->setText(s);
}

void MainWindow::onDesktopApply()
{
    if (!m_xdo) return;
    report(xdo_set_current_desktop(m_xdo, m_desktopIndex->value()),
           QString::fromUtf8("切换到桌面 %1").arg(m_desktopIndex->value()));
    refreshDesktopInfo();
}

void MainWindow::onDesktopSetCount()
{
    if (!m_xdo) return;
    report(xdo_set_number_of_desktops(m_xdo, m_desktopCount->value()),
           QString::fromUtf8("设置桌面总数为 %1").arg(m_desktopCount->value()));
    refreshDesktopInfo();
}

void MainWindow::onClearLog()
{
    m_log->clear();
}

void MainWindow::onCopyLog()
{
    QApplication::clipboard()->setText(m_log->toPlainText());
    log(QString::fromUtf8("日志已复制到剪贴板。"));
}

/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("xdotool-gui");
    app.setApplicationDisplayName(QString::fromUtf8("输入自动化助手"));
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("xdotool-gui");
    app.setStyle(QStyleFactory::create("Fusion"));
    QIcon ic = QIcon::fromTheme("xdotool-gui");
    if (!ic.isNull())
        app.setWindowIcon(ic);

    MainWindow w;

    int tab = 0;
    bool dumpGeom = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--tab") == 0 && i + 1 < argc)
            tab = atoi(argv[i + 1]);
        else if (strcmp(argv[i], "--dump-geom") == 0)
            dumpGeom = true;
    }
    w.setTab(tab);
    w.show();

    if (dumpGeom) {
        QTimer::singleShot(2500, [&w]() {
            QTabBar *tb = w.tabsBar();
            if (tb) {
                for (int i = 0; i < tb->count(); ++i) {
                    QPoint g = tb->mapToGlobal(tb->tabRect(i).center());
                    printf("TAB %d %s x=%d y=%d w=%d h=%d\n", i,
                           tb->tabText(i).toUtf8().constData(), g.x(), g.y(),
                           tb->tabRect(i).width(), tb->tabRect(i).height());
                }
            }
            printf("WINDOW x=%d y=%d w=%d h=%d\n", w.x(), w.y(), w.width(), w.height());
            fflush(stdout);
            QApplication::quit();
        });
    }

    return app.exec();
}

#include "main.moc"
