#include "ReviewControlCapture.h"

#include <QAccessible>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQuickWindow>
#include <QSaveFile>
#include <QTimer>
#include <QtTest>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <utility>

namespace {
struct ControlCapture {
    QImage image;
    QRect header;
    QRect footer;
};

QQuickItem *findVisual(QQuickItem *item,const QString &name)
{
    if (!item) return nullptr;
    if (item->objectName()==name) return item;
    for (QQuickItem *const child:item->childItems())
        if (QQuickItem *const match=findVisual(child,name)) return match;
    return nullptr;
}

class ReviewControlCapture final : public QObject {
public:
    ReviewControlCapture(QQmlApplicationEngine &engine,QString directory)
        : QObject(&engine),m_engine(engine),m_directory(std::move(directory)) {}

    void start()
    {
        if (!QDir().mkpath(m_directory)) {
            qCritical().noquote()<<QStringLiteral("CAPTURE_DIRECTORY: Cannot create %1. Choose a writable export directory.").arg(m_directory);
            QCoreApplication::exit(1);return;
        }
        m_window=m_engine.rootObjects().isEmpty()?nullptr:qobject_cast<QQuickWindow *>(m_engine.rootObjects().first());
        if (!m_window) {check(false,"gallery_window");finish();return;}
        m_capture=findVisual(m_window->contentItem(),QStringLiteral("controlCaptureArea"));
        m_preview=findVisual(m_window->contentItem(),QStringLiteral("controlDesignPreview"));
        m_queue=findVisual(m_window->contentItem(),QStringLiteral("controlQueuePreview"));
        check(m_capture&&m_preview&&m_queue,"gallery_preview_and_capture");
        if (!m_capture||!m_preview||!m_queue) {finish();return;}
        m_count=m_window->property("variantCount").toInt();
        check(m_count==5,"five_control_studies");
        if (m_count!=5) {finish();return;}
        setDark(true);
        QTimer::singleShot(100,this,[this] {
            m_background=m_window->color();m_background.setAlpha(255);
            check(m_window->grabWindow().save(QDir(m_directory).filePath("overview.png")),"overview_saved");
            verifySidebar();next();
        });
    }

private:
    void check(bool passed,const QString &name,const QString &detail={})
    {
        m_checks.append(QJsonObject{{"name",name},{"passed",passed},{"detail",detail}});
        if (!passed) {m_failures.append(name);qWarning().noquote()<<QStringLiteral("CONTROL_DESIGN_CHECK: %1 %2").arg(name,detail);}
    }
    bool invoke(QObject *object,const char *method,const QVariant &argument)
    { return QMetaObject::invokeMethod(object,method,Q_ARG(QVariant,argument)); }
    bool invoke(QObject *object,const char *method)
    { return QMetaObject::invokeMethod(object,method); }
    QQuickItem *item(const QString &name) const { return findVisual(m_window->contentItem(),name); }
    QRectF bounds(const QQuickItem *value) const
    { return value?value->mapRectToScene(QRectF(0,0,value->width(),value->height())):QRectF(); }
    bool whollyVisible(const QQuickItem *value) const
    {
        return value&&value->isVisible()&&value->width()>0&&value->height()>0
            &&QRectF(0,0,m_window->width(),m_window->height()).adjusted(-0.5,-0.5,0.5,0.5).contains(bounds(value));
    }
    bool click(QQuickItem *value)
    {
        if (!whollyVisible(value)||!value->isEnabled()) return false;
        QTest::mouseClick(m_window,Qt::LeftButton,Qt::NoModifier,bounds(value).center().toPoint());
        QTest::qWait(30);return true;
    }
    void setDark(bool dark)
    { check(invoke(m_window,"setDarkMode",dark),QStringLiteral("theme_%1_available").arg(dark?"dark":"light")); }
    void reset()
    { invoke(m_preview,"resetView");QTest::qWait(45); }
    QString name() const
    {
        const QString state=m_phase==0?QStringLiteral("dark"):m_phase==1?QStringLiteral("dark-hover"):QStringLiteral("light");
        return QStringLiteral("%1-%2").arg(m_index+1,2,10,QLatin1Char('0')).arg(state);
    }
    void next()
    {
        if (m_index>=m_count) {
            ++m_phase;m_index=0;
            if (m_phase>=3) {finish();return;}
            if (m_phase==2) setDark(false);
        }
        check(invoke(m_window,"selectDesign",m_index),name()+"_selected");
        QTimer::singleShot(100,this,[this] {
            if (m_phase==0) verifyControls();
            QQuickItem *const focusTarget=item("controlReset");
            if (focusTarget) focusTarget->forceActiveFocus(Qt::OtherFocusReason);
            else m_window->contentItem()->forceActiveFocus(Qt::OtherFocusReason);
            for (const QString &control:QStringList{"controlDefer","controlPostpone","controlReturn"}) {
                QQuickItem *const button=item(control);
                check(button&&!button->property("activeFocus").toBool(),name()+"_capture_clears_focus_"+control);
            }
            check(invoke(m_preview,"showHover",m_phase==1),name()+"_hover_state_available");
            QTest::mouseMove(m_window,QPoint(5,5));
            QTimer::singleShot(180,this,[this] {capture();});
        });
    }
    void verifySidebar()
    {
        const bool collapsed=m_window->property("sidebarCollapsed").toBool();
        const qreal width=m_capture->width();
        check(click(item("controlSidebarToggle")),"sidebar_toggle_available");
        check(m_window->property("sidebarCollapsed").toBool()!=collapsed,"sidebar_collapses");
        check(m_capture->width()>=width,"collapsed_sidebar_preserves_main_width");
        check(click(item("controlSidebarToggle")),"sidebar_reopening_available");
        check(m_window->property("sidebarCollapsed").toBool()==collapsed,"sidebar_restores");
    }
    void verifyControls()
    {
        const QString prefix=name()+"_";
        reset();
        const int selected=m_queue->property("selectedIndex").toInt(),count=m_queue->property("count").toInt();
        const QString order=m_preview->property("queueOrder").toString();
        check(!order.isEmpty(),prefix+"queue_order_observable");
        const qreal scroll=m_queue->property("contentX").toDouble();
        int actions=m_preview->property("actionCount").toInt();
        for (const QString &control:QStringList{"controlDefer","controlPostpone"}) {
            QQuickItem *const button=item(control);
            QAccessibleInterface *const accessible=button?QAccessible::queryAccessibleInterface(button):nullptr;
            check(accessible&&!accessible->text(QAccessible::Name).trimmed().isEmpty(),prefix+control+"_accessible_name");
            if (whollyVisible(button)) {
                QTest::mouseMove(m_window,bounds(button).center().toPoint());QTest::qWait(35);
            }
            check(button&&button->property("hovered").toBool(),prefix+control+"_native_hover");
            check(click(button),prefix+control+"_clickable");
            check(m_preview->property("actionCount").toInt()==++actions,prefix+control+"_records_action");
            check(m_queue->property("selectedIndex").toInt()==selected&&m_queue->property("count").toInt()==count
                      &&m_preview->property("queueOrder").toString()==order,prefix+control+"_preserves_queue_and_selection");
            check(std::abs(m_queue->property("contentX").toDouble()-scroll)<0.5,prefix+control+"_preserves_scroll");
            if (button) {
                button->forceActiveFocus(Qt::TabFocusReason);QTest::qWait(30);
                check(button->property("activeFocus").toBool(),prefix+control+"_keyboard_focusable");
                for (const Qt::Key key:{Qt::Key_Return,Qt::Key_Enter,Qt::Key_Space}) {
                    const QString keyName=key==Qt::Key_Return?QStringLiteral("return")
                        :key==Qt::Key_Enter?QStringLiteral("enter"):QStringLiteral("space");
                    QTest::keyClick(m_window,key);QTest::qWait(30);
                    check(m_preview->property("actionCount").toInt()==++actions,prefix+control+"_keyboard_single_action_"+keyName);
                    check(m_queue->property("selectedIndex").toInt()==selected&&m_queue->property("count").toInt()==count
                              &&m_preview->property("queueOrder").toString()==order
                              &&std::abs(m_queue->property("contentX").toDouble()-scroll)<0.5,
                          prefix+control+"_keyboard_preserves_queue_"+keyName);
                }
            }
        }
        QQuickItem *const returnButton=item("controlReturn");
        QAccessibleInterface *const accessible=returnButton?QAccessible::queryAccessibleInterface(returnButton):nullptr;
        check(accessible&&!accessible->text(QAccessible::Name).trimmed().isEmpty(),prefix+"return_accessible_name");
        check(m_queue->property("activeOffscreen").toBool()&&whollyVisible(returnButton),prefix+"return_available_for_offscreen_current");
        check(click(returnButton),prefix+"return_clickable");
        check(!m_queue->property("activeOffscreen").toBool()&&returnButton&&!returnButton->isVisible(),prefix+"return_centers_and_hides");
        check(m_queue->property("selectedIndex").toInt()==selected&&m_queue->property("count").toInt()==count
                  &&m_preview->property("queueOrder").toString()==order,prefix+"return_preserves_queue_and_selection");
        invoke(m_queue,"seekTo",0.0);QTest::qWait(30);
        qreal previous=m_queue->property("contentX").toDouble();bool monotonic=true;
        const QPointF wheelPoint=m_queue->mapToScene(QPointF(m_queue->width()/2,m_queue->height()/2));
        for (int step=0;step<36;++step) {
            QWheelEvent event(wheelPoint,m_window->mapToGlobal(wheelPoint.toPoint()),QPoint(),QPoint(-120,0),
                              Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
            QTest::lastMouseTimestamp+=25;event.setTimestamp(static_cast<quint64>(QTest::lastMouseTimestamp));
            QCoreApplication::sendEvent(m_window,&event);QTest::qWait(10);
            const qreal position=m_queue->property("contentX").toDouble();
            monotonic=monotonic&&position>=previous-0.5;previous=position;
        }
        check(monotonic&&previous>m_queue->property("minimumScroll").toDouble()+1,prefix+"horizontal_wheel_advances_without_reversing");
        check(previous<=m_queue->property("maximumScroll").toDouble()+0.5,prefix+"horizontal_scroll_is_bounded");
        check(m_queue->property("selectedIndex").toInt()==selected&&m_preview->property("queueOrder").toString()==order,prefix+"wheel_preserves_selection_and_order");
        reset();verifyCompact(prefix);reset();
    }
    void verifyCompact(const QString &prefix)
    {
        const QSize normal=m_window->size();m_window->resize(900,780);QTest::qWait(50);
        check(whollyVisible(m_capture),prefix+"compact_capture_inside_window");
        check(whollyVisible(m_queue)&&bounds(m_capture).adjusted(-0.5,-0.5,0.5,0.5).contains(bounds(m_queue)),prefix+"compact_queue_inside_capture");
        const QStringList controls{"controlThemeToggle","controlSidebarToggle","controlPrevious","controlNext","controlReset","controlDefer","controlPostpone","controlReturn"};
        for (const QString &control:controls)
            check(whollyVisible(item(control)),prefix+"compact_visible_"+control);
        QQuickItem *const timeline=findVisual(m_queue,QStringLiteral("designTimeline"));
        if (timeline) {
            for (const QString &control:QStringList{"controlDefer","controlPostpone","controlReturn"})
                check(!bounds(item(control)).intersects(bounds(timeline)),prefix+"compact_no_card_overlap_"+control);
        }
        m_window->resize(normal);QTest::qWait(40);
    }
    void capture()
    {
        check(whollyVisible(m_capture)&&m_capture->width()>500&&m_capture->height()>250,name()+"_capture_geometry");
        if (m_phase<2) {
            QQuickItem *const timeline=findVisual(m_queue,QStringLiteral("designTimeline"));
            QQuickItem *const returnButton=item("controlReturn");
            QQuickItem *const gradeRow=item("controlGradeRow");
            QQuickItem *const footer=item("controlFooter");
            const auto inCapture=[this](const QQuickItem *value) {
                return value?value->mapRectToItem(m_capture,QRectF(0,0,value->width(),value->height())):QRectF();
            };
            const qreal headerBottom=std::max(inCapture(timeline).top(),inCapture(returnButton).bottom());
            const qreal footerTop=inCapture(gradeRow).top();
            const bool valid=timeline&&returnButton&&gradeRow&&footer&&headerBottom>0&&headerBottom<footerTop
                &&footerTop<m_capture->height()&&inCapture(footer).bottom()<=m_capture->height()+0.5;
            check(valid,name()+"_focused_contact_geometry");
            m_headerBand=QRectF(0,0,m_capture->width(),headerBottom);
            m_footerBand=QRectF(0,footerTop,m_capture->width(),m_capture->height()-footerTop);
            m_captureSize=QSizeF(m_capture->width(),m_capture->height());
        }
        m_grab=m_capture->grabToImage();
        if (!m_grab) {check(false,name()+"_grab_started");++m_index;next();return;}
        auto *const timeout=new QTimer(this);timeout->setSingleShot(true);
        QObject::connect(timeout,&QTimer::timeout,this,[this] {check(false,name()+"_grab_timeout");finish();});timeout->start(5000);
        QObject::connect(m_grab.data(),&QQuickItemGrabResult::ready,this,[this,timeout] {
            timeout->stop();timeout->deleteLater();const QSharedPointer<QQuickItemGrabResult> complete=m_grab;
            const QImage image=complete->image();check(!image.isNull(),name()+"_image_rendered");
            const QString file=name()+".png";check(image.save(QDir(m_directory).filePath(file)),name()+"_image_saved");m_files.append(file);
            if (m_phase<2&&!image.isNull()) {
                const auto pixels=[this,&image](const QRectF &logical) {
                    const qreal scaleX=image.width()/m_captureSize.width(),scaleY=image.height()/m_captureSize.height();
                    return QRectF(logical.x()*scaleX,logical.y()*scaleY,logical.width()*scaleX,logical.height()*scaleY)
                        .toAlignedRect().intersected(image.rect());
                };
                const struct ControlCapture captured{image,pixels(m_headerBand),pixels(m_footerBand)};
                if (m_phase==0) {m_images.append(image);m_darkCaptures.append(captured);}
                else if (m_index<m_darkCaptures.size()) {
                    const struct ControlCapture &dark=m_darkCaptures.at(m_index);
                    check(image.size()==dark.image.size()&&captured.header==dark.header&&captured.footer==dark.footer,
                          name()+"_hover_contact_geometry_matches_default");
                    const struct ControlCapture hovered{image,dark.header,dark.footer};
                    m_hoverCaptures.append(hovered);
                }
            }
            m_grab.clear();++m_index;QTimer::singleShot(0,this,[this] {next();});
        });
    }
    void focusedSheet(const QList<struct ControlCapture> &captures,const QString &filename)
    {
        if (captures.isEmpty()) return;
        constexpr int tileWidth=1200,gap=16,separator=10;
        QList<QImage> strips;
        int totalHeight=gap;
        for (const struct ControlCapture &capture:captures) {
            const int sourceHeight=capture.header.height()+separator+capture.footer.height();
            if (capture.header.isEmpty()||capture.footer.isEmpty()||sourceHeight<=0) {
                check(false,filename+"_valid_source_bands");continue;
            }
            QImage strip(capture.image.width(),sourceHeight,QImage::Format_ARGB32_Premultiplied);strip.fill(m_background);
            QPainter painter(&strip);
            painter.drawImage(QPoint(0,0),capture.image,capture.header);
            painter.drawImage(QPoint(0,capture.header.height()+separator),capture.image,capture.footer);
            painter.end();
            strips.append(strip.scaledToWidth(tileWidth,Qt::SmoothTransformation));
            totalHeight+=strips.last().height()+gap;
        }
        QImage sheet(tileWidth+2*gap,totalHeight,QImage::Format_ARGB32_Premultiplied);sheet.fill(m_background);
        QPainter painter(&sheet);
        int top=gap;
        for (const QImage &strip:strips) {painter.drawImage(QPoint(gap,top),strip);top+=strip.height()+gap;}
        painter.end();check(sheet.save(QDir(m_directory).filePath(filename)),filename+"_saved");
    }
    void finish()
    {
        if (m_finished) return;
        m_finished=true;
        if (!m_images.isEmpty()) {
            constexpr int tileWidth=1000,maximumTileHeight=490,gap=16;
            int tileHeight=0;
            for (const QImage &image:m_images) tileHeight=std::max(tileHeight,image.size().scaled(QSize(tileWidth,maximumTileHeight),Qt::KeepAspectRatio).height());
            QImage sheet(tileWidth+gap*2,int(m_images.size())*(tileHeight+gap)+gap,QImage::Format_ARGB32_Premultiplied);sheet.fill(m_background);
            QPainter painter(&sheet);painter.setRenderHint(QPainter::SmoothPixmapTransform);
            for (qsizetype index=0;index<m_images.size();++index) {
                const QImage &image=m_images[index];const QSize size=image.size().scaled(QSize(tileWidth,maximumTileHeight),Qt::KeepAspectRatio);
                painter.drawImage(QRect(QPoint(gap,gap+int(index)*(tileHeight+gap)),size),image);
            }
            painter.end();check(sheet.save(QDir(m_directory).filePath("full-context-sheet.png")),"full_context_sheet_saved");
        }
        focusedSheet(m_darkCaptures,QStringLiteral("contact-sheet.png"));
        focusedSheet(m_hoverCaptures,QStringLiteral("contact-sheet-hover.png"));
        const QStringList warnings=m_engine.property("controlDesignWarnings").toStringList();check(warnings.isEmpty(),"no_qml_warnings",warnings.join(QLatin1Char('\n')));
        check(m_files.size()==m_count*3,"all_control_states_exported");
        const QJsonObject report{{"success",m_failures.isEmpty()},{"variantCount",m_count},{"checks",m_checks},{"failures",QJsonArray::fromStringList(m_failures)},
            {"qmlWarnings",QJsonArray::fromStringList(warnings)},{"images",QJsonArray::fromStringList(m_files)}};
        QSaveFile output(QDir(m_directory).filePath("report.json"));const QByteArray data=QJsonDocument(report).toJson(QJsonDocument::Indented);
        const bool saved=output.open(QIODevice::WriteOnly)&&output.write(data)==data.size()&&output.commit();
        if (!saved) qCritical()<<"CAPTURE_REPORT: Cannot save control design verification report.";
        QCoreApplication::exit(saved&&m_failures.isEmpty()?0:1);
    }
    QQmlApplicationEngine &m_engine;
    const QString m_directory;
    QQuickWindow *m_window=nullptr;
    QQuickItem *m_capture=nullptr,*m_preview=nullptr,*m_queue=nullptr;
    QSharedPointer<QQuickItemGrabResult> m_grab;
    QJsonArray m_checks;
    QStringList m_failures,m_files;
    QList<QImage> m_images;
    QList<struct ControlCapture> m_darkCaptures,m_hoverCaptures;
    QRectF m_headerBand,m_footerBand;
    QSizeF m_captureSize;
    QColor m_background;
    int m_count=0,m_index=0,m_phase=0;
    bool m_finished=false;
};
}

void runReviewControlCapture(QQmlApplicationEngine &engine,const QString &directory)
{
    auto *const capture=new ReviewControlCapture(engine,directory);capture->start();
}
